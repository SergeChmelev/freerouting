// Port of app.freerouting.board.model.items.DrillItem.
#include "board/model/items/DrillItem.h"
#include "geometry/planar/GeomCopy.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "core/library/Padstack.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"

namespace fr {

DrillItem::DrillItem(Point* center, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                     int32_t id, int32_t groupId, FixedState fixedState, BasicBoard* board)
    : Item(netNumbers, clearanceClassIndex, id, groupId, fixedState, board) {
  this->center = geomcopy::keep(center == nullptr ? nullptr : center->toIntegerExact());
}

void DrillItem::translateBy(Vector* vector) {
  if (center != nullptr) {
    center = center->translateBy(vector)->toIntegerExact();
  }
  this->clearDerivedData();
}

void DrillItem::turn90Degree(int32_t factor, IntPoint* pole) {
  if (center != nullptr) {
    center = center->turn90Degree(factor, pole);
  }
  this->clearDerivedData();
}

void DrillItem::rotateApprox(double angleInDegree, FloatPoint* pole) {
  if (center != nullptr) {
    FloatPoint* newCenter = center->toFloat()->rotate(jrt::Math::toRadians(angleInDegree), pole);
    this->center = geomcopy::keep(newCenter->round());
  }
  this->clearDerivedData();
}

void DrillItem::changePlacementSide(IntPoint* pole) {
  if (center != nullptr) {
    center = center->mirrorVertical(pole);
  }
  this->clearDerivedData();
}

void DrillItem::moveBy(Vector* vector) {
  Point* oldCenter = this->getCenter();
  // Item.moveBy removes the tree entry before translating; reject unsupported moves first.
  oldCenter->translateBy(vector)->toIntegerExact();
  // remember the contact situation of this drillitem to traces on each layer
  jrt::Set<DrillItem_TraceInfo*>* contactTraceInfo = new (jrt::kOp) jrt::TreeSet<DrillItem_TraceInfo*>();
  jrt::Collection<Item*>* contacts = this->getNormalContacts();
  for (Item* currentContact : *contacts) {
    if (auto* currentTrace = jrt::fast_cast<Trace>(currentContact)) {
      auto* currentTraceInfo =
          new (jrt::kBoard) DrillItem_TraceInfo(currentTrace->getLayer(), currentTrace->getHalfWidth(),
                                  currentTrace->clearanceClassIndex());
      contactTraceInfo->add(currentTraceInfo);
    }
  }
  Item::moveBy(vector);

  // Insert a Trace from the old center to the new center, on all layers, where
  // this DrillItem was connected to a Trace.
  jrt::Collection<Point*>* connectPointList = new (jrt::kOp) jrt::LinkedList<Point*>();
  connectPointList->add(oldCenter);
  Point* newCenter = this->getCenter();
  IntPoint* addCorner = nullptr;
  auto* point = jrt::fast_cast<IntPoint>(oldCenter);
  auto* point1 = jrt::fast_cast<IntPoint>(newCenter);
  if (point != nullptr && point1 != nullptr) {
    // Make sure, that the traces will remain 90- or 45-degree.
    if (board->rules->getTraceAngleRestriction() == AngleRestriction::NINETY_DEGREE) {
      addCorner = point->ninetyDegreeCorner(point1, true);
    } else if (board->rules->getTraceAngleRestriction() == AngleRestriction::FORTYFIVE_DEGREE) {
      addCorner = point->fortyfiveDegreeCorner(point1, true);
    }
  }
  if (addCorner != nullptr) {
    connectPointList->add(addCorner);
  }
  connectPointList->add(newCenter);
  auto* connectPoints = new (jrt::kGeom) jrt::Array<Point*>(connectPointList->size());
  jrt::Iterator<Point*>* it3 = connectPointList->iterator();
  for (int32_t i = 0; i < connectPoints->length; i++) {
    (*connectPoints)[i] = it3->next();
  }
  for (DrillItem_TraceInfo* currentTraceInfo : *contactTraceInfo) {
    board->insertTrace(connectPoints, currentTraceInfo->layer, currentTraceInfo->halfWidth,
                       this->netNumbers, currentTraceInfo->clearanceClassIndex,
                       FixedState::UNFIXED);
  }
}

int32_t DrillItem::shapeLayer(int32_t index) {
  index = jrt::Math::max(index, 0);
  int32_t fromLayer = firstLayer();
  int32_t toLayer = lastLayer();
  index = jrt::Math::min(index, toLayer - fromLayer);
  return fromLayer + index;
}

int32_t DrillItem::firstLayer() {
  if (this->precalculatedFirstLayer < 0) {
    Padstack* padstack = getPadstack();
    if (this->isPlacedOnFront() || padstack->placedAbsolute) {
      this->precalculatedFirstLayer = padstack->fromLayer();
    } else {
      this->precalculatedFirstLayer = padstack->boardLayerCount() - padstack->toLayer() - 1;
    }
  }
  return this->precalculatedFirstLayer;
}

int32_t DrillItem::lastLayer() {
  if (this->precalculatedLastLayer < 0) {
    Padstack* padstack = getPadstack();
    if (this->isPlacedOnFront() || padstack->placedAbsolute) {
      this->precalculatedLastLayer = padstack->toLayer();
    } else {
      this->precalculatedLastLayer = padstack->boardLayerCount() - padstack->fromLayer() - 1;
    }
  }
  return this->precalculatedLastLayer;
}

IntBox* DrillItem::boundingBox() {
  IntBox* result = IntBox::EMPTY();
  for (int32_t i = 0; i < tileShapeCount(); i++) {
    Shape* currentShape = this->getShape(i);
    if (currentShape != nullptr) {
      result = result->union_(currentShape->boundingBox());
    }
  }
  return result;
}

int32_t DrillItem::tileShapeCount() {
  Padstack* padstack = getPadstack();
  int32_t fromLayer = padstack->fromLayer();
  int32_t toLayer = padstack->toLayer();
  return jrt::Math::max(0, toLayer - fromLayer + 1);
}

jrt::Array<TileShape*>* DrillItem::calculateTreeShapes(ShapeSearchTree* searchTree) {
  return searchTree->calculateTreeShapes(this);
}

double DrillItem::smallestRadius() {
  double result = jrt::Double::MAX_VALUE;
  FloatPoint* c = getCenter()->toFloat();
  for (int32_t i = 0; i < tileShapeCount(); i++) {
    Shape* currentShape = getShape(i);
    if (currentShape != nullptr) {
      result = jrt::Math::min(result, currentShape->borderDistance(c));
    }
  }
  return result;
}

void DrillItem::setCenter(Point* center) {
  this->center = geomcopy::keep(center == nullptr ? nullptr : center->toIntegerExact());
}

TileShape* DrillItem::getTreeShapeOnLayer(ShapeSearchTree* tree, int32_t layer) {
  int32_t fromLayer = firstLayer();
  int32_t toLayer = lastLayer();
  if (layer < fromLayer || layer > toLayer) {
    FRLogger::warn("DrillItem.get_tree_shape_on_layer: layer out of range");
    return nullptr;
  }
  return getTreeShape(tree, layer - fromLayer);
}

TileShape* DrillItem::getTileShapeOnLayer(int32_t layer) {
  int32_t fromLayer = firstLayer();
  int32_t toLayer = lastLayer();
  if (layer < fromLayer || layer > toLayer) {
    FRLogger::warn("DrillItem.get_tile_shape_on_layer: layer out of range");
    return nullptr;
  }
  return getTileShape(layer - fromLayer);
}

Shape* DrillItem::getShapeOnLayer(int32_t layer) {
  int32_t fromLayer = firstLayer();
  int32_t toLayer = lastLayer();
  if (layer < fromLayer || layer > toLayer) {
    FRLogger::warn("DrillItem.get_shape_on_layer: layer out of range");
    return nullptr;
  }
  return getShape(layer - fromLayer);
}

bool DrillItem::containsCopper(Point* point, int32_t layer) {
  if (point == nullptr || !isOnLayer(layer)) {
    return false;
  }
  Shape* shape = this->getShape(layer - this->firstLayer());
  return shape != nullptr && shape->contains(point);
}

jrt::Set<Item*>* DrillItem::getNormalContacts() {
  Point* drillCenter = this->getCenter();
  TileShape* searchShape = TileShape::getInstance(drillCenter);
  jrt::Set<SearchTreeObject*>* overlaps = board->overlappingObjects(searchShape, -1);
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  // A plane is a contact where it overlaps the copper of a pin, its centre lying in the plane or
  // not: look up the planes on each layer by the shape of the pin there.
  for (int32_t layer = firstLayer(); jrt::fast_cast<Pin>(this) != nullptr && layer <= lastLayer();
       layer++) {
    TileShape* layerShape =
        getTreeShapeOnLayer(board->searchTreeManager->getDefaultTree(), layer);
    if (layerShape == nullptr) {
      continue;
    }
    for (SearchTreeObject* currentObject : *board->overlappingObjects(layerShape, layer)) {
      auto* area = jrt::fast_cast<ConductionArea>(currentObject);
      if (area != nullptr && area->overlapsCopperOf(this)) {
        result->add(area);
      }
    }
  }
  for (SearchTreeObject* currentObject : *overlaps) {
    auto* currentItem = jrt::fast_cast<Item>(currentObject);
    if (currentItem == nullptr) {
      continue;
    }
    if (currentItem != this && currentItem->sharesNet(this) && currentItem->sharesLayer(this)) {
      if (auto* currentTrace = jrt::fast_cast<Trace>(currentItem)) {
        // Use exact matching to match trace endpoints or centerline to pin/via center.
        // Tolerance-based matching causes false cycle detection during trace normalization
        // when nearby trace endpoints (but not at pin center) are incorrectly treated as
        // contacts.
        if (drillCenter != nullptr && (drillCenter->equals(currentTrace->firstCorner()) ||
                                       drillCenter->equals(currentTrace->lastCorner()) ||
                                       currentTrace->contains(drillCenter))) {
          result->add(currentItem);
        }
      } else if (auto* currentDrillItem = jrt::fast_cast<DrillItem>(currentItem)) {
        if (drillCenter->equals(currentDrillItem->getCenter())) {
          result->add(currentItem);
        }
      } else if (auto* currentArea = jrt::fast_cast<ConductionArea>(currentItem)) {
        if (currentArea->getArea()->contains(drillCenter)) {
          result->add(currentItem);
        }
      }
    }
  }
  return result;
}

bool DrillItem::isWithinTolerance(Point* p1, Point* p2, int32_t tolerance) {
  if (p1 == nullptr || p2 == nullptr) {
    return false;
  }
  // Convert to FloatPoint for distance calculation
  FloatPoint* fp1 = p1->toFloat();
  FloatPoint* fp2 = p2->toFloat();

  // Use Manhattan distance (|x1-x2| + |y1-y2|) which is faster than Euclidean
  // and sufficient for connectivity detection
  double dx = jrt::Math::abs(fp1->x - fp2->x);
  double dy = jrt::Math::abs(fp1->y - fp2->y);
  return (dx + dy) <= tolerance;
}

Point* DrillItem::normalContactPoint(Item* other) { return other->normalContactPoint(this); }

Point* DrillItem::normalContactPoint(DrillItem* other) {
  if (this->sharesLayer(other) && this->getCenter()->equals(other->getCenter())) {
    return this->getCenter();
  }
  return nullptr;
}

Point* DrillItem::normalContactPoint(Trace* trace) {
  if (!this->sharesLayer(trace)) {
    return nullptr;
  }
  Point* drillCenter = this->getCenter();
  if (drillCenter != nullptr && trace->contains(drillCenter)) {
    return drillCenter;
  }
  return nullptr;
}

jrt::Array<Point*>* DrillItem::getRatsnestCorners() {
  auto* result = new (jrt::kGeom) jrt::Array<Point*>(1);
  (*result)[0] = this->getCenter();
  return result;
}

TileShape* DrillItem::getTraceConnectionShape(ShapeSearchTree* searchTree, int32_t index) {
  return TileShape::getInstance(this->getCenter());
}

double DrillItem::minWidth() {
  if (this->precalculatedMinWidth < 0) {
    double minWidth = jrt::Integer::MAX_VALUE;
    int32_t beginLayer = this->firstLayer();
    int32_t endLayer = this->lastLayer();
    for (int32_t currentLayer = beginLayer; currentLayer <= endLayer; currentLayer++) {
      if (this->board != nullptr &&
          !(*this->board->layerStructure->layers)[currentLayer]->isSignal) {
        continue;
      }
      Shape* currentShape = this->getShapeOnLayer(currentLayer);
      if (currentShape != nullptr) {
        IntBox* currentBoundingBox = currentShape->boundingBox();
        minWidth = jrt::Math::min(minWidth, static_cast<double>(currentBoundingBox->width()));
        minWidth = jrt::Math::min(minWidth, static_cast<double>(currentBoundingBox->height()));
      }
    }
    this->precalculatedMinWidth = minWidth;
  }
  return this->precalculatedMinWidth;
}

void DrillItem::clearDerivedData() {
  Item::clearDerivedData();
  this->precalculatedFirstLayer = -1;
  this->precalculatedLastLayer = -1;
  this->exactCopperCache = nullptr;
}

}  // namespace fr
