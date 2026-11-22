// Port of app.freerouting.board.model.items.ObstacleArea.
#include "board/model/items/ObstacleArea.h"

#include <cmath>

#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/PlacementTransform.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "util/TextManager.h"

namespace fr {

ObstacleArea::ObstacleArea(Area* area, int32_t layer, Vector* translation, double rotationInDegree,
                           bool sideChanged, jrt::Array<int32_t>* netNumbers,
                           int32_t clearanceClassIndex, int32_t id, int32_t componentId,
                           const std::optional<std::string>& name, FixedState fixedState,
                           BasicBoard* board)
    : Item(netNumbers, clearanceClassIndex, id, componentId, fixedState, board),
      name(name),
      relativeArea(area) {
  this->layer = layer;
  this->translation = translation;
  this->rotationInDegree = rotationInDegree;
  this->sideChanged = sideChanged;
}

ObstacleArea::ObstacleArea(Area* area, int32_t layer, Vector* translation, double rotationInDegree,
                           bool sideChanged, int32_t clearanceClassIndex, int32_t id,
                           int32_t groupId, const std::optional<std::string>& name,
                           FixedState fixedState, BasicBoard* board)
    : ObstacleArea(area, layer, translation, rotationInDegree, sideChanged,
                   new jrt::Array<int32_t>(0), clearanceClassIndex, id, groupId, name, fixedState,
                   board) {}

Item* ObstacleArea::copy(int32_t id) {
  auto* copiedNetNos = new jrt::Array<int32_t>(netNumbers->length);
  jrt::arraycopy(netNumbers, 0, copiedNetNos, 0, netNumbers->length);
  return new ObstacleArea(relativeArea, layer, translation, rotationInDegree, sideChanged,
                          copiedNetNos, clearanceClassIndex(), id, getComponentId(), name,
                          getFixedState(), board);
}

Area* ObstacleArea::getArea() {
  if (this->precalculatedAbsoluteArea == nullptr) {
    if (this->relativeArea == nullptr) {
      FRLogger::warn("ObstacleArea.get_area: area is null");
      return nullptr;
    }
    if (getComponentId() > 0) {
      this->precalculatedAbsoluteArea =
          board->components->get(getComponentId())
              ->placementTransform(board->components->getFlipStyleRotateFirst())
              ->area(relativeArea);
      return this->precalculatedAbsoluteArea;
    }
    Area* turnedArea = this->relativeArea;
    if (this->sideChanged && !this->board->components->getFlipStyleRotateFirst()) {
      turnedArea = turnedArea->mirrorVertical(Point::ZERO());
    }
    if (this->rotationInDegree != 0) {
      double rotation = this->rotationInDegree;
      if (std::fmod(rotation, 90.0) == 0) {
        turnedArea = turnedArea->turn90Degree(jrt::d2i(rotation) / 90, Point::ZERO());
      } else {
        turnedArea = turnedArea->rotateApprox(jrt::Math::toRadians(rotation), FloatPoint::ZERO());
      }
    }
    if (this->sideChanged && this->board->components->getFlipStyleRotateFirst()) {
      turnedArea = turnedArea->mirrorVertical(Point::ZERO());
    }
    this->precalculatedAbsoluteArea = turnedArea->translateBy(this->translation);
  }
  return this->precalculatedAbsoluteArea;
}

IntBox* ObstacleArea::boundingBox() { return this->getArea()->boundingBox(); }

bool ObstacleArea::isObstacle(Item* other) {
  if (other->sharesNet(this)) {
    return false;
  }
  if (dynamic_cast<Pin*>(other) != nullptr) {
    return this->netCount() > 0;
  }
  if (auto* otherArea = dynamic_cast<ObstacleArea*>(other)) {
    return this->netCount() > 0 && otherArea->netCount() > 0;
  }
  return dynamic_cast<Trace*>(other) != nullptr || dynamic_cast<Via*>(other) != nullptr;
}

jrt::Array<TileShape*>* ObstacleArea::calculateTreeShapes(ShapeSearchTree* searchTree) {
  return searchTree->calculateTreeShapes(this);
}

int32_t ObstacleArea::tileShapeCount() {
  if (this->board != nullptr) {
    return this->treeShapeCount(this->board->searchTreeManager->getDefaultTree());
  }
  jrt::Array<TileShape*>* tileShapes = this->splitToConvex();
  if (tileShapes == nullptr) {
    return 0;
  }
  return tileShapes->length;
}

TileShape* ObstacleArea::getTileShape(int32_t no) {
  if (this->board != nullptr) {
    return Item::getTileShape(no);
  }
  jrt::Array<TileShape*>* tileShapes = this->splitToConvex();
  if (tileShapes == nullptr || no < 0 || no >= tileShapes->length) {
    std::string tileShapesStr = tileShapes == nullptr ? "null" : jrt::str(tileShapes->length);
    std::string nameStr = this->name.has_value() ? *this->name : std::string("null");
    std::string areaStr =
        this->getArea() != nullptr ? jrt::className(this->getArea()) : std::string("null");
    FRLogger::warn(jrt::format(
        "ConvexObstacle.get_tile_shape: requested index %d out of range (tileShapes=%s) for "
        "ObstacleArea id=%d layer=%d name=%s area=%s",
        no, tileShapesStr.c_str(), this->getId(), this->layer, nameStr.c_str(), areaStr.c_str()));
    return nullptr;
  }
  return (*tileShapes)[no];
}

void ObstacleArea::translateBy(Vector* vector) {
  this->translation = this->translation->add(vector);
  this->clearDerivedData();
}

void ObstacleArea::turn90Degree(int32_t factor, IntPoint* pole) {
  this->rotationInDegree += factor * 90;
  while (this->rotationInDegree >= 360) {
    this->rotationInDegree -= 360;
  }
  while (this->rotationInDegree < 0) {
    this->rotationInDegree += 360;
  }
  Point* relLocation = Point::ZERO()->translateBy(this->translation);
  this->translation = relLocation->turn90Degree(factor, pole)->differenceBy(Point::ZERO());
  this->clearDerivedData();
}

void ObstacleArea::rotateApprox(double angleInDegree, FloatPoint* pole) {
  double turnAngle = angleInDegree;
  if (this->sideChanged && this->board->components->getFlipStyleRotateFirst()) {
    turnAngle = 360 - angleInDegree;
  }
  this->rotationInDegree += turnAngle;
  while (this->rotationInDegree >= 360) {
    this->rotationInDegree -= 360;
  }
  while (this->rotationInDegree < 0) {
    this->rotationInDegree += 360;
  }
  FloatPoint* newTranslation =
      this->translation->toFloat()->rotate(jrt::Math::toRadians(angleInDegree), pole);
  this->translation = newTranslation->round()->differenceBy(Point::ZERO());
  this->clearDerivedData();
}

void ObstacleArea::changePlacementSide(IntPoint* pole) {
  this->sideChanged = !this->sideChanged;
  if (this->board != nullptr) {
    this->layer = board->getLayerCount() - this->layer - 1;
  }
  Point* relLocation = Point::ZERO()->translateBy(this->translation);
  this->translation = relLocation->mirrorVertical(pole)->differenceBy(Point::ZERO());
  this->clearDerivedData();
}

bool ObstacleArea::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::KEEPOUT);
}

void ObstacleArea::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager(jrt::className(this), locale);

  printer->appendBold(tm->getText("keepout"));
  int32_t cmpNo = this->getComponentId();
  if (cmpNo > 0) {
    printer->append(" " + tm->getText("of_component") + " ");
    Component* component = board->components->get(cmpNo);
    printer->append(component->name, tm->getText("component_info"), component);
  }
  this->printShapeInfo(printer, locale);
  this->printItemInfo(printer, locale);
  printer->newline();
}

void ObstacleArea::printShapeInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager(jrt::className(this), locale);

  printer->append(" " + tm->getText("at") + " ");
  FloatPoint* center = this->getArea()->getBorder()->centreOfGravity();
  printer->append(center);
  int32_t holeCount = this->relativeArea->getHoles()->length;
  if (holeCount > 0) {
    printer->append(" " + tm->getText("with") + " ");
    // PORT NOTE: NumberFormat.getInstance(locale).format(holeCount) -> plain decimal.
    printer->append(jrt::str(holeCount));
    if (holeCount == 1) {
      printer->append(" " + tm->getText("hole"));
    } else {
      printer->append(" " + tm->getText("holes"));
    }
  }
  printer->append(" " + tm->getText("on_layer") + " ");
  printer->append((*this->board->layerStructure->layers)[this->getLayer()]->name);
}

jrt::Array<TileShape*>* ObstacleArea::splitToConvex() {
  if (this->relativeArea == nullptr) {
    FRLogger::warn("ObstacleArea.split_to_convex: area is null");
    return nullptr;
  }
  return this->getArea()->splitToConvex();
}

void ObstacleArea::clearDerivedData() {
  Item::clearDerivedData();
  this->precalculatedAbsoluteArea = nullptr;
}

}  // namespace fr
