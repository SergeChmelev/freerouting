// Port of app.freerouting.board.searchtree.ShapeTraceEntries
#include "board/searchtree/ShapeTraceEntries.h"

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ComponentObstacleArea.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/items/ViaObstacleArea.h"
#include "board/model/items/WireObstacleArea.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/ShapeEntrySide.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/BoardObservers.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

ShapeTraceEntries::ShapeTraceEntries(TileShape* shape, int32_t layer,
                                     jrt::Array<int32_t>* ownNetNos, int32_t clearanceClassIndex,
                                     ShapeEntrySide* fromSide, RoutingBoard* board) {
  this->shape = shape;
  this->layer = layer;
  this->ownNetNos = ownNetNos;
  this->clearanceClassIndex = clearanceClassIndex;
  this->fromSide = fromSide;
  this->board = board;
  listAnchor = nullptr;
  tracePieceCount = 0;
  maxStackLevel = 0;
  shoveViaList = new (jrt::kOp) jrt::LinkedList<Via*>();
}

void ShapeTraceEntries::cutoutTrace(PolylineTrace* trace, ConvexShape* shape,
                                    int32_t clearanceClassIndex) {
  if (!trace->isOnTheBoard()) {
    FRLogger::warn("ShapeTraceEntries.cutout_trace : trace is deleted");
    return;
  }
  ConvexShape* offsetShape;
  BasicBoard* board = trace->board;
  ShapeSearchTree* searchTree = board->searchTreeManager->getDefaultTree();
  if (searchTree->isClearanceCompensationUsed()) {
    double currentOffset = trace->getCompensatedHalfWidth(searchTree) +
                           searchTree->additionalClearance(clearanceClassIndex,
                                                           trace->clearanceClassIndex(),
                                                           trace->getLayer()) +
                           c_offset_add;
    offsetShape = shape->offset(currentOffset);
  } else {
    // enlarge the shape in 2 steps  for symmetry reasons
    double clOffset =
        board->clearanceValue(trace->clearanceClassIndex(), clearanceClassIndex, trace->getLayer()) +
        c_offset_add;
    offsetShape = shape->offset(trace->getHalfWidth());
    offsetShape = offsetShape->offset(clOffset);
  }
  Polyline* traceLines = trace->polyline();
  jrt::Array<Polyline*>* pieces = offsetShape->cutout(traceLines);
  if (pieces->length == 1 && (*pieces)[0] == traceLines) {
    // nothing cut off
    return;
  }
  if (pieces->length == 2 && offsetShape->isOutside((*pieces)[0]->firstCorner()) &&
      offsetShape->isOutside((*pieces)[1]->lastCorner())) {
    fastCutoutTrace(trace, (*pieces)[0], (*pieces)[1]);
  } else {
    board->removeItem(trace);
    for (int32_t i = 0; i < pieces->length; i++) {
      board->insertTraceWithoutCleaning((*pieces)[i], trace->getLayer(), trace->getHalfWidth(),
                                        trace->netNumbers, trace->clearanceClassIndex(),
                                        FixedState::UNFIXED);
    }
  }
}

void ShapeTraceEntries::fastCutoutTrace(PolylineTrace* trace, Polyline* startPiece,
                                        Polyline* endPiece) {
  BasicBoard* board = trace->board;
  board->additionalUpdateAfterChange(trace);
  board->itemList->saveForUndo(trace);
  PolylineTrace* startTrace =
      new (jrt::kBoardData) PolylineTrace(startPiece, trace->getLayer(), trace->getHalfWidth(), trace->netNumbers,
                        trace->clearanceClassIndex(), 0, 0, FixedState::UNFIXED, board);
  startTrace->board = board;
  board->itemList->insert(startTrace);
  startTrace->setOnTheBoard(true);

  PolylineTrace* endTrace =
      new (jrt::kBoardData) PolylineTrace(endPiece, trace->getLayer(), trace->getHalfWidth(), trace->netNumbers,
                        trace->clearanceClassIndex(), 0, 0, FixedState::UNFIXED, board);
  endTrace->board = board;
  board->itemList->insert(endTrace);
  endTrace->setOnTheBoard(true);

  board->searchTreeManager->reuseEntriesAfterCutout(trace, startTrace, endTrace);
  board->removeItem(trace);

  if ((board->communication != nullptr) && (board->communication->observers != nullptr)) {
    board->communication->observers->notifyNew(startTrace);
    board->communication->observers->notifyNew(endTrace);
  }
}

bool ShapeTraceEntries::netNosEqual(jrt::Array<int32_t>* netNos1, jrt::Array<int32_t>* netNos2) {
  if (netNos1->length != netNos2->length) {
    return false;
  }
  for (int32_t currentNetNo1 : *netNos1) {
    bool netNoFound = false;
    for (int32_t currentNetNo2 : *netNos2) {
      if (currentNetNo1 == currentNetNo2) {
        netNoFound = true;
        break;
      }
    }
    if (!netNoFound) {
      return false;
    }
  }
  return true;
}

bool ShapeTraceEntries::storeItems(jrt::Collection<Item*>* itemList, bool isPadCheck,
                                   bool copperSharingAllowed) {
  return storeItems(itemList, isPadCheck, copperSharingAllowed, false);
}

bool ShapeTraceEntries::storeItems(jrt::Collection<Item*>* itemList, bool isPadCheck,
                                   bool copperSharingAllowed, bool holeOnly) {
  for (Item* currentItem : *itemList) {
    if ((!isPadCheck && jrt::fast_cast<ViaObstacleArea>(currentItem) != nullptr) ||
        jrt::fast_cast<ComponentObstacleArea>(currentItem) != nullptr ||
        (isPadCheck && jrt::fast_cast<WireObstacleArea>(currentItem) != nullptr)) {
      continue;
    }
    bool containsOwnNet = currentItem->sharesNetNo(this->ownNetNos);
    if (auto* area = jrt::fast_cast<ConductionArea>(currentItem)) {
      if (containsOwnNet || !area->getIsObstacle()) {
        continue;
      }
    }
    if (auto* outline = jrt::fast_cast<BoardOutline>(currentItem)) {
      if (!(isPadCheck ? outline->blocksNets(this->ownNetNos)
                       : outline->blocksTraceAt(this->shape, this->layer, this->ownNetNos,
                                                this->clearanceClassIndex))) {
        continue;
      }
    }
    if (currentItem->isShoveFixed() && !containsOwnNet) {
      this->foundObstacle = currentItem;
      return false;
    }
    auto* ownPin = jrt::fast_cast<Pin>(currentItem);
    if (auto* via = jrt::fast_cast<Via>(currentItem)) {
      if (!containsOwnNet) {
        shoveViaList->add(via);
      }
    } else if (isPadCheck && containsOwnNet && ownPin != nullptr) {
      bool mayOverlap =
          holeOnly || (ownPin->drillAllowed() && (copperSharingAllowed || ownPin->viaMayAttach()));
      if (!mayOverlap && overlapsCopper(ownPin)) {
        this->foundObstacle = currentItem;
        return false;
      }
    } else if (auto* currentTrace = jrt::fast_cast<PolylineTrace>(currentItem)) {
      if (!storeTrace(currentTrace)) {
        return false;
      }
    } else {
      if (containsOwnNet) {
        if (!copperSharingAllowed) {
          this->foundObstacle = currentItem;
          return false;
        }
        auto* pin = jrt::fast_cast<Pin>(currentItem);
        if (isPadCheck && !(pin != nullptr && pin->drillAllowed())) {
          this->foundObstacle = currentItem;
          return false;
        }
      } else {
        this->foundObstacle = currentItem;
        return false;
      }
    }
  }
  searchFromSide();
  resort();
  return calculateStackLevels();
}

bool ShapeTraceEntries::overlapsCopper(Pin* pin) {
  for (int32_t i = 0; i < pin->tileShapeCount(); i++) {
    TileShape* copper = pin->getTileShape(i);
    if (pin->shapeLayer(i) == this->layer && copper != nullptr &&
        this->shape->intersection(copper)->dimension() == 2) {
      return true;
    }
  }
  return false;
}

PolylineTrace* ShapeTraceEntries::nextSubstituteTracePiece() {
  jrt::Array<ShapeTraceEntries_EntryPoint*>* entries = popPiece();
  if (entries == nullptr) {
    return nullptr;
  }
  PolylineTrace* currentTrace = (*entries)[0]->trace;
  TileShape* offsetShape;
  ShapeSearchTree* searchTree = this->board->searchTreeManager->getDefaultTree();
  if (searchTree->isClearanceCompensationUsed()) {
    double currentOffset =
        currentTrace->getCompensatedHalfWidth(searchTree) +
        searchTree->additionalClearance(clearanceClassIndex, currentTrace->clearanceClassIndex(),
                                        layer) +
        c_offset_add;
    offsetShape = jrt::fast_cast<TileShape>(shape->offset(currentOffset));
  } else {
    // enlarge the shape in 2 steps  for symmetry reasons
    offsetShape = jrt::fast_cast<TileShape>(shape->offset(currentTrace->getHalfWidth()));
    double clOffset =
        board->clearanceValue(currentTrace->clearanceClassIndex(), clearanceClassIndex, layer) +
        c_offset_add;
    offsetShape = jrt::fast_cast<TileShape>(offsetShape->offset(clOffset));
  }
  int32_t edgeCount = shape->borderLineCount();
  int32_t edgeDiff = (*entries)[1]->edgeIndex - (*entries)[0]->edgeIndex;

  // calculate the polyline of the substitute trace

  auto* pieceLines = new (jrt::kGeom) jrt::Array<Line*>(edgeDiff + 3);
  // start with the intersecting line of the trace at the start entry.
  (*pieceLines)[0] = (*(*entries)[0]->trace->polyline()->lines)[(*entries)[0]->traceLineNo];
  // end with the intersecting line of the trace at the end entry
  (*pieceLines)[pieceLines->length - 1] =
      (*(*entries)[1]->trace->polyline()->lines)[(*entries)[1]->traceLineNo];
  // fill the interior lines of pieceLines with the appropriate edge
  // lines of the offset shape
  int32_t currentEdgeNo = (*entries)[0]->edgeIndex % edgeCount;
  for (int32_t i = 1; i < pieceLines->length - 1; i++) {
    (*pieceLines)[i] = offsetShape->borderLine(currentEdgeNo);
    if (currentEdgeNo == edgeCount - 1) {
      currentEdgeNo = 0;
    } else {
      ++currentEdgeNo;
    }
  }
  Polyline* piecePolyline = new (jrt::kGeom) Polyline(pieceLines);
  if (piecePolyline->isEmpty()) {
    // no valid trace piece, return the next one
    return nextSubstituteTracePiece();
  }
  return new (jrt::kOp) PolylineTrace(piecePolyline, this->layer, currentTrace->getHalfWidth(),
                           currentTrace->netNumbers, currentTrace->clearanceClassIndex(), 0, 0,
                           FixedState::UNFIXED, this->board);
}

int32_t ShapeTraceEntries::stackDepth() { return maxStackLevel; }

int32_t ShapeTraceEntries::substituteTraceCount() { return tracePieceCount; }

bool ShapeTraceEntries::traceTailsInShape() { return this->shapeContainsTraceTails; }

void ShapeTraceEntries::cutoutTraces(jrt::Collection<Item*>* itemList) {
  for (Item* currentItem : *itemList) {
    auto* trace = jrt::fast_cast<PolylineTrace>(currentItem);
    if (trace != nullptr && !currentItem->sharesNetNo(this->ownNetNos)) {
      cutoutTrace(trace, this->shape, this->clearanceClassIndex);
    }
  }
}

Item* ShapeTraceEntries::getFoundObstacle() { return this->foundObstacle; }

bool ShapeTraceEntries::storeTrace(PolylineTrace* trace) {
  ShapeSearchTree* searchTree = this->board->searchTreeManager->getDefaultTree();
  TileShape* offsetShape;
  if (searchTree->isClearanceCompensationUsed()) {
    double currentOffset = trace->getCompensatedHalfWidth(searchTree) +
                           searchTree->additionalClearance(clearanceClassIndex,
                                                           trace->clearanceClassIndex(),
                                                           trace->getLayer()) +
                           c_offset_add;
    offsetShape = jrt::fast_cast<TileShape>(shape->offset(currentOffset));
  } else {
    // enlarge the shape in 2 steps  for symmetry reasons
    double clOffset = board->clearanceValue(trace->clearanceClassIndex(),
                                            this->clearanceClassIndex, trace->getLayer()) +
                      c_offset_add;
    offsetShape = jrt::fast_cast<TileShape>(shape->offset(trace->getHalfWidth()));
    offsetShape = jrt::fast_cast<TileShape>(offsetShape->offset(clOffset));
  }

  // using enlarge here instead offset causes problems because of a
  // comparison in the constructor of class EntryPoint
  jrt::Array<jrt::Array<int32_t>*>* entries = offsetShape->entrancePoints(trace->polyline());
  for (int32_t i = 0; i < entries->length; i++) {
    jrt::Array<int32_t>* entryTuple = (*entries)[i];
    FloatPoint* entryApprox = (*trace->polyline()->lines)[(*entryTuple)[0]]->intersectionApprox(
        offsetShape->borderLine((*entryTuple)[1]));
    insertEntryPoint(trace, (*entryTuple)[0], (*entryTuple)[1], entryApprox);
  }

  // Look, if an end point of the trace lies in the interior of
  // the shape. This may be the case, if a via touches the shape

  if (!trace->sharesNetNo(ownNetNos)) {
    if (!trace->netsNormal()) {
      return false;
    }
    Point* endCorner = trace->firstCorner();
    jrt::Collection<Item*>* contactList;
    jrt::DisposeOnExit<jrt::Collection<Item*>> contactListOwner(nullptr);
    for (int32_t i = 0; i < 2; i++) {
      if (offsetShape->contains(endCorner)) {
        if (i == 0) {
          contactList = trace->getStartContacts();
          contactListOwner.reset(contactList);
        } else {
          contactList = trace->getEndContacts();
          contactListOwner.reset(contactList);
        }
        int32_t contactCount = 0;
        bool storeEndCorner = true;

        // check for contact object, which is not shovable
        for (Item* contactItem : *contactList) {
          if (!contactItem->isRoutable()) {
            this->foundObstacle = contactItem;
            return false;
          }
          if (auto* contactTrace = jrt::fast_cast<Trace>(contactItem)) {
            if (contactItem->isShoveFixed() ||
                contactTrace->getHalfWidth() != trace->getHalfWidth() ||
                contactItem->clearanceClassIndex() != contactTrace->clearanceClassIndex()) {
              if (offsetShape->containsInside(endCorner)) {
                this->foundObstacle = contactItem;
                return false;
              }
            }
          } else if (auto* via = jrt::fast_cast<Via>(contactItem)) {
            TileShape* viaShape = via->getTileShapeOnLayer(layer);

            double viaTraceDiff =
                viaShape->smallestRadius() - trace->getCompensatedHalfWidth(searchTree);
            int32_t viaClearance = searchTree->additionalClearance(
                contactItem->clearanceClassIndex(), this->clearanceClassIndex, this->layer);
            int32_t traceClearance = searchTree->additionalClearance(
                trace->clearanceClassIndex(), this->clearanceClassIndex, this->layer);
            if (traceClearance > viaClearance) {
              viaTraceDiff += viaClearance - traceClearance;
            }
            if (viaTraceDiff < 0) {
              // the via is smaller than the trace
              this->foundObstacle = contactItem;
              return false;
            }

            if (viaTraceDiff == 0 && !offsetShape->containsInside(endCorner)) {
              // the via need not to be shoved
              storeEndCorner = false;
            }
          }
          ++contactCount;
        }
        if (contactCount == 1 && storeEndCorner) {
          Point* projection = offsetShape->nearestBorderPoint(endCorner);
          {
            int32_t projectionSide = offsetShape->containsOnBorderLineNo(projection);
            int32_t traceLineSegmentNo;
            // the following may not be correct because the trace may not contain a suitable
            // line for the construction of the end line of the substitute trace.
            if (i == 0) {
              traceLineSegmentNo = 0;
            } else {
              traceLineSegmentNo = trace->polyline()->lines->length - 1;
            }

            if (projectionSide >= 0) {
              insertEntryPoint(trace, traceLineSegmentNo, projectionSide, projection->toFloat());
            }
          }
        } else if (contactCount != 1 && offsetShape->containsInside(endCorner)) {
          shapeContainsTraceTails = contactCount == 0;
          // Replacement pieces can project a single movable endpoint contact, but cannot
          // reconstruct an enclosed free endpoint or branching junction. No boundary
          // crossings must not mean "no obstacle", and partial cutouts must not erase
          // these endpoints. Reject for every pad/trace check and insertion, even when
          // other obstacles contribute valid substitute pieces.
          // Safety policy: refuse an unsupported move rather than risk a collision or lost
          // connectivity. Excess compensation can enclose a physically movable junction, so
          // this refusal may require another route or leave a connection unrouted. Recovering
          // such moves requires topology-preserving reconstruction, not weakening this guard.
          this->foundObstacle = trace;
          return false;
        }
      }
      endCorner = trace->lastCorner();
    }
  }
  this->foundObstacle = trace;
  return true;
}

void ShapeTraceEntries::searchFromSide() {
  if (this->fromSide != nullptr && this->fromSide->no >= 0) {
    return;  // from side is already legal
  }
  ShapeTraceEntries_EntryPoint* currentNode = this->listAnchor;
  int32_t currentFromsideNo = 0;
  FloatPoint* currentEntryApprox = nullptr;
  while (currentNode != nullptr) {
    if (currentNode->trace->sharesNetNo(this->ownNetNos)) {
      currentFromsideNo = currentNode->edgeIndex;
      currentEntryApprox = currentNode->entryApprox;
      break;
    }
    currentNode = currentNode->next;
  }
  this->fromSide = new (jrt::kBoard) ShapeEntrySide(currentFromsideNo, currentEntryApprox);
}

void ShapeTraceEntries::resort() {
  int32_t edgeCount = this->shape->borderLineCount();
  if (this->fromSide->no < 0 || fromSide->no >= edgeCount) {
    FRLogger::warn("ShapeTraceEntries.resort: from side not calculated");
    return;
  }
  // resort the intersection points, so that they start in the
  // middle of fromSide.
  FloatPoint* compareCorner1 = shape->cornerApprox(this->fromSide->no);
  FloatPoint* compareCorner2;
  if (fromSide->no == edgeCount - 1) {
    compareCorner2 = shape->cornerApprox(0);
  } else {
    compareCorner2 = shape->cornerApprox(fromSide->no + 1);
  }
  double fromPointDist = 0;
  FloatPoint* fromPointProjection = nullptr;
  if (fromSide->borderIntersection != nullptr) {
    fromPointProjection =
        fromSide->borderIntersection->projectionApprox(shape->borderLine(fromSide->no));
    fromPointDist = fromPointProjection->distanceSquare(compareCorner1);
    if (fromPointDist >= compareCorner1->distanceSquare(compareCorner2)) {
      fromSide = new (jrt::kBoard) ShapeEntrySide(fromSide->no, nullptr);
    }
  }
  // search the first intersection point between the side middle
  // and compareCorner2
  ShapeTraceEntries_EntryPoint* current = listAnchor;
  ShapeTraceEntries_EntryPoint* prev = nullptr;

  while (current != nullptr) {
    if (current->edgeIndex > this->fromSide->no) {
      break;
    }
    if (current->edgeIndex == fromSide->no) {
      if (fromSide->borderIntersection != nullptr) {
        FloatPoint* currentProjection =
            current->entryApprox->projectionApprox(shape->borderLine(fromSide->no));
        if (currentProjection->distanceSquare(compareCorner1) >= fromPointDist &&
            currentProjection->distanceSquare(fromPointProjection) <=
                currentProjection->distanceSquare(compareCorner1)) {
          break;
        }
      } else {
        if (current->entryApprox->distanceSquare(compareCorner2) <=
            current->entryApprox->distanceSquare(compareCorner1)) {
          break;
        }
      }
    }
    prev = current;
    current = prev->next;
  }
  if (current != nullptr && current != listAnchor) {
    rotateEntryListAroundAnchor(current, edgeCount);
  }
  // remove intersections between two other intersections of the same
  // connected set, so that only first and last intersection is kept.
  if (listAnchor == nullptr) {
    return;
  }
  prev = listAnchor;
  jrt::Array<int32_t>* prevNetNos = prev->trace->netNumbers;

  current = listAnchor->next;
  jrt::Array<int32_t>* currentNetNumbers;
  ShapeTraceEntries_EntryPoint* next;

  if (current != nullptr) {
    currentNetNumbers = current->trace->netNumbers;
    next = current->next;
  } else {
    next = nullptr;
    currentNetNumbers = new (jrt::kOp) jrt::Array<int32_t>(0);
  }
  ShapeTraceEntries_EntryPoint* beforePrev = nullptr;
  while (next != nullptr) {
    jrt::Array<int32_t>* nextNetNos = next->trace->netNumbers;
    if (netNosEqual(prevNetNos, currentNetNumbers) && netNosEqual(currentNetNumbers, nextNetNos)) {
      prev->next = next;
    } else {
      beforePrev = prev;
      prev = current;
      prevNetNos = currentNetNumbers;
    }
    currentNetNumbers = nextNetNos;
    current = next;
    next = current->next;
  }

  // remove nodes of own net at start and end of the list
  if (current != nullptr && netNosEqual(currentNetNumbers, ownNetNos)) {
    prev->next = nullptr;
    if (netNosEqual(prevNetNos, ownNetNos)) {
      if (beforePrev != nullptr) {
        beforePrev->next = nullptr;
      } else {
        listAnchor = nullptr;
      }
    }
  }

  if (listAnchor != nullptr && listAnchor->trace->netsEqual(ownNetNos)) {
    listAnchor = listAnchor->next;

    if (listAnchor != nullptr && listAnchor->trace->netsEqual(ownNetNos)) {
      listAnchor = listAnchor->next;
    }
  }
}

bool ShapeTraceEntries::calculateStackLevels() {
  if (listAnchor == nullptr) {
    return true;
  }
  ShapeTraceEntries_EntryPoint* currentEntry = listAnchor;
  jrt::Array<int32_t>* currentNetNumbers = currentEntry->trace->netNumbers;
  int32_t currentLevel;
  if (netNosEqual(currentNetNumbers, this->ownNetNos)) {
    // ignore own net when calculating the stack level
    currentLevel = 0;
  } else {
    currentLevel = 1;
  }

  while (currentEntry != nullptr) {
    if (currentEntry->stackLevel < 0) {  // not yet calculated
      ++tracePieceCount;
      currentEntry->stackLevel = currentLevel;
      if (currentLevel > maxStackLevel) {
        if (maxStackLevel > 1) {
          this->foundObstacle = currentEntry->trace;
        }
        maxStackLevel = currentLevel;
      }
    }

    // set stack level for all entries of the current net;
    ShapeTraceEntries_EntryPoint* checkEntry = currentEntry->next;
    int32_t indexOfNextForeignSet = 0;
    int32_t indexOfLastOccurrenceOfSet = 0;
    int32_t nextIndex = 0;
    ShapeTraceEntries_EntryPoint* lastOwnEntry = nullptr;
    ShapeTraceEntries_EntryPoint* firstForeignEntry = nullptr;

    while (checkEntry != nullptr) {
      ++nextIndex;
      jrt::Array<int32_t>* checkNetNos = checkEntry->trace->netNumbers;
      if (netNosEqual(checkNetNos, currentNetNumbers)) {
        indexOfLastOccurrenceOfSet = nextIndex;
        lastOwnEntry = checkEntry;
        checkEntry->stackLevel = currentEntry->stackLevel;
      } else if (indexOfNextForeignSet == 0) {
        // first occurrence of a foreign connected set
        indexOfNextForeignSet = nextIndex;
        firstForeignEntry = checkEntry;
      }
      checkEntry = checkEntry->next;
    }
    ShapeTraceEntries_EntryPoint* nextEntry;

    if (nextIndex != 0) {
      if (indexOfNextForeignSet != 0 && indexOfNextForeignSet < indexOfLastOccurrenceOfSet) {
        // raise level
        nextEntry = firstForeignEntry;
        if (nextEntry->stackLevel >= 0) {  // already calculated
          // stack property fails
          return false;
        }
        ++currentLevel;
      } else {
        if (indexOfLastOccurrenceOfSet != 0) {
          nextEntry = lastOwnEntry;
        } else {
          nextEntry = firstForeignEntry;
          if (nextEntry->stackLevel >= 0) {  // already calculated
            --currentLevel;
            if (nextEntry->stackLevel != currentLevel) {
              return false;
            }
          }
        }
      }
      currentNetNumbers = nextEntry->trace->netNumbers;
      // remove all entries between currentEntry and nextEntry, because
      // they are irrelevant;
      checkEntry = currentEntry->next;
      while (checkEntry != nextEntry) {
        checkEntry = checkEntry->next;
      }
      currentEntry->next = nextEntry;
      currentEntry = nextEntry;
    } else {
      currentEntry = nullptr;
    }
  }
  if (currentLevel != 1) {
    FRLogger::warn("ShapeTraceEntries.calculate_stack_levels: currentLevel inconsistent");
    return false;
  }
  return true;
}

jrt::Array<ShapeTraceEntries_EntryPoint*>* ShapeTraceEntries::popPiece() {
  if (listAnchor == nullptr) {
    if (this->tracePieceCount != 0) {
      FRLogger::warn("ShapeTraceEntries: tracePieceCount is inconsistent");
    }
    return nullptr;
  }
  ShapeTraceEntries_EntryPoint* first = listAnchor;
  ShapeTraceEntries_EntryPoint* prevFirst = nullptr;

  while (first != nullptr && first->stackLevel != this->maxStackLevel) {
    prevFirst = first;
    first = first->next;
  }
  if (first == nullptr) {
    FRLogger::warn("ShapeTraceEntries: maxStackLevel not found");
    return nullptr;
  }
  auto* result = new (jrt::kOp) jrt::Array<ShapeTraceEntries_EntryPoint*>(2);
  (*result)[0] = first;
  ShapeTraceEntries_EntryPoint* last = first;
  ShapeTraceEntries_EntryPoint* afterLast = first->next;

  while (afterLast != nullptr && afterLast->stackLevel == maxStackLevel &&
         afterLast->trace->netsEqual(static_cast<Item*>(first->trace))) {
    last = afterLast;
    afterLast = last->next;
  }
  (*result)[1] = last;

  // remove the nodes from first to last inclusive

  if (prevFirst != nullptr) {
    prevFirst->next = afterLast;
  } else {
    listAnchor = afterLast;
  }

  // recalculate maxStackLevel;
  maxStackLevel = 0;
  ShapeTraceEntries_EntryPoint* current = listAnchor;
  while (current != nullptr) {
    if (current->stackLevel > maxStackLevel) {
      maxStackLevel = current->stackLevel;
    }
    current = current->next;
  }
  --tracePieceCount;
  if (first->trace->netsEqual(this->ownNetNos)) {
    // own net is ignored and may occur only at the lowest level
    result = popPiece();
  }
  return result;
}

void ShapeTraceEntries::insertEntryPoint(PolylineTrace* trace, int32_t traceLineNo,
                                         int32_t edgeIndex, FloatPoint* entryApprox) {
  auto* newEntry = new (jrt::kOp) ShapeTraceEntries_EntryPoint(trace, traceLineNo, edgeIndex, entryApprox);
  ShapeTraceEntries_EntryPoint* currentPrev = nullptr;
  ShapeTraceEntries_EntryPoint* currentNext = listAnchor;
  // insert the new entry into the sorted list
  while (currentNext != nullptr) {
    if (currentNext->edgeIndex > newEntry->edgeIndex) {
      break;
    }
    if (currentNext->edgeIndex == newEntry->edgeIndex) {
      FloatPoint* prevCorner = shape->cornerApprox(edgeIndex);
      FloatPoint* nextCorner;
      if (edgeIndex == shape->borderLineCount() - 1) {
        nextCorner = shape->cornerApprox(0);
      } else {
        nextCorner = shape->cornerApprox(newEntry->edgeIndex + 1);
      }
      // than the projection of the line from prevCorner to
      // next.entryApprox onto the same line.
      if (prevCorner->scalarProduct(entryApprox, nextCorner) <=
          prevCorner->scalarProduct(currentNext->entryApprox, nextCorner)) {
        break;
      }
    }
    currentPrev = currentNext;
    currentNext = currentNext->next;
  }
  newEntry->next = currentNext;
  if (currentPrev != nullptr) {
    currentPrev->next = newEntry;
  } else {
    listAnchor = newEntry;
  }
}

void ShapeTraceEntries::rotateEntryListAroundAnchor(ShapeTraceEntries_EntryPoint* newAnchor,
                                                    int32_t edgeCount) {
  ShapeTraceEntries_EntryPoint* current = newAnchor;
  ShapeTraceEntries_EntryPoint* prev = nullptr;
  while (current != nullptr) {
    prev = current;
    current = prev->next;
  }
  prev->next = listAnchor;
  current = listAnchor;
  while (current != newAnchor) {
    // add edgeCount to current.side to differentiate points
    // before and after the middle of fromSide
    current->edgeIndex += edgeCount;
    prev = current;
    current = prev->next;
  }
  prev->next = nullptr;
  listAnchor = newAnchor;
}

}  // namespace fr
