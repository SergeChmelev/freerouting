#include "geometry/planar/Side.h"

namespace fr {

Side* Side::ON_THE_LEFT() {
  static Side* v = new Side("onTheLeft");
  return v;
}

Side* Side::ON_THE_RIGHT() {
  static Side* v = new Side("onTheRight");
  return v;
}

Side* Side::COLLINEAR() {
  static Side* v = new Side("collinear");
  return v;
}

Side* Side::of(double value) {
  Side* result;
  if (value > 0) {
    result = Side::ON_THE_LEFT();
  } else if (value < 0) {
    result = Side::ON_THE_RIGHT();
  } else {
    result = Side::COLLINEAR();
  }
  return result;
}

Side* Side::negate() {
  Side* result;
  if (this == ON_THE_LEFT()) {
    result = ON_THE_RIGHT();
  } else if (this == ON_THE_RIGHT()) {
    result = ON_THE_LEFT();
  } else {
    result = this;
  }
  return result;
}

}  // namespace fr
