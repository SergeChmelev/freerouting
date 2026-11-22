// Port of app.freerouting.board.trace.PolylineTrace.
#include "board/trace/PolylineTrace.h"

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/FixedState.h"
#include "board/optimize/TraceTightener.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/BoardObservers.h"
#include "board/state/ChangedArea.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTraceGeometry.h"
#include "board/trace/PolylineTraceNormalization.h"
#include "board/trace/PolylineTraceSearchTreeAdapter.h"
#include "datastructures/ShapeTree.h"
#include "datastructures/Signum.h"
#include "datastructures/Stoppable.h"
#include "datastructures/UndoableObjects.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"

namespace fr {

PolylineTrace::PolylineTrace(Polyline* polyline, int32_t layer, int32_t halfWidth,
                             jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                             int32_t id, int32_t groupId, FixedState fixedState,
                             BasicBoard* board)
    : Trace(layer, halfWidth, netNumbers, clearanceClassIndex, id, groupId, fixedState, board) {
  if (polyline->lines->length < 3) {
    FRLogger::warn("PolylineTrace: polyline.lines.length >= 3 expected");
  }
  lines = polyline;
}

Item* PolylineTrace::copy(int32_t id) {
  jrt::Array<int32_t>* currentNetNumbers = new jrt::Array<int32_t>(this->netCount());
  for (int32_t i = 0; i < currentNetNumbers->length; i++) {
    (*currentNetNumbers)[i] = getNetNumber(i);
  }
  return new PolylineTrace(lines, getLayer(), getHalfWidth(), currentNetNumbers,
                           clearanceClassIndex(), id, getComponentId(), getFixedState(), board);
}

Point* PolylineTrace::firstCorner() { return PolylineTraceGeometry::firstCorner(lines); }

Point* PolylineTrace::lastCorner() { return PolylineTraceGeometry::lastCorner(lines); }

int32_t PolylineTrace::cornerCount() { return PolylineTraceGeometry::cornerCount(lines); }

double PolylineTrace::getLength() { return PolylineTraceGeometry::length(lines); }

IntBox* PolylineTrace::boundingBox() {
  return PolylineTraceGeometry::boundingBox(lines, getHalfWidth());
}

bool PolylineTrace::contains(Point* point) {
  if (point == nullptr || point->isInfinite()) {
    return false;
  }
  Point* first = firstCorner();
  Point* last = lastCorner();
  if ((first != nullptr && point->equals(first)) || (last != nullptr && point->equals(last))) {
    return true;
  }
  if (!point->isContainedIn(boundingBox())) {
    return false;
  }
  return lines != nullptr && lines->contains(point);
}

bool PolylineTrace::centreLineContains(Point* point) {
  if (point == nullptr || lines == nullptr || lines->lines == nullptr) {
    return false;
  }
  jrt::Array<Line*>* segmentLines = lines->lines;
  for (int32_t i = 1; i < segmentLines->length - 1; i++) {
    if ((*segmentLines)[i]->sideOf(point) != Side::COLLINEAR()) {
      continue;
    }
    Side* fromStart = (*segmentLines)[i - 1]->sideOf(point);
    Side* fromEnd = (*segmentLines)[i + 1]->sideOf(point);
    if ((fromStart == Side::COLLINEAR() ||
         fromStart == (*segmentLines)[i - 1]->sideOf(lines->corner(i))) &&
        (fromEnd == Side::COLLINEAR() ||
         fromEnd == (*segmentLines)[i + 1]->sideOf(lines->corner(i - 1)))) {
      return true;
    }
  }
  return false;
}

jrt::Array<TileShape*>* PolylineTrace::calculateTreeShapes(ShapeSearchTree* searchTree) {
  return PolylineTraceSearchTreeAdapter::calculateTreeShapes(this, searchTree);
}

int32_t PolylineTrace::tileShapeCount() { return PolylineTraceGeometry::tileShapeCount(lines); }

void PolylineTrace::translateBy(Vector* vector) {
  lines = PolylineTraceGeometry::translate(lines, vector);
  this->clearDerivedData();
}

void PolylineTrace::turn90Degree(int32_t factor, IntPoint* pole) {
  lines = PolylineTraceGeometry::turn90Degree(lines, factor, pole);
  this->clearDerivedData();
}

void PolylineTrace::rotateApprox(double angleInDegree, FloatPoint* pole) {
  this->lines = PolylineTraceGeometry::rotateApprox(lines, angleInDegree, pole);
}

void PolylineTrace::changePlacementSide(IntPoint* pole) {
  lines = PolylineTraceGeometry::mirrorVertical(lines, pole);

  if (this->board != nullptr) {
    this->setLayer(board->getLayerCount() - this->getLayer() - 1);
  }
  this->clearDerivedData();
}

bool PolylineTrace::combine() {
  // Iterative instead of recursive (see Java comment): retry at the start first, then at the end.
  bool somethingChanged = false;
  while (this->isOnTheBoard() && (this->combineAtStart(true) || this->combineAtEnd(true))) {
    somethingChanged = true;
    // let the observers synchronize the changes
    if ((board->communication != nullptr) && (board->communication->observers != nullptr)) {
      board->communication->observers->notifyChanged(this);
    }
    board->additionalUpdateAfterChange(this);
  }
  return somethingChanged;
}

bool PolylineTrace::combineAtStart(bool ignoreAreas) {
  bool debugNet49 = this->netNumbers != nullptr && this->netNumbers->length > 0 &&
                    (*this->netNumbers)[0] == 49;
  Point* startCorner = firstCorner();
  jrt::Collection<Item*>* contacts = getNormalContacts(startCorner, false);
  if (ignoreAreas) {
    // remove conduction areas from the list
    contacts->removeIf([](Item* x) { return jrt::fast_cast<ConductionArea>(x) != nullptr; });
  }
  if (debugNet49) {
    FRLogger::trace("compare_trace_combine_at_start_net49 thisId=" + jrt::str(this->getId()) +
                    ", thisFixed=" + FixedState_toString(this->getFixedState()) +
                    ", start=" + jrt::str(startCorner) +
                    ", contacts=" + jrt::str(contacts->size()));
    for (Item* c : *contacts) {
      Trace* t = jrt::fast_cast<Trace>(c);
      FRLogger::trace("  contact id=" + jrt::str(c->getId()) + ", type=" + jrt::className(c) +
                      ", fixed=" + FixedState_toString(c->getFixedState()) +
                      (t != nullptr ? ", first=" + jrt::str(t->firstCorner()) +
                                          ", last=" + jrt::str(t->lastCorner())
                                    : std::string("")));
    }
  }
  if (contacts->size() != 1) {
    return false;
  }
  PolylineTrace* otherTrace = nullptr;
  bool traceFound = false;
  bool reverseOrder = false;
  for (Item* currentObject : *contacts) {
    if (auto* trace = jrt::fast_cast<PolylineTrace>(currentObject)) {
      otherTrace = trace;
      if (otherTrace->getLayer() == getLayer() && otherTrace->netsEqual(this) &&
          otherTrace->getHalfWidth() == getHalfWidth() &&
          otherTrace->getFixedState() == this->getFixedState() &&
          !otherTrace->isDeletionForbidden() && !this->isDeletionForbidden()) {
        if (startCorner->equals(otherTrace->lastCorner())) {
          traceFound = true;
          break;
        } else if (startCorner->equals(otherTrace->firstCorner())) {
          reverseOrder = true;
          traceFound = true;
          break;
        }
      } else if (debugNet49) {
        FRLogger::trace("  combineAtStart REJECTED: layer=" + jrt::str(otherTrace->getLayer()) +
                        "==" + jrt::str(getLayer()) +
                        ", nets=" + jrt::str(otherTrace->netsEqual(this)) +
                        ", width=" + jrt::str(otherTrace->getHalfWidth()) + "==" +
                        jrt::str(getHalfWidth()) +
                        ", fixed=" + FixedState_toString(otherTrace->getFixedState()) + "==" +
                        FixedState_toString(this->getFixedState()));
      }
    }
  }
  if (!traceFound) {
    return false;
  }

  board->itemList->saveForUndo(this);
  // create the lines of the joined polyline
  jrt::Array<Line*>* thisLines = lines->lines;
  jrt::Array<Line*>* otherLines;
  if (reverseOrder) {
    otherLines = new jrt::Array<Line*>(otherTrace->lines->lines->length);
    for (int32_t i = 0; i < otherLines->length; i++) {
      (*otherLines)[i] = (*otherTrace->lines->lines)[otherLines->length - 1 - i]->opposite();
    }
  } else {
    otherLines = otherTrace->lines->lines;
  }
  bool skipLine = (*otherLines)[otherLines->length - 2]->isEqualOrOpposite((*thisLines)[1]);
  int32_t newLineCount = thisLines->length + otherLines->length - 2;
  if (skipLine) {
    --newLineCount;
  }
  jrt::Array<Line*>* newLines = new jrt::Array<Line*>(newLineCount);
  jrt::arraycopy(otherLines, 0, newLines, 0, otherLines->length - 1);
  int32_t joinPos = otherLines->length - 1;
  if (skipLine) {
    --joinPos;
  }
  jrt::arraycopy(thisLines, 1, newLines, joinPos, thisLines->length - 1);
  Polyline* joinedPolyline = new Polyline(newLines);
  // Determine whether both traces have entries in the default search tree. If either is missing,
  // fall back to the safe full-remove + re-insert path.
  bool hasTreeEntries = PolylineTraceSearchTreeAdapter::hasDefaultEntries(this, otherTrace);
  if (joinedPolyline->lines->length != newLineCount || !hasTreeEntries) {
    // consecutive parallel lines were skipped at the join location OR a trace
    // lacks search-tree entries - combine without performance optimization
    PolylineTraceSearchTreeAdapter::replaceGeometry(this, joinedPolyline);
  } else {
    // reuse the tree entries for better performance
    // create the changed line shape at the join location
    int32_t toNo = otherLines->length;
    if (skipLine) {
      --toNo;
    }
    PolylineTraceSearchTreeAdapter::mergeEntriesInFront(otherTrace, this, joinedPolyline,
                                                        otherLines->length - 3, toNo);
    otherTrace->clearSearchTreeEntries();
    this->lines = joinedPolyline;
  }
  if (this->lines->lines->length < 3) {
    board->removeItem(this);
  }
  board->removeItem(otherTrace);
  if (auto* routingBoard = jrt::fast_cast<RoutingBoard>(board)) {
    routingBoard->joinChangedArea(startCorner->toFloat(), getLayer());
  }
  return true;
}

bool PolylineTrace::combineAtEnd(bool ignoreAreas) {
  bool debugNet49 = this->netNumbers != nullptr && this->netNumbers->length > 0 &&
                    (*this->netNumbers)[0] == 49;
  Point* endCorner = lastCorner();
  jrt::Collection<Item*>* contacts = getNormalContacts(endCorner, false);
  if (ignoreAreas) {
    // remove conduction areas from the list
    contacts->removeIf([](Item* x) { return jrt::fast_cast<ConductionArea>(x) != nullptr; });
  }
  if (debugNet49) {
    FRLogger::trace("compare_trace_combine_at_end_net49 thisId=" + jrt::str(this->getId()) +
                    ", thisFixed=" + FixedState_toString(this->getFixedState()) +
                    ", end=" + jrt::str(endCorner) +
                    ", contacts=" + jrt::str(contacts->size()));
    for (Item* c : *contacts) {
      Trace* t = jrt::fast_cast<Trace>(c);
      FRLogger::trace("  contact id=" + jrt::str(c->getId()) + ", type=" + jrt::className(c) +
                      ", fixed=" + FixedState_toString(c->getFixedState()) +
                      (t != nullptr ? ", first=" + jrt::str(t->firstCorner()) +
                                          ", last=" + jrt::str(t->lastCorner())
                                    : std::string("")));
    }
  }
  if (contacts->size() != 1) {
    return false;
  }
  PolylineTrace* otherTrace = nullptr;
  bool traceFound = false;
  bool reverseOrder = false;
  for (Item* currentObject : *contacts) {
    if (auto* trace = jrt::fast_cast<PolylineTrace>(currentObject)) {
      otherTrace = trace;
      if (otherTrace->getLayer() == getLayer() && otherTrace->netsEqual(this) &&
          otherTrace->getHalfWidth() == getHalfWidth() &&
          otherTrace->getFixedState() == this->getFixedState() &&
          !otherTrace->isDeletionForbidden() && !this->isDeletionForbidden()) {
        if (endCorner->equals(otherTrace->firstCorner())) {
          traceFound = true;
          break;
        } else if (endCorner->equals(otherTrace->lastCorner())) {
          reverseOrder = true;
          traceFound = true;
          break;
        }
      }
    }
  }
  if (!traceFound) {
    return false;
  }

  board->itemList->saveForUndo(this);
  // create the lines of the joined polyline
  jrt::Array<Line*>* thisLines = lines->lines;
  jrt::Array<Line*>* otherLines;
  if (reverseOrder) {
    otherLines = new jrt::Array<Line*>(otherTrace->lines->lines->length);
    for (int32_t i = 0; i < otherLines->length; i++) {
      (*otherLines)[i] = (*otherTrace->lines->lines)[otherLines->length - 1 - i]->opposite();
    }
  } else {
    otherLines = otherTrace->lines->lines;
  }
  bool skipLine = (*thisLines)[thisLines->length - 2]->isEqualOrOpposite((*otherLines)[1]);
  int32_t newLineCount = thisLines->length + otherLines->length - 2;
  if (skipLine) {
    --newLineCount;
  }
  jrt::Array<Line*>* newLines = new jrt::Array<Line*>(newLineCount);
  jrt::arraycopy(thisLines, 0, newLines, 0, thisLines->length - 1);
  int32_t joinPos = thisLines->length - 1;
  if (skipLine) {
    --joinPos;
  }
  jrt::arraycopy(otherLines, 1, newLines, joinPos, otherLines->length - 1);
  Polyline* joinedPolyline = new Polyline(newLines);
  // Determine whether both traces have entries in the default search tree. If either is missing,
  // fall back to the safe full-remove + re-insert path.
  bool hasTreeEntries = PolylineTraceSearchTreeAdapter::hasDefaultEntries(this, otherTrace);
  if (joinedPolyline->lines->length != newLineCount || !hasTreeEntries) {
    // consecutive parallel lines were skipped at the join location OR a trace
    // lacks search-tree entries - combine without performance optimization
    PolylineTraceSearchTreeAdapter::replaceGeometry(this, joinedPolyline);
  } else {
    // reuse tree entries for better performance
    // create the changed line shape at the join location
    int32_t toNo = thisLines->length;
    if (skipLine) {
      --toNo;
    }
    PolylineTraceSearchTreeAdapter::mergeEntriesAtEnd(otherTrace, this, joinedPolyline,
                                                      thisLines->length - 3, toNo);
    otherTrace->clearSearchTreeEntries();
    this->lines = joinedPolyline;
  }
  if (this->lines->lines->length < 3) {
    board->removeItem(this);
  }
  board->removeItem(otherTrace);
  if (auto* routingBoard = jrt::fast_cast<RoutingBoard>(board)) {
    routingBoard->joinChangedArea(endCorner->toFloat(), getLayer());
  }
  return true;
}

jrt::Collection<PolylineTrace*>* PolylineTrace::split(IntOctagon* clipShape) {
  jrt::Collection<PolylineTrace*>* result = new jrt::LinkedList<PolylineTrace*>();
  if (!this->netsNormal()) {
    // only normal nets are split
    result->add(this);
    return result;
  }
  bool ownTraceSplit = false;
  ShapeSearchTree* defaultTree = board->searchTreeManager->getDefaultTree();
  for (int32_t i = 0; i < this->lines->lines->length - 2; i++) {
    if (clipShape != nullptr) {
      LineSegment* currentSegment = new LineSegment(this->lines, i + 1);
      if (!clipShape->intersects(currentSegment->boundingBox())) {
        continue;
      }
    }
    TileShape* currentShape = this->getTreeShape(defaultTree, i);
    LineSegment* currentLineSegment = new LineSegment(this->lines, i + 1);
    jrt::Collection<ShapeTree_TreeEntry*>* overlappingTreeEntries =
        new jrt::LinkedList<ShapeTree_TreeEntry*>();
    // look for intersecting traces with the i-th line segment
    defaultTree->overlappingTreeEntries(currentShape, getLayer(), overlappingTreeEntries);
    jrt::Iterator<ShapeTree_TreeEntry*>* it = overlappingTreeEntries->iterator();
    while (it->hasNext()) {
      if (!this->isOnTheBoard()) {
        // this trace has been deleted in a cleanup operation
        return result;
      }
      ShapeTree_TreeEntry* foundEntry = it->next();
      Item* foundItem = jrt::fast_cast<Item>(foundEntry->object);
      if (foundItem == nullptr) {
        continue;
      }
      if (foundItem == this) {
        if (foundEntry->shapeIndexInObject >= i - 1 && foundEntry->shapeIndexInObject <= i + 1) {
          // don't split own trace at this line or at neighbour lines
          continue;
        }
        // try to handle intermediate segments of length 0 by comparing end corners
        if (i < foundEntry->shapeIndexInObject) {
          if (lines->corner(i + 1)->equals(lines->corner(foundEntry->shapeIndexInObject))) {
            continue;
          }
        } else {
          if (lines->corner(foundEntry->shapeIndexInObject + 1)->equals(lines->corner(i))) {
            continue;
          }
        }
      }
      if (!foundItem->sharesNet(this)) {
        continue;
      }
      if (auto* foundTrace = jrt::fast_cast<PolylineTrace>(foundItem)) {
        LineSegment* foundLineSegment =
            new LineSegment(foundTrace->lines, foundEntry->shapeIndexInObject + 1);
        jrt::Array<Line*>* intersectingLines = foundLineSegment->intersection(currentLineSegment);
        jrt::Collection<PolylineTrace*>* splitPieces = new jrt::LinkedList<PolylineTrace*>();

        bool debugNet49 = this->netNumbers != nullptr && this->netNumbers->length > 0 &&
                          (*this->netNumbers)[0] == 49;
        if (debugNet49 && intersectingLines->length > 0) {
          FRLogger::trace(
              "compare_trace_split_found_trace net=49, this_id=" + jrt::str(this->getId()) +
              ", this_seg=" + jrt::str(i) + ", this_first=" + jrt::str(this->firstCorner()) +
              ", this_last=" + jrt::str(this->lastCorner()) +
              ", found_id=" + jrt::str(foundTrace->getId()) +
              ", found_seg=" + jrt::str(foundEntry->shapeIndexInObject) +
              ", found_first=" + jrt::str(foundTrace->firstCorner()) +
              ", found_last=" + jrt::str(foundTrace->lastCorner()) +
              ", intersections=" + jrt::str(intersectingLines->length));
        }

        // try splitting the found trace first
        bool foundTraceSplit = false;

        if (foundTrace != this) {
          for (int32_t j = 0; j < intersectingLines->length; j++) {
            int32_t lineIndex = foundEntry->shapeIndexInObject + 1;
            jrt::Array<PolylineTrace*>* currentSplitPieces =
                foundTrace->split(lineIndex, (*intersectingLines)[j]);
            if (currentSplitPieces != nullptr) {
              for (int32_t k = 0; k < 2; k++) {
                if ((*currentSplitPieces)[k] != nullptr) {
                  foundTraceSplit = true;
                  if (this->netNumbers->length > 0 && (*this->netNumbers)[0] == 94) {
                    FRLogger::trace(
                        "PolylineTrace.split", "compare_trace_found_trace_split",
                        "foundTraceSplit=true at line index " +
                            jrt::str(foundEntry->shapeIndexInObject) + " with intersection " +
                            jrt::str((*intersectingLines)[j]),
                        "Net #" + jrt::str((*this->netNumbers)[0]) + ",Trace #" +
                            jrt::str(foundTrace->getId()) + ",Layer #" +
                            jrt::str(foundTrace->getLayer()),
                        jrt::Array<Point*>::of({foundTrace->firstCorner(), foundTrace->lastCorner(),
                                                lines->corner(i), lines->corner(i + 1)}));
                  }
                  splitPieces->add((*currentSplitPieces)[k]);
                }
              }
              if (foundTraceSplit) {
                // reread the overlapping tree entries and reset the iterator,
                // because the board has changed
                defaultTree->overlappingTreeEntries(currentShape, getLayer(),
                                                    overlappingTreeEntries);
                it = overlappingTreeEntries->iterator();
                break;
              }
            }
          }
          if (!foundTraceSplit) {
            splitPieces->add(foundTrace);
          }
        }
        // now try splitting the own trace

        intersectingLines = currentLineSegment->intersection(foundLineSegment);
        for (int32_t j = 0; j < intersectingLines->length; j++) {
          jrt::Array<PolylineTrace*>* currentSplitPieces = split(i + 1, (*intersectingLines)[j]);
          if (currentSplitPieces != nullptr) {
            ownTraceSplit = true;
            // this trace was split itself into 2.
            if ((*currentSplitPieces)[0] != nullptr) {
              result->addAll((*currentSplitPieces)[0]->split(clipShape));
            }
            if ((*currentSplitPieces)[1] != nullptr) {
              result->addAll((*currentSplitPieces)[1]->split(clipShape));
            }
            break;
          }
        }
        if (foundTraceSplit || ownTraceSplit) {
          // something was split,
          // remove cycles containing a split piece
          jrt::Iterator<PolylineTrace*>* it2 = splitPieces->iterator();
          for (int32_t j = 0; j < 2; j++) {
            while (it2->hasNext()) {
              PolylineTrace* currentPiece = it2->next();
              bool debugThis = this->netNumbers != nullptr && this->netNumbers->length > 0 &&
                               (*this->netNumbers)[0] == 49;
              Point* pieceFirst =
                  debugThis && currentPiece->isOnTheBoard() ? currentPiece->firstCorner() : nullptr;
              Point* pieceLast =
                  debugThis && currentPiece->isOnTheBoard() ? currentPiece->lastCorner() : nullptr;
              int32_t pieceId = currentPiece->getId();
              bool removedAsCycle = board->removeIfCycle(currentPiece);
              if (debugThis && removedAsCycle) {
                FRLogger::trace("compare_trace_split_cycle_removed net=49, pass=" + jrt::str(j) +
                                ", piece_id=" + jrt::str(pieceId) +
                                ", piece_first=" + jrt::str(pieceFirst) +
                                ", piece_last=" + jrt::str(pieceLast) +
                                ", this_id=" + jrt::str(this->getId()));
              }
            }

            // remove cycles in the own split pieces last
            // to preserve them, if possible
            it2 = result->iterator();
          }
        }
        if (ownTraceSplit) {
          break;
        }
      } else if (auto* currentDrillItem = jrt::fast_cast<DrillItem>(foundItem)) {
        Point* splitPoint = currentDrillItem->getCenter();
        if (currentLineSegment->contains(splitPoint)) {
          Direction* splitLineDirection = currentLineSegment->getLine()->direction()->turn45Degree(2);
          Line* splitLine = new Line(splitPoint, splitLineDirection);
          split(i + 1, splitLine);
        }
      } else if (!this->isUserFixed() && jrt::fast_cast<ConductionArea>(foundItem) != nullptr) {
        auto* foundArea = jrt::fast_cast<ConductionArea>(foundItem);
        bool ignoreAreas = false;
        if (this->netNumbers->length > 0) {
          Net* currentNet = this->board->rules->nets->get((*this->netNumbers)[0]);
          if (currentNet != nullptr && currentNet->getNetClass() != nullptr) {
            ignoreAreas = currentNet->getNetClass()->getIgnoreCyclesWithAreas();
          }
        }
        if (!ignoreAreas && this->getStartContacts()->contains(foundItem) &&
            this->getEndContacts()->contains(foundItem) && foundArea->joinsEndsOf(this)) {
          // this trace can be removed because of cycle with conduction area
          board->removeItem(this);
          return result;
        }
      }
    }
    if (ownTraceSplit) {
      break;
    }
  }
  if (!ownTraceSplit) {
    result->add(this);
  }
  if (result->size() > 1) {
    for (Item* currentItem : *result) {
      board->additionalUpdateAfterChange(currentItem);
    }
  }
  return result;
}

jrt::Array<Trace*>* PolylineTrace::split(Point* point) {
  for (int32_t i = 0; i < this->lines->lines->length - 2; i++) {
    LineSegment* currentLineSegment = new LineSegment(this->lines, i + 1);
    Line* splitLine = currentLineSegment->splitLine(point);
    if (splitLine != nullptr) {
      jrt::Array<PolylineTrace*>* pieces = split(i + 1, splitLine);
      if (pieces != nullptr) {
        // PORT NOTE: Java returns the PolylineTrace[] through array covariance as Trace[]; C++
        // arrays are not covariant, so the two pieces are copied into a new Trace* array.
        jrt::Array<Trace*>* result = new jrt::Array<Trace*>(pieces->length);
        for (int32_t k = 0; k < pieces->length; k++) {
          (*result)[k] = (*pieces)[k];
        }
        return result;
      }
    }
  }
  return nullptr;
}

jrt::Array<PolylineTrace*>* PolylineTrace::split(int32_t lineIndex, Line* newEndLine) {
  if (!this->isOnTheBoard()) {
    return nullptr;
  }
  // Guard: if this trace cannot be deleted (e.g. USER_FIXED / protect), splitting would leave
  // the original on the board AND insert duplicate pieces.
  if (isDeletionForbidden()) {
    return nullptr;
  }
  jrt::Array<Polyline*>* splitPolylines = lines->split(lineIndex, newEndLine);
  if (splitPolylines == nullptr) {
    return nullptr;
  }
  if (splitPolylines->length != 2) {
    FRLogger::warn("PolylineTrace.split: array of length 2 expected for splitPolylines");
    return nullptr;
  }
  if (splitInsideDrillPadProhibited(lineIndex, newEndLine)) {
    return nullptr;
  }
  board->removeItem(this);
  jrt::Array<PolylineTrace*>* result = new jrt::Array<PolylineTrace*>(2);
  (*result)[0] = board->insertTraceWithoutCleaning((*splitPolylines)[0], getLayer(),
                                                   getHalfWidth(), netNumbers,
                                                   clearanceClassIndex(), getFixedState());
  (*result)[1] = board->insertTraceWithoutCleaning((*splitPolylines)[1], getLayer(),
                                                   getHalfWidth(), netNumbers,
                                                   clearanceClassIndex(), getFixedState());
  return result;
}

bool PolylineTrace::splitInsideDrillPadProhibited(int32_t lineIndex, Line* line) {
  if (this->board == nullptr) {
    return false;
  }
  Point* intersection = (*this->lines->lines)[lineIndex]->intersection(line);
  jrt::Collection<Item*>* overlapItems =
      this->board->pickItems(intersection, this->getLayer(), nullptr);
  bool padFound = false;
  for (Item* currentItem : *overlapItems) {
    if (!currentItem->sharesNet(this)) {
      continue;
    }
    if (auto* currentDrillItem = jrt::fast_cast<Pin>(currentItem)) {
      if (currentDrillItem->getCenter()->equals(intersection)) {
        return false;  // split always at the center of a drill item.
      }
      padFound = true;
    } else if (auto* currentTrace = jrt::fast_cast<Trace>(currentItem)) {
      if ((currentTrace != this && currentTrace->firstCorner()->equals(intersection)) ||
          currentTrace->lastCorner()->equals(intersection)) {
        return false;
      }
    }
  }
  return padFound;
}

bool PolylineTrace::normalize(IntOctagon* clipShape) {
  return PolylineTraceNormalization::normalize(this, clipShape);
}

bool PolylineTrace::pullTight(TraceTightener* pullTightAlgo) {
  if (!this->isOnTheBoard()) {
    // This trace may have been deleted in a trace split for example
    return false;
  }
  if (this->isShoveFixed()) {
    return false;
  }
  if (!this->netsNormal()) {
    return false;
  }
  if (pullTightAlgo->onlyNetNoArr->length > 0 && !this->netsEqual(pullTightAlgo->onlyNetNoArr)) {
    return false;
  }
  if (this->netNumbers->length > 0) {
    if (!this->board->rules->nets->get((*this->netNumbers)[0])->getNetClass()->getPullTight()) {
      return false;
    }
  }
  Polyline* newLines = pullTightAlgo->pullTight(lines, getLayer(), getHalfWidth(), netNumbers,
                                                clearanceClassIndex(),
                                                this->touchingPinsAtEndCorners());
  if (newLines != lines) {
    change(newLines);
    return true;
  }
  AngleRestriction angleRestriction = this->board->rules->getTraceAngleRestriction();
  if (angleRestriction != AngleRestriction::NINETY_DEGREE &&
      this->board->rules->getPinEdgeToTurnDist() > 0) {
    if (this->swapConnectionToPin(true)) {
      pullTight(pullTightAlgo);
      return true;
    }
    if (this->swapConnectionToPin(false)) {
      pullTight(pullTightAlgo);
      return true;
    }
    // optimize algorithm could not improve the trace, try to remove acid traps
    if (this->correctConnectionToPin(true, angleRestriction)) {
      pullTight(pullTightAlgo);
      return true;
    }
    if (this->correctConnectionToPin(false, angleRestriction)) {
      pullTight(pullTightAlgo);
      return true;
    }
  }
  return false;
}

bool PolylineTrace::pullTight(bool ownNetOnly, int32_t pullTightAccuracy,
                              Stoppable* stoppableThread) {
  auto* routingBoard = jrt::fast_cast<RoutingBoard>(this->board);
  if (routingBoard == nullptr) {
    return false;
  }
  jrt::Array<int32_t>* optNetNoArr;
  if (ownNetOnly) {
    optNetNoArr = this->netNumbers;
  } else {
    optNetNoArr = new jrt::Array<int32_t>(0);
  }
  TraceTightener* pullTightAlgo = TraceTightener::getInstance(
      routingBoard, optNetNoArr, nullptr, pullTightAccuracy, stoppableThread, -1, nullptr, -1);
  return pullTight(pullTightAlgo);
}

bool PolylineTrace::smoothenEndCornersFork(bool ownNetOnly, int32_t pullTightAccuracy,
                                           Stoppable* stoppableThread) {
  auto* routingBoard = jrt::fast_cast<RoutingBoard>(this->board);
  if (routingBoard == nullptr) {
    return false;
  }
  jrt::Array<int32_t>* optNetNoArr;
  if (ownNetOnly) {
    optNetNoArr = this->netNumbers;
  } else {
    optNetNoArr = new jrt::Array<int32_t>(0);
  }
  TraceTightener* pullTightAlgo = TraceTightener::getInstance(
      routingBoard, optNetNoArr, nullptr, pullTightAccuracy, stoppableThread, -1, nullptr, -1);
  return pullTightAlgo->smoothenEndCornersAtTrace(this);
}

TileShape* PolylineTrace::getTraceConnectionShape(ShapeSearchTree* searchTree, int32_t index) {
  if (index < 0 || index >= this->tileShapeCount()) {
    FRLogger::warn("PolylineTrace.get_trace_connection_shape index out of range");
    return nullptr;
  }
  return PolylineTraceGeometry::connectionShape(lines, index);
}

void PolylineTrace::change(Polyline* newPolyline) {
  if (!this->isOnTheBoard()) {
    // Just change the polyline of this trace.
    lines = newPolyline;
    return;
  }

  board->additionalUpdateAfterChange(this);

  // The precalculated tile shapes must not be cleared here because they are used and modified
  // in ShapeSearchTree.change_entries.

  board->itemList->saveForUndo(this);

  // for performance reasons there is some effort to reuse
  // ShapeTree entries of the old trace in the changed trace

  // look for the first line in newPolyline different from
  // the lines of the existing trace
  int32_t lastIndex = jrt::Math::min(newPolyline->lines->length, lines->lines->length);
  int32_t indexOfFirstDifferentLine = lastIndex;
  for (int32_t i = 0; i < lastIndex; i++) {
    if ((*newPolyline->lines)[i] != (*lines->lines)[i]) {
      indexOfFirstDifferentLine = i;
      break;
    }
  }
  if (indexOfFirstDifferentLine == lastIndex) {
    return;  // both polylines are equal, no change necessary
  }
  // look for the last line in newPolyline different from
  // the lines of the existing trace
  int32_t indexOfLastDifferentLine = -1;
  for (int32_t i = 1; i <= lastIndex; i++) {
    if ((*newPolyline->lines)[newPolyline->lines->length - i] !=
        (*lines->lines)[lines->lines->length - i]) {
      indexOfLastDifferentLine = newPolyline->lines->length - i;
      break;
    }
  }
  if (indexOfLastDifferentLine < 0) {
    return;  // both polylines are equal, no change necessary
  }
  int32_t keepAtStartCount = jrt::Math::max(indexOfFirstDifferentLine - 2, 0);
  int32_t keepAtEndCount =
      jrt::Math::max(newPolyline->lines->length - indexOfLastDifferentLine - 3, 0);
  PolylineTraceSearchTreeAdapter::changeEntries(this, newPolyline, keepAtStartCount,
                                                keepAtEndCount);
  lines = newPolyline;
  // the geometry changed in place: caches keyed on the revision of the board must not keep the
  // old ends
  board->incrementRevision();

  // let the observers synchronize the changes
  if ((board->communication != nullptr) && (board->communication->observers != nullptr)) {
    board->communication->observers->notifyChanged(this);
  }

  IntOctagon* clipShape = nullptr;
  if (auto* routingBoard = jrt::fast_cast<RoutingBoard>(board)) {
    ChangedArea* changedArea = routingBoard->changedArea;
    if (changedArea != nullptr) {
      clipShape = changedArea->getArea(this->getLayer());
    }
  }

  try {
    this->normalize(clipShape);
  } catch (const jrt::Exception& e) {
    FRLogger::error("Couldn't change the trace, because its normalization failed.", &e);
  }
}

bool PolylineTrace::checkConnectionToPin(bool atStart) {
  if (this->board == nullptr) {
    return true;
  }
  if (this->cornerCount() < 2) {
    return true;
  }
  jrt::Collection<Item*>* contactList;
  if (atStart) {
    contactList = this->getStartContacts();
  } else {
    contactList = this->getEndContacts();
  }
  Pin* contactPin = nullptr;
  for (Item* currentContact : *contactList) {
    if (auto* pin = jrt::fast_cast<Pin>(currentContact)) {
      contactPin = pin;
      break;
    }
  }
  if (contactPin == nullptr) {
    return true;
  }
  jrt::Collection<Pin_TraceExitRestriction*>* traceExitRestrictions =
      contactPin->getTraceExitRestrictions(this->getLayer());
  if (traceExitRestrictions->isEmpty()) {
    return true;
  }
  Point* endCorner;
  Point* prevEndCorner;
  if (atStart) {
    endCorner = this->firstCorner();
    prevEndCorner = this->lines->corner(1);
  } else {
    endCorner = this->lastCorner();
    prevEndCorner = this->lines->corner(this->lines->cornerCount() - 2);
  }
  Direction* traceEndDirection = Direction::getInstance(endCorner, prevEndCorner);
  if (traceEndDirection == nullptr) {
    return true;
  }
  Pin_TraceExitRestriction* matchingExitRestriction = nullptr;
  for (Pin_TraceExitRestriction* currentExitRestriction : *traceExitRestrictions) {
    if (currentExitRestriction->direction->equals(traceEndDirection)) {
      matchingExitRestriction = currentExitRestriction;
      break;
    }
  }
  if (matchingExitRestriction == nullptr) {
    return false;
  }
  const double edgeToTurnDist = this->board->rules->getPinEdgeToTurnDist();
  if (edgeToTurnDist < 0) {
    return false;
  }
  double endLineLength = endCorner->toFloat()->distance(prevEndCorner->toFloat());
  double currentClearance = board->clearanceValue(
      this->clearanceClassIndex(), contactPin->clearanceClassIndex(), this->getLayer());
  double addWidth = jrt::Math::max(edgeToTurnDist, currentClearance + 1);
  double preserveLength = matchingExitRestriction->minLength + this->getHalfWidth() + addWidth;
  return preserveLength <= endLineLength;
}

bool PolylineTrace::correctConnectionToPin(bool atStart, AngleRestriction angleRestriction) {
  if (this->checkConnectionToPin(atStart)) {
    return false;
  }

  Polyline* tracePolyline;
  jrt::Collection<Item*>* contactList;
  if (atStart) {
    tracePolyline = this->polyline();
    contactList = this->getStartContacts();
  } else {
    tracePolyline = this->polyline()->reverse();
    contactList = this->getEndContacts();
  }
  Pin* contactPin = nullptr;
  for (Item* currentContact : *contactList) {
    if (auto* pin = jrt::fast_cast<Pin>(currentContact)) {
      contactPin = pin;
      break;
    }
  }
  if (contactPin == nullptr) {
    return false;
  }
  jrt::Collection<Pin_TraceExitRestriction*>* traceExitRestrictions =
      contactPin->getTraceExitRestrictions(this->getLayer());
  if (traceExitRestrictions->isEmpty()) {
    return false;
  }
  Shape* pinShape = contactPin->getShape(this->getLayer() - contactPin->firstLayer());
  if (jrt::fast_cast<TileShape>(pinShape) == nullptr) {
    return false;
  }
  const double edgeToTurnDist = this->board->rules->getPinEdgeToTurnDist();
  if (edgeToTurnDist < 0) {
    return false;
  }
  double currentClearance = board->clearanceValue(
      this->clearanceClassIndex(), contactPin->clearanceClassIndex(), this->getLayer());
  double addWidth = jrt::Math::max(edgeToTurnDist, currentClearance + 1);
  TileShape* offsetPinShape = jrt::fast_cast<TileShape>(
      jrt::fast_cast<TileShape>(pinShape)->offset(this->getHalfWidth() + addWidth));
  if (angleRestriction == AngleRestriction::NINETY_DEGREE || offsetPinShape->isIntBox()) {
    offsetPinShape = offsetPinShape->boundingBox();
  } else if (angleRestriction == AngleRestriction::FORTYFIVE_DEGREE) {
    offsetPinShape = offsetPinShape->boundingOctagon();
  }
  jrt::Array<jrt::Array<int32_t>*>* entries = offsetPinShape->entrancePoints(tracePolyline);
  if (entries->length == 0) {
    return false;
  }
  jrt::Array<int32_t>* latestEntryTuple = (*entries)[entries->length - 1];
  FloatPoint* traceEntryLocationApprox =
      (*tracePolyline->lines)[(*latestEntryTuple)[0]]->intersectionApprox(
          offsetPinShape->borderLine((*latestEntryTuple)[1]));
  // calculate the nearest legal pin exit point to traceEntryLocationApprox
  double minExitCornerDistance = jrt::Double::MAX_VALUE;
  Line* nearestPinExitRay = nullptr;
  int32_t nearestBorderLineNo = -1;
  Direction* pinExitDirection = nullptr;
  FloatPoint* nearestExitCorner = nullptr;
  const double tolerance = 1;
  Point* pinCenter = contactPin->getCenter();
  for (Pin_TraceExitRestriction* currentExitRestriction : *traceExitRestrictions) {
    int32_t currentIntersectingBorderLineNo =
        offsetPinShape->intersectingBorderLineNo(pinCenter, currentExitRestriction->direction);
    Line* currentPinExitRay = new Line(pinCenter, currentExitRestriction->direction);
    FloatPoint* currentExitCorner = currentPinExitRay->intersectionApprox(
        offsetPinShape->borderLine(currentIntersectingBorderLineNo));
    double currentExitCornerDistance = currentExitCorner->distanceSquare(traceEntryLocationApprox);
    bool newNearestCornerFound = false;
    if (currentExitCornerDistance + tolerance < minExitCornerDistance) {
      newNearestCornerFound = true;
    } else if (currentExitCornerDistance < minExitCornerDistance + tolerance) {
      // the distances are near equal, compare to the previous corners of
      // tracePolyline
      for (int32_t i = 1; i < tracePolyline->cornerCount(); i++) {
        FloatPoint* currentTraceCorner = tracePolyline->cornerApprox(i);
        double currentTraceCornerDistance = currentTraceCorner->distanceSquare(currentExitCorner);
        double oldTraceCornerDistance = currentTraceCorner->distanceSquare(nearestExitCorner);
        if (currentTraceCornerDistance + tolerance < oldTraceCornerDistance) {
          newNearestCornerFound = true;
          break;
        } else if (currentTraceCornerDistance > oldTraceCornerDistance + tolerance) {
          break;
        }
      }
    }
    if (newNearestCornerFound) {
      minExitCornerDistance = currentExitCornerDistance;
      nearestPinExitRay = currentPinExitRay;
      nearestBorderLineNo = currentIntersectingBorderLineNo;
      pinExitDirection = currentExitRestriction->direction;
      nearestExitCorner = currentExitCorner;
    }
  }

  // append the polygon piece around the border of the pin shape.

  jrt::Array<Line*>* currentLines;

  int32_t cornerCount = offsetPinShape->borderLineCount();
  int32_t clockWiseSideDiff =
      (nearestBorderLineNo - (*latestEntryTuple)[1] + cornerCount) % cornerCount;
  int32_t counterClockWiseSideDiff =
      ((*latestEntryTuple)[1] - nearestBorderLineNo + cornerCount) % cornerCount;
  int32_t currentBorderLineNo = nearestBorderLineNo;
  if (counterClockWiseSideDiff <= clockWiseSideDiff) {
    currentLines = new jrt::Array<Line*>(counterClockWiseSideDiff + 3);
    for (int32_t i = 0; i <= counterClockWiseSideDiff; i++) {
      (*currentLines)[i + 1] = offsetPinShape->borderLine(currentBorderLineNo);
      currentBorderLineNo = (currentBorderLineNo + 1) % cornerCount;
    }
  } else {
    currentLines = new jrt::Array<Line*>(clockWiseSideDiff + 3);
    for (int32_t i = 0; i <= clockWiseSideDiff; i++) {
      (*currentLines)[i + 1] = offsetPinShape->borderLine(currentBorderLineNo);
      currentBorderLineNo = (currentBorderLineNo - 1 + cornerCount) % cornerCount;
    }
  }
  (*currentLines)[0] = nearestPinExitRay;
  (*currentLines)[currentLines->length - 1] = (*tracePolyline->lines)[(*latestEntryTuple)[0]];

  Polyline* borderPolyline = new Polyline(currentLines);
  if (!this->board->checkPolylineTrace(borderPolyline, this->getLayer(), this->getHalfWidth(),
                                       this->netNumbers, this->clearanceClassIndex())) {
    return false;
  }

  jrt::Array<Line*>* cutLines =
      new jrt::Array<Line*>(tracePolyline->lines->length - (*latestEntryTuple)[0] + 1);
  (*cutLines)[0] = (*currentLines)[currentLines->length - 2];
  jrt::arraycopy(tracePolyline->lines, (*latestEntryTuple)[0], cutLines, 1, cutLines->length - 1);
  Polyline* cutPolyline = new Polyline(cutLines);
  Polyline* changedPolyline;
  if (cutPolyline->firstCorner()->equals(cutPolyline->lastCorner())) {
    changedPolyline = borderPolyline;
  } else {
    changedPolyline = borderPolyline->combine(cutPolyline);
  }
  if (!atStart) {
    changedPolyline = changedPolyline->reverse();
  }
  this->change(changedPolyline);

  // create a shoveFixed exit line.
  currentLines = new jrt::Array<Line*>(3);
  (*currentLines)[0] = new Line(pinCenter, pinExitDirection->turn45Degree(2));
  (*currentLines)[1] = nearestPinExitRay;
  (*currentLines)[2] = offsetPinShape->borderLine(nearestBorderLineNo);
  Polyline* exitLineSegment = new Polyline(currentLines);
  this->board->insertTrace(exitLineSegment, this->getLayer(), this->getHalfWidth(),
                           this->netNumbers, this->clearanceClassIndex(),
                           FixedState::SHOVE_FIXED);
  return true;
}

bool PolylineTrace::swapConnectionToPin(bool atStart) {
  Polyline* tracePolyline;
  jrt::Collection<Item*>* contactList;
  if (atStart) {
    tracePolyline = this->polyline();
    contactList = this->getStartContacts();
  } else {
    tracePolyline = this->polyline()->reverse();
    contactList = this->getEndContacts();
  }
  if (contactList->size() != 1) {
    return false;
  }
  Item* currentContact = contactList->iterator()->next();
  auto* contactTrace = jrt::fast_cast<PolylineTrace>(currentContact);
  if (!(currentContact->getFixedState() == FixedState::SHOVE_FIXED && contactTrace != nullptr)) {
    return false;
  }
  Polyline* contactPolyline = contactTrace->polyline();
  if (!contactPolyline->lastCorner()->equals(tracePolyline->firstCorner())) {
    return false;
  }
  Line* contactLastLine = (*contactPolyline->lines)[contactPolyline->lines->length - 2];
  // look, if this trace has a sharp angle with the contact trace.
  Line* firstLine = (*tracePolyline->lines)[1];
  // check for sharp angle
  bool checkSwap =
      contactLastLine->direction()->projection(firstLine->direction()) == Signum::NEGATIVE();
  if (!checkSwap) {
    double halfWidth = this->getHalfWidth();
    if (tracePolyline->lines->length > 3 &&
        tracePolyline->cornerApprox(0)->distanceSquare(tracePolyline->cornerApprox(1)) <=
            halfWidth * halfWidth) {
      // check also for sharp angle with the second line
      checkSwap = contactLastLine->direction()->projection((*tracePolyline->lines)[2]->direction()) ==
                  Signum::NEGATIVE();
    }
  }
  if (!checkSwap) {
    return false;
  }
  Pin* contactPin = nullptr;
  jrt::Collection<Item*>* currentContacts = contactTrace->getStartContacts();
  for (Item* tmpContact : *currentContacts) {
    if (auto* pin = jrt::fast_cast<Pin>(tmpContact)) {
      contactPin = pin;
      break;
    }
  }
  if (contactPin == nullptr) {
    return false;
  }
  Polyline* combinedPolyline = contactPolyline->combine(tracePolyline);
  Direction* nearestPinExitDirection = contactPin->calcNearestExitRestrictionDirection(
      combinedPolyline, this->getHalfWidth(), this->getLayer());
  if (nearestPinExitDirection == nullptr ||
      nearestPinExitDirection->equals((*contactPolyline->lines)[1]->direction())) {
    return false;  // direction would not be changed
  }
  contactTrace->setFixedState(this->getFixedState());
  this->combine();
  return true;
}

}  // namespace fr
