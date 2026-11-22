#include "autoroute/path/FoundConnectionInserter.h"

#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/path/FoundConnectionLocator.h"
#include "autoroute/path/PlannedConnection.h"
#include "board/actions/ForcedViaInserter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/Padstack.h"
#include "datastructures/IdGenerator.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/ViaInfo.h"
#include "rules/ViaRule.h"

namespace fr {

// ---------------------------------------------------------------------------------------------
// FoundConnectionInserter_TerminalAnchor (Java record)
// ---------------------------------------------------------------------------------------------

bool FoundConnectionInserter_TerminalAnchor::equals(jrt::Object* o) {
  if (this == o) {
    return true;
  }
  auto* other = jrt::fast_cast<FoundConnectionInserter_TerminalAnchor>(o);
  if (other == nullptr) {
    return false;
  }
  return jrt::equalsOf<jrt::Set<Item*>*>(component_, other->component_)
         && jrt::equalsOf<Point*>(point_, other->point_) && layer_ == other->layer_
         && fanoutEnd_ == other->fanoutEnd_;
}

int32_t FoundConnectionInserter_TerminalAnchor::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf<jrt::Set<Item*>*>(component_));
  h = jrt::hashCombine(h, jrt::hashOf<Point*>(point_));
  h = jrt::hashCombine(h, jrt::Integer::hashCode(layer_));
  h = jrt::hashCombine(h, jrt::Boolean::hashCode(fanoutEnd_));
  return h;
}

std::string FoundConnectionInserter_TerminalAnchor::toString() {
  return "TerminalAnchor[component=" + jrt::str(component_) + ", point=" + jrt::str(point_)
         + ", layer=" + jrt::str(layer_) + ", fanoutEnd=" + jrt::str(fanoutEnd_) + "]";
}

// ---------------------------------------------------------------------------------------------
// FoundConnectionInserter
// ---------------------------------------------------------------------------------------------

FoundConnectionInserter* FoundConnectionInserter::getInstance(FoundConnectionLocator* connection,
                                                              RoutingBoard* board,
                                                              AutorouteControl* ctrl) {
  if (connection == nullptr || connection->connectionItems == nullptr) {
    return nullptr;
  }
  auto* segments =
      new jrt::Array<PlannedConnection_Segment*>(connection->connectionItems->size());
  int32_t segmentIndex = 0;
  for (FoundConnectionLocator_ResultItem* item : *connection->connectionItems) {
    (*segments)[segmentIndex++] = new PlannedConnection_Segment(item->corners, item->layer);
  }
  return insertSegments(board, ctrl, connection->startItem, connection->targetItem,
                        connection->startLayer, connection->targetLayer, segments);
}

FoundConnectionInserter* FoundConnectionInserter::insertSegments(
    RoutingBoard* board, AutorouteControl* ctrl, Item* startItem, Item* targetItem,
    int32_t startLayer, int32_t targetLayer, jrt::Array<PlannedConnection_Segment*>* segments) {
  if (!isContinuousPlan(segments, board->getLayerCount()) || startLayer < 0
      || startLayer >= board->getLayerCount() || targetLayer < 0
      || targetLayer >= board->getLayerCount()) {
    return nullptr;
  }
  auto* newInstance = new FoundConnectionInserter(board, ctrl);
  PlannedConnection_Segment* lastSegment = (*segments)[segments->length - 1];
  FoundConnectionInserter_TerminalAnchor* startAnchor = newInstance->captureTerminal(
      startItem, (*lastSegment->corners)[lastSegment->corners->length - 1], startLayer, false);
  FoundConnectionInserter_TerminalAnchor* targetAnchor = newInstance->captureTerminal(
      targetItem, (*(*segments)[0]->corners)[0], targetLayer, ctrl->isFanout);
  if (startAnchor == nullptr || targetAnchor == nullptr) {
    return nullptr;
  }
  int32_t currentLayer = targetLayer;
  for (PlannedConnection_Segment* currentNewItem : *segments) {
    if (true) {
      Point* startCorner =
          currentNewItem->corners->length > 0 ? (*currentNewItem->corners)[0] : nullptr;
      Point* endCorner = currentNewItem->corners->length > 0
                             ? (*currentNewItem->corners)[currentNewItem->corners->length - 1]
                             : nullptr;
      FRLogger::trace("compare_trace_connection_item_raw net=" + jrt::str(ctrl->netNumber)
                      + ", item_layer=" + jrt::str(currentNewItem->layer)
                      + ", cornerCount=" + jrt::str(currentNewItem->corners->length)
                      + ", start=" + formatPoint(startCorner) + ", end=" + formatPoint(endCorner));
    }
    if (!newInstance->insertVia((*currentNewItem->corners)[0], currentLayer, currentNewItem->layer)) {
      return nullptr;
    }
    currentLayer = currentNewItem->layer;
    if (!newInstance->insertTrace(currentNewItem)) {
      return nullptr;
    }
  }
  if (!newInstance->insertVia(newInstance->lastCorner, currentLayer, startLayer)) {
    return nullptr;
  }
  if (auto* toTrace = jrt::fast_cast<PolylineTrace>(targetItem)) {
    if (!board->connectToTrace(newInstance->firstCorner, toTrace,
                               (*ctrl->traceHalfWidth)[targetLayer],
                               ctrl->traceClearanceClassIndex)) {
      return nullptr;
    }
  }
  if (auto* toTrace = jrt::fast_cast<PolylineTrace>(startItem)) {
    if (!board->connectToTrace(newInstance->lastCorner, toTrace,
                               (*ctrl->traceHalfWidth)[startLayer],
                               ctrl->traceClearanceClassIndex)) {
      return nullptr;
    }
  }

  // A new bridge can look like a tail until its exact endpoint attachments are complete.
  for (PlannedConnection_Segment* segment : *segments) {
    newInstance->removeTraceStubs(segment);
  }

  board->normalizeTraces(ctrl->netNumber);
  if (!newInstance->connectsTerminals(startAnchor, targetAnchor)) {
    FRLogger::trace("FoundConnectionInserter.insertSegments", "connection_postcondition",
                    "Inserted copper does not connect both terminal components",
                    "Net #" + jrt::str(ctrl->netNumber),
                    jrt::Array<Point*>::of({startAnchor->point(), targetAnchor->point()}));
    return nullptr;
  }
  return newInstance;
}

std::string FoundConnectionInserter::formatPoint(Point* point) {
  if (point == nullptr) {
    return "null";
  }
  if (auto* intPoint = jrt::fast_cast<IntPoint>(point)) {
    return "(" + jrt::str(intPoint->x) + "," + jrt::str(intPoint->y) + ")";
  }
  return point->toString();
}

bool FoundConnectionInserter::insertTrace(PlannedConnection_Segment* trace) {
  if (trace->corners->length == 1) {
    // Single-point trace: the start and end are the same location (already at the target).
    // Set both firstCorner and lastCorner so that connect_to_trace is not called with null.
    if (this->firstCorner == nullptr) {
      this->firstCorner = (*trace->corners)[0];
    }
    this->lastCorner = (*trace->corners)[0];
    return true;
  }

  // switch off correcting connection to pin because it may get wrong in inserting the polygon
  // line for line.
  const double savedEdgeToTurnDist = board->rules->getPinEdgeToTurnDist();
  board->rules->setPinEdgeToTurnDist(-1);

  // Look for pins att the start and the end of trace in case that neckdown is necessary.
  Pin* startPin = nullptr;
  Pin* endPin = nullptr;
  if (ctrl->withNeckdown) {
    auto* itemFilter = new ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::PINS);
    Point* currentEndCorner = (*trace->corners)[0];
    for (int32_t i = 0; i < 2; i++) {
      jrt::Set<Item*>* pickedItems = this->board->pickItems(currentEndCorner, trace->layer, itemFilter);
      for (Item* currentItem : *pickedItems) {
        auto* currentPin = jrt::fast_cast<Pin>(currentItem);
        if (currentPin->containsNet(ctrl->netNumber)
            && currentPin->getCenter()->equals(currentEndCorner)) {
          if (i == 0) {
            startPin = currentPin;
          } else {
            endPin = currentPin;
          }
        }
      }
      currentEndCorner = (*trace->corners)[trace->corners->length - 1];
    }
  }
  auto* netNumbers = new jrt::Array<int32_t>(1);
  (*netNumbers)[0] = ctrl->netNumber;

  int32_t fromCornerNo = 0;
  bool result = true;
  for (int32_t i = 1; i < trace->corners->length; i++) {
    // Arrays.copyOfRange(trace.corners, fromCornerNo, i + 1)
    auto* currentCornerArr = new jrt::Array<Point*>(i + 1 - fromCornerNo);
    for (int32_t k = 0; k < currentCornerArr->length; k++) {
      (*currentCornerArr)[k] = (*trace->corners)[fromCornerNo + k];
    }
    Polyline* insertPolyline = new Polyline(currentCornerArr);
    int32_t maxItemIdBeforeSeg = board->communication->idGenerator->maxGeneratedId();
    Point* okPoint = board->insertForcedTracePolyline(
        insertPolyline, (*ctrl->traceHalfWidth)[trace->layer], trace->layer, netNumbers,
        ctrl->traceClearanceClassIndex, ctrl->maxShoveTraceRecursionDepth,
        ctrl->maxShoveViaRecursionDepth, ctrl->maxSpringOverRecursionDepth,
        jrt::Integer::MAX_VALUE, ctrl->pullTightAccuracy, true, nullptr);
    int32_t maxItemIdAfterSeg = board->communication->idGenerator->maxGeneratedId();
    FRLogger::trace("compare_trace_insert_segment_ids net=" + jrt::str(ctrl->netNumber)
                    + ", i=" + jrt::str(i) + ", maxItemIdBefore=" + jrt::str(maxItemIdBeforeSeg)
                    + ", maxItemIdAfter=" + jrt::str(maxItemIdAfterSeg)
                    + ", delta=" + jrt::str(maxItemIdAfterSeg - maxItemIdBeforeSeg));
    bool neckdownInserted = false;
    bool microNeckdownInserted = false;
    if (okPoint != nullptr && okPoint != insertPolyline->lastCorner() && ctrl->withNeckdown
        && currentCornerArr->length == 2) {
      neckdownInserted =
          insertNeckdown(okPoint, (*currentCornerArr)[1], trace->layer, startPin, endPin);
    }
    // The micro-neckdown narrows the trace below its net-class width, so it is part of automatic
    // neckdown and must stay off when the user disabled it, fanout or not (issue #973).
    if (!neckdownInserted && okPoint != insertPolyline->lastCorner() && ctrl->withNeckdown
        && currentCornerArr->length == 2) {
      microNeckdownInserted = insertFanoutMicroNeckdown(okPoint, (*currentCornerArr)[1],
                                                        trace->layer, netNumbers, startPin, endPin);
    }
    if (okPoint == insertPolyline->lastCorner() || neckdownInserted || microNeckdownInserted) {
      fromCornerNo = i;
      if (true) {
        FRLogger::trace("compare_trace_insert_segment_raw net=" + jrt::str(ctrl->netNumber)
                        + ", layer=" + jrt::str(trace->layer) + ", i=" + jrt::str(i)
                        + ", fromCornerNo=" + jrt::str(fromCornerNo)
                        + ", decision=ADVANCE, neckdown=" + jrt::str(neckdownInserted)
                        + ", micro_neckdown=" + jrt::str(microNeckdownInserted)
                        + ", okPoint=" + formatPoint(okPoint)
                        + ", first=" + formatPoint(insertPolyline->firstCorner())
                        + ", last=" + formatPoint(insertPolyline->lastCorner()));
        FRLogger::trace("FoundConnectionInserter.insert_trace", "compare_trace_insert_segment",
                        "net=" + jrt::str(ctrl->netNumber) + ", layer=" + jrt::str(trace->layer)
                            + ", i=" + jrt::str(i) + ", fromCornerNo=" + jrt::str(fromCornerNo)
                            + ", decision=ADVANCE, neckdown=" + jrt::str(neckdownInserted)
                            + ", micro_neckdown=" + jrt::str(microNeckdownInserted)
                            + ", okPoint=" + jrt::str(okPoint)
                            + ", first=" + jrt::str(insertPolyline->firstCorner())
                            + ", last=" + jrt::str(insertPolyline->lastCorner()),
                        "Net #" + jrt::str(ctrl->netNumber), new jrt::Array<Point*>(0));
      }
    } else if (okPoint == insertPolyline->firstCorner() && i != trace->corners->length - 1) {
      // if okPoint == insertPolyline.firstCorner() the spring over may have failed.
      // Spring over may correct the situation because an insertion, which is ok with clearance
      // compensation may cause violations without clearance compensation.
      // In this case repeating the insertion with more distant corners may allow the spring over
      // to correct the situation.
      if (fromCornerNo > 0) {
        // trace.corners[i] may be inside the offset for the substitute trace around
        // a spring_over obstacle (if clearance compensation is off).
        if (currentCornerArr->length < 3) {
          // first correction
          --fromCornerNo;
        }
      }
      FRLogger::trace("FoundConnectionInserter: violation corrected");
      if (true) {
        FRLogger::trace("compare_trace_insert_segment_raw net=" + jrt::str(ctrl->netNumber)
                        + ", layer=" + jrt::str(trace->layer) + ", i=" + jrt::str(i)
                        + ", fromCornerNo=" + jrt::str(fromCornerNo)
                        + ", decision=VIOLATION_CORRECTED, neckdown=" + jrt::str(neckdownInserted)
                        + ", okPoint=" + formatPoint(okPoint)
                        + ", first=" + formatPoint(insertPolyline->firstCorner())
                        + ", last=" + formatPoint(insertPolyline->lastCorner()));
        FRLogger::trace("FoundConnectionInserter.insert_trace", "compare_trace_insert_segment",
                        "net=" + jrt::str(ctrl->netNumber) + ", layer=" + jrt::str(trace->layer)
                            + ", i=" + jrt::str(i) + ", fromCornerNo=" + jrt::str(fromCornerNo)
                            + ", decision=VIOLATION_CORRECTED, neckdown="
                            + jrt::str(neckdownInserted) + ", okPoint=" + jrt::str(okPoint)
                            + ", first=" + jrt::str(insertPolyline->firstCorner())
                            + ", last=" + jrt::str(insertPolyline->lastCorner()),
                        "Net #" + jrt::str(ctrl->netNumber), new jrt::Array<Point*>(0));
      }
    } else {
      FRLogger::debug("FoundConnectionInserter: insert trace failed for net #"
                      + jrt::str(ctrl->netNumber) + " at corner " + jrt::str(i) + "/"
                      + jrt::str(trace->corners->length - 1) + " on layer "
                      + jrt::str(trace->layer)
                      + ", trace width: " + jrt::str((*ctrl->traceHalfWidth)[trace->layer])
                      + ", from corner: " + jrt::str(fromCornerNo) + ", okPoint: "
                      + (okPoint != nullptr ? okPoint->toString() : std::string("null"))
                      + ", target: " + jrt::str(insertPolyline->lastCorner()));
      traceFanoutDiagnostic(
          "trace_insert_failed",
          "layer=" + jrt::str(trace->layer) + ", corner_index=" + jrt::str(i)
              + ", from_corner_index=" + jrt::str(fromCornerNo)
              + ", traceHalfWidth=" + jrt::str((*ctrl->traceHalfWidth)[trace->layer])
              + ", traceClearanceClass=" + jrt::str(ctrl->traceClearanceClassIndex)
              + ", start_pin_clearance_class="
              + jrt::str(startPin != nullptr ? startPin->clearanceClassIndex() : -1)
              + ", end_pin_clearance_class="
              + jrt::str(endPin != nullptr ? endPin->clearanceClassIndex() : -1)
              + ", okPoint=" + formatPoint(okPoint)
              + ", target=" + formatPoint(insertPolyline->lastCorner()));
      if (true) {
        FRLogger::trace("compare_trace_insert_segment_raw net=" + jrt::str(ctrl->netNumber)
                        + ", layer=" + jrt::str(trace->layer) + ", i=" + jrt::str(i)
                        + ", fromCornerNo=" + jrt::str(fromCornerNo)
                        + ", decision=FAIL, neckdown=" + jrt::str(neckdownInserted)
                        + ", micro_neckdown=" + jrt::str(microNeckdownInserted)
                        + ", okPoint=" + formatPoint(okPoint)
                        + ", first=" + formatPoint(insertPolyline->firstCorner())
                        + ", last=" + formatPoint(insertPolyline->lastCorner()));
        FRLogger::trace("FoundConnectionInserter.insert_trace", "compare_trace_insert_segment",
                        "net=" + jrt::str(ctrl->netNumber) + ", layer=" + jrt::str(trace->layer)
                            + ", i=" + jrt::str(i) + ", fromCornerNo=" + jrt::str(fromCornerNo)
                            + ", decision=FAIL, neckdown=" + jrt::str(neckdownInserted)
                            + ", micro_neckdown=" + jrt::str(microNeckdownInserted)
                            + ", okPoint=" + jrt::str(okPoint)
                            + ", first=" + jrt::str(insertPolyline->firstCorner())
                            + ", last=" + jrt::str(insertPolyline->lastCorner()),
                        "Net #" + jrt::str(ctrl->netNumber), new jrt::Array<Point*>(0));
      }
      result = false;
      break;
    }
  }

  board->rules->setPinEdgeToTurnDist(savedEdgeToTurnDist);
  if (this->firstCorner == nullptr) {
    this->firstCorner = (*trace->corners)[0];
  }
  this->lastCorner = (*trace->corners)[trace->corners->length - 1];
  return result;
}

void FoundConnectionInserter::removeTraceStubs(PlannedConnection_Segment* trace) {
  auto* netNumbers = jrt::Array<int32_t>::of({ctrl->netNumber});
  int32_t removedTraceStubs = 0;
  for (int32_t i = 0; i < trace->corners->length - 1; i++) {
    Trace* traceStub = board->getTraceTail((*trace->corners)[i], trace->layer, netNumbers);
    if (traceStub != nullptr) {
      FRLogger::trace("compare_trace_stub_found net=" + jrt::str(ctrl->netNumber)
                      + ", corner_idx=" + jrt::str(i) + ", corner=" + jrt::str((*trace->corners)[i])
                      + ", stub_id=" + jrt::str(traceStub->getId())
                      + ", stub_first=" + jrt::str(traceStub->firstCorner())
                      + ", stub_last=" + jrt::str(traceStub->lastCorner())
                      + ", startContacts=" + jrt::str(traceStub->getStartContacts()->size())
                      + ", endContacts=" + jrt::str(traceStub->getEndContacts()->size()));
      board->removeItem(traceStub);
      removedTraceStubs++;
    }
  }

  FRLogger::trace("FoundConnectionInserter.insert_trace", "compare_trace_stub_cleanup",
                  "net=" + jrt::str(ctrl->netNumber) + ", layer=" + jrt::str(trace->layer)
                      + ", removed_stubs=" + jrt::str(removedTraceStubs)
                      + ", trace_enabled=" + jrt::str(FRLogger::isTraceEnabled()),
                  "Net #" + jrt::str(ctrl->netNumber), new jrt::Array<Point*>(0));
}

bool FoundConnectionInserter::isContinuousPlan(jrt::Array<PlannedConnection_Segment*>* segments,
                                               int32_t layerCount) {
  if (segments == nullptr || segments->length == 0) {
    return false;
  }
  IntPoint* previousEnd = nullptr;
  for (PlannedConnection_Segment* segment : *segments) {
    if (segment == nullptr || segment->corners == nullptr || segment->corners->length == 0
        || segment->layer < 0 || segment->layer >= layerCount) {
      return false;
    }
    for (IntPoint* corner : *segment->corners) {
      if (corner == nullptr) {
        return false;
      }
    }
    if (previousEnd != nullptr && !previousEnd->equals((*segment->corners)[0])) {
      return false;
    }
    previousEnd = (*segment->corners)[segment->corners->length - 1];
  }
  return true;
}

FoundConnectionInserter_TerminalAnchor* FoundConnectionInserter::captureTerminal(Item* item,
                                                                                IntPoint* endpoint,
                                                                                int32_t layer,
                                                                                bool fanoutEnd) {
  if (item == nullptr) {
    // PORT NOTE: Java Set.of() (immutable empty set) -> an empty HashSet that is never modified.
    return fanoutEnd ? new FoundConnectionInserter_TerminalAnchor(new jrt::HashSet<Item*>(),
                                                                  endpoint, layer, true)
                     : nullptr;
  }
  if (!item->isOnTheBoard() || !item->containsNet(ctrl->netNumber) || !item->isOnLayer(layer)) {
    return nullptr;
  }
  Point* anchor = endpoint;
  auto* trace = jrt::fast_cast<PolylineTrace>(item);
  if (trace != nullptr && !trace->contains(endpoint)) {
    LineSegment* projection = trace->polyline()->projectionLine(endpoint);
    if (projection == nullptr) {
      return nullptr;
    }
    anchor = projection->endPoint();
  } else if (auto* drill = jrt::fast_cast<DrillItem>(item)) {
    anchor = drill->getCenter();
  }
  // Even a fixed terminal can have mutable branches. Preserve reachability of every surviving
  // member, while allowing trace splitting, combining and legitimate via movement.
  jrt::Set<Item*>* component = terminalComponent(item);
  return new FoundConnectionInserter_TerminalAnchor(component, anchor, layer, false);
}

jrt::Set<Item*>* FoundConnectionInserter::resolveTerminal(
    FoundConnectionInserter_TerminalAnchor* anchor) {
  jrt::Set<Item*>* live = new jrt::TreeSet<Item*>();
  for (Item* item : *anchor->component()) {
    if (item->isOnTheBoard() && item->containsNet(ctrl->netNumber)) {
      live->add(item);
    }
  }
  if (!live->isEmpty()) {
    return live;
  }
  for (SearchTreeObject* object :
       *board->overlappingObjects(TileShape::getInstance(anchor->point()), anchor->layer())) {
    auto* item = jrt::fast_cast<Item>(object);
    if (item == nullptr || !item->containsNet(ctrl->netNumber)) {
      continue;
    }
    // Search-tree overlap includes trace widths and clearance compensation, not just contacts.
    if (anchor->fanoutEnd()) {
      auto* via = jrt::fast_cast<Via>(item);
      if (via != nullptr && via->getCenter()->equals(anchor->point())) {
        live->add(item);
      }
    } else if (jrt::fast_cast<Trace>(item) != nullptr
               && jrt::fast_cast<Trace>(item)->contains(anchor->point())) {
      live->add(item);
    } else if (jrt::fast_cast<DrillItem>(item) != nullptr
               && jrt::fast_cast<DrillItem>(item)->getCenter()->equals(anchor->point())) {
      live->add(item);
    } else if (jrt::fast_cast<ConductionArea>(item) != nullptr
               && jrt::fast_cast<ConductionArea>(item)->getArea()->contains(anchor->point())) {
      live->add(item);
    }
  }
  return live;
}

bool FoundConnectionInserter::connectsTerminals(FoundConnectionInserter_TerminalAnchor* start,
                                                FoundConnectionInserter_TerminalAnchor* target) {
  jrt::Set<Item*>* starts = resolveTerminal(start);
  jrt::Set<Item*>* targets = resolveTerminal(target);
  if (starts->isEmpty() || targets->isEmpty()) {
    return false;
  }
  // Seeding from every former member would conceal a component fractured during insertion.
  jrt::Set<Item*>* reached = terminalComponent(starts->iterator()->next());
  return reached->containsAll(starts) && reached->containsAll(targets);
}

jrt::Set<Item*>* FoundConnectionInserter::terminalComponent(Item* start) {
  jrt::Set<Item*>* visited = new jrt::HashSet<Item*>();
  auto* pending = new jrt::ArrayDeque<Item*>();
  pending->add(start);
  visited->add(start);
  while (!pending->isEmpty()) {
    Item* item = pending->removeFirst();
    jrt::Set<Item*>* contacts = item->getNormalContacts();
    if (contacts != nullptr) {
      for (Item* contact : *contacts) {
        if (contact->isOnTheBoard() && contact->containsNet(ctrl->netNumber)
            && visited->add(contact)) {
          pending->addLast(contact);
        }
      }
    }
  }
  return visited;
}

bool FoundConnectionInserter::insertFanoutMicroNeckdown(Point* okPoint, Point* targetPoint,
                                                        int32_t layer,
                                                        jrt::Array<int32_t>* netNumbers,
                                                        Pin* startPin, Pin* endPin) {
  Point* fromPoint = okPoint != nullptr ? okPoint : targetPoint;
  if (fromPoint == nullptr || targetPoint == nullptr || fromPoint->equals(targetPoint)) {
    return false;
  }
  // Automatic neckdown narrows a trace at a pin. Away from the pins of the connection, a
  // narrowed segment is only a way through a gap between foreign copper, below the net-class
  // width the board asked for.
  if (!reachesNeckdownZone(fromPoint, targetPoint, layer, startPin)
      && !reachesNeckdownZone(fromPoint, targetPoint, layer, endPin)) {
    return false;
  }
  int32_t baseHalfWidth = (*ctrl->traceHalfWidth)[layer];
  auto* candidateHalfWidths = new jrt::LinkedHashSet<int32_t>();
  if (startPin != nullptr && startPin->isOnLayer(layer)) {
    candidateHalfWidths->add(startPin->getTraceNeckdownHalfwidth(layer));
  }
  if (endPin != nullptr && endPin->isOnLayer(layer)) {
    candidateHalfWidths->add(endPin->getTraceNeckdownHalfwidth(layer));
  }
  // neck_width_um is the narrowest trace the user allows; the necked retry already routes at
  // that width, so percentage narrowing below it would produce traces the board house cannot
  // make. Pin-neckdown candidates above stay consistent with insertNeckdown so a trace can still
  // enter a pad whose neckdown width is below neck_width_um.
  int32_t minHalfWidth = AutorouteControl::neckHalfWidth(board, ctrl->settings);
  for (int32_t pctHalfWidth :
       *jrt::Array<int32_t>::of({jrt::Math::max(1, (baseHalfWidth * 3) / 4),
                                 jrt::Math::max(1, (baseHalfWidth * 3) / 5),
                                 jrt::Math::max(1, baseHalfWidth / 2)})) {
    if (pctHalfWidth >= minHalfWidth) {
      candidateHalfWidths->add(pctHalfWidth);
    }
  }

  for (int32_t candidateHalfWidth : *candidateHalfWidths) {
    if (candidateHalfWidth <= 0 || candidateHalfWidth >= baseHalfWidth) {
      continue;
    }
    // A candidate that stops short keeps the piece it inserted. Every candidate starts at the
    // same point, so the next one would lay its piece over it: same ends, each touching the
    // others, never a tail for tail removal, and dangling copper in the routed board.
    board->generateSnapshot();
    Point* candidateOkPoint = board->insertForcedTraceSegment(
        fromPoint, targetPoint, candidateHalfWidth, layer, netNumbers,
        ctrl->traceClearanceClassIndex, ctrl->maxShoveTraceRecursionDepth,
        ctrl->maxShoveViaRecursionDepth, ctrl->maxSpringOverRecursionDepth,
        jrt::Integer::MAX_VALUE, ctrl->pullTightAccuracy, true, nullptr);
    if (candidateOkPoint == targetPoint) {
      board->popSnapshot();
      traceFanoutDiagnostic("trace_insert_micro_neckdown_success",
                            "layer=" + jrt::str(layer)
                                + ", candidate_half_width=" + jrt::str(candidateHalfWidth)
                                + ", baseHalfWidth=" + jrt::str(baseHalfWidth)
                                + ", traceClearanceClass="
                                + jrt::str(ctrl->traceClearanceClassIndex)
                                + ", from=" + formatPoint(fromPoint)
                                + ", to=" + formatPoint(targetPoint));
      return true;
    }
    board->undo(nullptr);
  }
  traceFanoutDiagnostic("trace_insert_micro_neckdown_failed",
                        "layer=" + jrt::str(layer) + ", baseHalfWidth=" + jrt::str(baseHalfWidth)
                            + ", traceClearanceClass=" + jrt::str(ctrl->traceClearanceClassIndex)
                            + ", from=" + formatPoint(fromPoint)
                            + ", to=" + formatPoint(targetPoint));
  return false;
}

bool FoundConnectionInserter::reachesNeckdownZone(Point* a, Point* b, int32_t layer, Pin* pin) {
  if (pin == nullptr || !pin->isOnLayer(layer)) {
    return false;
  }
  FloatPoint* pinCenter = pin->getCenter()->toFloat();
  double zone = pinNeckDownDistance(pin, layer);
  return pinCenter->distance(a->toFloat()) < zone || pinCenter->distance(b->toFloat()) < zone;
}

double FoundConnectionInserter::pinNeckDownDistance(Pin* pin, int32_t layer) {
  double clearance = this->board->rules->clearanceMatrix->getValue(
      ctrl->traceClearanceClassIndex, pin->clearanceClassIndex(), layer, true);
  return 2 * (0.5 * pin->getMaxWidth(layer) + clearance);
}

bool FoundConnectionInserter::insertNeckdown(Point* fromCorner, Point* toCorner, int32_t layer,
                                             Pin* startPin, Pin* endPin) {
  if (startPin != nullptr) {
    Point* okPoint = tryNeckDown(toCorner, fromCorner, layer, startPin, true);
    if (okPoint == fromCorner) {
      return true;
    }
  }
  if (endPin != nullptr) {
    Point* okPoint = tryNeckDown(fromCorner, toCorner, layer, endPin, false);
    return okPoint == toCorner;
  }
  return false;
}

Point* FoundConnectionInserter::tryNeckDown(Point* fromCorner, Point* toCorner, int32_t layer,
                                            Pin* pin, bool atStart) {
  if (!pin->isOnLayer(layer)) {
    return nullptr;
  }
  FloatPoint* pinCenter = pin->getCenter()->toFloat();
  if (pinCenter->distance(toCorner->toFloat()) >= pinNeckDownDistance(pin, layer)) {
    return nullptr;
  }

  int32_t neckDownHalfwidth = pin->getTraceNeckdownHalfwidth(layer);
  if (neckDownHalfwidth >= (*ctrl->traceHalfWidth)[layer]) {
    return nullptr;
  }

  FloatPoint* const floatFromCorner = fromCorner->toFloat();
  FloatPoint* const floatToCorner = toCorner->toFloat();

  const int32_t tolerance = 2;

  auto* netNumbers = new jrt::Array<int32_t>(1);
  (*netNumbers)[0] = ctrl->netNumber;

  double okLength =
      board->checkTraceSegment(fromCorner, toCorner, layer, netNumbers,
                               (*ctrl->traceHalfWidth)[layer], ctrl->traceClearanceClassIndex, true);
  if (okLength >= jrt::Integer::MAX_VALUE) {
    return fromCorner;
  }
  okLength -= tolerance;
  Point* neckDownEndPoint;
  if (okLength <= tolerance) {
    neckDownEndPoint = fromCorner;
  } else {
    FloatPoint* floatNeckDownEndPoint = floatFromCorner->changeLength(floatToCorner, okLength);
    neckDownEndPoint = floatNeckDownEndPoint->round();
    // add a corner in case  neckDownEndPoint is not exactly on the line from fromCorner to
    // toCorner
    bool horizontalFirst = jrt::Math::abs(floatFromCorner->x - floatNeckDownEndPoint->x)
                           >= jrt::Math::abs(floatFromCorner->y - floatNeckDownEndPoint->y);
    IntPoint* addCorner =
        FoundConnectionLocator::calculateAdditionalCorner(floatFromCorner, floatNeckDownEndPoint,
                                                          horizontalFirst,
                                                          board->rules->getTraceAngleRestriction())
            ->round();
    Point* currentOkPoint = board->insertForcedTraceSegment(
        fromCorner, addCorner, (*ctrl->traceHalfWidth)[layer], layer, netNumbers,
        ctrl->traceClearanceClassIndex, ctrl->maxShoveTraceRecursionDepth,
        ctrl->maxShoveViaRecursionDepth, ctrl->maxSpringOverRecursionDepth,
        jrt::Integer::MAX_VALUE, ctrl->pullTightAccuracy, true, nullptr);
    if (currentOkPoint != addCorner) {
      return fromCorner;
    }
    currentOkPoint = board->insertForcedTraceSegment(
        addCorner, neckDownEndPoint, (*ctrl->traceHalfWidth)[layer], layer, netNumbers,
        ctrl->traceClearanceClassIndex, ctrl->maxShoveTraceRecursionDepth,
        ctrl->maxShoveViaRecursionDepth, ctrl->maxSpringOverRecursionDepth,
        jrt::Integer::MAX_VALUE, ctrl->pullTightAccuracy, true, nullptr);
    if (currentOkPoint != neckDownEndPoint) {
      return fromCorner;
    }
    addCorner =
        FoundConnectionLocator::calculateAdditionalCorner(floatNeckDownEndPoint, floatToCorner,
                                                          !horizontalFirst,
                                                          board->rules->getTraceAngleRestriction())
            ->round();
    if (!addCorner->equals(toCorner)) {
      currentOkPoint = board->insertForcedTraceSegment(
          neckDownEndPoint, addCorner, (*ctrl->traceHalfWidth)[layer], layer, netNumbers,
          ctrl->traceClearanceClassIndex, ctrl->maxShoveTraceRecursionDepth,
          ctrl->maxShoveViaRecursionDepth, ctrl->maxSpringOverRecursionDepth,
          jrt::Integer::MAX_VALUE, ctrl->pullTightAccuracy, true, nullptr);
      if (currentOkPoint != addCorner) {
        return fromCorner;
      }
      neckDownEndPoint = addCorner;
    }
  }

  return board->insertForcedTraceSegment(
      neckDownEndPoint, toCorner, neckDownHalfwidth, layer, netNumbers,
      ctrl->traceClearanceClassIndex, ctrl->maxShoveTraceRecursionDepth,
      ctrl->maxShoveViaRecursionDepth, ctrl->maxSpringOverRecursionDepth, jrt::Integer::MAX_VALUE,
      ctrl->pullTightAccuracy, true, nullptr);
}

bool FoundConnectionInserter::insertVia(Point* location, int32_t inputFromLayer,
                                        int32_t inputToLayer) {
  if (inputFromLayer == inputToLayer) {
    return true;  // no via necessary
  }
  int32_t fromLayer;
  int32_t toLayer;
  // sort the input layers
  if (inputFromLayer < inputToLayer) {
    fromLayer = inputFromLayer;
    toLayer = inputToLayer;
  } else {
    fromLayer = inputToLayer;
    toLayer = inputFromLayer;
  }
  auto* netNumbers = new jrt::Array<int32_t>(1);
  (*netNumbers)[0] = ctrl->netNumber;
  ViaInfo* viaInfo = nullptr;
  bool foundSuitableSpan = false;
  for (int32_t i = 0; i < this->ctrl->viaRule->viaCount(); i++) {
    ViaInfo* currentViaInfo = this->ctrl->viaRule->getVia(i);
    Padstack* currentViaPadstack = currentViaInfo->getPadstack();
    if (currentViaPadstack->fromLayer() > fromLayer || currentViaPadstack->toLayer() < toLayer) {
      continue;
    }
    foundSuitableSpan = true;
    // Check with the same attach rule the maze search and the insert below use: with
    // via_in_pad or via_touching_pad the via may attach to an SMD pad of its own net.
    if (ForcedViaInserter::check(currentViaInfo, location, netNumbers,
                                 this->ctrl->maxShoveTraceRecursionDepth,
                                 this->ctrl->maxShoveViaRecursionDepth, this->board,
                                 this->ctrl->traceHalfWidth, this->ctrl->traceClearanceClassIndex,
                                 std::optional<bool>(this->ctrl->viaAttachAllowed(currentViaInfo)))
        && this->ctrl->viaHoleAllowed(this->board, currentViaInfo, location, netNumbers)) {
      viaInfo = currentViaInfo;
      break;
    }
  }
  if (viaInfo == nullptr) {
    if (!foundSuitableSpan) {
      FRLogger::debug("FoundConnectionInserter: via mask not found for net #"
                      + jrt::str(ctrl->netNumber) + " covering layers " + jrt::str(fromLayer)
                      + " to " + jrt::str(toLayer));
    } else {
      FRLogger::debug(
          "FoundConnectionInserter: via placement blocked by clearance/shove limits for net #"
          + jrt::str(ctrl->netNumber));
    }
    traceFanoutDiagnostic(
        "via_mask_not_found",
        "fromLayer=" + jrt::str(fromLayer) + ", toLayer=" + jrt::str(toLayer)
            + ", location=" + formatPoint(location)
            + ", traceClearanceClass=" + jrt::str(ctrl->traceClearanceClassIndex)
            + ", viaClearanceClass=" + jrt::str(ctrl->viaClearanceClass)
            + ", trace_half_width_from=" + jrt::str((*ctrl->traceHalfWidth)[fromLayer])
            + ", trace_half_width_to=" + jrt::str((*ctrl->traceHalfWidth)[toLayer]));
    return false;
  }
  // insert the via
  if (!ForcedViaInserter::insert(viaInfo, location, netNumbers,
                                 this->ctrl->traceClearanceClassIndex, this->ctrl->traceHalfWidth,
                                 this->ctrl->maxShoveTraceRecursionDepth,
                                 this->ctrl->maxShoveViaRecursionDepth, this->board,
                                 std::optional<bool>(this->ctrl->viaAttachAllowed(viaInfo)))) {
    FRLogger::debug("FoundConnectionInserter: forced via failed for net #"
                    + jrt::str(ctrl->netNumber));
    traceFanoutDiagnostic(
        "forced_via_insert_failed",
        "fromLayer=" + jrt::str(fromLayer) + ", toLayer=" + jrt::str(toLayer)
            + ", location=" + formatPoint(location)
            + ", selected_via_clearance_class=" + jrt::str(viaInfo->getClearanceClassIndex())
            + ", selected_via_padstack=" + viaInfo->getPadstack()->name
            + ", traceClearanceClass=" + jrt::str(ctrl->traceClearanceClassIndex)
            + ", trace_half_width_from=" + jrt::str((*ctrl->traceHalfWidth)[fromLayer])
            + ", trace_half_width_to=" + jrt::str((*ctrl->traceHalfWidth)[toLayer]));
    return false;
  }
  return true;
}

bool FoundConnectionInserter::shouldTraceFanoutDiagnostics() {
  return ctrl->isFanout && ctrl->fanoutStartPinName.has_value()
         && jrt::startsWith(*ctrl->fanoutStartPinName, "U27-");
}

void FoundConnectionInserter::traceFanoutDiagnostic(const std::string& event,
                                                    const std::string& message) {
  if (!shouldTraceFanoutDiagnostics()) {
    return;
  }
  FRLogger::trace("FANOUT_DIAG event=" + event + ", pin=" + jrt::str(ctrl->fanoutStartPinName)
                  + ", net=" + jrt::str(ctrl->netNumber) + ", " + message);
}

}  // namespace fr
