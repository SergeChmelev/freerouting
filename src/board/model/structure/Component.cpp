// Port of app.freerouting.board.model.structure.Component
#include "board/model/structure/Component.h"

#include <cmath>

#include "core/library/LogicalPart.h"
#include "core/library/Package.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/PlacementTransform.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Vector.h"
#include "util/TextManager.h"

namespace fr {

Component::Component(const std::string& name, FloatPoint* exactLocation, double rotationInDegree,
                     bool onFront, Package* packageFront, Package* packageBack, int32_t id,
                     bool positionFixed, const std::optional<std::string>& partNumber)
    : name(name), id(id), positionFixed(positionFixed) {
  this->exactLocation = exactLocation;
  this->location = exactLocation == nullptr ? nullptr : static_cast<Point*>(exactLocation->round());
  this->rotationInDegree = rotationInDegree;
  while (this->rotationInDegree >= 360) {
    this->rotationInDegree -= 360;
  }
  while (this->rotationInDegree < 0) {
    this->rotationInDegree += 360;
  }
  this->onFront = onFront;
  libPackageFront = packageFront;
  libPackageBack = packageBack;
  this->partNumber = partNumber;
}

Component::Component(const std::string& name, Point* location, double rotationInDegree,
                     bool onFront, Package* packageFront, Package* packageBack, int32_t id,
                     bool positionFixed, const std::optional<std::string>& partNumber)
    : Component(name, location == nullptr ? static_cast<FloatPoint*>(nullptr) : location->toFloat(),
                rotationInDegree, onFront, packageFront, packageBack, id, positionFixed,
                partNumber) {}

Point* Component::getLocation() {
  if (exactLocation != nullptr) {
    return exactLocation->round();
  }
  return location;
}

FloatPoint* Component::getExactLocation() {
  if (exactLocation != nullptr) {
    return exactLocation;
  }
  return location == nullptr ? nullptr : location->toFloat();
}

PlacementTransform* Component::placementTransform(bool rotateFirst) {
  return new (jrt::kGeom) PlacementTransform(getExactLocation(), rotationInDegree, !onFront, rotateFirst);
}

void Component::restorePose(Component* original) {
  exactLocation = original->exactLocation;
  location = original->location;
  rotationInDegree = original->rotationInDegree;
  onFront = original->onFront;
}

bool Component::isPlaced() { return exactLocation != nullptr || location != nullptr; }

void Component::translateBy(Vector* vector) {
  if (exactLocation != nullptr) {
    FloatPoint* vf = vector->toFloat();
    exactLocation = new (jrt::kGeom) FloatPoint(exactLocation->x + vf->x, exactLocation->y + vf->y);
    location = exactLocation->round();
  } else if (location != nullptr) {
    location = location->translateBy(vector);
    exactLocation = location->toFloat();
  }
}

void Component::turn90Degree(int32_t factor, IntPoint* pole) { turn90Degree(factor, pole, false); }

void Component::turn90Degree(int32_t factor, IntPoint* pole, bool flipStyleRotateFirst) {
  if (factor == 0) {
    return;
  }
  this->rotationInDegree =
      this->rotationInDegree + (flipStyleRotateFirst && !onFront ? -factor : factor) * 90;
  while (this->rotationInDegree >= 360) {
    this->rotationInDegree -= 360;
  }
  while (this->rotationInDegree < 0) {
    this->rotationInDegree += 360;
  }
  if (exactLocation != nullptr) {
    this->exactLocation = this->exactLocation->turn90Degree(factor, pole->toFloat());
    this->location = this->exactLocation->round();
  } else if (this->location != nullptr) {
    this->location = this->location->turn90Degree(factor, static_cast<Point*>(pole));
    this->exactLocation = this->location->toFloat();
  }
}

void Component::rotate(double angleInDegree, IntPoint* pole, bool flipStyleRotateFirst) {
  if (angleInDegree == 0) {
    return;
  }
  if (std::fmod(angleInDegree, 90.0) == 0) {
    turn90Degree(jrt::d2i(angleInDegree / 90), pole, flipStyleRotateFirst);
    return;
  }
  double turnAngle = angleInDegree;
  if (flipStyleRotateFirst && !this->placedOnFront()) {
    // take care of the order of mirroring and rotating on the back side of the board
    turnAngle = 360 - angleInDegree;
  }
  this->rotationInDegree = this->rotationInDegree + turnAngle;
  while (this->rotationInDegree >= 360) {
    this->rotationInDegree -= 360;
  }
  while (this->rotationInDegree < 0) {
    this->rotationInDegree += 360;
  }
  if (exactLocation != nullptr) {
    this->exactLocation =
        this->exactLocation->rotate(jrt::Math::toRadians(angleInDegree), pole->toFloat());
    this->location = this->exactLocation->round();
  } else if (this->location != nullptr) {
    this->location = this->location->toFloat()
                         ->rotate(jrt::Math::toRadians(angleInDegree), pole->toFloat())
                         ->round();
    this->exactLocation = this->location->toFloat();
  }
}

void Component::changeSide(IntPoint* pole) { changeSide(pole, false); }

void Component::changeSide(IntPoint* pole, bool flipStyleRotateFirst) {
  if (!flipStyleRotateFirst) {
    rotationInDegree = std::fmod(360 - rotationInDegree, 360.0);
  }
  this->onFront = !this->onFront;
  if (exactLocation != nullptr) {
    this->exactLocation =
        new (jrt::kGeom) FloatPoint(2 * pole->x - this->exactLocation->x, this->exactLocation->y);
    this->location = this->exactLocation->round();
  } else if (this->location != nullptr) {
    this->location = this->location->mirrorVertical(static_cast<Point*>(pole));
    this->exactLocation = this->location->toFloat();
  }
}

int32_t Component::compareTo(jrt::Object* other) {
  if (auto* component = jrt::fast_cast<Component>(other)) {
    return jrt::compareStringsIgnoreCase(this->name, component->name);
  }
  return 1;
}

Component* Component::clone() {
  auto* result = new (jrt::kBoard) Component(name, exactLocation, rotationInDegree, onFront, libPackageFront,
                               libPackageBack, id, positionFixed, partNumber);
  result->logicalPart = this->logicalPart;
  return result;
}

void Component::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager("app.freerouting.board.model.structure.Component", locale);

  printer->appendBold(tm->getText("component") + " ");
  printer->appendBold(this->name);
  if (this->exactLocation != nullptr) {
    printer->append(" " + tm->getText("at") + " ");
    printer->append(this->exactLocation);

    printer->append(", " + tm->getText("rotation") + " ");
    printer->appendWithoutTransforming(rotationInDegree);

    if (this->onFront) {
      printer->append(", " + tm->getText("front"));
    } else {
      printer->append(", " + tm->getText("back"));
    }
  } else {
    printer->append(" " + tm->getText("not_yet_placed"));
  }
  printer->append(", " + tm->getText("package"));
  Package* libPackage = this->getPackage();
  printer->append(libPackage->name, tm->getText("package_info"), libPackage);
  if (this->logicalPart != nullptr) {
    printer->append(", " + tm->getText("logicalPart") + " ");
    printer->append(this->logicalPart->name, tm->getText("logical_part_info"), this->logicalPart);
  }
  printer->newline();
}

Package* Component::getPackage() {
  Package* result;
  if (this->onFront) {
    result = libPackageFront;
  } else {
    result = libPackageBack;
  }
  return result;
}

}  // namespace fr
