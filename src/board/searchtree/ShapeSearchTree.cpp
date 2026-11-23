// Port of app.freerouting.board.searchtree.ShapeSearchTree
#include "board/searchtree/ShapeSearchTree.h"

#include <algorithm>

#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/IncompleteFreeSpaceExpansionRoom.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/ExactCopper.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/PadOval.h"
#include "core/library/Padstack.h"
#include "datastructures/ArrayStack.h"
#include "datastructures/Signum.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/ShapeBoundingDirections.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"

namespace fr {

namespace {

/** Java `try { ... } finally { list.clear(); }` for the shared query leaf buffer. */
struct ClearListOnExit {
  jrt::List<ShapeTree_Leaf*>* list;
  ~ClearListOnExit() { list->clear(); }  // ArrayList::clear nulls the vacated slots
};

/** Java `finally { releaseScratchStack(stack); }`. */
struct ReleaseStackOnExit {
  ArrayStack<ShapeTree_TreeNode*>* stack;
  void (*release)(ArrayStack<ShapeTree_TreeNode*>*);
  ~ReleaseStackOnExit() { release(stack); }
};

/** Java `list.subList(0, count).sort(null)` with the natural (Leaf) order; stable like Java. */
void sortLeafPrefix(jrt::ArrayList<ShapeTree_Leaf*>* list, int32_t count) {
  std::vector<ShapeTree_Leaf*>& v = list->vec();
  std::stable_sort(v.begin(), v.begin() + count, [](ShapeTree_Leaf* a, ShapeTree_Leaf* b) {
    return a->compareTo(b) < 0;
  });
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// ShapeSearchTree_EntrySortedByClearance
// ---------------------------------------------------------------------------------------------

int32_t ShapeSearchTree_EntrySortedByClearance::compareTo(
    ShapeSearchTree_EntrySortedByClearance* other) {
  if (clearance != other->clearance) {
    return Signum::asInt(clearance - other->clearance);
  }
  return leaf->compareTo(other->leaf);
}

// ---------------------------------------------------------------------------------------------
// ShapeSearchTree
// ---------------------------------------------------------------------------------------------

jrt::ArrayList<ShapeTree_Leaf*>* ShapeSearchTree::QUERY_LEAF_BUFFER() {
  static jrt::ArrayList<ShapeTree_Leaf*>* v = new (jrt::kBoard) jrt::ArrayList<ShapeTree_Leaf*>(128);
  return v;
}

ShapeSearchTree::ShapeSearchTree(ShapeBoundingDirections* directions, BasicBoard* board,
                                 int32_t compensatedClearanceClassNo)
    : MinAreaTree(directions) {
  this->compensatedClearanceClassNo = compensatedClearanceClassNo;
  this->board = board;
  // PORT NOTE: in C++ the dynamic type here is ShapeSearchTree; derived constructors recompute key.
  key = getKey(this, directions, compensatedClearanceClassNo);
}

std::string ShapeSearchTree::getKey(ShapeSearchTree* searchTree,
                                    ShapeBoundingDirections* directions, int32_t clearanceClass) {
  return jrt::className(searchTree) + "_" +
         jrt::replace(jrt::className(directions), "BoundingDirections", "") + "_cc" +
         jrt::str(clearanceClass);
}

int32_t ShapeSearchTree::clearanceCompensationValue(int32_t clearanceClassIndex, int32_t layer) {
  if (clearanceClassIndex <= 0) {
    return 0;
  }
  int32_t result = board->rules->clearanceMatrix->getValue(
                       clearanceClassIndex, this->compensatedClearanceClassNo, layer, false) -
                   board->rules->clearanceMatrix->clearanceCompensationValue(
                       this->compensatedClearanceClassNo, layer);
  return jrt::Math::max(result, 0);
}

int32_t ShapeSearchTree::additionalClearance(int32_t firstClass, int32_t secondClass,
                                             int32_t layer) {
  if (!isClearanceCompensationUsed()) {
    return board->clearanceValue(firstClass, secondClass, layer);
  }
  // Keep the compensated convention: the callers supply their geometric rounding guards.
  // The explicit (uncompensated) path retains its existing matrix safety margin.
  int32_t clearance = board->rules->clearanceMatrix->getValue(firstClass, secondClass, layer, false);
  return jrt::Math::max(0, clearance - clearanceCompensationValue(firstClass, layer) -
                               clearanceCompensationValue(secondClass, layer));
}

void ShapeSearchTree::changeEntries(PolylineTrace* obj, Polyline* newPolyline,
                                    int32_t keepAtStartCount, int32_t keepAtEndCount) {
  changeEntriesUnlocked(obj, newPolyline, keepAtStartCount, keepAtEndCount);
}

void ShapeSearchTree::changeEntriesUnlocked(PolylineTrace* obj, Polyline* newPolyline,
                                            int32_t keepAtStartCount, int32_t keepAtEndCount) {
  // calculate the shapes of newPolyline from keepAtStartCount to
  // newShapeCount - keepAtEndCount - 1;
  int32_t compensatedHalfWidth =
      obj->getHalfWidth() + this->clearanceCompensationValue(obj->clearanceClassIndex(),
                                                             obj->getLayer());
  jrt::Array<TileShape*>* changedShapes =
      this->offsetShapes(newPolyline, compensatedHalfWidth, keepAtStartCount,
                         newPolyline->lines->length - 1 - keepAtEndCount);
  int32_t oldShapeCount = obj->treeShapeCount(this);
  int32_t newShapeCount = changedShapes->length + keepAtStartCount + keepAtEndCount;
  auto* newLeafArr = new (jrt::kBoardData) jrt::Array<ShapeTree_Leaf*>(newShapeCount);
  auto* newPrecalculatedTreeShapes = new (jrt::kGeom) jrt::Array<TileShape*>(newShapeCount);
  jrt::Array<ShapeTree_Leaf*>* oldEntries = obj->getSearchTreeEntries(this);
  for (int32_t i = 0; i < keepAtStartCount; i++) {
    (*newLeafArr)[i] = (*oldEntries)[i];
    (*newPrecalculatedTreeShapes)[i] = obj->getTreeShape(this, i);
  }
  for (int32_t i = keepAtStartCount; i < oldShapeCount - keepAtEndCount; i++) {
    removeLeaf((*oldEntries)[i]);
  }
  for (int32_t i = 0; i < keepAtEndCount; i++) {
    int32_t newIndex = newShapeCount - keepAtEndCount + i;
    int32_t oldIndex = oldShapeCount - keepAtEndCount + i;

    (*newLeafArr)[newIndex] = (*oldEntries)[oldIndex];
    (*newLeafArr)[newIndex]->shapeIndexInObject = newIndex;
    (*newPrecalculatedTreeShapes)[newIndex] = obj->getTreeShape(this, oldIndex);
  }

  // correct the precalculated tree shapes first, because it is used in
  // this.insert
  jrt::arraycopy(changedShapes, 0, newPrecalculatedTreeShapes, keepAtStartCount,
                 changedShapes->length);
  obj->setPrecalculatedTreeShapes(newPrecalculatedTreeShapes, this);

  for (int32_t i = keepAtStartCount; i < newShapeCount - keepAtEndCount; i++) {
    (*newLeafArr)[i] = insert(static_cast<ShapeTree_Storable*>(obj), i);
  }
  obj->setSearchTreeEntries(newLeafArr, this);
}

void ShapeSearchTree::mergeEntriesInFront(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                          Polyline* joinedPolyline, int32_t fromEntryNo,
                                          int32_t toEntryNo) {
  mergeEntriesInFrontUnlocked(fromTrace, toTrace, joinedPolyline, fromEntryNo, toEntryNo);
}

void ShapeSearchTree::mergeEntriesInFrontUnlocked(PolylineTrace* fromTrace,
                                                  PolylineTrace* toTrace,
                                                  Polyline* joinedPolyline, int32_t fromEntryNo,
                                                  int32_t toEntryNo) {
  bool changeOrder = fromTrace->firstCorner()->equals(toTrace->firstCorner());
  // remove the last or first tree entry from fromTrace and the
  // first tree entry from toTrace, because they will be replaced by
  // the new link entries.
  int32_t fromShapeCountMinus1 = fromTrace->tileShapeCount() - 1;
  int32_t removeNo;
  if (changeOrder) {
    removeNo = 0;
  } else {
    removeNo = fromShapeCountMinus1;
  }
  jrt::Array<ShapeTree_Leaf*>* fromTraceEntries = fromTrace->getSearchTreeEntries(this);
  jrt::Array<ShapeTree_Leaf*>* toTraceEntries = toTrace->getSearchTreeEntries(this);
  removeLeaf((*fromTraceEntries)[removeNo]);
  removeLeaf((*toTraceEntries)[0]);
  jrt::Array<TileShape*>* linkShapes = this->offsetShapes(
      joinedPolyline,
      toTrace->getHalfWidth() +
          this->clearanceCompensationValue(toTrace->clearanceClassIndex(), toTrace->getLayer()),
      fromEntryNo, toEntryNo);
  int32_t newShapeCount =
      fromTraceEntries->length + linkShapes->length + toTraceEntries->length - 2;
  auto* newLeafArr = new (jrt::kBoardData) jrt::Array<ShapeTree_Leaf*>(newShapeCount);
  int32_t oldToShapeCount = toTraceEntries->length;
  auto* newPrecalculatedTreeShapes = new (jrt::kGeom) jrt::Array<TileShape*>(newShapeCount);
  // transfer the tree entries except the last or first from fromTrace to
  // toTrace
  for (int32_t i = 0; i < fromShapeCountMinus1; i++) {
    int32_t fromNo;
    if (changeOrder) {
      fromNo = fromShapeCountMinus1 - i;
    } else {
      fromNo = i;
    }
    (*newPrecalculatedTreeShapes)[i] = fromTrace->getTreeShape(this, fromNo);
    (*newLeafArr)[i] = (*fromTraceEntries)[fromNo];
    (*newLeafArr)[i]->object = toTrace;
    (*newLeafArr)[i]->shapeIndexInObject = i;
  }
  for (int32_t i = 1; i < oldToShapeCount; i++) {
    int32_t currentInd = fromShapeCountMinus1 + linkShapes->length + i - 1;
    (*newPrecalculatedTreeShapes)[currentInd] = toTrace->getTreeShape(this, i);
    (*newLeafArr)[currentInd] = (*toTraceEntries)[i];
    (*newLeafArr)[currentInd]->shapeIndexInObject = currentInd;
  }

  // correct the precalculated tree shapes first, because it is used in
  // this.insert
  for (int32_t i = 0; i < linkShapes->length; i++) {
    int32_t currentInd = fromShapeCountMinus1 + i;
    (*newPrecalculatedTreeShapes)[currentInd] = (*linkShapes)[i];
  }
  toTrace->setPrecalculatedTreeShapes(newPrecalculatedTreeShapes, this);

  // create the new link entries
  for (int32_t i = 0; i < linkShapes->length; i++) {
    int32_t currentInd = fromShapeCountMinus1 + i;
    (*newLeafArr)[currentInd] = insert(static_cast<ShapeTree_Storable*>(toTrace), currentInd);
  }

  toTrace->setSearchTreeEntries(newLeafArr, this);
}

void ShapeSearchTree::mergeEntriesAtEnd(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                        Polyline* joinedPolyline, int32_t fromEntryNo,
                                        int32_t toEntryNo) {
  mergeEntriesAtEndUnlocked(fromTrace, toTrace, joinedPolyline, fromEntryNo, toEntryNo);
}

void ShapeSearchTree::mergeEntriesAtEndUnlocked(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                                Polyline* joinedPolyline, int32_t fromEntryNo,
                                                int32_t toEntryNo) {
  bool changeOrder = fromTrace->lastCorner()->equals(toTrace->lastCorner());
  jrt::Array<ShapeTree_Leaf*>* fromTraceEntries = fromTrace->getSearchTreeEntries(this);
  jrt::Array<ShapeTree_Leaf*>* toTraceEntries = toTrace->getSearchTreeEntries(this);
  // remove the last or first tree entry from fromTrace and the
  // last tree entry from toTrace, because they will be replaced by
  // the new link entries.
  int32_t toShapeCountMinus1 = toTrace->tileShapeCount() - 1;
  removeLeaf((*toTraceEntries)[toShapeCountMinus1]);
  int32_t removeNo;
  if (changeOrder) {
    removeNo = fromTrace->tileShapeCount() - 1;
  } else {
    removeNo = 0;
  }
  removeLeaf((*fromTraceEntries)[removeNo]);
  jrt::Array<TileShape*>* linkShapes = this->offsetShapes(
      joinedPolyline,
      toTrace->getHalfWidth() +
          this->clearanceCompensationValue(toTrace->clearanceClassIndex(), toTrace->getLayer()),
      fromEntryNo, toEntryNo);
  int32_t newShapeCount =
      fromTraceEntries->length + linkShapes->length + toTraceEntries->length - 2;
  auto* newLeafArr = new (jrt::kBoardData) jrt::Array<ShapeTree_Leaf*>(newShapeCount);
  auto* newPrecalculatedTreeShapes = new (jrt::kGeom) jrt::Array<TileShape*>(newShapeCount);
  // transfer the tree entries except the last from the old shapes
  // of toTrace to the new shapes of toTrace
  for (int32_t i = 0; i < toShapeCountMinus1; i++) {
    (*newPrecalculatedTreeShapes)[i] = toTrace->getTreeShape(this, i);
    (*newLeafArr)[i] = (*toTraceEntries)[i];
  }

  for (int32_t i = 1; i < fromTraceEntries->length; i++) {
    int32_t currentInd = toShapeCountMinus1 + linkShapes->length + i - 1;
    int32_t fromNo;
    if (changeOrder) {
      fromNo = fromTraceEntries->length - i - 1;
    } else {
      fromNo = i;
    }
    (*newPrecalculatedTreeShapes)[currentInd] = fromTrace->getTreeShape(this, fromNo);
    (*newLeafArr)[currentInd] = (*fromTraceEntries)[fromNo];
    (*newLeafArr)[currentInd]->object = toTrace;
    (*newLeafArr)[currentInd]->shapeIndexInObject = currentInd;
  }

  // correct the precalculated tree shapes first, because it is used in
  // this.insert
  for (int32_t i = 0; i < linkShapes->length; i++) {
    int32_t currentInd = toShapeCountMinus1 + i;
    (*newPrecalculatedTreeShapes)[currentInd] = (*linkShapes)[i];
  }
  toTrace->setPrecalculatedTreeShapes(newPrecalculatedTreeShapes, this);

  // create the new link entries
  for (int32_t i = 0; i < linkShapes->length; i++) {
    int32_t currentInd = toShapeCountMinus1 + i;
    (*newLeafArr)[currentInd] = insert(static_cast<ShapeTree_Storable*>(toTrace), currentInd);
  }
  toTrace->setSearchTreeEntries(newLeafArr, this);
}

void ShapeSearchTree::reuseEntriesAfterCutout(PolylineTrace* fromTrace, PolylineTrace* startPiece,
                                              PolylineTrace* endPiece) {
  reuseEntriesAfterCutoutUnlocked(fromTrace, startPiece, endPiece);
}

void ShapeSearchTree::reuseEntriesAfterCutoutUnlocked(PolylineTrace* fromTrace,
                                                      PolylineTrace* startPiece,
                                                      PolylineTrace* endPiece) {
  auto* startPieceLeafArr =
      new (jrt::kBoardData) jrt::Array<ShapeTree_Leaf*>(startPiece->polyline()->lines->length - 2);
  jrt::Array<ShapeTree_Leaf*>* fromTraceEntries = fromTrace->getSearchTreeEntries(this);
  // transfer the entries at the start of fromTrace to startPiece.
  for (int32_t i = 0; i < startPieceLeafArr->length - 1; i++) {
    (*startPieceLeafArr)[i] = (*fromTraceEntries)[i];
    (*startPieceLeafArr)[i]->object = startPiece;
    (*startPieceLeafArr)[i]->shapeIndexInObject = i;
    (*fromTraceEntries)[i] = nullptr;
  }
  (*startPieceLeafArr)[startPieceLeafArr->length - 1] =
      insert(static_cast<ShapeTree_Storable*>(startPiece), startPieceLeafArr->length - 1);

  // create the last tree entry of the start piece.

  auto* endPieceLeafArr = new (jrt::kBoardData) jrt::Array<ShapeTree_Leaf*>(endPiece->polyline()->lines->length - 2);

  // create the first tree entry of the end piece.
  (*endPieceLeafArr)[0] = insert(static_cast<ShapeTree_Storable*>(endPiece), 0);

  for (int32_t i = 1; i < endPieceLeafArr->length; i++) {
    int32_t fromIndex = fromTraceEntries->length - endPieceLeafArr->length + i;
    (*endPieceLeafArr)[i] = (*fromTraceEntries)[fromIndex];
    (*endPieceLeafArr)[i]->object = endPiece;
    (*endPieceLeafArr)[i]->shapeIndexInObject = i;
    (*fromTraceEntries)[fromIndex] = nullptr;
  }

  startPiece->setSearchTreeEntries(startPieceLeafArr, this);
  endPiece->setSearchTreeEntries(endPieceLeafArr, this);
}

void ShapeSearchTree::overlappingObjects(ConvexShape* shape, int32_t layer,
                                         jrt::Array<int32_t>* ignoreNetNos,
                                         jrt::Set<SearchTreeObject*>* obstacles) {
  // A naturally ordered set sorts the objects itself: the entries need not come in leaf order.
  // Java: `obstacles instanceof SortedSet && comparator() == null`.
  auto* sortedObstacles = dynamic_cast<jrt::TreeSet<SearchTreeObject*>*>(obstacles);
  bool inLeafOrder = !(sortedObstacles != nullptr && sortedObstacles->hasNaturalOrdering());
  // PORT NOTE: the kept leaves are added directly instead of through a temporary list of tree
  // entries (same objects, same order).
  if (shape == nullptr) {
    return;
  }
  jrt::ArrayList<ShapeTree_Leaf*>* leaves = QUERY_LEAF_BUFFER();
  leaves->clear();
  ClearListOnExit guard{leaves};
  int32_t keptCount = overlappingLeavesUnlocked(shape, layer, ignoreNetNos,
                                                static_cast<Item*>(nullptr), inLeafOrder, leaves);
  if (obstacles != nullptr) {
    for (int32_t i = 0; i < keptCount; i++) {
      obstacles->add(jrt::fast_cast<SearchTreeObject>(leaves->get(i)->object));
    }
  }
}

jrt::Set<SearchTreeObject*>* ShapeSearchTree::overlappingObjects(
    ConvexShape* shape, int32_t layer, std::function<bool(SearchTreeObject*)> accept) {
  jrt::Set<SearchTreeObject*>* result = new (jrt::kOp) jrt::TreeSet<SearchTreeObject*>();
  RegularTileShape* bounds = shape == nullptr ? nullptr : shape->boundingShape(boundingDirections);
  if (bounds == nullptr) {
    FRLogger::warn("ShapeSearchTree.overlaps: shape not bounded");
    return result;
  }
  bool is45Degree = jrt::fast_cast<IntOctagon>(shape) != nullptr;
  jrt::ArrayList<ShapeTree_Leaf*>* leaves = QUERY_LEAF_BUFFER();
  {
    ClearListOnExit guard{leaves};
    leaves->clear();
    this->collectOverlapsUnlocked(bounds, leaves);
    for (ShapeTree_Leaf* currentLeaf : *leaves) {
      SearchTreeObject* currentObject = jrt::fast_cast<SearchTreeObject>(currentLeaf->object);
      int32_t shapeIndex = currentLeaf->shapeIndexInObject;
      if ((layer >= 0 && currentObject->shapeLayer(shapeIndex) != layer) ||
          result->contains(currentObject) || !accept(currentObject)) {
        continue;
      }
      TileShape* currentShape = currentObject->getTreeShape(this, shapeIndex);
      // Same test as overlappingTreeEntriesUnlocked: two octagons that overlap by their
      // bounding octagons overlap.
      if ((is45Degree && jrt::fast_cast<IntOctagon>(currentShape) != nullptr) ||
          currentShape->intersects(static_cast<Shape*>(shape))) {
        result->add(currentObject);
      }
    }
  }
  return result;
}

jrt::Set<SearchTreeObject*>* ShapeSearchTree::overlappingObjects(ConvexShape* shape,
                                                                 int32_t layer) {
  jrt::Set<SearchTreeObject*>* result = new (jrt::kOp) jrt::TreeSet<SearchTreeObject*>();
  jrt::DisposeOnExit noNets(new (jrt::kOp) jrt::Array<int32_t>(0));
  this->overlappingObjects(shape, layer, noNets.get(), result);
  return result;
}

jrt::Set<SearchTreeObject*>* ShapeSearchTree::overlappingObjectsWithPointIn(
    Item* area, int32_t layer, std::function<jrt::Array<Point*>*(SearchTreeObject*)> points,
    std::function<bool(SearchTreeObject*)> byShape) {
  jrt::ArrayList<ShapeTree_Leaf*>* leaves = QUERY_LEAF_BUFFER();
  ClearListOnExit guard{leaves};
  jrt::Array<ShapeTree_Leaf*>* areaLeaves = area->getSearchTreeEntries(this);
  if (areaLeaves == nullptr) {
    return nullptr;
  }
  RegularTileShape* areaBounds = nullptr;
  for (ShapeTree_Leaf* areaLeaf : *areaLeaves) {
    if (areaLeaf != nullptr) {
      areaBounds = areaBounds == nullptr ? areaLeaf->boundingShape
                                         : areaBounds->union_(areaLeaf->boundingShape);
    }
  }
  jrt::Set<SearchTreeObject*>* result = new (jrt::kOp) jrt::TreeSet<SearchTreeObject*>();
  if (areaBounds == nullptr) {
    return result;
  }
  jrt::DisposeOnExit pointLeavesOwner(new (jrt::kOp) jrt::ArrayList<ShapeTree_Leaf*>());
  auto* pointLeaves = pointLeavesOwner.get();

  leaves->clear();

  this->collectOverlapsUnlocked(areaBounds, leaves);
  for (ShapeTree_Leaf* currentLeaf : *leaves) {
    SearchTreeObject* currentObject = jrt::fast_cast<SearchTreeObject>(currentLeaf->object);
    int32_t shapeIndex = currentLeaf->shapeIndexInObject;
    if ((layer >= 0 && currentObject->shapeLayer(shapeIndex) != layer) ||
        result->contains(currentObject)) {
      continue;
    }
    if (byShape(currentObject)) {
      TileShape* objectShape = currentObject->getTreeShape(this, shapeIndex);
      pointLeaves->clear();
      this->collectOverlapsUnlocked(currentLeaf->boundingShape, pointLeaves);
      for (ShapeTree_Leaf* areaLeaf : *pointLeaves) {
        if (areaLeaf->object == static_cast<ShapeTree_Storable*>(area) &&
            area->getTreeShape(this, areaLeaf->shapeIndexInObject)
                    ->intersection(objectShape)
                    ->dimension() == 2) {
          result->add(currentObject);
          break;
        }
      }
      continue;
    }
    jrt::Array<Point*>* objectPoints = points(currentObject);
    if (objectPoints == nullptr) {
      continue;
    }
    TileShape* leafShape = nullptr;
    bool searchDone = false;  // Java: labelled `break search`
    for (Point* point : *objectPoints) {
      pointLeaves->clear();
      this->collectOverlapsUnlocked(point->surroundingBox()->boundingShape(boundingDirections),
                                    pointLeaves);
      for (ShapeTree_Leaf* areaLeaf : *pointLeaves) {
        if (areaLeaf->object != static_cast<ShapeTree_Storable*>(area) ||
            !currentLeaf->boundingShape->intersects(static_cast<Shape*>(areaLeaf->boundingShape))) {
          continue;
        }
        TileShape* areaShape = area->getTreeShape(this, areaLeaf->shapeIndexInObject);
        if (!areaShape->contains(point)) {
          continue;
        }
        if (leafShape == nullptr) {
          leafShape = currentObject->getTreeShape(this, shapeIndex);
        }
        // Same test as overlappingTreeEntriesUnlocked: two octagons that overlap by their
        // bounding octagons overlap.
        if ((jrt::fast_cast<IntOctagon>(areaShape) != nullptr &&
             jrt::fast_cast<IntOctagon>(leafShape) != nullptr) ||
            leafShape->intersects(static_cast<Shape*>(areaShape))) {
          result->add(currentObject);
          searchDone = true;
          break;
        }
      }
      if (searchDone) {
        break;
      }
    }
  }
  return result;
}

void ShapeSearchTree::overlappingTreeEntries(ConvexShape* shape, int32_t layer,
                                             jrt::Collection<ShapeTree_TreeEntry*>* treeEntries) {
  jrt::DisposeOnExit noNets(new (jrt::kOp) jrt::Array<int32_t>(0));
  overlappingTreeEntries(shape, layer, noNets.get(), treeEntries);

}

void ShapeSearchTree::overlappingTreeEntries(ConvexShape* shape, int32_t layer,
                                             jrt::Array<int32_t>* ignoreNetNos,
                                             jrt::Collection<ShapeTree_TreeEntry*>* treeEntries) {
  overlappingTreeEntriesUnlocked(shape, layer, ignoreNetNos, treeEntries);
}

void ShapeSearchTree::overlappingTreeEntriesUnlocked(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    jrt::Collection<ShapeTree_TreeEntry*>* treeEntries) {
  overlappingTreeEntriesUnlocked(shape, layer, ignoreNetNos, treeEntries,
                                 static_cast<Item*>(nullptr));
}

void ShapeSearchTree::overlappingTreeEntriesUnlocked(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    jrt::Collection<ShapeTree_TreeEntry*>* treeEntries, Item* obstaclesOf) {
  overlappingTreeEntriesUnlocked(shape, layer, ignoreNetNos, treeEntries, obstaclesOf, true);
}

void ShapeSearchTree::overlappingTreeEntriesUnlocked(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    jrt::Collection<ShapeTree_TreeEntry*>* treeEntries, Item* obstaclesOf, bool inLeafOrder) {
  if (shape == nullptr) {
    return;
  }
  if (treeEntries == nullptr) {
    FRLogger::warn("ShapeSearchTree.overlaps: obstacleEntries is null");
    return;
  }
  jrt::ArrayList<ShapeTree_Leaf*>* tmpList = QUERY_LEAF_BUFFER();
  tmpList->clear();
  {
    ClearListOnExit guard{tmpList};
    int32_t keptCount =
        overlappingLeavesUnlocked(shape, layer, ignoreNetNos, obstaclesOf, inLeafOrder, tmpList);
    for (int32_t i = 0; i < keptCount; i++) {
      ShapeTree_Leaf* currentLeaf = tmpList->get(i);
      treeEntries->add(new (jrt::kOp) ShapeTree_TreeEntry(
          jrt::fast_cast<SearchTreeObject>(currentLeaf->object), currentLeaf->shapeIndexInObject));
    }
  }
}

int32_t ShapeSearchTree::overlappingLeavesUnlocked(ConvexShape* shape, int32_t layer,
                                                   jrt::Array<int32_t>* ignoreNetNos,
                                                   Item* obstaclesOf, bool inLeafOrder,
                                                   jrt::ArrayList<ShapeTree_Leaf*>* tmpList) {
  RegularTileShape* bounds = shape->boundingShape(boundingDirections);
  if (bounds == nullptr) {
    FRLogger::warn("ShapeSearchTree.overlaps: shape not bounded");
    return 0;
  }
  bool is45Degree = jrt::fast_cast<IntOctagon>(shape) != nullptr;
  {
    // Filter in traversal order, then sort only the kept leaves: the same order as sorting all
    // candidates first, for a fraction of the comparisons.
    this->collectOverlapsUnlocked(bounds, tmpList);
    int32_t keptCount = 0;
    for (int32_t leafIndex = 0; leafIndex < tmpList->size(); leafIndex++) {
      ShapeTree_Leaf* currentLeaf = tmpList->get(leafIndex);
      SearchTreeObject* currentObject = jrt::fast_cast<SearchTreeObject>(currentLeaf->object);
      int32_t shapeIndex = currentLeaf->shapeIndexInObject;
      bool ignoreObject = layer >= 0 && currentObject->shapeLayer(shapeIndex) != layer;
      if (!ignoreObject) {
        for (int32_t i = 0; i < ignoreNetNos->length; i++) {
          if (!currentObject->isObstacle((*ignoreNetNos)[i])) {
            ignoreObject = true;
          }
        }
      }
      if (!ignoreObject && obstaclesOf != nullptr) {
        auto* currentItem = jrt::fast_cast<Item>(currentObject);
        if (currentItem == nullptr || currentItem == obstaclesOf ||
            !currentItem->isObstacle(obstaclesOf)) {
          ignoreObject = true;
        }
      }
      if (!ignoreObject) {
        TileShape* currentShape =
            currentObject->getTreeShape(this, currentLeaf->shapeIndexInObject);
        bool addItem;
        if (is45Degree && jrt::fast_cast<IntOctagon>(currentShape) != nullptr) {
          // in this case the check for intersection is redundant and
          // therefore skipped for performance reasons
          addItem = true;
        } else {
          addItem = currentShape->intersects(static_cast<Shape*>(shape));
        }
        if (addItem) {
          tmpList->set(keptCount++, currentLeaf);
        }
      }
    }
    if (inLeafOrder) {
      sortLeafPrefix(tmpList, keptCount);
    }
    return keptCount;
  }
}

void ShapeSearchTree::overlappingTreeEntriesWithClearance(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries) {
  overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                      obstacleEntries, static_cast<Item*>(nullptr));
}

void ShapeSearchTree::overlappingTreeEntriesWithClearance(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
    Shape* exactShape) {
  overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                      obstacleEntries, static_cast<Item*>(nullptr),
                                      ExactCopper::of(exactShape));
}

void ShapeSearchTree::overlappingTreeEntriesWithClearance(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
    Item* obstaclesOf) {
  overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                      obstacleEntries, obstaclesOf,
                                      static_cast<ExactCopper*>(nullptr));
}

void ShapeSearchTree::overlappingTreeEntriesWithClearance(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
    Item* obstaclesOf, ExactCopper* exactQuery) {
  if (isClearanceCompensationUsed() && clearanceClassIndex == compensatedClearanceClassNo) {
    overlappingTreeEntriesUnlocked(shape, layer, ignoreNetNos, obstacleEntries, obstaclesOf);
  } else {
    overlappingTreeEntriesWithClearanceUnlocked(shape, layer, ignoreNetNos, clearanceClassIndex,
                                                obstacleEntries, obstaclesOf, exactQuery);
  }
}

void ShapeSearchTree::overlappingTreeEntriesWithClearanceUnlocked(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
    Item* obstaclesOf) {
  overlappingTreeEntriesWithClearanceUnlocked(shape, layer, ignoreNetNos, clearanceClassIndex,
                                              obstacleEntries, obstaclesOf,
                                              static_cast<ExactCopper*>(nullptr));
}

void ShapeSearchTree::overlappingTreeEntriesWithClearanceUnlocked(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
    Item* obstaclesOf, ExactCopper* exactQuery) {
  if (shape == nullptr) {
    return;
  }
  if (obstacleEntries == nullptr) {
    FRLogger::warn("ShapeSearchTree.overlaps_with_clearance: obstacleEntries is null");
    return;
  }
  if (layer < 0) {
    // Clearance is layer-specific. A negative layer is not a clearance-matrix index.
    for (int32_t currentLayer = 0; currentLayer < board->layerStructure->layers->length;
         currentLayer++) {
      overlappingTreeEntriesWithClearanceUnlocked(shape, currentLayer, ignoreNetNos,
                                                  clearanceClassIndex, obstacleEntries,
                                                  obstaclesOf, exactQuery);
    }
    return;
  }
  ClearanceMatrix* clMatrix = board->rules->clearanceMatrix;
  RegularTileShape* bounds = shape->boundingShape(boundingDirections);
  if (bounds == nullptr) {
    FRLogger::warn("ShapeSearchTree.overlaps_with_clearance: shape is not bounded");
    bounds = board->getBoundingBox();
  }
  int32_t maxClearance = jrt::d2i(1.2 * clMatrix->maxValue(clearanceClassIndex, layer));
  // search with the bounds enlarged by the maximum clearance to
  // get all candidates for overlap
  // a factor less than sqr2 has evtl. be added because
  // enlarging is not symmetric.
  RegularTileShape* offsetBounds = jrt::fast_cast<RegularTileShape>(bounds->offset(maxClearance));
  jrt::ArrayList<ShapeTree_Leaf*>* tmpList = QUERY_LEAF_BUFFER();
  tmpList->clear();
  // sort the found items by its clearances to clearanceClassIndex on layer layer
  // Entries never compare equal (see EntrySortedByClearance), so sorting a list gives the
  // order a TreeSet would, without its rebalancing on every insertion.
  auto* sortedItems = new (jrt::kOp) jrt::ArrayList<ShapeSearchTree_EntrySortedByClearance*>();
  jrt::DisposeAllOnExit<ShapeSearchTree_EntrySortedByClearance> sortedItemsOwner(sortedItems);
  {
    ClearListOnExit guard{tmpList};
    // Filter in traversal order, then sort only the kept leaves: the same order as sorting all
    // candidates first, for a fraction of the comparisons.
    collectOverlapsUnlocked(offsetBounds, tmpList);
    int32_t keptCount = 0;
    for (int32_t leafIndex = 0; leafIndex < tmpList->size(); leafIndex++) {
      ShapeTree_Leaf* currentLeaf = tmpList->get(leafIndex);
      SearchTreeObject* currentObject = jrt::fast_cast<SearchTreeObject>(currentLeaf->object);
      int32_t shapeIndex = currentLeaf->shapeIndexInObject;
      bool ignoreItem = layer >= 0 && currentObject->shapeLayer(shapeIndex) != layer;
      if (!ignoreItem) {
        for (int32_t i = 0; i < ignoreNetNos->length; i++) {
          if (!currentObject->isObstacle((*ignoreNetNos)[i])) {
            ignoreItem = true;
          }
        }
      }
      if (!ignoreItem) {
        if (auto* currentItem = jrt::fast_cast<Item>(currentObject)) {
          // Erik Gavriluk (freerouting.net): skip items that are not obstacles to obstaclesOf
          // before sorting candidates and running exact geometric intersections.
          if (obstaclesOf != nullptr &&
              (currentItem == obstaclesOf || !currentItem->isObstacle(obstaclesOf))) {
            continue;
          }
          int32_t currentClearance =
              additionalClearance(clearanceClassIndex, currentItem->clearanceClassIndex(), layer);
          sortedItems->add(
              new (jrt::kOp) ShapeSearchTree_EntrySortedByClearance(currentLeaf, currentClearance));
        } else if (obstaclesOf == nullptr &&
                   currentObject->getTreeShape(this, shapeIndex)
                       ->intersects(static_cast<Shape*>(shape))) {
          tmpList->set(keptCount++, currentLeaf);
        }
      }
    }
    // Expansion rooms have no physical clearance class. Preserve their direct overlap
    // semantics even when a mismatched query class needs conservative item checking.
    sortLeafPrefix(tmpList, keptCount);
    for (int32_t i = 0; i < keptCount; i++) {
      ShapeTree_Leaf* currentLeaf = tmpList->get(i);
      obstacleEntries->add(new (jrt::kOp) ShapeTree_TreeEntry(
          jrt::fast_cast<SearchTreeObject>(currentLeaf->object), currentLeaf->shapeIndexInObject));
    }
  }
  sortedItems->sort(nullptr);
  bool exactCopper = board != nullptr && board->rules != nullptr && board->rules->isExactCopper();
  auto* queryCopperHolder = jrt::Array<ExactCopper*>::of({exactQuery});
  auto* queryCopperKnownHolder = jrt::Array<bool>::of({exactQuery != nullptr});
  // The holders are operation temporaries (not the query copper they hold).
  jrt::DisposeOnExit queryCopperHolderOwner(queryCopperHolder);
  jrt::DisposeOnExit queryCopperKnownHolderOwner(queryCopperKnownHolder);
  int32_t currentHalfClearance = 0;
  ConvexShape* currentOffsetShape = shape;
  IntOctagon* currentOffsetOctagon = boundingOctagonOf(currentOffsetShape);
  for (ShapeSearchTree_EntrySortedByClearance* tmpEntry : *sortedItems) {
    int32_t tmpHalfClearance =
        isClearanceCompensationUsed() ? (tmpEntry->clearance + 1) / 2 : tmpEntry->clearance / 2;
    if (tmpHalfClearance != currentHalfClearance) {
      currentHalfClearance = tmpHalfClearance;
      currentOffsetShape = jrt::fast_cast<ConvexShape>(shape->enlarge(currentHalfClearance));
      currentOffsetOctagon = boundingOctagonOf(currentOffsetShape);
    }
    TileShape* tmpShape =
        tmpEntry->leaf->object->getTreeShape(this, tmpEntry->leaf->shapeIndexInObject);
    if (enlargedSimplexApart(currentOffsetOctagon, tmpShape, currentHalfClearance)) {
      // boundingOctagonsApart below would reject this entry: skip enlarging its shape.
      continue;
    }
    // enlarge both item shapes by the half clearance to create
    // symmetry.
    ConvexShape* tmpOffsetShape = jrt::fast_cast<ConvexShape>(tmpShape->enlarge(currentHalfClearance));
    if (!boundingOctagonsApart(currentOffsetShape, tmpOffsetShape) &&
        currentOffsetShape->intersects(static_cast<Shape*>(tmpOffsetShape)) &&
        !(exactCopper &&
          copperApart(shape, exactQuery != nullptr, queryCopperHolder, queryCopperKnownHolder,
                      tmpEntry->leaf->object, tmpEntry->leaf->shapeIndexInObject, tmpShape,
                      tmpEntry->clearance))) {
      obstacleEntries->add(
          new (jrt::kOp) ShapeTree_TreeEntry(tmpEntry->leaf->object, tmpEntry->leaf->shapeIndexInObject));
    }
  }
}

bool ShapeSearchTree::copperApart(ConvexShape* queryShape, bool queryIsCopper,
                                  jrt::Array<ExactCopper*>* queryCopperHolder,
                                  jrt::Array<bool>* queryCopperKnownHolder,
                                  jrt::Object* entryObject, int32_t entryIndex,
                                  TileShape* entryTile, int32_t clearance) {
  auto* treeObject = jrt::fast_cast<SearchTreeObject>(entryObject);
  ExactCopper* entry = treeObject != nullptr ? exactCopperOf(treeObject, entryIndex) : nullptr;
  if (entry == nullptr) {
    if (!queryIsCopper) {
      return false;
    }
    entry = ExactCopper::of(entryTile);
  }
  if (!(*queryCopperKnownHolder)[0]) {
    (*queryCopperHolder)[0] = ExactCopper::of(queryShape);
    (*queryCopperKnownHolder)[0] = true;
  }
  if (entry == nullptr || (*queryCopperHolder)[0] == nullptr) {
    return false;
  }
  return ExactCopper::distance((*queryCopperHolder)[0], entry) >=
         jrt::Math::max(clearance, 1);
}

ExactCopper* ShapeSearchTree::exactCopperOf(SearchTreeObject* treeObject, int32_t index) {
  auto* drillItem = jrt::fast_cast<DrillItem>(treeObject);
  if (drillItem == nullptr || this->board == nullptr || index < 0 ||
      index >= drillItem->tileShapeCount()) {
    return nullptr;
  }
  TileShape* treeShape = drillItem->getTreeShape(this, index);
  jrt::Array<DrillItem_ExactCopperEntry*>* cache = drillItem->exactCopperCache;
  if (treeShape != nullptr && cache != nullptr && index < cache->length &&
      (*cache)[index] != nullptr && (*cache)[index]->treeShape == treeShape) {
    return (*cache)[index]->copper;
  }
  // PORT NOTE (stage E): the result is cached in the item: compute it in the item's domain.
  jrt::DomainScope domain(drillItem);
  Shape* shape = drillItem->getShape(index);
  if (shape == nullptr) {
    shape = drillHoleObstacle(drillItem);
  }
  ExactCopper* result = nullptr;
  if (shape != nullptr) {
    int32_t layer = drillItem->shapeLayer(index);
    int32_t grow = this->clearanceCompensationValue(drillItem->clearanceClassIndex(), layer) +
                   drillHoleClearanceDelta(drillItem, shape, layer);
    PadOval* oval = nullptr;
    if (auto* pin = jrt::fast_cast<Pin>(drillItem)) {
      if (shape == drillItem->getShape(index)) {
        oval = pin->getPlacedOval(index);
      }
    }
    // A round or oval pad's shape is the bounding polygon of its copper; measure the copper.
    result = oval != nullptr ? ExactCopper::ofOval(oval, grow) : ExactCopper::of(shape, grow);
  }
  if (treeShape != nullptr) {
    if (cache == nullptr || cache->length != drillItem->tileShapeCount()) {
      cache = new (jrt::kBoard) jrt::Array<DrillItem_ExactCopperEntry*>(drillItem->tileShapeCount());
      drillItem->exactCopperCache = cache;
    }
    (*cache)[index] = new (jrt::kBoard) DrillItem_ExactCopperEntry(treeShape, result);
  }
  return result;
}

IntOctagon* ShapeSearchTree::boundingOctagonOf(ConvexShape* shape) {
  auto* tile = jrt::fast_cast<TileShape>(shape);
  return tile != nullptr ? tile->boundingOctagon() : nullptr;
}

bool ShapeSearchTree::enlargedSimplexApart(IntOctagon* queryOctagon, TileShape* entryShape,
                                           int32_t halfClearance) {
  auto* entrySimplex = jrt::fast_cast<Simplex>(entryShape);
  if (queryOctagon == nullptr || entrySimplex == nullptr) {
    return false;
  }
  IntOctagon* entryOctagon = entrySimplex->boundingOctagon();
  return entryOctagon != nullptr &&
         !entryOctagon->offset(halfClearance + ENLARGED_OCTAGON_MARGIN)->intersects(queryOctagon);
}

bool ShapeSearchTree::boundingOctagonsApart(ConvexShape* a, ConvexShape* b) {
  if (jrt::fast_cast<Simplex>(a) == nullptr && jrt::fast_cast<Simplex>(b) == nullptr) {
    return false;
  }
  auto* tileA = jrt::fast_cast<TileShape>(a);
  auto* tileB = jrt::fast_cast<TileShape>(b);
  if (tileA == nullptr || tileB == nullptr) {
    return false;
  }
  IntOctagon* octagonA = tileA->boundingOctagon();
  IntOctagon* octagonB = tileB->boundingOctagon();
  return octagonA != nullptr && octagonB != nullptr && !octagonA->intersects(octagonB);
}

jrt::Collection<ShapeTree_TreeEntry*>* ShapeSearchTree::overlappingTreeEntriesWithClearance(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex) {
  return overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                             static_cast<Item*>(nullptr));
}

jrt::Collection<ShapeTree_TreeEntry*>* ShapeSearchTree::overlappingTreeEntriesWithClearance(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex, Item* obstaclesOf) {
  return overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                             obstaclesOf, static_cast<ExactCopper*>(nullptr));
}

jrt::Collection<ShapeTree_TreeEntry*>* ShapeSearchTree::overlappingTreeEntriesWithClearance(
    ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
    int32_t clearanceClassIndex, Item* obstaclesOf, ExactCopper* exactQuery) {
  jrt::Collection<ShapeTree_TreeEntry*>* result = new (jrt::kOp) jrt::LinkedList<ShapeTree_TreeEntry*>();
  this->overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                            result, obstaclesOf, exactQuery);
  return result;
}

void ShapeSearchTree::overlappingObjectsWithClearance(ConvexShape* shape, int32_t layer,
                                                      jrt::Array<int32_t>* ignoreNetNos,
                                                      int32_t clearanceClassIndex,
                                                      jrt::Set<SearchTreeObject*>* obstacles) {
  jrt::Collection<ShapeTree_TreeEntry*>* treeEntries = new (jrt::kOp) jrt::LinkedList<ShapeTree_TreeEntry*>();
  jrt::DisposeAllOnExit<ShapeTree_TreeEntry> treeEntriesOwner(treeEntries);
  this->overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                            treeEntries);
  for (ShapeTree_TreeEntry* currentEntry : *treeEntries) {
    obstacles->add(jrt::fast_cast<SearchTreeObject>(currentEntry->object));
  }
}

void ShapeSearchTree::overlappingObjectsWithClearance(ConvexShape* shape, int32_t layer,
                                                      jrt::Array<int32_t>* ignoreNetNos,
                                                      int32_t clearanceClassIndex,
                                                      jrt::Collection<Item*>* obstacles) {
  overlappingObjectsWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex, obstacles,
                                  static_cast<Shape*>(nullptr));
}

void ShapeSearchTree::overlappingObjectsWithClearance(ConvexShape* shape, int32_t layer,
                                                      jrt::Array<int32_t>* ignoreNetNos,
                                                      int32_t clearanceClassIndex,
                                                      jrt::Collection<Item*>* obstacles,
                                                      Shape* exactShape) {
  jrt::Collection<ShapeTree_TreeEntry*>* treeEntries = new (jrt::kOp) jrt::LinkedList<ShapeTree_TreeEntry*>();
  jrt::DisposeAllOnExit<ShapeTree_TreeEntry> treeEntriesOwner(treeEntries);
  this->overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                            treeEntries, exactShape);
  for (ShapeTree_TreeEntry* currentEntry : *treeEntries) {
    if (auto* item = jrt::fast_cast<Item>(currentEntry->object)) {
      obstacles->add(item);
    }
  }
}

jrt::Set<Item*>* ShapeSearchTree::overlappingItemsWithClearance(ConvexShape* shape, int32_t layer,
                                                                jrt::Array<int32_t>* ignoreNetNos,
                                                                int32_t clearanceClassIndex) {
  return overlappingItemsWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                       static_cast<Shape*>(nullptr));
}

jrt::Set<Item*>* ShapeSearchTree::overlappingItemsWithClearance(ConvexShape* shape, int32_t layer,
                                                                jrt::Array<int32_t>* ignoreNetNos,
                                                                int32_t clearanceClassIndex,
                                                                Shape* exactShape) {
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  this->overlappingObjectsWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                        static_cast<jrt::Collection<Item*>*>(result), exactShape);
  return result;
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree::completeShape(
    IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
    TileShape* ignoreShape) {
  return completeShapeUnlocked(room, netNumber, ignoreObject, ignoreShape);
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree::completeShapeUnlocked(
    IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
    TileShape* ignoreShape) {
  if (room->getContainedShape() == nullptr) {
    FRLogger::warn("ShapeSearchTree.completeShape: shapeToBeContained != null expected");
    return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }
  if (this->root == nullptr) {
    return new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  }
  TileShape* startShape = board->getBoundingBox();
  if (room->getShape() != nullptr) {
    startShape = startShape->intersection(room->getShape());
  }
  RegularTileShape* boundingShape = startShape->boundingShape(this->boundingDirections);
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* result =
      new (jrt::kOp) jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();
  if (startShape->dimension() == 2) {
    IncompleteFreeSpaceExpansionRoom* newRoom =
        new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(startShape, room->getLayer(), room->getContainedShape());
    result->add(newRoom);
  }

  // To ensure exact algorithmic parity with v1.9, we need to visit obstacles
  // in a deterministic order. The non-deterministic order of tree traversal
  // causes different room partitioning.
  auto* overlappingLeaves = new (jrt::kOp) jrt::ArrayList<ShapeTree_Leaf*>();
  int32_t roomLayer = room->getLayer();
  ArrayStack<ShapeTree_TreeNode*>* stack = acquireScratchStack();
  {
    ReleaseStackOnExit guard{stack, &MinAreaTree::releaseScratchStack};
    stack->push(this->root);
    ShapeTree_TreeNode* currentNode;

    while ((currentNode = stack->pop()) != nullptr) {
      if (currentNode->boundingShape->intersects(static_cast<Shape*>(boundingShape))) {
        if (auto* leaf = jrt::fast_cast<ShapeTree_Leaf>(currentNode)) {
          overlappingLeaves->add(leaf);
        } else {
          stack->push(static_cast<ShapeTree_InnerNode*>(currentNode)->firstChild);
          stack->push(static_cast<ShapeTree_InnerNode*>(currentNode)->secondChild);
        }
      }
    }
  }

  // Sort obstacles to ensure deterministic room partitioning.
  // v1.9's "natural" order was based on its tree structure.
  // Sorting with Leaf's natural comparison (which uses item idNo and shapeIndex)
  // provides a stable visit order.
  jrt::Collections::sort<ShapeTree_Leaf*>(overlappingLeaves);

  for (ShapeTree_Leaf* currentLeaf : *overlappingLeaves) {
    SearchTreeObject* currentObject = jrt::fast_cast<SearchTreeObject>(currentLeaf->object);
    int32_t shapeIndex = currentLeaf->shapeIndexInObject;
    if (currentObject->isTraceObstacle(netNumber, shapeIndex) &&
        currentObject->shapeLayer(shapeIndex) == roomLayer && currentObject != ignoreObject) {
      TileShape* currentObjectShape = currentObject->getTreeShape(this, shapeIndex);
      jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* newResult =
          new (jrt::kOp) jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();
      RegularTileShape* newBoundingShape = IntOctagon::EMPTY();

      for (IncompleteFreeSpaceExpansionRoom* currentIncompleteRoom : *result) {
        bool somethingChanged = false;
        TileShape* intersection =
            currentIncompleteRoom->getShape()->intersection(currentObjectShape);
        if (intersection->dimension() == 2) {
          bool ignoreExpansionRoom =
              jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(currentObject) != nullptr &&
              ignoreShape != nullptr && ignoreShape->contains(intersection);
          if (FRLogger::isTraceEnabled()) {
            FRLogger::trace(
                "COMPLETE_SHAPE_DECISION" + std::string(", net=") + jrt::str(netNumber) +
                ", layer=" + jrt::str(roomLayer) +
                ", action=" + (ignoreExpansionRoom ? "IGNORE" : "RESTRAIN") +
                ", obstacle_type=" + jrt::className(currentObject) +
                ", obstacle_bounds=" + jrt::str(currentObjectShape->boundingBox()) +
                ", overlap_bounds=" + jrt::str(intersection->boundingBox()) + ", ignore_bounds=" +
                (ignoreShape == nullptr ? std::string("null")
                                        : jrt::str(ignoreShape->boundingBox())));
          }

          if (!ignoreExpansionRoom) {
            somethingChanged = true;
            jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* newRooms =
                restrainShape(currentIncompleteRoom, currentObjectShape);
            newResult->addAll(newRooms);
            // Keep v1.9 semantics: the bounding shape must include all accumulated rooms.
            for (IncompleteFreeSpaceExpansionRoom* tmpRoom : *newResult) {
              newBoundingShape = newBoundingShape->union_(
                  tmpRoom->getShape()->boundingShape(this->boundingDirections));
            }
          }
        }
        if (!somethingChanged) {
          newResult->add(currentIncompleteRoom);
          newBoundingShape = newBoundingShape->union_(
              currentIncompleteRoom->getShape()->boundingShape(this->boundingDirections));
        }
      }
      result = newResult;
      boundingShape = newBoundingShape;
    }
  }
  return divideLargeRoom(result, board->getBoundingBox());
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree::restrainShape(
    IncompleteFreeSpaceExpansionRoom* incompleteRoom, TileShape* obstacleShape) {
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* result =
      new (jrt::kOp) jrt::ArrayList<IncompleteFreeSpaceExpansionRoom*>();
  // Search the edge line of obstacleShape, so that shapeToBeContained
  // are on the right side of this line, and that the line segment
  // intersects with the interior of shape.
  // If there are more than 1 such lines take the line which is
  // furthest away from pointsToBeContained.
  // Then intersect shape with the halfplane defined by the
  // opposite of this line.

  // Always convert to Simplex to match v1.9 semantics - the comment below explains why:
  // "otherwise border_lines of length 0 for octagons may not be handled correctly"
  Simplex* obstacleSimplex = obstacleShape->toSimplex();

  TileShape* shapeToBeContained = incompleteRoom->getContainedShape();
  if (shapeToBeContained != nullptr) {
    shapeToBeContained = shapeToBeContained->toSimplex();  // There may be a performance problem,
    // if a point shape is represented as an octagon
  }
  TileShape* roomShape = incompleteRoom->getShape();
  if (shapeToBeContained == nullptr || shapeToBeContained->isEmpty()) {
    FRLogger::trace("ShapeSearchTree.restrain_shape: shapeToBeContained is empty");
    return result;
  }
  int32_t layer = incompleteRoom->getLayer();
  Line* cutLine = nullptr;
  double cutLineDistance = -1;

  for (int32_t i = 0; i < obstacleSimplex->borderLineCount(); i++) {
    LineSegment* currentLineSegment = new (jrt::kGeom) LineSegment(static_cast<PolylineShape*>(obstacleSimplex), i);
    if (roomShape->isIntersectedInteriorBy(currentLineSegment)) {
      // otherwise currentObject may not touch the intersection
      // of shape with the half_plane defined by the cutLine.
      // That may lead to problems when creating the ExpansionRooms.
      Line* currentLine = obstacleSimplex->borderLine(i);

      double currentMinDistance = shapeToBeContained->distanceToTheLeft(currentLine);

      if (currentMinDistance > cutLineDistance) {
        cutLineDistance = currentMinDistance;
        cutLine = currentLine->opposite();
      }
    }
  }

  if (cutLine != nullptr) {
    TileShape* resultPiece = TileShape::getInstance(cutLine);
    if (roomShape != nullptr) {
      resultPiece = roomShape->intersection(resultPiece);
    }
    if (resultPiece->dimension() >= 2) {
      result->add(new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(resultPiece, layer, shapeToBeContained));
    }
  } else {
    // There is no cut line, so that all shapeToBeContained is
    // completely on the right side of that line. Search a cut line, so that
    // at least part of shapeToBeContained is on the right side.
    if (shapeToBeContained->dimension() < 1) {
      // There is already a completed expansion room around shapeToBeContained.
      return result;
    }

    for (int32_t i = 0; i < obstacleSimplex->borderLineCount(); i++) {
      LineSegment* currentLineSegment =
          new (jrt::kGeom) LineSegment(static_cast<PolylineShape*>(obstacleSimplex), i);
      if (roomShape->isIntersectedInteriorBy(currentLineSegment)) {
        Line* currentLine = obstacleSimplex->borderLine(i);
        if (shapeToBeContained->sideOf(currentLine) == Side::COLLINEAR()) {
          // currentLine intersects with the interior of shapeToBeContained
          cutLine = currentLine->opposite();
          break;
        }
      }
    }

    if (cutLine == nullptr) {
      // cut line not found, parts or the whole of shape may be already
      // occupied from somewhere else.
      return result;
    }
    // Calculate the new shape to be contained in the result shape.
    TileShape* cutHalfPlane = TileShape::getInstance(cutLine);
    TileShape* newShapeToBeContained = shapeToBeContained->intersection(cutHalfPlane);

    TileShape* resultPiece;
    if (roomShape == nullptr) {
      resultPiece = cutHalfPlane;
    } else {
      resultPiece = roomShape->intersection(cutHalfPlane);
    }
    if (resultPiece->dimension() >= 2) {
      result->add(new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(resultPiece, layer, newShapeToBeContained));
    }
    TileShape* oppositeHalfPlane = TileShape::getInstance(cutLine->opposite());
    TileShape* restPiece;
    if (roomShape == nullptr) {
      restPiece = oppositeHalfPlane;
    } else {
      restPiece = roomShape->intersection(oppositeHalfPlane);
    }
    if (restPiece->dimension() >= 2) {
      TileShape* restShapeToBeContained = shapeToBeContained->intersection(oppositeHalfPlane);
      IncompleteFreeSpaceExpansionRoom* restIncompleteRoom =
          new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(restPiece, layer, restShapeToBeContained);
      result->addAll(restrainShape(restIncompleteRoom, obstacleShape));
    }
  }
  return result;
}

void ShapeSearchTree::reduceTraceShapeAtTiePin(Pin* tiePin, PolylineTrace* trace) {
  reduceTraceShapeAtTiePinUnlocked(tiePin, trace);
}

void ShapeSearchTree::reduceTraceShapeAtTiePinUnlocked(Pin* tiePin, PolylineTrace* trace) {
  TileShape* pinShape = tiePin->getTreeShapeOnLayer(this, trace->getLayer());
  FloatPoint* compareCorner;
  int32_t traceShapeNo;
  if (trace->firstCorner()->equals(tiePin->getCenter())) {
    traceShapeNo = 0;
    compareCorner = trace->polyline()->cornerApprox(1);

  } else if (trace->lastCorner()->equals(tiePin->getCenter())) {
    traceShapeNo = trace->cornerCount() - 2;
    compareCorner = trace->polyline()->cornerApprox(trace->cornerCount() - 2);
  } else {
    return;
  }
  TileShape* traceShape = trace->getTreeShape(this, traceShapeNo);
  TileShape* intersection = traceShape->intersection(pinShape);
  if (intersection->dimension() < 2) {
    return;
  }
  jrt::Array<TileShape*>* shapePieces = traceShape->cutout(pinShape);
  TileShape* newTraceShape = Simplex::EMPTY();
  for (int32_t i = 0; i < shapePieces->length; i++) {
    if ((*shapePieces)[i]->dimension() == 2) {
      if (newTraceShape == static_cast<TileShape*>(Simplex::EMPTY()) ||
          (*shapePieces)[i]->contains(compareCorner)) {
        newTraceShape = (*shapePieces)[i];
      }
    }
  }
  changeItemShape(trace, traceShapeNo, newTraceShape);
}

void ShapeSearchTree::changeItemShape(Item* item, int32_t shapeIndex, TileShape* newShape) {
  changeItemShapeUnlocked(item, shapeIndex, newShape);
}

void ShapeSearchTree::changeItemShapeUnlocked(Item* item, int32_t shapeIndex,
                                              TileShape* newShape) {
  jrt::Array<ShapeTree_Leaf*>* oldEntries = item->getSearchTreeEntries(this);
  auto* newLeafArr = new (jrt::kBoardData) jrt::Array<ShapeTree_Leaf*>(oldEntries->length);
  auto* newPrecalculatedTreeShapes = new (jrt::kGeom) jrt::Array<TileShape*>(oldEntries->length);
  removeLeaf((*oldEntries)[shapeIndex]);
  for (int32_t i = 0; i < newPrecalculatedTreeShapes->length; i++) {
    if (i == shapeIndex) {
      (*newPrecalculatedTreeShapes)[i] = newShape;

    } else {
      (*newPrecalculatedTreeShapes)[i] = item->getTreeShape(this, i);
      (*newLeafArr)[i] = (*oldEntries)[i];
    }
  }
  item->setPrecalculatedTreeShapes(newPrecalculatedTreeShapes, this);
  (*newLeafArr)[shapeIndex] = insert(static_cast<ShapeTree_Storable*>(item), shapeIndex);
  item->setSearchTreeEntries(newLeafArr, this);
}

jrt::Array<TileShape*>* ShapeSearchTree::calculateTreeShapes(DrillItem* drillItem) {
  if (this->board == nullptr) {
    return new (jrt::kGeom) jrt::Array<TileShape*>(0);
  }
  auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(drillItem->tileShapeCount());
  for (int32_t i = 0; i < result->length; i++) {
    Shape* currentShape = drillItem->getShape(i);
    if (currentShape == nullptr) {
      currentShape = drillHoleObstacle(drillItem);
    }
    if (currentShape == nullptr) {
      (*result)[i] = nullptr;
    } else {
      TileShape* currentTileShape;
      if (this->board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
        currentTileShape = currentShape->boundingBox();
      } else if (this->board->rules->getTraceAngleRestriction() ==
                 AngleRestriction::FORTYFIVE_DEGREE) {
        currentTileShape = currentShape->boundingOctagon();
      } else {
        currentTileShape = currentShape->boundingTile();
      }
      int32_t offsetWidth = this->clearanceCompensationValue(drillItem->clearanceClassIndex(),
                                                             drillItem->shapeLayer(i));
      offsetWidth += drillHoleClearanceDelta(drillItem, currentShape, drillItem->shapeLayer(i));
      if (currentTileShape == nullptr) {
        FRLogger::warn("ShapeSearchTree.calculate_tree_shapes: shape is null");
      } else {
        currentTileShape = jrt::fast_cast<TileShape>(currentTileShape->enlarge(offsetWidth));
      }
      (*result)[i] = currentTileShape;
    }
  }
  return result;
}

jrt::Array<TileShape*>* ShapeSearchTree::calculateTreeShapes(ObstacleArea* obstacleArea) {
  if (this->board == nullptr) {
    return new (jrt::kGeom) jrt::Array<TileShape*>(0);
  }
  jrt::Array<TileShape*>* convexShapes = obstacleArea->splitToConvex();
  if (convexShapes == nullptr) {
    return new (jrt::kGeom) jrt::Array<TileShape*>(0);
  }
  double maxTreeShapeWidth = 50000;
  if (this->board->communication->hostCadExists()) {
    maxTreeShapeWidth =
        jrt::Math::min(500 * this->board->communication->getResolution(Unit::MIL),
                       maxTreeShapeWidth);
  }

  jrt::Collection<TileShape*>* treeShapeList = new (jrt::kOp) jrt::LinkedList<TileShape*>();
  for (int32_t i = 0; i < convexShapes->length; i++) {
    TileShape* currentConvexShape = (*convexShapes)[i];
    int32_t offsetWidth = this->clearanceCompensationValue(obstacleArea->clearanceClassIndex(),
                                                           obstacleArea->getLayer());
    currentConvexShape = jrt::fast_cast<TileShape>(currentConvexShape->enlarge(offsetWidth));
    jrt::Array<TileShape*>* currentTreeShapes =
        currentConvexShape->divideIntoSections(maxTreeShapeWidth);
    treeShapeList->addAll(jrt::Arrays::asList(currentTreeShapes));
  }
  auto* obstacleResult = new (jrt::kGeom) jrt::Array<TileShape*>(treeShapeList->size());
  jrt::Iterator<TileShape*>* it = treeShapeList->iterator();
  for (int32_t i = 0; i < obstacleResult->length; i++) {
    (*obstacleResult)[i] = it->next();
  }
  return obstacleResult;
}

jrt::Array<TileShape*>* ShapeSearchTree::calculateTreeShapes(BoardOutline* boardOutline) {
  if (this->board == nullptr) {
    return new (jrt::kGeom) jrt::Array<TileShape*>(0);
  }
  jrt::Array<TileShape*>* result;
  if (boardOutline->keepoutOutsideOutlineGenerated()) {
    jrt::Array<TileShape*>* convexShapes = boardOutline->getKeepoutTiles();
    if (convexShapes == nullptr) {
      return new (jrt::kGeom) jrt::Array<TileShape*>(0);
    }
    jrt::Collection<TileShape*>* treeShapeList = new (jrt::kOp) jrt::LinkedList<TileShape*>();
    for (int32_t layerIndex = 0; layerIndex < this->board->layerStructure->layers->length;
         layerIndex++) {
      for (int32_t i = 0; i < convexShapes->length; i++) {
        TileShape* currentConvexShape = (*convexShapes)[i];
        int32_t offsetWidth =
            this->clearanceCompensationValue(boardOutline->clearanceClassIndex(), layerIndex);
        currentConvexShape = jrt::fast_cast<TileShape>(currentConvexShape->enlarge(offsetWidth));
        treeShapeList->add(currentConvexShape);
      }
    }
    result = new (jrt::kGeom) jrt::Array<TileShape*>(treeShapeList->size());
    jrt::Iterator<TileShape*>* it = treeShapeList->iterator();
    for (int32_t i = 0; i < result->length; i++) {
      (*result)[i] = it->next();
    }
  } else {
    // Only the line shapes of the outline are inserted as obstacles into the tree, oblique
    // lines in several slices (see BoardOutline.lineSlices).
    int32_t lineCount = boardOutline->lineCount();
    result = new (jrt::kGeom) jrt::Array<TileShape*>(boardOutline->tileShapeCount());
    int32_t halfWidth = boardOutline->getHalfWidth();
    int32_t currentNo = 0;
    for (int32_t layerIndex = 0; layerIndex < this->board->layerStructure->layers->length;
         layerIndex++) {
      int32_t cmpValue =
          this->clearanceCompensationValue(boardOutline->clearanceClassIndex(), layerIndex);
      for (int32_t lineIndex = 0; lineIndex < lineCount; lineIndex++) {
        TileShape* lineShape = boardOutline->lineShape(lineIndex, halfWidth + cmpValue);
        for (TileShape* slice : *boardOutline->splitLine(lineIndex, lineShape, halfWidth + cmpValue)) {
          (*result)[currentNo] = slice;
          ++currentNo;
        }
      }
    }
  }
  return result;
}

jrt::Array<TileShape*>* ShapeSearchTree::calculateTreeShapes(PolylineTrace* trace) {
  if (this->board == nullptr) {
    return new (jrt::kGeom) jrt::Array<TileShape*>(0);
  }
  int32_t offsetWidth = trace->getHalfWidth() + this->clearanceCompensationValue(
                                                    trace->clearanceClassIndex(), trace->getLayer());
  auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(trace->tileShapeCount());
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] = this->offsetShape(trace->polyline(), offsetWidth, i);
  }
  return result;
}

Shape* ShapeSearchTree::drillHoleObstacle(DrillItem* drillItem) {
  if (this->board == nullptr || this->board->rules == nullptr ||
      this->board->rules->getHoleClearance() <= 0 || drillItem->getPadstack() == nullptr) {
    return nullptr;
  }
  double drillRadius = drillItem->getPadstack()->getDrillRadius();
  if (drillRadius <= 0) {
    return nullptr;
  }
  Point* center = drillItem->getCenter();
  if (jrt::fast_cast<IntPoint>(center) == nullptr) {
    center = center->toFloat()->round();
  }
  return new (jrt::kOp) Circle(jrt::fast_cast<IntPoint>(center), jrt::d2i(jrt::Math::ceil(drillRadius)));
}

int32_t ShapeSearchTree::drillHoleClearanceDelta(DrillItem* drillItem, Shape* shape,
                                                 int32_t layer) {
  if (this->board == nullptr || this->board->rules == nullptr) {
    return 0;
  }
  int32_t holeClearance = this->board->rules->getHoleClearance();
  if (holeClearance <= 0 || shape == nullptr || drillItem->getPadstack() == nullptr) {
    return 0;
  }

  double drillRadius = drillItem->getPadstack()->getDrillRadius();
  if (drillRadius <= 0) {
    return 0;
  }
  double copperRadius;
  if (drillItem->getPadstack()->holeOnly) {
    copperRadius = drillRadius;
  } else {
    copperRadius = shape->borderDistance(drillItem->getCenter()->toFloat());
    if (copperRadius <= 0) {
      Shape* padShape = drillItem->getPadstack()->getShape(layer);
      copperRadius =
          padShape == nullptr ? drillRadius : padShape->borderDistance(FloatPoint::ZERO());
    }
  }
  int32_t clearanceClass = this->compensatedClearanceClassNo > 0
                               ? this->compensatedClearanceClassNo
                               : BoardRules::defaultClearanceClass();
  int32_t copperClearance = this->board->rules->clearanceMatrix->getValue(
      drillItem->clearanceClassIndex(), clearanceClass, layer, false);
  return jrt::Math::max(
      0, jrt::d2i(jrt::Math::ceil(drillRadius + holeClearance + DRILL_HOLE_CLEARANCE_MARGIN -
                                  copperRadius - copperClearance)));
}

TileShape* ShapeSearchTree::offsetShape(Polyline* polyline, int32_t halfWidth, int32_t no) {
  return polyline->offsetShape(halfWidth, no);
}

jrt::Array<TileShape*>* ShapeSearchTree::offsetShapes(Polyline* polyline, int32_t halfWidth,
                                                      int32_t fromNo, int32_t toNo) {
  return polyline->offsetShapes(halfWidth, fromNo, toNo);
}

jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* ShapeSearchTree::divideLargeRoom(
    jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* roomList, IntBox* boardBoundingBox) {
  if (roomList->size() != 1) {
    return roomList;
  }
  IncompleteFreeSpaceExpansionRoom* currentRoom = roomList->iterator()->next();
  IntBox* roomBoundingBox = currentRoom->getShape()->boundingBox();
  if (2 * roomBoundingBox->height() <= boardBoundingBox->height() ||
      2 * roomBoundingBox->width() <= boardBoundingBox->width()) {
    return roomList;
  }
  double maxSectionWidth =
      0.5 * jrt::Math::max(boardBoundingBox->height(), boardBoundingBox->width());
  jrt::Array<TileShape*>* sectionArr = currentRoom->getShape()->divideIntoSections(maxSectionWidth);
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* result =
      new (jrt::kOp) jrt::LinkedList<IncompleteFreeSpaceExpansionRoom*>();
  for (TileShape* currentSection : *sectionArr) {
    TileShape* currentShapeToBeContained =
        currentSection->intersection(currentRoom->getContainedShape());
    IncompleteFreeSpaceExpansionRoom* currentSectionRoom = new (jrt::kOp) IncompleteFreeSpaceExpansionRoom(
        currentSection, currentRoom->getLayer(), currentShapeToBeContained);
    result->add(currentSectionRoom);
  }
  return result;
}

bool ShapeSearchTree::validateEntries(Item* item) {
  jrt::Array<ShapeTree_Leaf*>* currentTreeEntries = item->getSearchTreeEntries(this);
  if (currentTreeEntries == nullptr) {
    return false;
  }
  for (int32_t i = 0; i < currentTreeEntries->length; i++) {
    ShapeTree_Leaf* currentLeaf = (*currentTreeEntries)[i];
    if (currentLeaf == nullptr || currentLeaf->shapeIndexInObject != i) {
      FRLogger::warn("tree entry inconsistent for Item");
      return false;
    }
  }
  return true;
}

}  // namespace fr
