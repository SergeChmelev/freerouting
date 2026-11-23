#include "geometry/planar/IntBox.h"

#include "geometry/planar/Circle.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/ShapeBoundingDirections.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

IntBox* IntBox::EMPTY() {
  static IntBox* v =
      new (jrt::kGeom) IntBox(Limits::CRIT_INT, Limits::CRIT_INT, -Limits::CRIT_INT, -Limits::CRIT_INT);
  return v;
}

IntBox::IntBox(int32_t lowerLeftX, int32_t lowerLeftY, int32_t upperRightX,
               int32_t upperRightY)
    : ll(new (jrt::kGeom) IntPoint(lowerLeftX, lowerLeftY)), ur(new (jrt::kGeom) IntPoint(upperRightX, upperRightY)) {}

IntBox* IntBox::ofInts(int32_t llx, int32_t lly, int32_t urx, int32_t ury) {
  // One block: [IntBox][IntPoint ll][IntPoint ur]; interior pointers keep the block alive.
  static_assert(sizeof(IntBox) % alignof(IntPoint) == 0);
  constexpr std::size_t kBox = sizeof(IntBox);
  char* mem = static_cast<char*>(::operator new(kBox + 2 * sizeof(IntPoint), jrt::kGeom));
  IntPoint* lowerLeft = new (mem + kBox) IntPoint(llx, lly);
  IntPoint* upperRight = new (mem + kBox + sizeof(IntPoint)) IntPoint(urx, ury);
  return new (mem) IntBox(lowerLeft, upperRight);
}

std::string IntBox::toString() {
  return "IntBox(ll=" + jrt::str(ll) + ", ur=" + jrt::str(ur) + ")";
}

bool IntBox::isEmpty() { return ll->x > ur->x || ll->y > ur->y; }

int32_t IntBox::borderLineCount() { return 4; }

int32_t IntBox::width() { return ur->x - ll->x; }

int32_t IntBox::height() { return ur->y - ll->y; }

double IntBox::maxWidth() { return jrt::Math::max(ur->x - ll->x, ur->y - ll->y); }

double IntBox::minWidth() { return jrt::Math::min(ur->x - ll->x, ur->y - ll->y); }

double IntBox::area() {
  return (static_cast<double>(ur->x - ll->x)) * (static_cast<double>(ur->y - ll->y));
}

double IntBox::circumference() { return 2 * ((ur->x - ll->x) + (ur->y - ll->y)); }

IntPoint* IntBox::corner(int32_t no) {
  if (no == 0) {
    return ll;
  }
  if (no == 1) {
    return new (jrt::kGeom) IntPoint(ur->x, ll->y);
  }
  if (no == 2) {
    return ur;
  }
  if (no == 3) {
    return new (jrt::kGeom) IntPoint(ll->x, ur->y);
  }
  throw jrt::IllegalArgumentException("IntBox.corner: no out of range");
}

int32_t IntBox::dimension() {
  if (isEmpty()) {
    return -1;
  }
  if (ll->equals(ur)) {
    return 0;
  }
  if (ur->x == ll->x || ll->y == ur->y) {
    return 1;
  }
  return 2;
}

bool IntBox::containsInside(IntPoint* point) {
  return point->x > this->ll->x && point->x < this->ur->x && point->y > this->ll->y &&
         point->y < this->ur->y;
}

bool IntBox::isIntBox() { return true; }

TileShape* IntBox::simplify() { return this; }

FloatPoint* IntBox::nearestPoint(FloatPoint* fromPoint) {
  double x;
  if (fromPoint->x <= ll->x) {
    x = ll->x;
  } else if (fromPoint->x >= ur->x) {
    x = ur->x;
  } else {
    x = fromPoint->x;
  }

  double y;
  if (fromPoint->y <= ll->y) {
    y = ll->y;
  } else if (fromPoint->y >= ur->y) {
    y = ur->y;
  } else {
    y = fromPoint->y;
  }

  return new (jrt::kGeom) FloatPoint(x, y);
}

jrt::Array<IntPoint*>* IntBox::nearestBorderProjections(IntPoint* point,
                                                        int32_t maxResultPoints) {
  if (maxResultPoints <= 0) {
    return new (jrt::kGeom) jrt::Array<IntPoint*>(0);
  }
  maxResultPoints = jrt::Math::min(maxResultPoints, 2);
  int32_t lowerHorizontalDifference = point->x - ll->x;
  int32_t upperHorizontalDifference = ur->x - point->x;
  int32_t lowerVerticalDifference = point->y - ll->y;
  int32_t upperVerticalDifference = ur->y - point->y;

  int32_t minDiff;
  int32_t secondMinDiff;

  int32_t nearestProjectionX = point->x;
  int32_t nearestProjectionY = point->y;
  int32_t secondNearestProjectionX = point->x;
  int32_t secondNearestProjectionY = point->y;
  if (lowerHorizontalDifference <= upperHorizontalDifference) {
    minDiff = lowerHorizontalDifference;
    secondMinDiff = upperHorizontalDifference;
    nearestProjectionX = ll->x;
    secondNearestProjectionX = ur->x;
  } else {
    minDiff = upperHorizontalDifference;
    secondMinDiff = lowerHorizontalDifference;
    nearestProjectionX = ur->x;
    secondNearestProjectionX = ll->x;
  }
  if (lowerVerticalDifference < minDiff) {
    secondMinDiff = minDiff;
    minDiff = lowerVerticalDifference;
    secondNearestProjectionX = nearestProjectionX;
    secondNearestProjectionY = nearestProjectionY;
    nearestProjectionX = point->x;
    nearestProjectionY = ll->y;
  } else if (lowerVerticalDifference < secondMinDiff) {
    secondMinDiff = lowerVerticalDifference;
    secondNearestProjectionX = point->x;
    secondNearestProjectionY = ll->y;
  }
  if (upperVerticalDifference < minDiff) {
    secondMinDiff = minDiff;
    minDiff = upperVerticalDifference;
    secondNearestProjectionX = nearestProjectionX;
    secondNearestProjectionY = nearestProjectionY;
    nearestProjectionX = point->x;
    nearestProjectionY = ur->y;
  } else if (upperVerticalDifference < secondMinDiff) {
    secondMinDiff = upperVerticalDifference;
    secondNearestProjectionX = point->x;
    secondNearestProjectionY = ur->y;
  }
  auto* result = new (jrt::kGeom) jrt::Array<IntPoint*>(maxResultPoints);
  (*result)[0] = new (jrt::kGeom) IntPoint(nearestProjectionX, nearestProjectionY);
  if (result->length > 1) {
    (*result)[1] = new (jrt::kGeom) IntPoint(secondNearestProjectionX, secondNearestProjectionY);
  }

  return result;
}

double IntBox::distance(FloatPoint* fromPoint) {
  return fromPoint->distance(nearestPoint(fromPoint));
}

double IntBox::weightedDistance(IntBox* other, double horizontalWeight, double verticalWeight) {
  double result;

  double maxLlX = jrt::Math::max(this->ll->x, other->ll->x);
  double maxLlY = jrt::Math::max(this->ll->y, other->ll->y);
  double minUrX = jrt::Math::min(this->ur->x, other->ur->x);
  double minUrY = jrt::Math::min(this->ur->y, other->ur->y);

  if (minUrX >= maxLlX) {
    result = jrt::Math::max(verticalWeight * (maxLlY - minUrY), 0.0);
  } else if (minUrY >= maxLlY) {
    result = jrt::Math::max(horizontalWeight * (maxLlX - minUrX), 0.0);
  } else {
    double deltaX = maxLlX - minUrX;
    double deltaY = maxLlY - minUrY;
    deltaX *= horizontalWeight;
    deltaY *= verticalWeight;
    result = jrt::Math::sqrt(deltaX * deltaX + deltaY * deltaY);
  }
  return result;
}

int32_t IntBox::getId() { return 31 * ll->getId() + ur->getId(); }

IntOctagon* IntBox::boundingOctagon() { return toIntOctagon(); }

bool IntBox::isBounded() { return true; }

bool IntBox::cornerIsBounded(int32_t no) { return true; }

RegularTileShape* IntBox::union_(RegularTileShape* other) { return other->union_(this); }

IntBox* IntBox::union_(IntBox* other) {
  int32_t lowerLeftX = jrt::Math::min(ll->x, other->ll->x);
  int32_t lowerLeftY = jrt::Math::min(ll->y, other->ll->y);
  int32_t upperRightX = jrt::Math::max(ur->x, other->ur->x);
  int32_t upperRightY = jrt::Math::max(ur->y, other->ur->y);
  return new (jrt::kGeom) IntBox(lowerLeftX, lowerLeftY, upperRightX, upperRightY);
}

IntOctagon* IntBox::union_(IntOctagon* other) { return other->union_(toIntOctagon()); }

IntBox* IntBox::intersection(IntBox* other) {
  if (other->ll->x > ur->x) {
    return EMPTY();
  }
  if (other->ll->y > ur->y) {
    return EMPTY();
  }
  if (ll->x > other->ur->x) {
    return EMPTY();
  }
  if (ll->y > other->ur->y) {
    return EMPTY();
  }
  int32_t lowerLeftX = jrt::Math::max(ll->x, other->ll->x);
  int32_t upperRightX = jrt::Math::min(ur->x, other->ur->x);
  int32_t lowerLeftY = jrt::Math::max(ll->y, other->ll->y);
  int32_t upperRightY = jrt::Math::min(ur->y, other->ur->y);
  return new (jrt::kGeom) IntBox(lowerLeftX, lowerLeftY, upperRightX, upperRightY);
}

TileShape* IntBox::intersection(TileShape* other) { return other->intersection(this); }

IntOctagon* IntBox::intersection(IntOctagon* other) {
  return other->intersection(this->toIntOctagon());
}

Simplex* IntBox::intersection(Simplex* other) { return other->intersection(this->toSimplex()); }

bool IntBox::intersects(Shape* other) { return other->intersects(this); }

bool IntBox::intersects(IntBox* other) {
  if (other->ll->x > this->ur->x) {
    return false;
  }
  if (other->ll->y > this->ur->y) {
    return false;
  }
  if (this->ll->x > other->ur->x) {
    return false;
  }
  return this->ll->y <= other->ur->y;
}

bool IntBox::intersects(IntOctagon* other) {
  // PORT NOTE: the octagon of this box as a local value (IntOctagon.intersects does not retain it).
  IntOctagon octagon(ll->x, ll->y, ur->x, ur->y, ll->x - ur->y, ur->x - ll->y, ll->x + ll->y,
                     ur->x + ur->y);
  return other->intersects(&octagon);
}

bool IntBox::intersects(Simplex* other) { return other->intersects(toSimplex()); }

bool IntBox::intersects(Circle* other) { return other->intersects(this); }

bool IntBox::overlaps(IntBox* other) {
  if (other->ll->x >= this->ur->x) {
    return false;
  }
  if (other->ll->y >= this->ur->y) {
    return false;
  }
  if (this->ll->x >= other->ur->x) {
    return false;
  }
  return this->ll->y < other->ur->y;
}

bool IntBox::contains(RegularTileShape* other) { return other->isContainedIn(this); }

RegularTileShape* IntBox::boundingShape(ShapeBoundingDirections* dirs) {
  return dirs->bounds(this);
}

IntOctagon* IntBox::enlarge(double offset) { return boundingOctagon()->offset(offset); }

IntBox* IntBox::translateBy(Vector* relCoor) {
  // This function is at the moment only implemented for Vectors
  // with integer coordinates.
  // The general implementation is still missing.

  if (relCoor->equals(Vector::ZERO())) {
    return this;
  }
  IntVector* translation = relCoor->toIntegerExact();
  IntPoint* newLl = jrt::fast_cast<IntPoint>(ll->translateBy(translation));
  IntPoint* newUr = jrt::fast_cast<IntPoint>(ur->translateBy(translation));
  return new (jrt::kGeom) IntBox(newLl, newUr);
}

IntBox* IntBox::turn90Degree(int32_t factor, IntPoint* pole) {
  IntPoint* p1 = jrt::fast_cast<IntPoint>(ll->turn90Degree(factor, pole));
  IntPoint* p2 = jrt::fast_cast<IntPoint>(ur->turn90Degree(factor, pole));

  int32_t lowerLeftX = jrt::Math::min(p1->x, p2->x);
  int32_t lowerLeftY = jrt::Math::min(p1->y, p2->y);
  int32_t upperRightX = jrt::Math::max(p1->x, p2->x);
  int32_t upperRightY = jrt::Math::max(p1->y, p2->y);
  return new (jrt::kGeom) IntBox(lowerLeftX, lowerLeftY, upperRightX, upperRightY);
}

namespace {
bool setBorderLine(int32_t* c, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
  c[0] = ax;
  c[1] = ay;
  c[2] = bx;
  c[3] = by;
  return true;
}
}  // namespace

bool IntBox::borderLineCoordinates(int32_t no, int32_t* c) {
  switch (no) {
    case 0:
      return setBorderLine(c, 0, ll->y, 1, ll->y);  // lower boundary line
    case 1:
      return setBorderLine(c, ur->x, 0, ur->x, 1);  // right boundary line
    case 2:
      return setBorderLine(c, 0, ur->y, -1, ur->y);  // upper boundary line
    case 3:
      return setBorderLine(c, ll->x, 0, ll->x, -1);  // left boundary line
    default:
      throw jrt::IllegalArgumentException("IntBox.borderLine: no out of range");
  }
}

Line* IntBox::borderLine(int32_t no) {
  int32_t c[4];
  borderLineCoordinates(no, c);
  return Line::ofInts(c[0], c[1], c[2], c[3]);
}

int32_t IntBox::borderLineIndex(Line* line) {
  FRLogger::warn("borderLineIndex not yet implemented for IntBoxes");
  return -1;
}

IntBox* IntBox::offset(double dist) {
  if (dist == 0 || isEmpty()) {
    return this;
  }
  int32_t roundedDistance = jrt::l2i(jrt::Math::round(dist));
  IntPoint* lowerLeft = new (jrt::kGeom) IntPoint(ll->x - roundedDistance, ll->y - roundedDistance);
  IntPoint* upperRight = new (jrt::kGeom) IntPoint(ur->x + roundedDistance, ur->y + roundedDistance);
  return new (jrt::kGeom) IntBox(lowerLeft, upperRight);
}

IntBox* IntBox::horizontalOffset(double dist) {
  if (dist == 0 || isEmpty()) {
    return this;
  }
  int32_t roundedDistance = jrt::l2i(jrt::Math::round(dist));
  IntPoint* lowerLeft = new (jrt::kGeom) IntPoint(ll->x - roundedDistance, ll->y);
  IntPoint* upperRight = new (jrt::kGeom) IntPoint(ur->x + roundedDistance, ur->y);
  return new (jrt::kGeom) IntBox(lowerLeft, upperRight);
}

IntBox* IntBox::verticalOffset(double dist) {
  if (dist == 0 || isEmpty()) {
    return this;
  }
  int32_t roundedDistance = jrt::l2i(jrt::Math::round(dist));
  IntPoint* lowerLeft = new (jrt::kGeom) IntPoint(ll->x, ll->y - roundedDistance);
  IntPoint* upperRight = new (jrt::kGeom) IntPoint(ur->x, ur->y + roundedDistance);
  return new (jrt::kGeom) IntBox(lowerLeft, upperRight);
}

IntBox* IntBox::shrink(int32_t width) {
  int32_t lowerLeftX;
  int32_t upperRightX;
  if (2 * width <= this->ur->x - this->ll->x) {
    lowerLeftX = this->ll->x + width;
    upperRightX = this->ur->x - width;
  } else {
    lowerLeftX = (this->ll->x + this->ur->x) / 2;
    upperRightX = lowerLeftX;
  }
  int32_t lowerLeftY;
  int32_t upperRightY;
  if (2 * width <= this->ur->y - this->ll->y) {
    lowerLeftY = this->ll->y + width;
    upperRightY = this->ur->y - width;
  } else {
    lowerLeftY = (this->ll->y + this->ur->y) / 2;
    upperRightY = lowerLeftY;
  }
  return new (jrt::kGeom) IntBox(lowerLeftX, lowerLeftY, upperRightX, upperRightY);
}

Side* IntBox::compare(RegularTileShape* other, int32_t edgeIndex) {
  Side* result = other->compare(this, edgeIndex);
  return result->negate();
}

Side* IntBox::compare(IntBox* other, int32_t edgeIndex) {
  Side* result;
  switch (edgeIndex) {
    case 0:
      // compare the lower edge line
      if (ll->y > other->ll->y) {
        result = Side::ON_THE_LEFT();
      } else if (ll->y < other->ll->y) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 1:
      // compare the right edge line
      if (ur->x < other->ur->x) {
        result = Side::ON_THE_LEFT();
      } else if (ur->x > other->ur->x) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 2:
      // compare the upper edge line
      if (ur->y < other->ur->y) {
        result = Side::ON_THE_LEFT();
      } else if (ur->y > other->ur->y) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 3:
      // compare the left edge line
      if (ll->x > other->ll->x) {
        result = Side::ON_THE_LEFT();
      } else if (ll->x < other->ll->x) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    default:
      throw jrt::IllegalArgumentException("IntBox.compare: edgeIndex out of range");
  }
  return result;
}

Side* IntBox::compare(IntOctagon* other, int32_t edgeIndex) {
  return toIntOctagon()->compare(other, edgeIndex);
}

IntOctagon* IntBox::toIntOctagon() {
  return new (jrt::kGeom) IntOctagon(ll->x, ll->y, ur->x, ur->y, ll->x - ur->y, ur->x - ll->y, ll->x + ll->y,
                        ur->x + ur->y);
}

Simplex* IntBox::toSimplex() {
  jrt::Array<Line*>* lines;
  if (isEmpty()) {
    lines = new (jrt::kGeom) jrt::Array<Line*>(0);
  } else {
    lines = new (jrt::kGeom) jrt::Array<Line*>(4);
    (*lines)[0] = Line::getInstance(ll, IntDirection::RIGHT());
    (*lines)[1] = Line::getInstance(ur, IntDirection::UP());
    (*lines)[2] = Line::getInstance(ur, IntDirection::LEFT());
    (*lines)[3] = Line::getInstance(ll, IntDirection::DOWN());
  }
  return new (jrt::kGeom) Simplex(lines);
}

bool IntBox::isContainedIn(IntBox* other) {
  if (isEmpty() || this == other) {
    return true;
  }
  return ll->x >= other->ll->x && ll->y >= other->ll->y && ur->x <= other->ur->x &&
         ur->y <= other->ur->y;
}

bool IntBox::isContainedIn(IntOctagon* other) { return other->contains(toIntOctagon()); }

bool IntBox::containsInInterior(IntBox* other) {
  if (other->isEmpty()) {
    return true;
  }
  return other->ll->x > ll->x && other->ll->y > ll->y && other->ur->x < ur->x &&
         other->ur->y < ur->y;
}

IntBox* IntBox::nearestPart(IntBox* fromBox) {
  int32_t llX;

  if (fromBox->ll->x >= this->ll->x) {
    llX = fromBox->ll->x;
  } else {
    llX = jrt::Math::min(fromBox->ur->x, this->ll->x);
  }

  int32_t urX;

  if (fromBox->ur->x <= this->ur->x) {
    urX = fromBox->ur->x;
  } else {
    urX = jrt::Math::max(fromBox->ll->x, this->ur->x);
  }

  int32_t llY;

  if (fromBox->ll->y >= this->ll->y) {
    llY = fromBox->ll->y;
  } else {
    llY = jrt::Math::min(fromBox->ur->y, this->ll->y);
  }

  int32_t urY;

  if (fromBox->ur->y <= this->ur->y) {
    urY = fromBox->ur->y;
  } else {
    urY = jrt::Math::max(fromBox->ll->y, this->ur->y);
  }
  return new (jrt::kGeom) IntBox(llX, llY, urX, urY);
}

jrt::Array<TileShape*>* IntBox::divideIntoSections(double maxSectionWidth) {
  // PORT NOTE: Java returns IntBox[] (array covariance); C++ returns a TileShape* array.
  if (maxSectionWidth <= 0) {
    return new (jrt::kGeom) jrt::Array<TileShape*>(0);
  }
  double length = this->ur->x - this->ll->x;
  double height = this->ur->y - this->ll->y;
  int32_t xcount = jrt::d2i(jrt::Math::ceil(length / maxSectionWidth));
  int32_t ycount = jrt::d2i(jrt::Math::ceil(height / maxSectionWidth));
  int32_t sectionLengthX = jrt::d2i(jrt::Math::ceil(length / xcount));
  int32_t sectionLengthY = jrt::d2i(jrt::Math::ceil(height / ycount));
  auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(xcount * ycount);
  int32_t currentIndex = 0;
  for (int32_t j = 0; j < ycount; j++) {
    int32_t currentLowerLeftY = this->ll->y + j * sectionLengthY;
    int32_t currentUpperRightY;
    if (j == (ycount - 1)) {
      currentUpperRightY = this->ur->y;
    } else {
      currentUpperRightY = currentLowerLeftY + sectionLengthY;
    }
    for (int32_t i = 0; i < xcount; i++) {
      int32_t currentLowerLeftX = this->ll->x + i * sectionLengthX;
      int32_t currentUpperRightX;
      if (i == (xcount - 1)) {
        currentUpperRightX = this->ur->x;
      } else {
        currentUpperRightX = currentLowerLeftX + sectionLengthX;
      }
      (*result)[currentIndex] =
          new (jrt::kGeom) IntBox(currentLowerLeftX, currentLowerLeftY, currentUpperRightX, currentUpperRightY);
      ++currentIndex;
    }
  }
  return result;
}

jrt::Array<TileShape*>* IntBox::cutout(TileShape* shape) {
  jrt::Array<TileShape*>* tmpResult = shape->cutoutFrom(this);
  auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(tmpResult->length);
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] = (*tmpResult)[i]->simplify();
  }
  return result;
}

jrt::Array<TileShape*>* IntBox::cutoutFrom(IntBox* d) {
  // PORT NOTE: Java returns IntBox[] (array covariance); C++ returns a TileShape* array.
  IntBox* c = this->intersection(d);
  if (this->isEmpty() || c->dimension() < this->dimension()) {
    // there is only an overlap at the border
    auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(1);
    (*result)[0] = d;
    return result;
  }

  auto* result = new (jrt::kGeom) jrt::Array<IntBox*>(4);

  (*result)[0] = new (jrt::kGeom) IntBox(d->ll->x, d->ll->y, c->ur->x, c->ll->y);

  (*result)[1] = new (jrt::kGeom) IntBox(d->ll->x, c->ll->y, c->ll->x, d->ur->y);

  (*result)[2] = new (jrt::kGeom) IntBox(c->ur->x, d->ll->y, d->ur->x, c->ur->y);

  (*result)[3] = new (jrt::kGeom) IntBox(c->ll->x, c->ur->y, d->ur->x, d->ur->y);

  // now the division will be optimised, so that the cumulative
  // circumference will be minimal.

  IntBox* b;

  if (c->ll->x - d->ll->x > c->ll->y - d->ll->y) {
    // switch left dividing line to lower
    b = (*result)[0];
    (*result)[0] = new (jrt::kGeom) IntBox(c->ll->x, b->ll->y, b->ur->x, b->ur->y);
    b = (*result)[1];
    (*result)[1] = new (jrt::kGeom) IntBox(b->ll->x, d->ll->y, b->ur->x, b->ur->y);
  }
  if (d->ur->y - c->ur->y > c->ll->x - d->ll->x) {
    // switch upper dividing line to the left
    b = (*result)[1];
    (*result)[1] = new (jrt::kGeom) IntBox(b->ll->x, b->ll->y, b->ur->x, c->ur->y);
    b = (*result)[3];
    (*result)[3] = new (jrt::kGeom) IntBox(d->ll->x, b->ll->y, b->ur->x, b->ur->y);
  }
  if (d->ur->x - c->ur->x > d->ur->y - c->ur->y) {
    // switch right dividing line to upper
    b = (*result)[2];
    (*result)[2] = new (jrt::kGeom) IntBox(b->ll->x, b->ll->y, b->ur->x, d->ur->y);
    b = (*result)[3];
    (*result)[3] = new (jrt::kGeom) IntBox(b->ll->x, b->ll->y, c->ur->x, b->ur->y);
  }
  if (c->ll->y - d->ll->y > d->ur->x - c->ur->x) {
    // switch lower dividing line to the left
    b = (*result)[0];
    (*result)[0] = new (jrt::kGeom) IntBox(b->ll->x, b->ll->y, d->ur->x, b->ur->y);
    b = (*result)[2];
    (*result)[2] = new (jrt::kGeom) IntBox(b->ll->x, c->ll->y, b->ur->x, b->ur->y);
  }
  auto* converted = new (jrt::kGeom) jrt::Array<TileShape*>(4);
  for (int32_t i = 0; i < 4; i++) {
    (*converted)[i] = (*result)[i];
  }
  return converted;
}

jrt::Array<TileShape*>* IntBox::cutoutFrom(Simplex* simplex) {
  return this->toSimplex()->cutoutFrom(simplex);
}

jrt::Array<TileShape*>* IntBox::cutoutFrom(IntOctagon* oct) {
  return this->toIntOctagon()->cutoutFrom(oct);
}

}  // namespace fr
