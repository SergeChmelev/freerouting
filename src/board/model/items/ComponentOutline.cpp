// Port of app.freerouting.board.model.items.ComponentOutline.
#include "board/model/items/ComponentOutline.h"
#include "geometry/planar/GeomCopy.h"

#include <cmath>

#include "board/facade/BasicBoard.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/PlacementTransform.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

ComponentOutline::ComponentOutline(Area* area, bool isFront, Vector* translation,
                                   double rotationInDegree, int32_t id, int32_t componentId,
                                   bool isCourtyard, bool isFabrication, bool isClosed,
                                   FixedState fixedState, BasicBoard* board)
    : Item(new (jrt::kBoard) jrt::Array<int32_t>(0), 0, id, componentId, fixedState, board),
      relativeArea(area) {
  this->isFront_ = isFront;
  this->translation = geomcopy::keep(translation, this);
  this->rotationInDegree = rotationInDegree;
  this->isCourtyard_ = isCourtyard;
  this->isFabrication_ = isFabrication;
  this->isClosed_ = isClosed;
}

Item* ComponentOutline::copy(int32_t id) {
  // PORT NOTE (deep copy): like Java, the copy resets clearanceClassIndex to 0 and netNumbers to
  // empty (both are only ever changed through setClearanceClassIndex / assignNetNo).
  return new (jrt::kBoardData) ComponentOutline(this->relativeArea, this->isFront_, this->translation,
                              this->rotationInDegree, id, this->getComponentId(),
                              this->isCourtyard_, this->isFabrication_, this->isClosed_,
                              this->getFixedState(), this->board);
}

int32_t ComponentOutline::getLayer() {
  int32_t result;
  if (this->isFront_) {
    result = 0;
  } else {
    result = this->board->getLayerCount() - 1;
  }
  return result;
}

IntBox* ComponentOutline::boundingBox() { return getArea()->boundingBox(); }

void ComponentOutline::translateBy(Vector* vector) {
  this->translation = geomcopy::keep(this->translation->add(vector), this);
  clearDerivedData();
}

void ComponentOutline::changePlacementSide(IntPoint* pole) {
  this->isFront_ = !this->isFront_;
  Point* relLocation = Point::ZERO()->translateBy(this->translation);
  this->translation = geomcopy::keep(relLocation->mirrorVertical(pole)->differenceBy(Point::ZERO()), this);
  clearDerivedData();
}

void ComponentOutline::rotateApprox(double angleInDegree, FloatPoint* pole) {
  double turnAngle = angleInDegree;
  if (!this->isFront_ && this->board->components->getFlipStyleRotateFirst()) {
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
  this->translation = geomcopy::keep(newTranslation->round()->differenceBy(Point::ZERO()), this);
  clearDerivedData();
}

void ComponentOutline::turn90Degree(int32_t factor, IntPoint* pole) {
  this->rotationInDegree += factor * 90;
  while (this->rotationInDegree >= 360) {
    this->rotationInDegree -= 360;
  }
  while (this->rotationInDegree < 0) {
    this->rotationInDegree += 360;
  }
  Point* relLocation = Point::ZERO()->translateBy(this->translation);
  this->translation = geomcopy::keep(relLocation->turn90Degree(factor, pole)->differenceBy(Point::ZERO()), this);
  clearDerivedData();
}

Area* ComponentOutline::getArea() {
  if (this->precalculatedAbsoluteArea == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
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
    if (!this->isFront_ && !this->board->components->getFlipStyleRotateFirst()) {
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
    if (!this->isFront_ && this->board->components->getFlipStyleRotateFirst()) {
      turnedArea = turnedArea->mirrorVertical(Point::ZERO());
    }
    this->precalculatedAbsoluteArea = turnedArea->translateBy(this->translation);
  }
  return this->precalculatedAbsoluteArea;
}

void ComponentOutline::clearDerivedData() {
  Item::clearDerivedData();
  precalculatedAbsoluteArea = nullptr;
}

void ComponentOutline::printInfo(ItemInfoPrinter* printer, const std::string& locale) {}

}  // namespace fr
