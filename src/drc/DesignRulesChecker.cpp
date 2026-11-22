// Port of app.freerouting.drc.DesignRulesChecker
#include "drc/DesignRulesChecker.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/Package.h"
#include "datastructures/UndoableObjects.h"
#include "drc/AirLine.h"
#include "drc/ClearanceViolation.h"
#include "drc/DrcSummaryResponse.h"
#include "drc/NetIncompletes.h"
#include "drc/UnconnectedItems.h"
#include "drc/ZoneIslandViolation.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/PlanarArea.h"
#include "geometry/planar/Point.h"
#include "io/CoordinateTransform.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"
#include "settings/DesignRulesCheckerSettings.h"

namespace fr {

jrt::Collection<ClearanceViolation*>* DesignRulesChecker::getAllClearanceViolations() {
  auto* allViolations = new jrt::ArrayList<ClearanceViolation*>();
  auto* seenViolations = new jrt::HashSet<std::string>();

  // Iterate through all items on the board
  jrt::Collection<Item*>* items = board->getItems();
  for (Item* item : *items) {
    if (item != nullptr) {
      // Get clearance violations for this item
      jrt::Collection<ClearanceViolation*>* itemViolations = item->clearanceViolations();

      // Deduplicate violations - A-B and B-A are the same violation
      for (ClearanceViolation* violation : *itemViolations) {
        int32_t id1 = violation->firstItem->getId();
        int32_t id2 = violation->secondItem->getId();

        // Create a unique key using sorted IDs to avoid duplicates
        std::string key =
            (id1 < id2 ? jrt::str(id1) + "-" + jrt::str(id2) + "-" + jrt::str(violation->layer)
                       : jrt::str(id2) + "-" + jrt::str(id1) + "-" + jrt::str(violation->layer)) +
            (violation->holeToHole ? "-h" : "");

        if (!seenViolations->contains(key)) {
          seenViolations->add(key);
          allViolations->add(violation);
        }
      }
    }
  }

  return allViolations;
}

jrt::Collection<UnconnectedItems*>* DesignRulesChecker::getAllUnconnectedItems() {
  auto* unconnectedItems = new jrt::ArrayList<UnconnectedItems*>();

  // Group items by net
  auto* itemsByNet = new jrt::HashMap<int32_t, jrt::List<Item*>*>();
  for (Item* item : *board->getItems()) {
    if (dynamic_cast<Connectable*>(item) != nullptr && item->netCount() > 0) {
      int32_t netNumber = item->getNetNumber(0);
      itemsByNet
          ->computeIfAbsent(netNumber,
                            [](int32_t) -> jrt::List<Item*>* { return new jrt::ArrayList<Item*>(); })
          ->add(item);
    }
  }

  // For each net, find truly unconnected items
  for (auto* entry : *itemsByNet->entrySet()) {
    int32_t netNumber = entry->getKey();
    jrt::List<Item*>* netItems = entry->getValue();

    if (netItems->size() <= 1) {
      continue;  // Single item nets are not unconnected
    }

    // Get all connected sets for this net
    auto* connectedSets = new jrt::ArrayList<jrt::Set<Item*>*>();
    auto* processedItems = new jrt::HashSet<Item*>();

    for (Item* item : *netItems) {
      if (processedItems->contains(item)) {
        continue;
      }

      // Get the connected set for this item
      jrt::Collection<Item*>* connectedSet = item->getConnectedSet(netNumber);
      jrt::Set<Item*>* setItems = new jrt::HashSet<Item*>(connectedSet);

      // Only add items that are actually in this net
      setItems->retainAll(netItems);

      if (!setItems->isEmpty()) {
        connectedSets->add(setItems);
        processedItems->addAll(setItems);
      }
    }

    // If there are multiple connected sets, we have unconnected items
    // Report only ONE entry per net, but include items from the disconnected groups
    if (connectedSets->size() >= 2) {
      // Find representative items from the first two sets
      Item* item1 = findRepresentativeItem(connectedSets->get(0));
      Item* item2 = findRepresentativeItem(connectedSets->get(1));

      if (item1 != nullptr && item2 != nullptr) {
        // Include only items from the two disconnected groups for investigation
        auto* unconnectedGroupItems = new jrt::ArrayList<Item*>();
        unconnectedGroupItems->addAll(connectedSets->get(0));
        unconnectedGroupItems->addAll(connectedSets->get(1));
        unconnectedItems->add(new UnconnectedItems(item1, item2, unconnectedGroupItems));
      }
    }
  }

  // Check for dangling traces - traces with unconnected ends
  for (Item* item : *board->getItems()) {
    if (auto* trace = dynamic_cast<Trace*>(item)) {
      jrt::Collection<Item*>* startContacts = trace->getStartContacts();
      jrt::Collection<Item*>* endContacts = trace->getEndContacts();

      // A trace is dangling if either its start or end has no contacts
      if (startContacts->isEmpty() || endContacts->isEmpty()) {
        // Only add if not already in the list
        bool anyMatch = false;
        for (UnconnectedItems* ui : *unconnectedItems) {
          if (ui->firstItem == static_cast<Item*>(trace)) {
            anyMatch = true;
            break;
          }
        }
        if (!anyMatch) {
          unconnectedItems->add(new UnconnectedItems(trace, nullptr, std::string("track_dangling")));
        }
      }
    }
  }

  // Check for dangling vias - vias not connected or connected on only one layer
  for (Item* item : *board->getItems()) {
    if (auto* via = dynamic_cast<Via*>(item)) {
      // Use the isTail() method which checks if via has contacts on at most 1 layer
      if (via->isTail()) {
        unconnectedItems->add(new UnconnectedItems(via, nullptr, std::string("via_dangling")));
      }
    }
  }

  return unconnectedItems;
}

jrt::Collection<ZoneIslandViolation*>* DesignRulesChecker::getZoneIslandViolations() {
  auto* violations = new jrt::ArrayList<ZoneIslandViolation*>();
  jrt::Collection<ConductionArea*>* conductionAreas = board->getConductionAreas();
  if (conductionAreas == nullptr || conductionAreas->isEmpty()) {
    return violations;
  }

  double minDeadCopperArea = 1.0 * board->communication->getResolution(Unit::MM) *
                             board->communication->getResolution(Unit::MM);

  for (ConductionArea* ca : *conductionAreas) {
    PlanarArea* detailedFill = ca->getDetailedFillArea();
    if (detailedFill == nullptr || detailedFill->isEmpty()) {
      continue;
    }

    jrt::List<PlanarArea*>* islands = detailedFill->decomposeIntoIslands();
    if (islands->size() <= 1) {
      continue;  // Single continuous pour, no fragmentation
    }

    int32_t netNumber = ca->netCount() > 0 ? ca->getNetNumber(0) : 0;
    int32_t layer = ca->getLayer();

    // Find all connectable items belonging to this net on this layer
    auto* sameNetItemsOnLayer = new jrt::ArrayList<Item*>();
    for (Item* item : *board->getItems()) {
      if (item != static_cast<Item*>(ca) && dynamic_cast<Connectable*>(item) != nullptr &&
          item->sharesNet(ca) && item->sharesLayer(ca)) {
        sameNetItemsOnLayer->add(item);
      }
    }

    // Map items to each island
    auto* islandItems = new jrt::ArrayList<jrt::List<Item*>*>();
    for (PlanarArea* island : *islands) {
      auto* contained = new jrt::ArrayList<Item*>();
      for (Item* item : *sameNetItemsOnLayer) {
        Point* center = nullptr;
        if (auto* drillItem = dynamic_cast<DrillItem*>(item)) {
          center = drillItem->getCenter();
        } else if (auto* trace = dynamic_cast<Trace*>(item)) {
          center = trace->firstCorner();
        }
        if (center != nullptr) {
          FloatPoint* fp = center->toFloat();
          if (island->contains(fp->x, fp->y)) {
            contained->add(item);
          }
        }
      }
      islandItems->add(contained);
    }

    // Determine which island is the primary (has the most connected items or connects to the
    // main set)
    int32_t primaryIndex = -1;
    int32_t maxConnectedItems = -1;
    for (int32_t i = 0; i < islands->size(); i++) {
      int32_t count = islandItems->get(i)->size();
      if (count > maxConnectedItems) {
        maxConnectedItems = count;
        primaryIndex = i;
      }
    }

    // Check each island against the primary
    for (int32_t i = 0; i < islands->size(); i++) {
      PlanarArea* island = islands->get(i);
      jrt::List<Item*>* items = islandItems->get(i);

      if (!items->isEmpty()) {
        if (i != primaryIndex) {
          // Check if any item in this island can reach the items in the primary island through
          // traces/vias elsewhere on the board
          bool connectedToPrimary = false;
          if (primaryIndex >= 0 && !islandItems->get(primaryIndex)->isEmpty()) {
            Item* repItemInPrimary = islandItems->get(primaryIndex)->get(0);
            jrt::Collection<Item*>* connectedSet =
                repItemInPrimary->getConnectedSet(netNumber, true);
            for (Item* item : *items) {
              if (connectedSet->contains(item)) {
                connectedToPrimary = true;
                break;
              }
            }
          }

          if (!connectedToPrimary) {
            violations->add(new ZoneIslandViolation(ca, island, layer, netNumber,
                                                    "isolated_island_unconnected", items));
          }
        }
      } else {
        // Island with zero contact items: check if it exceeds minimum dead copper threshold
        double approxArea = island->getArea();
        if (approxArea >= minDeadCopperArea) {
          violations->add(new ZoneIslandViolation(ca, island, layer, netNumber,
                                                  "isolated_island_dead_copper",
                                                  new jrt::ArrayList<Item*>()));
        }
      }
    }
  }

  return violations;
}

Item* DesignRulesChecker::findRepresentativeItem(jrt::Set<Item*>* connectedSet) {
  // Prefer Pins
  for (Item* item : *connectedSet) {
    if (dynamic_cast<Pin*>(item) != nullptr) {
      return item;
    }
  }
  // Then Traces
  for (Item* item : *connectedSet) {
    if (dynamic_cast<Trace*>(item) != nullptr) {
      return item;
    }
  }
  // Finally any item
  return connectedSet->isEmpty() ? nullptr : connectedSet->iterator()->next();
}

// PORT NOTE: generateReport(sourceFile, coordinateUnit), generateReportJson and the three
// convertToDrcViolation overloads build io.kicad.KiCadDrc* reports serialized with gson; they are
// out of scope and dropped (see header). The helpers below are kept.

bool DesignRulesChecker::isHole(Item* item) {
  if (dynamic_cast<Via*>(item) != nullptr) {
    return true;
  }
  // Pins are treated as holes for DRC classification to match expected output,
  // although this might include SMT pins (DrillItem).
  return dynamic_cast<Pin*>(item) != nullptr;
}

std::string DesignRulesChecker::getItemDescription(Item* item) {
  std::string desc;

  if (dynamic_cast<Trace*>(item) != nullptr) {
    desc += "Trace";
  } else if (dynamic_cast<Via*>(item) != nullptr) {
    desc += "Via";
  } else if (dynamic_cast<Pin*>(item) != nullptr) {
    desc += "Pin";
  } else if (dynamic_cast<ConductionArea*>(item) != nullptr) {
    desc += "Conduction Area";
  } else {
    desc += jrt::className(item);
  }

  // Add net information
  if (item->netCount() > 0) {
    std::string netName = board->rules->nets->get(item->getNetNumber(0))->name;
    desc += " [" + netName + "]";
  }

  return desc;
}

std::string DesignRulesChecker::getDetailedTraceDescription(Item* item,
                                                            const std::string& coordinateUnit) {
  std::string desc = "Track";

  // Add net information
  if (item->netCount() > 0) {
    std::string netName = board->rules->nets->get(item->getNetNumber(0))->name;
    desc += " [" + netName + "]";
  }

  // Add layer information
  if (auto* trace = dynamic_cast<Trace*>(item)) {
    int32_t layer = trace->getLayer();
    std::string layerName = (*board->layerStructure->layers)[layer]->name;
    desc += " on " + layerName;

    // Add length information
    double lengthInBoardUnits = trace->getLength();
    double lengthInTargetUnits = convertCoordinate(lengthInBoardUnits, coordinateUnit);
    desc += ", length " + jrt::format("%.4f", lengthInTargetUnits) + " " + coordinateUnit;
  }

  return desc;
}

double DesignRulesChecker::convertCoordinate(double boardCoordinate,
                                             const std::string& coordinateUnit) {
  // First, convert from board's internal coordinate system to DSN coordinates (in the board's
  // unit)
  double dsnCoordinate = board->communication->coordinateTransform->boardToDsn(boardCoordinate);

  // Get the board's native unit
  Unit boardUnit = board->communication->unit;

  // Determine target unit
  Unit targetUnit;
  if (coordinateUnit == "mm") {
    targetUnit = Unit::MM;
  } else if (coordinateUnit == "mil") {
    targetUnit = Unit::MIL;
  } else if (coordinateUnit == "inch") {
    targetUnit = Unit::INCH;
  } else if (coordinateUnit == "um") {
    targetUnit = Unit::UM;
  } else {
    // Default to board unit if unknown
    targetUnit = boardUnit;
  }

  // If the target unit is different from the board unit, convert
  if (targetUnit != boardUnit) {
    return Unit_scale(dsnCoordinate, boardUnit, targetUnit);
  }

  return dsnCoordinate;
}

void DesignRulesChecker::calculateAllIncompletes() {
  int32_t maxNetNo = board->rules->nets->maxNetNumber();
  // Create the net item lists at once for performance reasons.
  auto* netItemLists = new jrt::ArrayList<jrt::Collection<Item*>*>();
  for (int32_t i = 0; i < maxNetNo; i++) {
    netItemLists->add(new jrt::LinkedList<Item*>());
  }
  auto* it = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = dynamic_cast<Item*>(board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    if (dynamic_cast<Connectable*>(currentItem) != nullptr) {
      for (int32_t i = 0; i < currentItem->netCount(); i++) {
        netItemLists->get(currentItem->getNetNumber(i) - 1)->add(currentItem);
      }
    }
  }
  // Correct formula: for each net with >= 2 items, (items - 1) connections are needed
  // (minimum spanning tree). Nets with 0 or 1 items contribute 0.
  int32_t sum = 0;
  for (jrt::Collection<Item*>* list : *netItemLists) {
    if (list->isEmpty()) {
      continue;
    }
    int64_t endpointCount = 0;
    for (Item* item : *list) {
      if (dynamic_cast<Pin*>(item) != nullptr || dynamic_cast<ConductionArea*>(item) != nullptr) {
        endpointCount++;
      }
    }
    sum += static_cast<int32_t>(jrt::Math::max(static_cast<int64_t>(0), endpointCount - 1));
  }
  this->maxConnections = sum;

  int32_t totalItems = 0;
  for (jrt::Collection<Item*>* list : *netItemLists) {
    totalItems += list->size();
  }
  FRLogger::trace("DesignRulesChecker.calculateAllIncompletes", "maxConnections",
                  "Calculated maxConnections=" + jrt::str(this->maxConnections) +
                      ", total_items=" + jrt::str(totalItems) +
                      ", netCount=" + jrt::str(netItemLists->size()) +
                      " (formula: total_items - netCount)",
                  "Incomplete Count", new jrt::Array<Point*>(0));

  auto* focusNets = jrt::Array<int32_t>::of({98, 99});
  for (int32_t netNumber : *focusNets) {
    if (netNumber >= 1 && netNumber <= netItemLists->size()) {
      int32_t netItemsCount = netItemLists->get(netNumber - 1)->size();
      Net* net = board->rules->nets->get(netNumber);
      std::string netName = net != nullptr ? net->name : "unknown";
      FRLogger::trace("DesignRulesChecker.calculateAllIncompletes", "netItemCount",
                      "Net item count: net=" + jrt::str(netNumber) + ", name=" + netName +
                          ", items=" + jrt::str(netItemsCount),
                      "Net #" + jrt::str(netNumber) + " (" + netName + ")",
                      new jrt::Array<Point*>(0));

      // Let's validate all the polyline traces for this net
      jrt::Collection<Item*>* netItems = netItemLists->get(netNumber - 1);
      for (Item* item : *netItems) {
        if (auto* trace = dynamic_cast<PolylineTrace*>(item)) {
          (void)trace;  // trace.validateAndLogPolylineIntegrity();
        }
      }
    }
  }

  this->netIncompletes = new jrt::Array<NetIncompletes*>(maxNetNo);
  for (int32_t i = 0; i < netIncompletes->length; i++) {
    // netNumber is 1-based, index is 0-based
    int32_t netNumber = i + 1;
    (*netIncompletes)[i] = new NetIncompletes(netNumber, netItemLists->get(i), board);
  }
}

void DesignRulesChecker::recalculateNetIncompletes(int32_t netNumber) {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
    return;
  }
  if (netNumber >= 1 && netNumber <= netIncompletes->length) {
    jrt::Collection<Item*>* itemList = board->getConnectableItems(netNumber);
    (*netIncompletes)[netNumber - 1] = new NetIncompletes(netNumber, itemList, board);
  }
}

void DesignRulesChecker::recalculateNetIncompletes(int32_t netNumber,
                                                   jrt::Collection<Item*>* itemList) {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();  // Initialize if not already done
  }
  if (netNumber >= 1 && netNumber <= netIncompletes->length) {
    // copy itemList, because it will be changed inside the constructor of NetIncompletes
    jrt::Collection<Item*>* items = new jrt::LinkedList<Item*>(itemList);
    (*netIncompletes)[netNumber - 1] = new NetIncompletes(netNumber, items, board);
  }
}

int32_t DesignRulesChecker::getIncompleteCount() {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
  }

  int32_t result = 0;
  std::string detailsBuilder;
  int32_t netsWithIncompletes = 0;

  for (int32_t i = 0; i < netIncompletes->length; i++) {
    int32_t count = (*this->netIncompletes)[i]->count();
    if (count > 0) {
      result += count;
      netsWithIncompletes++;
      if (netsWithIncompletes <= 10) {  // Log first 10 nets with incompletes
        Net* net = board->rules->nets->get(i + 1);
        std::string netName = net != nullptr ? net->name : "unknown";
        detailsBuilder += "Net #" + jrt::str(i + 1) + " (" + netName + "): " + jrt::str(count) +
                          " incomplete(s); ";
      }
    }
  }

  FRLogger::trace("DesignRulesChecker.getIncompleteCount", "total_incompletes_calculated",
                  "Total incomplete count: " + jrt::str(result) +
                      ", nets_with_incompletes=" + jrt::str(netsWithIncompletes) +
                      ", first_few_nets=" + detailsBuilder,
                  "Incomplete Count", new jrt::Array<Point*>(0));

  return result;
}

jrt::Set<int32_t>* DesignRulesChecker::incompleteNetNumbers() {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
  }
  jrt::Set<int32_t>* result = new jrt::TreeSet<int32_t>();
  for (int32_t i = 0; i < netIncompletes->length; i++) {
    if ((*this->netIncompletes)[i]->count() > 0) {
      result->add(i + 1);
    }
  }
  return result;
}

int32_t DesignRulesChecker::getIncompleteCount(int32_t netNumber) {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
  }
  if (netNumber <= 0 || netNumber > netIncompletes->length) {
    return 0;
  }

  int32_t result = (*netIncompletes)[netNumber - 1]->count();
  Net* net = board->rules->nets->get(netNumber);
  std::string netName = net != nullptr ? net->name : "unknown";

  FRLogger::trace("DesignRulesChecker.getIncompleteCount", "net_incomplete_count",
                  "Net incomplete count: net=" + jrt::str(netNumber) + ", name=" + netName +
                      ", incompleteCount=" + jrt::str(result),
                  "Net #" + jrt::str(netNumber) + " (" + netName + ")",
                  new jrt::Array<Point*>(0));

  return result;
}

int32_t DesignRulesChecker::getLengthViolationCount() {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
  }
  int32_t result = 0;
  for (int32_t i = 0; i < netIncompletes->length; i++) {
    if ((*netIncompletes)[i]->getLengthViolation() != 0) {
      ++result;
    }
  }
  return result;
}

double DesignRulesChecker::getLengthViolation(int32_t netNumber) {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
  }
  if (netNumber <= 0 || netNumber > netIncompletes->length) {
    return 0;
  }
  return (*netIncompletes)[netNumber - 1]->getLengthViolation();
}

bool DesignRulesChecker::recalculateLengthViolations() {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
    return true;  // Technically changed from nothing to something
  }
  bool result = false;
  for (int32_t i = 0; i < netIncompletes->length; i++) {
    if ((*netIncompletes)[i]->calcLengthViolation()) {
      result = true;
    }
  }
  return result;
}

jrt::Array<AirLine*>* DesignRulesChecker::getAllAirlines() {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
  }
  int32_t count = getIncompleteCount();
  auto* result = new jrt::Array<AirLine*>(count);
  int32_t currentIndex = 0;
  for (int32_t i = 0; i < netIncompletes->length; i++) {
    jrt::Collection<AirLine*>* currentList = (*netIncompletes)[i]->incompletes;
    for (AirLine* currentLine : *currentList) {
      (*result)[currentIndex] = currentLine;
      ++currentIndex;
    }
  }
  return result;
}

NetIncompletes* DesignRulesChecker::getNetIncompletes(int32_t netNumber) {
  if (netIncompletes == nullptr) {
    calculateAllIncompletes();
  }
  if (netNumber <= 0 || netNumber > netIncompletes->length) {
    return nullptr;
  }
  return (*netIncompletes)[netNumber - 1];
}

DrcSummaryResponse* DesignRulesChecker::generateSummary() {
  DrcSummaryResponse* summary = new DrcSummaryResponse();

  jrt::Collection<ClearanceViolation*>* violations = getAllClearanceViolations();
  summary->clearanceViolationsCount = violations->size();

  jrt::Collection<UnconnectedItems*>* unconnecteds = getAllUnconnectedItems();
  jrt::Set<int32_t>* unconnectedNetNumbers = new jrt::HashSet<int32_t>();
  int32_t danglingStubsCount = 0;
  for (UnconnectedItems* u : *unconnecteds) {
    if (u->type == "track_dangling" || u->type == "via_dangling") {
      danglingStubsCount++;
    } else if (u->firstItem != nullptr && u->firstItem->netCount() > 0) {
      unconnectedNetNumbers->add(u->firstItem->getNetNumber(0));
    } else {
      danglingStubsCount++;
    }
  }

  // 1. Process clearance violations
  for (ClearanceViolation* v : *violations) {
    std::string layerName = (v->layer >= 0 && v->layer < board->layerStructure->layers->length)
                                ? (*board->layerStructure->layers)[v->layer]->name
                                : ("Layer " + jrt::str(v->layer));

    std::string item1Desc = formatItemWithContext(v->firstItem);
    std::string item2Desc = formatItemWithContext(v->secondItem);

    double expectedMm = v->expectedClearance / 10000.0;
    double actualMm = v->actualClearance / 10000.0;
    double shortfallMm =
        jrt::Math::max(0.0, (v->expectedClearance - v->actualClearance) / 10000.0);

    std::string explanation = jrt::format(
        "Clearance violation between %s and %s on %s: required %.3f mm, found %.3f mm "
        "(shortfall: %.3f mm).",
        item1Desc.c_str(), item2Desc.c_str(), layerName.c_str(), expectedMm, actualMm,
        shortfallMm);

    auto* items = new jrt::ArrayList<std::string>();
    items->add(item1Desc);
    items->add(item2Desc);
    summary->violations->add(new DrcSummaryResponse_DiagnosticViolation(
        "clearance", "error", layerName, explanation, expectedMm, actualMm, shortfallMm, items));
  }

  // 2. Process unconnected items
  for (UnconnectedItems* u : *unconnecteds) {
    std::string item1Desc = formatItemWithContext(u->firstItem);
    std::optional<std::string> item2Desc =
        u->secondItem != nullptr ? std::optional<std::string>(formatItemWithContext(u->secondItem))
                                 : std::nullopt;

    std::string netName = "unknown";
    if (u->firstItem != nullptr && u->firstItem->netCount() > 0) {
      Net* net = board->rules->nets->get(u->firstItem->getNetNumber(0));
      if (net != nullptr) {
        netName = net->name;
      }
    }

    std::string explanation;
    if (u->type == "track_dangling") {
      explanation = "Dangling trace segment on net [" + netName + "] with unconnected endpoint.";
    } else if (u->type == "via_dangling") {
      explanation = "Dangling via on net [" + netName + "] connected on at most one layer.";
    } else if (item2Desc.has_value()) {
      explanation = jrt::format("Unconnected net [%s]: break between %s and %s (%d elements).",
                                netName.c_str(), item1Desc.c_str(), item2Desc->c_str(),
                                u->allItems->size());
    } else {
      explanation = "Unconnected item on net [" + netName + "]: " + item1Desc + ".";
    }

    auto* items = new jrt::ArrayList<std::string>();
    if (u->firstItem != nullptr) {
      items->add(item1Desc);
    }
    if (item2Desc.has_value()) {
      items->add(*item2Desc);
    }

    summary->violations->add(new DrcSummaryResponse_DiagnosticViolation(
        u->type, "warning", "all", explanation, std::nullopt, std::nullopt, std::nullopt, items));
  }

  // 3. Process zone island violations
  jrt::Collection<ZoneIslandViolation*>* zoneViolations = getZoneIslandViolations();
  for (ZoneIslandViolation* zv : *zoneViolations) {
    std::string layerName = (zv->layer >= 0 && zv->layer < board->layerStructure->layers->length)
                                ? (*board->layerStructure->layers)[zv->layer]->name
                                : ("Layer " + jrt::str(zv->layer));
    Net* net = board->rules->nets->get(zv->netNumber);
    std::string netName = (net != nullptr) ? net->name : ("Net " + jrt::str(zv->netNumber));

    auto* items = new jrt::ArrayList<std::string>();
    items->add("Zone on " + layerName + " [" + netName + "]");
    for (Item* it : *zv->itemsInIsland) {
      items->add(formatItemWithContext(it));
    }

    std::string explanation;
    std::string severity;
    if (zv->type == "isolated_island_unconnected") {
      severity = "error";
      if (zv->netNumber > 0) {
        unconnectedNetNumbers->add(zv->netNumber);
      } else {
        danglingStubsCount++;
      }
      explanation = jrt::format(
          "Copper pour on %s [net %s] is fragmented: island contains %d pin(s)/via(s) "
          "disconnected from main pour.",
          layerName.c_str(), netName.c_str(), zv->itemsInIsland->size());
    } else {
      severity = "warning";
      double areaMm2 = zv->getAreaInBoardUnits() / (board->communication->getResolution(Unit::MM) *
                                                    board->communication->getResolution(Unit::MM));
      explanation = jrt::format(
          "Dead copper island detected on %s [net %s]: area %.2f mm\xC2\xB2 with no electrical "
          "connections.",
          layerName.c_str(), netName.c_str(), areaMm2);
    }

    summary->violations->add(new DrcSummaryResponse_DiagnosticViolation(
        zv->type, severity, layerName, explanation, std::nullopt, std::nullopt, std::nullopt,
        items));
  }

  summary->unconnectedNetsCount = unconnectedNetNumbers->size() + danglingStubsCount;

  // 4. Detect spatial congestion zones
  summary->congestionZones = detectCongestionZones(violations);

  // 5. Generate actionable hints
  summary->hints = generateActionableHints(violations, unconnecteds, summary->congestionZones);

  return summary;
}

std::string DesignRulesChecker::formatItemWithContext(Item* item) {
  if (item == nullptr) {
    return "none";
  }
  std::string sb;
  if (auto* pin = dynamic_cast<Pin*>(item)) {
    sb += "Pin ";
    Component* comp = board->components->get(pin->getComponentId());
    if (comp != nullptr) {
      sb += comp->name + ".";
      Package* pkg = comp->getPackage();
      if (pkg != nullptr && pin->pinIndex >= 0 && pin->pinIndex < pkg->pinCount()) {
        sb += pkg->getPin(pin->pinIndex)->name;
      } else {
        sb += jrt::str(pin->pinIndex + 1);
      }
    } else {
      sb += "#" + jrt::str(pin->pinIndex + 1);
    }
  } else if (dynamic_cast<Trace*>(item) != nullptr) {
    sb += "Trace";
  } else if (dynamic_cast<Via*>(item) != nullptr) {
    sb += "Via";
  } else if (dynamic_cast<ConductionArea*>(item) != nullptr) {
    sb += "ConductionArea";
  } else {
    sb += jrt::className(item);
  }

  if (item->netCount() > 0) {
    Net* net = board->rules->nets->get(item->getNetNumber(0));
    if (net != nullptr) {
      sb += " (net " + net->name + ")";
    }
  }
  return sb;
}

jrt::List<DrcSummaryResponse_CongestionZone*>* DesignRulesChecker::detectCongestionZones(
    jrt::Collection<ClearanceViolation*>* violations) {
  auto* zones = new jrt::ArrayList<DrcSummaryResponse_CongestionZone*>();
  if (violations->isEmpty()) {
    return zones;
  }

  // Simple centroid clustering with a 5.0 mm neighborhood threshold
  const double clusterRadiusMm = 5.0;
  auto* points = new jrt::ArrayList<Point*>();
  for (ClearanceViolation* v : *violations) {
    if (v->shape != nullptr) {
      points->add(v->shape->centreOfGravity()->round());
    }
  }

  auto* visited = new jrt::Array<bool>(points->size());
  for (int32_t i = 0; i < points->size(); i++) {
    if ((*visited)[i]) {
      continue;
    }
    (*visited)[i] = true;
    Point* p1 = points->get(i);
    double p1Xmm = p1->toFloat()->x / 10000.0;
    double p1Ymm = p1->toFloat()->y / 10000.0;

    int32_t clusterCount = 1;
    double sumX = p1Xmm;
    double sumY = p1Ymm;

    for (int32_t j = i + 1; j < points->size(); j++) {
      if ((*visited)[j]) {
        continue;
      }
      Point* p2 = points->get(j);
      double p2Xmm = p2->toFloat()->x / 10000.0;
      double p2Ymm = p2->toFloat()->y / 10000.0;
      double dx = p1Xmm - p2Xmm;
      double dy = p1Ymm - p2Ymm;
      if (jrt::Math::hypot(dx, dy) <= clusterRadiusMm) {
        (*visited)[j] = true;
        clusterCount++;
        sumX += p2Xmm;
        sumY += p2Ymm;
      }
    }

    if (clusterCount >= 2) {
      double avgX = sumX / clusterCount;
      double avgY = sumY / clusterCount;
      zones->add(new DrcSummaryResponse_CongestionZone(
          avgX, avgY, clusterRadiusMm, clusterCount,
          jrt::format("High bottleneck concentration: %d violations within %.1f mm of (%.2f, "
                      "%.2f) mm.",
                      clusterCount, clusterRadiusMm, avgX, avgY)));
    }
  }
  return zones;
}

jrt::List<std::string>* DesignRulesChecker::generateActionableHints(
    jrt::Collection<ClearanceViolation*>* violations,
    jrt::Collection<UnconnectedItems*>* unconnecteds,
    jrt::List<DrcSummaryResponse_CongestionZone*>* zones) {
  auto* hints = new jrt::ArrayList<std::string>();

  if (!violations->isEmpty()) {
    double maxShortfall = 0.0;
    for (ClearanceViolation* v : *violations) {
      double shortfall = (v->expectedClearance - v->actualClearance) / 10000.0;
      if (shortfall > maxShortfall) {
        maxShortfall = shortfall;
      }
    }
    if (maxShortfall > 0.0) {
      hints->add(jrt::format(
          "Maximum clearance shortfall is %.3f mm. Consider reducing trace width or clearance "
          "class threshold if manufacturing constraints permit.",
          maxShortfall));
    }
  }

  if (!zones->isEmpty()) {
    hints->add(jrt::format(
        "Detected %d congested hotspot zone(s). Increasing pin escape distance or spreading "
        "component pads in these areas may resolve routing conflicts.",
        zones->size()));
  }

  if (!unconnecteds->isEmpty()) {
    int64_t danglingCount = 0;
    for (UnconnectedItems* u : *unconnecteds) {
      if (u->type == "track_dangling" || u->type == "via_dangling") {
        danglingCount++;
      }
    }
    if (danglingCount > 0) {
      hints->add(jrt::str(danglingCount) +
                 " dangling stub(s) detected. Running an optimizer cleanup pass will prune "
                 "unneeded stubs.");
    }
    int64_t incompleteNetCount = unconnecteds->size() - danglingCount;
    if (incompleteNetCount > 0) {
      hints->add(jrt::str(incompleteNetCount) +
                 " unrouted net connection(s) remain. Verify layer count or allow additional "
                 "routing passes (-mp / maxPasses).");
    }
  }

  if (hints->isEmpty()) {
    hints->add("All design rules satisfied. Board is DRC clean.");
  }

  return hints;
}

}  // namespace fr
