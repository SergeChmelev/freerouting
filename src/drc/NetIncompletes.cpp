// Port of app.freerouting.drc.NetIncompletes
#include "drc/NetIncompletes.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "datastructures/Signum.h"
#include "drc/AirLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr {

// PORT NOTE: Java calls the granular FRLogger.trace(...) unconditionally; the port only builds the
// messages when FRLogger::isGranularTraceActive() (trace() is a no-op otherwise), because the
// constructor runs for every net on every ledger update.

NetIncompletes::NetIncompletes(int32_t netNumber, jrt::Collection<Item*>* netItems,
                               BasicBoard* board) {
  this->drawMarkerRadius = board->rules->getMinTraceHalfWidth() * 2;
  this->incompletes = new jrt::LinkedList<AirLine*>();
  this->net = board->rules->nets->get(netNumber);

  bool traceActive = FRLogger::isGranularTraceActive();
  std::string netLabel;
  if (traceActive) {
    netLabel = "Net #" + jrt::str(netNumber) + (net != nullptr ? " (" + net->name + ")" : "");

    FRLogger::trace("NetIncompletes.<init>", "start_calculation",
                    "Starting incomplete calculation: net=" + jrt::str(netNumber) +
                        ", name=" + (net != nullptr ? net->name : std::string("null")) +
                        ", total_items_in_collection=" + jrt::str(netItems->size()),
                    netLabel, new jrt::Array<Point*>(0));
  }

  // Filter out dangling items (vias and tracks with isTail() == true)
  // AND items with zero contacts (unconnected pins/pads)
  // These are DRC violations, not unrouted connections, and should not be counted
  // as incompletes
  jrt::Collection<Item*>* filteredItems = new jrt::LinkedList<Item*>();
  int32_t danglingCount = 0;
  int32_t unconnectedCount = 0;
  int32_t conductionAreaCount = 0;
  int32_t conductionAreaFilteredCount = 0;
  for (Item* item : *netItems) {
    // Track ConductionArea items
    if (jrt::fast_cast<ConductionArea>(item) != nullptr) {
      conductionAreaCount++;
    }

    // Skip dangling vias and traces - they're violations, not incomplete
    // connections
    if (item->isTail()) {
      danglingCount++;
      continue;
    }
    // Skip items with no contacts - they're isolated/unconnected, not incomplete connections
    // EXCEPT for DrillItems (pins/vias) - unrouted pins legitimately have no contacts
    // and SHOULD appear in the ratsnest
    // EXCEPT for ConductionArea which acts as a connection medium, unless it is dead copper
    auto* area = jrt::fast_cast<ConductionArea>(item);
    if (area != nullptr ? area->isDeadCopper() || area->isFloatingFragment(netNumber)
                        : jrt::fast_cast<DrillItem>(item) == nullptr &&
                              item->getNormalContacts()->isEmpty()) {
      unconnectedCount++;
      continue;
    }

    // Track if ConductionArea made it through the filter
    if (jrt::fast_cast<ConductionArea>(item) != nullptr) {
      conductionAreaFilteredCount++;
    }

    filteredItems->add(item);
  }

  if (traceActive) {
    FRLogger::trace("NetIncompletes.<init>", "filtering_complete",
                    "Filtering complete: filteredItems=" + jrt::str(filteredItems->size()) +
                        ", dangling=" + jrt::str(danglingCount) +
                        ", unconnected=" + jrt::str(unconnectedCount) +
                        ", conduction_areas_total=" + jrt::str(conductionAreaCount) +
                        ", conduction_areas_kept=" + jrt::str(conductionAreaFilteredCount),
                    netLabel, new jrt::Array<Point*>(0));
  }

  // Create an array of Item-connectedSet pairs.
  jrt::Array<NetIncompletes_NetItem*>* groupedNetItems = calculateNetItems(filteredItems);

  auto* uniqueConnectedSets = new jrt::HashSet<jrt::Collection<Item*>*>();
  for (NetIncompletes_NetItem* netItem : *groupedNetItems) {
    uniqueConnectedSets->add(netItem->connectedSet);
  }
  this->connectedGroupCount = uniqueConnectedSets->size();

  if (traceActive) {
    FRLogger::trace("NetIncompletes.<init>", "connected_sets_calculated",
                    "Connected sets calculated: net_items_count=" +
                        jrt::str(groupedNetItems->length) +
                        ", uniqueConnectedSets=" + jrt::str(uniqueConnectedSets->size()) +
                        " (for N groups, expect N-1 airlines)",
                    netLabel, new jrt::Array<Point*>(0));
  }

  if (this->connectedGroupCount <= 1) {
    if (traceActive) {
      FRLogger::trace("NetIncompletes.<init>", "fully_connected",
                      "Net is fully connected or has no routable items: netItems=" +
                          jrt::str(groupedNetItems->length),
                      netLabel, new jrt::Array<Point*>(0));
    }
    calcLengthViolation();
    return;
  }

  // create a Delaunay Triangulation for the netItems
  auto* triangulationObjects = new jrt::LinkedList<PlanarDelaunayTriangulation_Storable*>();
  for (NetIncompletes_NetItem* netItem : *groupedNetItems) {
    triangulationObjects->add(netItem);
  }
  PlanarDelaunayTriangulation* triangulation =
      new PlanarDelaunayTriangulation(triangulationObjects);

  // sort the result edges of the triangulation by length in ascending order.
  jrt::Collection<PlanarDelaunayTriangulation_ResultEdge*>* triangulationLines =
      triangulation->getEdgeLines();
  auto* sortedEdges = new jrt::TreeSet<NetIncompletes_Edge*>();

  for (PlanarDelaunayTriangulation_ResultEdge* currentLine : *triangulationLines) {
    NetIncompletes_Edge* newEdge = new NetIncompletes_Edge(
        jrt::fast_cast<NetIncompletes_NetItem>(currentLine->startObject),
        currentLine->startPoint->toFloat(),
        jrt::fast_cast<NetIncompletes_NetItem>(currentLine->endObject),
        currentLine->endPoint->toFloat());
    sortedEdges->add(newEdge);
  }

  // Create the Airlines. Skip edges, whose fromItem and toItem are already in the same
  // connected set or whose connected sets have already an airline.
  Net* currentNet = board->rules->nets->get(netNumber);
  for (NetIncompletes_Edge* currentEdge : *sortedEdges) {
    if (currentEdge->fromItem->connectedSet == currentEdge->toItem->connectedSet) {
      continue;  // airline exists already
    }

    this->incompletes->add(new AirLine(currentNet, currentEdge->fromItem->item,
                                       currentEdge->fromCorner, currentEdge->toItem->item,
                                       currentEdge->toCorner));
    joinConnectedSets(groupedNetItems, currentEdge->fromItem->connectedSet,
                      currentEdge->toItem->connectedSet);
  }

  if (traceActive) {
    FRLogger::trace(
        "NetIncompletes.<init>", "airlines_created",
        "Airlines created: incompleteCount=" + jrt::str(this->incompletes->size()) +
            ", total_items=" + jrt::str(netItems->size()) +
            ", filteredItems=" + jrt::str(filteredItems->size()) +
            ", netItems=" + jrt::str(groupedNetItems->length) +
            ", connected_groups=" + jrt::str(uniqueConnectedSets->size()) +
            " => Formula: total_items - incompleteCount = " +
            jrt::str(netItems->size() - this->incompletes->size()),
        netLabel, new jrt::Array<Point*>(0));
  }

  calcLengthViolation();
}

bool NetIncompletes::calcLengthViolation() {
  double maxLength = this->net->getMaximumTraceLength();
  double minLength = this->net->getMinimumTraceLength();
  if (maxLength <= 0 && minLength <= 0) {
    this->lengthViolation = 0;
    return false;
  }
  double newViolation = 0;
  double traceLength = this->net->getTraceLength();
  if (maxLength > 0 && traceLength > maxLength) {
    newViolation = traceLength - maxLength;
  }
  if (minLength > 0 && traceLength < minLength && this->incompletes->isEmpty()) {
    if (traceLength <= 0 && this->connectedGroupCount <= 1 &&
        this->net->getTerminalItems()->size() < 2) {
      // Empty or single-pin unconnected net with no trace has no minimum violation
    } else {
      newViolation = traceLength - minLength;
    }
  }
  double oldViolation = this->lengthViolation;
  this->lengthViolation = newViolation;
  return jrt::Math::abs(newViolation - oldViolation) > 0.1;
}

jrt::Array<NetIncompletes_NetItem*>* NetIncompletes::calculateNetItems(
    jrt::Collection<Item*>* itemList) {
  auto* result = new jrt::ArrayList<NetIncompletes_NetItem*>();
  // Ordered by item id: on a fractured plane the connected set depends on the start item.
  auto* uniqueItems = new jrt::TreeSet<Item*>(itemList);
  int32_t uniqueItemsCount = uniqueItems->size();

  while (!uniqueItems->isEmpty()) {
    Item* startItem = uniqueItems->first();  // Java: iterator().next()
    jrt::Collection<Item*>* currentConnectedSet = startItem->getConnectedSet(this->net->netNumber);

    // Prevent ConcurrentModificationException by creating a list of items to remove
    jrt::Collection<Item*>* itemsInComponent = new jrt::ArrayList<Item*>();
    for (Item* itemInSet : *currentConnectedSet) {
      if (uniqueItems->contains(itemInSet)) {
        itemsInComponent->add(itemInSet);
      }
    }

    for (Item* currentItem : *itemsInComponent) {
      result->add(new NetIncompletes_NetItem(currentItem, currentConnectedSet));
    }
    uniqueItems->removeAll(itemsInComponent);
  }

  if (result->size() > uniqueItemsCount) {
    FRLogger::warn("NetIncompletes.calculate_net_items: too many items");
  } else if (result->size() < uniqueItemsCount) {
    FRLogger::warn("NetIncompletes.calculate_net_items: too few items");
  }
  return new jrt::Array<NetIncompletes_NetItem*>(result->toVector());
}

void NetIncompletes::joinConnectedSets(jrt::Array<NetIncompletes_NetItem*>* netItems,
                                       jrt::Collection<Item*>* fromConnectedSet,
                                       jrt::Collection<Item*>* toConnectedSet) {
  for (int32_t i = 0; i < netItems->length; i++) {
    NetIncompletes_NetItem* currentItem = (*netItems)[i];
    if (currentItem->connectedSet == fromConnectedSet) {
      toConnectedSet->add(currentItem->item);
      currentItem->connectedSet = toConnectedSet;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// NetIncompletes_Edge
// ---------------------------------------------------------------------------------------------

NetIncompletes_Edge::NetIncompletes_Edge(NetIncompletes_NetItem* fromItem, FloatPoint* fromCorner,
                                         NetIncompletes_NetItem* toItem, FloatPoint* toCorner)
    : fromItem(fromItem), fromCorner(fromCorner), toItem(toItem), toCorner(toCorner) {
  lengthSquare = toCorner->distanceSquare(fromCorner);
}

int32_t NetIncompletes_Edge::compareTo(NetIncompletes_Edge* other) {
  double result = this->lengthSquare - other->lengthSquare;
  if (result == 0) {
    // prevent result 0, so that edges with the same length as another edge are not skipped in
    // the set
    result = this->fromCorner->x - other->fromCorner->x;
    if (result == 0) {
      result = this->fromCorner->y - other->fromCorner->y;
    }
    if (result == 0) {
      result = this->toCorner->x - other->toCorner->x;
    }
    if (result == 0) {
      result = this->toCorner->y - other->toCorner->y;
    }
  }
  return Signum::asInt(result);
}

// ---------------------------------------------------------------------------------------------
// NetIncompletes_NetItem
// ---------------------------------------------------------------------------------------------

jrt::Array<Point*>* NetIncompletes_NetItem::getTriangulationCorners() {
  return this->item->getRatsnestCorners();
}

}  // namespace fr
