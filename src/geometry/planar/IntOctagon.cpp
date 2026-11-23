#include "geometry/planar/IntOctagon.h"

#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/FortyfiveDegreeDirection.h"
#include "geometry/planar/IntBox.h"
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

namespace {
// PORT NOTE: Java returns IntOctagon[] where the C++ signature is TileShape[] (no array
// covariance in C++); this copies the elements into a TileShape* array.
jrt::Array<TileShape*>* toTileShapeArray(jrt::Array<IntOctagon*>* arr) {
  auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(arr->length);
  for (int32_t i = 0; i < arr->length; i++) {
    (*result)[i] = (*arr)[i];
  }
  return result;
}
}  // namespace

IntOctagon* IntOctagon::EMPTY() {
  static IntOctagon* v =
      new (jrt::kGeom) IntOctagon(Limits::CRIT_INT, Limits::CRIT_INT, -Limits::CRIT_INT, -Limits::CRIT_INT,
                     Limits::CRIT_INT, -Limits::CRIT_INT, Limits::CRIT_INT, -Limits::CRIT_INT);
  return v;
}

bool IntOctagon::isEmpty() {
  return this == EMPTY() || leftX > rightX || bottomY > topY ||
         lowerLeftDiagonalX > upperRightDiagonalX || upperLeftDiagonalX > lowerRightDiagonalX;
}

bool IntOctagon::isBounded() { return true; }

bool IntOctagon::cornerIsBounded(int32_t no) { return true; }

IntBox* IntOctagon::boundingBox() { return new (jrt::kGeom) IntBox(leftX, bottomY, rightX, topY); }

int32_t IntOctagon::dimension() {
  if (this == EMPTY() || this->isEmpty()) {
    return -1;
  }
  int32_t result;

  if (rightX > leftX && topY > bottomY && lowerRightDiagonalX > upperLeftDiagonalX &&
      upperRightDiagonalX > lowerLeftDiagonalX) {
    result = 2;
  } else if (rightX == leftX && topY == bottomY) {
    result = 0;
  } else {
    result = 1;
  }
  return result;
}

IntPoint* IntOctagon::corner(int32_t no) {
  switch (no) {
    case 0:
      return new (jrt::kGeom) IntPoint(lowerLeftDiagonalX - bottomY, bottomY);  // lower-left (bottom horizontal)
    case 1:
      return new (jrt::kGeom) IntPoint(lowerRightDiagonalX + bottomY,
                          bottomY);  // lower-right (bottom horizontal)
    case 2:
      return new (jrt::kGeom) IntPoint(rightX, rightX - lowerRightDiagonalX);  // bottom-right vertical
    case 3:
      return new (jrt::kGeom) IntPoint(rightX, upperRightDiagonalX - rightX);  // top-right vertical
    case 4:
      return new (jrt::kGeom) IntPoint(upperRightDiagonalX - topY, topY);  // upper-right (top horizontal)
    case 5:
      return new (jrt::kGeom) IntPoint(upperLeftDiagonalX + topY, topY);  // upper-left (top horizontal)
    case 6:
      return new (jrt::kGeom) IntPoint(leftX, leftX - upperLeftDiagonalX);  // top-left vertical
    case 7:
      return new (jrt::kGeom) IntPoint(leftX, lowerLeftDiagonalX - leftX);  // bottom-left vertical
    default:
      throw jrt::IllegalArgumentException("IntOctagon.corner: no out of range");
  }
}

int32_t IntOctagon::getId() {
  int32_t result = leftX;
  result = 31 * result + rightX;
  result = 31 * result + bottomY;
  result = 31 * result + topY;
  result = 31 * result + lowerLeftDiagonalX;
  result = 31 * result + upperRightDiagonalX;
  result = 31 * result + upperLeftDiagonalX;
  return 31 * result + lowerRightDiagonalX;
}

FloatPoint* IntOctagon::cornerApprox(int32_t no) {
  return new (jrt::kGeom) FloatPoint(cornerX(no), cornerY(no));
}

int32_t IntOctagon::cornerY(int32_t no) {
  switch (no) {
    case 0:
    case 1:
      return bottomY;
    case 2:
      return rightX - lowerRightDiagonalX;
    case 3:
      return upperRightDiagonalX - rightX;
    case 4:
    case 5:
      return topY;
    case 6:
      return leftX - upperLeftDiagonalX;
    case 7:
      return lowerLeftDiagonalX - leftX;
    default:
      throw jrt::IllegalArgumentException("IntOctagon.corner: no out of range");
  }
}

int32_t IntOctagon::cornerX(int32_t no) {
  switch (no) {
    case 0:
      return lowerLeftDiagonalX - bottomY;
    case 1:
      return lowerRightDiagonalX + bottomY;
    case 2:
    case 3:
      return rightX;
    case 4:
      return upperRightDiagonalX - topY;
    case 5:
      return upperLeftDiagonalX + topY;
    case 6:
    case 7:
      return leftX;
    default:
      throw jrt::IllegalArgumentException("IntOctagon.corner: no out of range");
  }
}

double IntOctagon::area() {
  return areaOf(leftX, bottomY, rightX, topY, upperLeftDiagonalX, lowerRightDiagonalX,
                lowerLeftDiagonalX, upperRightDiagonalX);
}

double IntOctagon::unionArea(IntOctagon* other) {
  // Same fields as union_(other) (and other->union_(this): min/max are symmetric).
  return areaOf(jrt::Math::min(leftX, other->leftX), jrt::Math::min(bottomY, other->bottomY),
                jrt::Math::max(rightX, other->rightX), jrt::Math::max(topY, other->topY),
                jrt::Math::min(upperLeftDiagonalX, other->upperLeftDiagonalX),
                jrt::Math::max(lowerRightDiagonalX, other->lowerRightDiagonalX),
                jrt::Math::min(lowerLeftDiagonalX, other->lowerLeftDiagonalX),
                jrt::Math::max(upperRightDiagonalX, other->upperRightDiagonalX));
}

double IntOctagon::areaOf(int32_t leftX, int32_t bottomY, int32_t rightX, int32_t topY,
                          int32_t upperLeftDiagonalX, int32_t lowerRightDiagonalX,
                          int32_t lowerLeftDiagonalX, int32_t upperRightDiagonalX) {
  // calculate half of the absolute value of
  // x0 (y1 - y7) + x1 (y2 - y0) + x2 (y3 - y1) + ...+ x7( y0 - y6)
  // where xi, yi are the coordinates of the i-th corner of this Octagon.

  // Overwrites the same implementation in TileShape for performance
  // reasons to avoid Point allocation.

  double result = static_cast<double>(lowerLeftDiagonalX - bottomY) *
                  static_cast<double>(bottomY - lowerLeftDiagonalX + leftX);
  result += static_cast<double>(lowerRightDiagonalX + bottomY) *
            static_cast<double>(rightX - lowerRightDiagonalX - bottomY);
  result += static_cast<double>(rightX) *
            static_cast<double>(upperRightDiagonalX - 2 * rightX - bottomY + topY +
                                lowerRightDiagonalX);
  result += static_cast<double>(upperRightDiagonalX - topY) *
            static_cast<double>(topY - upperRightDiagonalX + rightX);
  result += static_cast<double>(upperLeftDiagonalX + topY) *
            static_cast<double>(leftX - upperLeftDiagonalX - topY);
  result += static_cast<double>(leftX) *
            static_cast<double>(lowerLeftDiagonalX - 2 * leftX - topY + bottomY +
                                upperLeftDiagonalX);

  return 0.5 * jrt::Math::abs(result);
}

int32_t IntOctagon::borderLineCount() { return 8; }

namespace {
bool setBorderLine(int32_t* c, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
  c[0] = ax;
  c[1] = ay;
  c[2] = bx;
  c[3] = by;
  return true;
}
}  // namespace

bool IntOctagon::borderLineCoordinates(int32_t no, int32_t* c) {
  switch (no) {
    case 0:
      return setBorderLine(c, 0, bottomY, 1, bottomY);  // lower boundary line
    case 1:
      return setBorderLine(c, lowerRightDiagonalX, 0, lowerRightDiagonalX + 1,
                      1);  // lower right boundary line
    case 2:
      return setBorderLine(c, rightX, 0, rightX, 1);  // right boundary line
    case 3:
      return setBorderLine(c, upperRightDiagonalX, 0, upperRightDiagonalX - 1,
                      1);  // upper right boundary line
    case 4:
      return setBorderLine(c, 0, topY, -1, topY);  // upper boundary line
    case 5:
      return setBorderLine(c, upperLeftDiagonalX, 0, upperLeftDiagonalX - 1,
                      -1);  // upper left boundary line
    case 6:
      return setBorderLine(c, leftX, 0, leftX, -1);  // left boundary line
    case 7:
      return setBorderLine(c, lowerLeftDiagonalX, 0, lowerLeftDiagonalX + 1,
                      -1);  // lower left boundary line
    default:
      throw jrt::IllegalArgumentException("IntOctagon.borderLine: no out of range");
  }
}

Line* IntOctagon::borderLine(int32_t no) {
  int32_t c[4];
  borderLineCoordinates(no, c);
  return Line::ofInts(c[0], c[1], c[2], c[3]);
}

IntOctagon* IntOctagon::translateBy(Vector* relCoor) {
  // This function is at the moment only implemented for Vectors
  // with integer coordinates.
  // The general implementation is still missing.

  if (relCoor->equals(Vector::ZERO())) {
    return this;
  }
  IntVector* relativeCoordinate = relCoor->toIntegerExact();
  return new (jrt::kGeom) IntOctagon(leftX + relativeCoordinate->x, bottomY + relativeCoordinate->y,
                        rightX + relativeCoordinate->x, topY + relativeCoordinate->y,
                        upperLeftDiagonalX + relativeCoordinate->x - relativeCoordinate->y,
                        lowerRightDiagonalX + relativeCoordinate->x - relativeCoordinate->y,
                        lowerLeftDiagonalX + relativeCoordinate->x + relativeCoordinate->y,
                        upperRightDiagonalX + relativeCoordinate->x + relativeCoordinate->y);
}

double IntOctagon::maxWidth() {
  double width1 = jrt::Math::max(rightX - leftX, topY - bottomY);
  double width2 = jrt::Math::max(upperRightDiagonalX - lowerLeftDiagonalX,
                                 lowerRightDiagonalX - upperLeftDiagonalX);
  return jrt::Math::max(width1, width2 / Limits::sqrt2);
}

double IntOctagon::minWidth() {
  double width1 = jrt::Math::min(rightX - leftX, topY - bottomY);
  double width2 = jrt::Math::min(upperRightDiagonalX - lowerLeftDiagonalX,
                                 lowerRightDiagonalX - upperLeftDiagonalX);
  return jrt::Math::min(width1, width2 / Limits::sqrt2);
}

IntOctagon* IntOctagon::offset(double distance) {
  int32_t width = jrt::l2i(jrt::Math::round(distance));
  if (width == 0) {
    return this;
  }
  int32_t diaWidth = jrt::l2i(jrt::Math::round(Limits::sqrt2 * distance));
  IntOctagon* result =
      new (jrt::kGeom) IntOctagon(leftX - width, bottomY - width, rightX + width, topY + width,
                     upperLeftDiagonalX - diaWidth, lowerRightDiagonalX + diaWidth,
                     lowerLeftDiagonalX - diaWidth, upperRightDiagonalX + diaWidth);
  return result->normalize();
}

IntOctagon* IntOctagon::enlarge(double offset) { return this->offset(offset); }

bool IntOctagon::contains(RegularTileShape* other) { return other->isContainedIn(this); }

bool IntOctagon::contains(FloatPoint* point) {
  if (leftX > point->x || bottomY > point->y || rightX < point->x || topY < point->y) {
    return false;
  }
  double tmp1 = point->x - point->y;
  double tmp2 = point->x + point->y;
  return upperLeftDiagonalX <= tmp1 && lowerRightDiagonalX >= tmp1 &&
         lowerLeftDiagonalX <= tmp2 && upperRightDiagonalX >= tmp2;
}

RegularTileShape* IntOctagon::union_(RegularTileShape* other) { return other->union_(this); }

IntOctagon* IntOctagon::union_(IntOctagon* other) {
  return new (jrt::kGeom) IntOctagon(jrt::Math::min(leftX, other->leftX),
                        jrt::Math::min(bottomY, other->bottomY),
                        jrt::Math::max(rightX, other->rightX), jrt::Math::max(topY, other->topY),
                        jrt::Math::min(upperLeftDiagonalX, other->upperLeftDiagonalX),
                        jrt::Math::max(lowerRightDiagonalX, other->lowerRightDiagonalX),
                        jrt::Math::min(lowerLeftDiagonalX, other->lowerLeftDiagonalX),
                        jrt::Math::max(upperRightDiagonalX, other->upperRightDiagonalX));
}

IntOctagon* IntOctagon::union_(IntBox* other) { return union_(other->toIntOctagon()); }

TileShape* IntOctagon::intersection(TileShape* other) { return other->intersection(this); }

Simplex* IntOctagon::intersection(Simplex* other) { return other->intersection(this); }

IntOctagon* IntOctagon::intersection(IntOctagon* other) {
  return normalized(
      jrt::Math::max(leftX, other->leftX), jrt::Math::max(bottomY, other->bottomY),
      jrt::Math::min(rightX, other->rightX), jrt::Math::min(topY, other->topY),
      jrt::Math::max(upperLeftDiagonalX, other->upperLeftDiagonalX),
      jrt::Math::min(lowerRightDiagonalX, other->lowerRightDiagonalX),
      jrt::Math::max(lowerLeftDiagonalX, other->lowerLeftDiagonalX),
      jrt::Math::min(upperRightDiagonalX, other->upperRightDiagonalX));
}

IntOctagon* IntOctagon::intersection(IntBox* other) {
  return intersection(other->toIntOctagon());
}

// PORT NOTE: the coordinate computation of normalize(), shared with normalized(). Takes the
// coordinates in place; returns false if the octagon is empty.
bool IntOctagon::normalizeCoordinates(int32_t& newLx, int32_t& newLy, int32_t& newRx,
                                      int32_t& newUy, int32_t& newUlx, int32_t& newLrx,
                                      int32_t& newLlx, int32_t& newUrx) {
  if (newLx > newRx || newLy > newUy || newLlx > newUrx || newUlx > newLrx) {
    return false;
  }

  if (newLx < newLlx - newUy) {
    // the point newLx, newUy is the lower left border line of
    // this octagon
    // change newLx , that the lower left border line runs through
    // this point
    newLx = newLlx - newUy;
  }

  if (newLx < newUlx + newLy) {
    // the point newLx, newLy is above the upper left border line of
    // this octagon
    // change newLx , that the upper left border line runs through
    // this point
    newLx = newUlx + newLy;
  }

  if (newRx > newUrx - newLy) {
    // the point newRx, newLy is above the upper right border line of
    // this octagon
    // change newRx , that the upper right border line runs through
    // this point
    newRx = newUrx - newLy;
  }

  if (newRx > newLrx + newUy) {
    // the point newRx, newUy is below the lower right border line of
    // this octagon
    // change rx , that the lower right border line runs through
    // this point
    newRx = newLrx + newUy;
  }

  if (newLy < newLx - newLrx) {
    // the point lx, ly is below the lower right border line of this
    // octagon
    // change ly, so that the lower right border line runs through
    // this point
    newLy = newLx - newLrx;
  }

  if (newLy < newLlx - newRx) {
    // the point rx, ly is below the lower left border line of
    // this octagon.
    // change ly, so that the lower left border line runs through
    // this point
    newLy = newLlx - newRx;
  }

  if (newUy > newUrx - newLx) {
    // the point lx, uy is above the upper right border line of
    // this octagon.
    // Change the uy, so that the upper right border line runs through
    // this point.
    newUy = newUrx - newLx;
  }

  if (newUy > newRx - newUlx) {
    // the point rx, uy is above the upper left border line of
    // this octagon.
    // Change the uy, so that the upper left border line runs through
    // this point.
    newUy = newRx - newUlx;
  }

  if (newLlx - newLx < newLy) {
    // The point lx, ly is above the lower left border line of
    // this octagon.
    // Change the lower left line, so that it runs through this point.
    newLlx = newLx + newLy;
  }

  if (newRx - newLrx < newLy) {
    // the point rx, ly is above the lower right border line of
    // this octagon.
    // Change the lower right line, so that it runs through this point.
    newLrx = newRx - newLy;
  }

  if (newUrx - newRx > newUy) {
    // the point rx, uy is below the upper right border line of oct.
    // Change the upper right line, so that it runs through this point.
    newUrx = newUy + newRx;
  }

  if (newLx - newUlx > newUy) {
    // the point lx, uy is below the upper left border line of
    // this octagon.
    // Change the upper left line, so that it runs through this point.
    newUlx = newLx - newUy;
  }

  int32_t diagUpperY = jrt::d2i(jrt::Math::ceil((newUrx - newUlx) / 2.0));

  if (newUy > diagUpperY) {
    // the intersection of the upper right and the upper left border
    // line is below newUy.  Adjust newUy to diagUpperY.
    newUy = diagUpperY;
  }

  int32_t diagLowerY = jrt::d2i(jrt::Math::floor((newLlx - newLrx) / 2.0));

  if (newLy < diagLowerY) {
    // the intersection of the lower right and the lower left border
    // line is above newLy.  Adjust newLy to diagLowerY.
    newLy = diagLowerY;
  }

  int32_t diagRightX = jrt::d2i(jrt::Math::ceil((newUrx + newLrx) / 2.0));

  if (newRx > diagRightX) {
    // the intersection of the upper right and the lower right border
    // line is to the left of  right x.  Adjust newRx to diagRightX.
    newRx = diagRightX;
  }

  int32_t diagLeftX = jrt::d2i(jrt::Math::floor((newLlx + newUlx) / 2.0));

  if (newLx < diagLeftX) {
    // the intersection of the lower left and the upper left border
    // line is to the right of left x.  Adjust newLx to diagLeftX.
    newLx = diagLeftX;
  }
  if (newLx > newRx || newLy > newUy || newLlx > newUrx || newUlx > newLrx) {
    return false;
  }
  return true;
}

IntOctagon* IntOctagon::normalize() {
  int32_t newLx = leftX;
  int32_t newRx = rightX;
  int32_t newLy = bottomY;
  int32_t newUy = topY;
  int32_t newLlx = lowerLeftDiagonalX;
  int32_t newUlx = upperLeftDiagonalX;
  int32_t newLrx = lowerRightDiagonalX;
  int32_t newUrx = upperRightDiagonalX;
  if (!normalizeCoordinates(newLx, newLy, newRx, newUy, newUlx, newLrx, newLlx, newUrx)) {
    return EMPTY();
  }
  if (leftX == newLx && rightX == newRx && bottomY == newLy && topY == newUy &&
      lowerLeftDiagonalX == newLlx && upperLeftDiagonalX == newUlx &&
      lowerRightDiagonalX == newLrx && upperRightDiagonalX == newUrx) {
    return this;
  }
  return new (jrt::kGeom) IntOctagon(newLx, newLy, newRx, newUy, newUlx, newLrx, newLlx, newUrx);
}

IntOctagon* IntOctagon::normalized(int32_t lx, int32_t ly, int32_t rx, int32_t uy, int32_t ulx,
                                   int32_t lrx, int32_t llx, int32_t urx) {
  // Same result as (new IntOctagon(...)).normalize(): EMPTY() or a new octagon.
  if (!normalizeCoordinates(lx, ly, rx, uy, ulx, lrx, llx, urx)) {
    return EMPTY();
  }
  return new (jrt::kGeom) IntOctagon(lx, ly, rx, uy, ulx, lrx, llx, urx);
}

bool IntOctagon::isNormalized() {
  // PORT NOTE: compares with the normalized coordinates without creating the octagon.
  int32_t lx = leftX, ly = bottomY, rx = rightX, uy = topY;
  int32_t ulx = upperLeftDiagonalX, lrx = lowerRightDiagonalX, llx = lowerLeftDiagonalX,
          urx = upperRightDiagonalX;
  if (!normalizeCoordinates(lx, ly, rx, uy, ulx, lrx, llx, urx)) {
    IntOctagon* on = EMPTY();
    lx = on->leftX, ly = on->bottomY, rx = on->rightX, uy = on->topY;
    ulx = on->upperLeftDiagonalX, lrx = on->lowerRightDiagonalX, llx = on->lowerLeftDiagonalX;
    urx = on->upperRightDiagonalX;
  }
  return leftX == lx && bottomY == ly && rightX == rx && topY == uy &&
         lowerLeftDiagonalX == llx && lowerRightDiagonalX == lrx && upperLeftDiagonalX == ulx &&
         upperRightDiagonalX == urx;
}

Simplex* IntOctagon::toSimplex() {
  if (isEmpty()) {
    return Simplex::EMPTY();
  }
  if (precalculatedToSimplex == nullptr) {
    auto* lines = new (jrt::kGeom) jrt::Array<Line*>(8);
    for (int32_t i = 0; i < 8; i++) {
      (*lines)[i] = borderLine(i);
    }
    Simplex* currentSimplex = new (jrt::kGeom) Simplex(lines);
    precalculatedToSimplex = currentSimplex->removeRedundantLines();
  }
  return precalculatedToSimplex;
}

RegularTileShape* IntOctagon::boundingShape(ShapeBoundingDirections* dirs) {
  return dirs->bounds(this);
}

Side* IntOctagon::sideOfBorderLine(int32_t x, int32_t y, int32_t borderLineNo) {
  int32_t tmp;
  switch (borderLineNo) {
    case 0:
      tmp = this->bottomY - y;  // lower boundary line
      break;
    case 1:
      tmp = x - y - this->lowerRightDiagonalX;  // lower-right diagonal line
      break;
    case 2:
      tmp = x - this->rightX;  // right boundary line
      break;
    case 3:
      tmp = x + y - this->upperRightDiagonalX;  // upper-right diagonal line
      break;
    case 4:
      tmp = y - this->topY;  // upper boundary line
      break;
    case 5:
      tmp = this->upperLeftDiagonalX + y - x;  // upper-left diagonal line
      break;
    case 6:
      tmp = this->leftX - x;  // left boundary line
      break;
    case 7:
      tmp = this->lowerLeftDiagonalX - x - y;  // lower-left diagonal line
      break;
    default:
      FRLogger::warn("IntOctagon.sideOfBorderLine: borderLineNo out of range");
      tmp = 0;
      break;
  }
  Side* result;
  if (tmp < 0) {
    result = Side::ON_THE_LEFT();
  } else if (tmp > 0) {
    result = Side::ON_THE_RIGHT();
  } else {
    result = Side::COLLINEAR();
  }
  return result;
}

bool IntOctagon::isContainedIn(IntBox* box) {
  return leftX >= box->ll->x && bottomY >= box->ll->y && rightX <= box->ur->x &&
         topY <= box->ur->y;
}

bool IntOctagon::isContainedIn(IntOctagon* other) {
  return leftX >= other->leftX && bottomY >= other->bottomY && rightX <= other->rightX &&
         topY <= other->topY && lowerLeftDiagonalX >= other->lowerLeftDiagonalX &&
         upperLeftDiagonalX >= other->upperLeftDiagonalX &&
         lowerRightDiagonalX <= other->lowerRightDiagonalX &&
         upperRightDiagonalX <= other->upperRightDiagonalX;
}

bool IntOctagon::intersects(Shape* other) { return other->intersects(this); }

bool IntOctagon::intersects(IntBox* other) { return intersects(other->toIntOctagon()); }

bool IntOctagon::intersects(IntOctagon* other) {
  int32_t isLx;
  int32_t isRx;
  isLx = jrt::Math::max(other->leftX, this->leftX);
  isRx = jrt::Math::min(other->rightX, this->rightX);
  if (isLx > isRx) {
    return false;
  }

  int32_t isLy;
  int32_t isUy;
  isLy = jrt::Math::max(other->bottomY, this->bottomY);
  isUy = jrt::Math::min(other->topY, this->topY);
  if (isLy > isUy) {
    return false;
  }

  int32_t isLlx;
  int32_t isUrx;
  isLlx = jrt::Math::max(other->lowerLeftDiagonalX, this->lowerLeftDiagonalX);
  isUrx = jrt::Math::min(other->upperRightDiagonalX, this->upperRightDiagonalX);
  if (isLlx > isUrx) {
    return false;
  }

  int32_t isUlx;
  int32_t isLrx;
  isUlx = jrt::Math::max(other->upperLeftDiagonalX, this->upperLeftDiagonalX);
  isLrx = jrt::Math::min(other->lowerRightDiagonalX, this->lowerRightDiagonalX);
  return isUlx <= isLrx;
}

bool IntOctagon::intersects(Simplex* other) { return other->intersects(this); }

bool IntOctagon::intersects(Circle* other) { return other->intersects(this); }

bool IntOctagon::overlaps(IntOctagon* other) {
  int32_t isLx;
  int32_t isRx;
  isLx = jrt::Math::max(other->leftX, this->leftX);
  isRx = jrt::Math::min(other->rightX, this->rightX);
  if (isLx >= isRx) {
    return false;
  }

  int32_t isLy;
  int32_t isUy;
  isLy = jrt::Math::max(other->bottomY, this->bottomY);
  isUy = jrt::Math::min(other->topY, this->topY);
  if (isLy >= isUy) {
    return false;
  }

  int32_t isLlx;
  int32_t isUrx;
  isLlx = jrt::Math::max(other->lowerLeftDiagonalX, this->lowerLeftDiagonalX);
  isUrx = jrt::Math::min(other->upperRightDiagonalX, this->upperRightDiagonalX);
  if (isLlx >= isUrx) {
    return false;
  }

  int32_t isUlx;
  int32_t isLrx;
  isUlx = jrt::Math::max(other->upperLeftDiagonalX, this->upperLeftDiagonalX);
  isLrx = jrt::Math::min(other->lowerRightDiagonalX, this->lowerRightDiagonalX);
  return isUlx < isLrx;
}

int32_t IntOctagon::leftXValue(int32_t y) {
  int32_t result = jrt::Math::max(leftX, upperLeftDiagonalX + y);
  return jrt::Math::max(result, lowerLeftDiagonalX - y);
}

int32_t IntOctagon::rightXValue(int32_t y) {
  int32_t result = jrt::Math::min(rightX, upperRightDiagonalX - y);
  return jrt::Math::min(result, lowerRightDiagonalX + y);
}

int32_t IntOctagon::lowerYValue(int32_t x) {
  int32_t result = jrt::Math::max(bottomY, lowerLeftDiagonalX - x);
  return jrt::Math::max(result, x - lowerRightDiagonalX);
}

int32_t IntOctagon::upperYValue(int32_t x) {
  int32_t result = jrt::Math::min(topY, x - upperLeftDiagonalX);
  return jrt::Math::min(result, upperRightDiagonalX - x);
}

Side* IntOctagon::compare(RegularTileShape* other, int32_t edgeIndex) {
  Side* result = other->compare(this, edgeIndex);
  return result->negate();
}

Side* IntOctagon::compare(IntOctagon* other, int32_t edgeIndex) {
  Side* result;
  switch (edgeIndex) {
    case 0:
      // compare the lower edge line
      if (bottomY > other->bottomY) {
        result = Side::ON_THE_LEFT();
      } else if (bottomY < other->bottomY) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 1:
      // compare the lower right edge line
      if (lowerRightDiagonalX < other->lowerRightDiagonalX) {
        result = Side::ON_THE_LEFT();
      } else if (lowerRightDiagonalX > other->lowerRightDiagonalX) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 2:
      // compare the right edge line
      if (rightX < other->rightX) {
        result = Side::ON_THE_LEFT();
      } else if (rightX > other->rightX) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 3:
      // compare the upper right edge line
      if (upperRightDiagonalX < other->upperRightDiagonalX) {
        result = Side::ON_THE_LEFT();
      } else if (upperRightDiagonalX > other->upperRightDiagonalX) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 4:
      // compare the upper edge line
      if (topY < other->topY) {
        result = Side::ON_THE_LEFT();
      } else if (topY > other->topY) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 5:
      // compare the upper left edge line
      if (upperLeftDiagonalX > other->upperLeftDiagonalX) {
        result = Side::ON_THE_LEFT();
      } else if (upperLeftDiagonalX < other->upperLeftDiagonalX) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 6:
      // compare the left edge line
      if (leftX > other->leftX) {
        result = Side::ON_THE_LEFT();
      } else if (leftX < other->leftX) {
        result = Side::ON_THE_RIGHT();
      } else {
        result = Side::COLLINEAR();
      }
      break;
    case 7:
      // compare the lower left edge line
      if (lowerLeftDiagonalX > other->lowerLeftDiagonalX) {
        result = Side::ON_THE_LEFT();
      } else if (lowerLeftDiagonalX < other->lowerLeftDiagonalX) {
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

Side* IntOctagon::compare(IntBox* other, int32_t edgeIndex) {
  return compare(other->toIntOctagon(), edgeIndex);
}

int32_t IntOctagon::borderLineIndex(Line* line) {
  FRLogger::warn("edge_index_of_line not yet implemented for octagons");
  return -1;
}

IntPoint* IntOctagon::borderPoint(IntPoint* point, FortyfiveDegreeDirection dir) {
  int32_t resultX;
  int32_t resultY;
  switch (dir) {
    case FortyfiveDegreeDirection::RIGHT:
      resultX = jrt::Math::min(rightX, upperRightDiagonalX - point->y);
      resultX = jrt::Math::min(resultX, lowerRightDiagonalX + point->y);
      resultY = point->y;
      break;
    case FortyfiveDegreeDirection::LEFT:
      resultX = jrt::Math::max(leftX, upperLeftDiagonalX + point->y);
      resultX = jrt::Math::max(resultX, lowerLeftDiagonalX - point->y);
      resultY = point->y;
      break;
    case FortyfiveDegreeDirection::UP:
      resultX = point->x;
      resultY = jrt::Math::min(topY, point->x - upperLeftDiagonalX);
      resultY = jrt::Math::min(resultY, upperRightDiagonalX - point->x);
      break;
    case FortyfiveDegreeDirection::DOWN:
      resultX = point->x;
      resultY = jrt::Math::max(bottomY, lowerLeftDiagonalX - point->x);
      resultY = jrt::Math::max(resultY, point->x - lowerRightDiagonalX);
      break;
    case FortyfiveDegreeDirection::RIGHT45:
      resultX = jrt::d2i(jrt::Math::ceil(0.5 * (point->x - point->y + upperRightDiagonalX)));
      resultX = jrt::Math::min(resultX, rightX);
      resultX = jrt::Math::min(resultX, point->x - point->y + topY);
      resultY = point->y - point->x + resultX;
      break;
    case FortyfiveDegreeDirection::UP45:
      resultX = jrt::d2i(jrt::Math::floor(0.5 * (point->x + point->y + upperLeftDiagonalX)));
      resultX = jrt::Math::max(resultX, leftX);
      resultX = jrt::Math::max(resultX, point->x + point->y - topY);
      resultY = point->y + point->x - resultX;
      break;
    case FortyfiveDegreeDirection::LEFT45:
      resultX = jrt::d2i(jrt::Math::floor(0.5 * (point->x - point->y + lowerLeftDiagonalX)));
      resultX = jrt::Math::max(resultX, leftX);
      resultX = jrt::Math::max(resultX, point->x - point->y + bottomY);
      resultY = point->y - point->x + resultX;
      break;
    case FortyfiveDegreeDirection::DOWN45:
      resultX = jrt::d2i(jrt::Math::ceil(0.5 * (point->x + point->y + lowerRightDiagonalX)));
      resultX = jrt::Math::min(resultX, rightX);
      resultX = jrt::Math::min(resultX, point->x + point->y - bottomY);
      resultY = point->y + point->x - resultX;
      break;
    default:
      FRLogger::warn("IntOctagon.border_point: unexpected 45 degree direction");
      resultX = 0;
      resultY = 0;
      break;
  }
  return new (jrt::kGeom) IntPoint(resultX, resultY);
}

jrt::Array<IntPoint*>* IntOctagon::nearestBorderProjections(IntPoint* point,
                                                            int32_t maxResultPoints) {
  if (!this->contains(point) || maxResultPoints <= 0) {
    return new (jrt::kGeom) jrt::Array<IntPoint*>(0);
  }
  maxResultPoints = jrt::Math::min(maxResultPoints, 8);
  auto* result = new (jrt::kGeom) jrt::Array<IntPoint*>(maxResultPoints);
  auto* minDist = new (jrt::kOp) jrt::Array<double>(maxResultPoints);
  for (int32_t i = 0; i < maxResultPoints; i++) {
    (*minDist)[i] = jrt::Double::MAX_VALUE;
  }
  FloatPoint* insidePoint = point->toFloat();
  for (FortyfiveDegreeDirection currentDirection : *FortyfiveDegreeDirection_values()) {
    IntPoint* currentBorderPoint = borderPoint(point, currentDirection);
    double currentDistance = insidePoint->distanceSquare(currentBorderPoint->toFloat());
    for (int32_t i = 0; i < maxResultPoints; i++) {
      if (currentDistance < (*minDist)[i]) {
        for (int32_t k = maxResultPoints - 1; k > i; k--) {
          (*minDist)[k] = (*minDist)[k - 1];
          (*result)[k] = (*result)[k - 1];
        }
        (*minDist)[i] = currentDistance;
        (*result)[i] = currentBorderPoint;
        break;
      }
    }
  }
  return result;
}

Side* IntOctagon::borderLineSideOf(FloatPoint* point, int32_t lineIndex, double tolerance) {
  switch (lineIndex) {
    case 0:
      if (point->y > this->bottomY + tolerance) {
        return Side::ON_THE_RIGHT();
      } else if (point->y < this->bottomY - tolerance) {
        return Side::ON_THE_LEFT();
      } else {
        return Side::COLLINEAR();
      }
    case 2:
      if (point->x < this->rightX - tolerance) {
        return Side::ON_THE_RIGHT();
      } else if (point->x > this->rightX + tolerance) {
        return Side::ON_THE_LEFT();
      } else {
        return Side::COLLINEAR();
      }
    case 4:
      if (point->y < this->topY - tolerance) {
        return Side::ON_THE_RIGHT();
      } else if (point->y > this->topY + tolerance) {
        return Side::ON_THE_LEFT();
      } else {
        return Side::COLLINEAR();
      }
    case 6:
      if (point->x > this->leftX + tolerance) {
        return Side::ON_THE_RIGHT();
      } else if (point->x < this->leftX - tolerance) {
        return Side::ON_THE_LEFT();
      } else {
        return Side::COLLINEAR();
      }
    case 1: {
      double tmp = point->y - point->x + lowerRightDiagonalX;
      if (tmp > tolerance) {
        // the point is above the lower right border line of this octagon
        return Side::ON_THE_RIGHT();
      } else if (tmp < -tolerance) {
        // the point is below the lower right border line of this octagon
        return Side::ON_THE_LEFT();
      } else {
        return Side::COLLINEAR();
      }
    }
    case 3: {
      double tmp = point->x + point->y - upperRightDiagonalX;
      if (tmp < -tolerance) {
        // the point is below the upper right border line of this octagon
        return Side::ON_THE_RIGHT();
      } else if (tmp > tolerance) {
        // the point is above the upper right border line of this octagon
        return Side::ON_THE_LEFT();
      } else {
        return Side::COLLINEAR();
      }
    }
    case 5: {
      double tmp = point->y - point->x + upperLeftDiagonalX;
      if (tmp < -tolerance) {
        // the point is below the upper left border line of this octagon
        return Side::ON_THE_RIGHT();
      } else if (tmp > tolerance) {
        // the point is above the upper left border line of this octagon
        return Side::ON_THE_LEFT();
      } else {
        return Side::COLLINEAR();
      }
    }
    case 7: {
      double tmp = point->x + point->y - lowerLeftDiagonalX;
      if (tmp > tolerance) {
        // the point is above the lower left border line of this octagon
        return Side::ON_THE_RIGHT();
      } else if (tmp < -tolerance) {
        // the point is below the lower left border line of this octagon
        return Side::ON_THE_LEFT();
      } else {
        return Side::COLLINEAR();
      }
    }
    default:
      FRLogger::warn("IntOctagon.borderLineSideOf: lineIndex out of range");
      return Side::COLLINEAR();
  }
}

bool IntOctagon::isIntBox() {
  if (lowerLeftDiagonalX != leftX + bottomY) {
    return false;
  }
  if (lowerRightDiagonalX != rightX - bottomY) {
    return false;
  }
  if (upperRightDiagonalX != rightX + topY) {
    return false;
  }
  return upperLeftDiagonalX == leftX - topY;
}

TileShape* IntOctagon::simplify() {
  if (this->isIntBox()) {
    return this->boundingBox();
  }
  return this;
}

jrt::Array<TileShape*>* IntOctagon::cutout(TileShape* shape) { return shape->cutoutFrom(this); }

jrt::Array<TileShape*>* IntOctagon::cutoutFrom(IntBox* d) {
  IntOctagon* c = this->intersection(d);

  if (this->isEmpty() || c->dimension() < this->dimension()) {
    // there is only an overlap at the border
    auto* result = new (jrt::kGeom) jrt::Array<IntOctagon*>(1);
    (*result)[0] = d->toIntOctagon();
    return toTileShapeArray(result);
  }

  auto* boxes = new (jrt::kGeom) jrt::Array<IntBox*>(4);

  // construct left box

  (*boxes)[0] = new (jrt::kGeom) IntBox(d->ll->x, c->lowerLeftDiagonalX - c->leftX, c->leftX,
                           c->leftX - c->upperLeftDiagonalX);

  // construct right box

  (*boxes)[1] = new (jrt::kGeom) IntBox(c->rightX, c->rightX - c->lowerRightDiagonalX, d->ur->x,
                           c->upperRightDiagonalX - c->rightX);

  // construct lower box

  (*boxes)[2] = new (jrt::kGeom) IntBox(c->lowerLeftDiagonalX - c->bottomY, d->ll->y,
                           c->lowerRightDiagonalX + c->bottomY, c->bottomY);

  // construct upper box

  (*boxes)[3] = new (jrt::kGeom) IntBox(c->upperLeftDiagonalX + c->topY, c->topY,
                           c->upperRightDiagonalX - c->topY, d->ur->y);

  auto* octagons = new (jrt::kGeom) jrt::Array<IntOctagon*>(4);

  // construct upper left octagon

  IntOctagon* currentOct =
      new (jrt::kGeom) IntOctagon(d->ll->x, (*boxes)[0]->ur->y, (*boxes)[3]->ll->x, d->ur->y,
                     -Limits::CRIT_INT, c->upperLeftDiagonalX, -Limits::CRIT_INT, Limits::CRIT_INT);
  (*octagons)[0] = currentOct->normalize();

  // construct lower left octagon

  currentOct = new (jrt::kGeom) IntOctagon(d->ll->x, d->ll->y, (*boxes)[2]->ll->x, (*boxes)[0]->ll->y,
                              -Limits::CRIT_INT, Limits::CRIT_INT, -Limits::CRIT_INT,
                              c->lowerLeftDiagonalX);
  (*octagons)[1] = currentOct->normalize();

  // construct lower right octagon

  currentOct = new (jrt::kGeom) IntOctagon((*boxes)[2]->ur->x, d->ll->y, d->ur->x, (*boxes)[1]->ll->y,
                              c->lowerRightDiagonalX, Limits::CRIT_INT, -Limits::CRIT_INT,
                              Limits::CRIT_INT);
  (*octagons)[2] = currentOct->normalize();

  // construct upper right octagon

  currentOct = new (jrt::kGeom) IntOctagon((*boxes)[3]->ur->x, (*boxes)[1]->ur->y, d->ur->x, d->ur->y,
                              -Limits::CRIT_INT, Limits::CRIT_INT, c->upperRightDiagonalX,
                              Limits::CRIT_INT);
  (*octagons)[3] = currentOct->normalize();

  // optimise the result to minimum cumulative circumference

  IntBox* b = (*boxes)[0];
  IntOctagon* o = (*octagons)[0];
  if (b->ur->x - b->ll->x > o->topY - o->bottomY) {
    // switch the horizontal upper left divide line to vertical

    (*boxes)[0] = new (jrt::kGeom) IntBox(b->ll->x, b->ll->y, b->ur->x, o->topY);
    currentOct = new (jrt::kGeom) IntOctagon(b->ur->x, o->bottomY, o->rightX, o->topY, o->upperLeftDiagonalX,
                                o->lowerRightDiagonalX, o->lowerLeftDiagonalX,
                                o->upperRightDiagonalX);
    (*octagons)[0] = currentOct->normalize();
  }

  b = (*boxes)[3];
  o = (*octagons)[0];
  if (b->ur->y - b->ll->y > o->rightX - o->leftX) {
    // switch the vertical upper left divide line to horizontal

    (*boxes)[3] = new (jrt::kGeom) IntBox(o->leftX, b->ll->y, b->ur->x, b->ur->y);
    currentOct = new (jrt::kGeom) IntOctagon(o->leftX, o->bottomY, o->rightX, b->ll->y, o->upperLeftDiagonalX,
                                o->lowerRightDiagonalX, o->lowerLeftDiagonalX,
                                o->upperRightDiagonalX);
    (*octagons)[0] = currentOct->normalize();
  }
  b = (*boxes)[3];
  o = (*octagons)[3];
  if (b->ur->y - b->ll->y > o->rightX - o->leftX) {
    // switch the vertical upper right divide line to horizontal

    (*boxes)[3] = new (jrt::kGeom) IntBox(b->ll->x, b->ll->y, o->rightX, b->ur->y);
    currentOct = new (jrt::kGeom) IntOctagon(o->leftX, o->bottomY, o->rightX, o->topY, o->upperLeftDiagonalX,
                                o->lowerRightDiagonalX, o->lowerLeftDiagonalX,
                                o->upperRightDiagonalX);
    (*octagons)[3] = currentOct->normalize();
  }
  b = (*boxes)[1];
  o = (*octagons)[3];
  if (b->ur->x - b->ll->x > o->topY - o->bottomY) {
    // switch the horizontal upper right divide line to vertical

    (*boxes)[1] = new (jrt::kGeom) IntBox(b->ll->x, b->ll->y, b->ur->x, o->topY);
    currentOct = new (jrt::kGeom) IntOctagon(o->leftX, o->bottomY, b->ll->x, o->topY, o->upperLeftDiagonalX,
                                o->lowerRightDiagonalX, o->lowerLeftDiagonalX,
                                o->upperRightDiagonalX);
    (*octagons)[3] = currentOct->normalize();
  }
  b = (*boxes)[1];
  o = (*octagons)[2];
  if (b->ur->x - b->ll->x > o->topY - o->bottomY) {
    // switch the horizontal lower right divide line to vertical

    (*boxes)[1] = new (jrt::kGeom) IntBox(b->ll->x, o->bottomY, b->ur->x, b->ur->y);
    currentOct = new (jrt::kGeom) IntOctagon(o->leftX, o->bottomY, b->ll->x, o->topY, o->upperLeftDiagonalX,
                                o->lowerRightDiagonalX, o->lowerLeftDiagonalX,
                                o->upperRightDiagonalX);
    (*octagons)[2] = currentOct->normalize();
  }
  b = (*boxes)[2];
  o = (*octagons)[2];
  if (b->ur->y - b->ll->y > o->rightX - o->leftX) {
    // switch the vertical lower right divide line to horizontal

    (*boxes)[2] = new (jrt::kGeom) IntBox(b->ll->x, b->ll->y, o->rightX, b->ur->y);
    currentOct = new (jrt::kGeom) IntOctagon(o->leftX, b->ur->y, o->rightX, o->topY, o->upperLeftDiagonalX,
                                o->lowerRightDiagonalX, o->lowerLeftDiagonalX,
                                o->upperRightDiagonalX);
    (*octagons)[2] = currentOct->normalize();
  }
  b = (*boxes)[2];
  o = (*octagons)[1];
  if (b->ur->y - b->ll->y > o->rightX - o->leftX) {
    // switch the vertical lower  left divide line to horizontal

    (*boxes)[2] = new (jrt::kGeom) IntBox(o->leftX, b->ll->y, b->ur->x, b->ur->y);
    currentOct = new (jrt::kGeom) IntOctagon(o->leftX, b->ur->y, o->rightX, o->topY, o->upperLeftDiagonalX,
                                o->lowerRightDiagonalX, o->lowerLeftDiagonalX,
                                o->upperRightDiagonalX);
    (*octagons)[1] = currentOct->normalize();
  }
  b = (*boxes)[0];
  o = (*octagons)[1];
  if (b->ur->x - b->ll->x > o->topY - o->bottomY) {
    // switch the horizontal lower left divide line to vertical
    (*boxes)[0] = new (jrt::kGeom) IntBox(b->ll->x, o->bottomY, b->ur->x, b->ur->y);
    currentOct = new (jrt::kGeom) IntOctagon(b->ur->x, o->bottomY, o->rightX, o->topY, o->upperLeftDiagonalX,
                                o->lowerRightDiagonalX, o->lowerLeftDiagonalX,
                                o->upperRightDiagonalX);
    (*octagons)[1] = currentOct->normalize();
  }

  auto* result = new (jrt::kGeom) jrt::Array<IntOctagon*>(8);

  // add the 4 boxes to the result
  for (int32_t i = 0; i < 4; i++) {
    (*result)[i] = (*boxes)[i]->toIntOctagon();
  }

  // add the 4 octagons to the result
  jrt::arraycopy(octagons, 0, result, 4, 4);
  return toTileShapeArray(result);
}

jrt::Array<TileShape*>* IntOctagon::cutoutFrom(IntOctagon* d) {
  IntOctagon* c = this->intersection(d);

  if (this->isEmpty() || c->dimension() < this->dimension()) {
    // there is only an overlap at the border
    auto* result = new (jrt::kGeom) jrt::Array<IntOctagon*>(1);
    (*result)[0] = d;
    return toTileShapeArray(result);
  }

  auto* result = new (jrt::kGeom) jrt::Array<IntOctagon*>(8);

  int32_t tmp = c->lowerLeftDiagonalX - c->leftX;

  (*result)[0] = new (jrt::kGeom) IntOctagon(d->leftX, tmp, c->leftX, c->leftX - c->upperLeftDiagonalX,
                                d->upperLeftDiagonalX, d->lowerRightDiagonalX,
                                d->lowerLeftDiagonalX, d->upperRightDiagonalX);

  int32_t tmp2 = c->lowerLeftDiagonalX - c->bottomY;

  (*result)[1] = new (jrt::kGeom) IntOctagon(d->leftX, d->bottomY, tmp2, tmp, d->upperLeftDiagonalX,
                                d->lowerRightDiagonalX, d->lowerLeftDiagonalX,
                                c->lowerLeftDiagonalX);

  tmp = c->lowerRightDiagonalX + c->bottomY;

  (*result)[2] = new (jrt::kGeom) IntOctagon(tmp2, d->bottomY, tmp, c->bottomY, d->upperLeftDiagonalX,
                                d->lowerRightDiagonalX, d->lowerLeftDiagonalX,
                                d->upperRightDiagonalX);

  tmp2 = c->rightX - c->lowerRightDiagonalX;

  (*result)[3] = new (jrt::kGeom) IntOctagon(tmp, d->bottomY, d->rightX, tmp2, c->lowerRightDiagonalX,
                                d->lowerRightDiagonalX, d->lowerLeftDiagonalX,
                                d->upperRightDiagonalX);

  tmp = c->upperRightDiagonalX - c->rightX;

  (*result)[4] = new (jrt::kGeom) IntOctagon(c->rightX, tmp2, d->rightX, tmp, d->upperLeftDiagonalX,
                                d->lowerRightDiagonalX, d->lowerLeftDiagonalX,
                                d->upperRightDiagonalX);

  tmp2 = c->upperRightDiagonalX - c->topY;

  (*result)[5] = new (jrt::kGeom) IntOctagon(tmp2, tmp, d->rightX, d->topY, d->upperLeftDiagonalX,
                                d->lowerRightDiagonalX, c->upperRightDiagonalX,
                                d->upperRightDiagonalX);

  tmp = c->upperLeftDiagonalX + c->topY;

  (*result)[6] = new (jrt::kGeom) IntOctagon(tmp, c->topY, tmp2, d->topY, d->upperLeftDiagonalX,
                                d->lowerRightDiagonalX, d->lowerLeftDiagonalX,
                                d->upperRightDiagonalX);

  tmp2 = c->leftX - c->upperLeftDiagonalX;

  (*result)[7] = new (jrt::kGeom) IntOctagon(d->leftX, tmp2, tmp, d->topY, d->upperLeftDiagonalX,
                                c->upperLeftDiagonalX, d->lowerLeftDiagonalX,
                                d->upperRightDiagonalX);

  for (int32_t i = 0; i < 8; i++) {
    (*result)[i] = (*result)[i]->normalize();
  }

  IntOctagon* curr1 = (*result)[0];
  IntOctagon* curr2 = (*result)[7];

  if (!(curr1->isEmpty() || curr2->isEmpty()) &&
      curr1->rightX - curr1->leftXValue(curr1->topY) >
          curr2->upperYValue(curr1->rightX) - curr2->bottomY) {
    // switch the horizontal upper left divide line to vertical
    curr1 = new (jrt::kGeom) IntOctagon(jrt::Math::min(curr1->leftX, curr2->leftX), curr1->bottomY,
                           curr1->rightX, curr2->topY, curr2->upperLeftDiagonalX,
                           curr1->lowerRightDiagonalX, curr1->lowerLeftDiagonalX,
                           curr2->upperRightDiagonalX);

    curr2 = new (jrt::kGeom) IntOctagon(curr1->rightX, curr2->bottomY, curr2->rightX, curr2->topY,
                           curr2->upperLeftDiagonalX, curr2->lowerRightDiagonalX,
                           curr2->lowerLeftDiagonalX, curr2->upperRightDiagonalX);

    (*result)[0] = curr1->normalize();
    (*result)[7] = curr2->normalize();
  }
  curr1 = (*result)[7];
  curr2 = (*result)[6];
  if (!(curr1->isEmpty() || curr2->isEmpty()) &&
      curr2->upperYValue(curr1->rightX) - curr2->bottomY >
          curr1->rightX - curr1->leftXValue(curr2->bottomY)) {
    // switch the vertical upper left divide line to horizontal
    curr2 = new (jrt::kGeom) IntOctagon(curr1->leftX, curr2->bottomY, curr2->rightX,
                           jrt::Math::max(curr2->topY, curr1->topY), curr1->upperLeftDiagonalX,
                           curr2->lowerRightDiagonalX, curr1->lowerLeftDiagonalX,
                           curr2->upperRightDiagonalX);

    curr1 = new (jrt::kGeom) IntOctagon(curr1->leftX, curr1->bottomY, curr1->rightX, curr2->bottomY,
                           curr1->upperLeftDiagonalX, curr1->lowerRightDiagonalX,
                           curr1->lowerLeftDiagonalX, curr1->upperRightDiagonalX);

    (*result)[7] = curr1->normalize();
    (*result)[6] = curr2->normalize();
  }
  curr1 = (*result)[6];
  curr2 = (*result)[5];
  if (!(curr1->isEmpty() || curr2->isEmpty()) &&
      curr2->upperYValue(curr1->rightX) - curr1->bottomY >
          curr2->rightXValue(curr1->bottomY) - curr2->leftX) {
    // switch the vertical upper right divide line to horizontal
    curr1 = new (jrt::kGeom) IntOctagon(curr1->leftX, curr1->bottomY, curr2->rightX,
                           jrt::Math::max(curr2->topY, curr1->topY), curr1->upperLeftDiagonalX,
                           curr2->lowerRightDiagonalX, curr1->lowerLeftDiagonalX,
                           curr2->upperRightDiagonalX);

    curr2 = new (jrt::kGeom) IntOctagon(curr2->leftX, curr2->bottomY, curr2->rightX, curr1->bottomY,
                           curr2->upperLeftDiagonalX, curr2->lowerRightDiagonalX,
                           curr2->lowerLeftDiagonalX, curr2->upperRightDiagonalX);

    (*result)[6] = curr1->normalize();
    (*result)[5] = curr2->normalize();
  }
  curr1 = (*result)[5];
  curr2 = (*result)[4];
  if (!(curr1->isEmpty() || curr2->isEmpty()) &&
      curr2->rightXValue(curr2->topY) - curr2->leftX >
          curr1->upperYValue(curr2->leftX) - curr2->topY) {
    // switch the horizontal upper right divide line to vertical
    curr2 = new (jrt::kGeom) IntOctagon(curr2->leftX, curr2->bottomY,
                           jrt::Math::max(curr2->rightX, curr1->rightX), curr1->topY,
                           curr1->upperLeftDiagonalX, curr2->lowerRightDiagonalX,
                           curr2->lowerLeftDiagonalX, curr1->upperRightDiagonalX);

    curr1 = new (jrt::kGeom) IntOctagon(curr1->leftX, curr1->bottomY, curr2->leftX, curr1->topY,
                           curr1->upperLeftDiagonalX, curr1->lowerRightDiagonalX,
                           curr1->lowerLeftDiagonalX, curr1->upperRightDiagonalX);

    (*result)[5] = curr1->normalize();
    (*result)[4] = curr2->normalize();
  }
  curr1 = (*result)[4];
  curr2 = (*result)[3];
  if (!(curr1->isEmpty() || curr2->isEmpty()) &&
      curr1->rightXValue(curr1->bottomY) - curr1->leftX >
          curr1->bottomY - curr2->lowerYValue(curr1->leftX)) {
    // switch the horizontal lower right divide line to vertical
    curr1 = new (jrt::kGeom) IntOctagon(curr1->leftX, curr2->bottomY,
                           jrt::Math::max(curr2->rightX, curr1->rightX), curr1->topY,
                           curr1->upperLeftDiagonalX, curr2->lowerRightDiagonalX,
                           curr2->lowerLeftDiagonalX, curr1->upperRightDiagonalX);

    curr2 = new (jrt::kGeom) IntOctagon(curr2->leftX, curr2->bottomY, curr1->leftX, curr2->topY,
                           curr2->upperLeftDiagonalX, curr2->lowerRightDiagonalX,
                           curr2->lowerLeftDiagonalX, curr2->upperRightDiagonalX);

    (*result)[4] = curr1->normalize();
    (*result)[3] = curr2->normalize();
  }

  curr1 = (*result)[3];
  curr2 = (*result)[2];

  if (!(curr1->isEmpty() || curr2->isEmpty()) &&
      curr2->topY - curr2->lowerYValue(curr2->rightX) >
          curr1->rightXValue(curr2->topY) - curr2->rightX) {
    // switch the vertical lower right divide line to horizontal
    curr2 = new (jrt::kGeom) IntOctagon(curr2->leftX, jrt::Math::min(curr1->bottomY, curr2->bottomY),
                           curr1->rightX, curr2->topY, curr2->upperLeftDiagonalX,
                           curr1->lowerRightDiagonalX, curr2->lowerLeftDiagonalX,
                           curr1->upperRightDiagonalX);

    curr1 = new (jrt::kGeom) IntOctagon(curr1->leftX, curr2->topY, curr1->rightX, curr1->topY,
                           curr1->upperLeftDiagonalX, curr1->lowerRightDiagonalX,
                           curr1->lowerLeftDiagonalX, curr1->upperRightDiagonalX);

    (*result)[3] = curr1->normalize();
    (*result)[2] = curr2->normalize();
  }

  curr1 = (*result)[2];
  curr2 = (*result)[1];

  if (!(curr1->isEmpty() || curr2->isEmpty()) &&
      curr1->topY - curr1->lowerYValue(curr1->leftX) >
          curr1->leftX - curr2->leftXValue(curr1->topY)) {
    // switch the vertical lower left divide line to horizontal
    curr1 = new (jrt::kGeom) IntOctagon(curr2->leftX, jrt::Math::min(curr1->bottomY, curr2->bottomY),
                           curr1->rightX, curr1->topY, curr2->upperLeftDiagonalX,
                           curr1->lowerRightDiagonalX, curr2->lowerLeftDiagonalX,
                           curr1->upperRightDiagonalX);

    curr2 = new (jrt::kGeom) IntOctagon(curr2->leftX, curr1->topY, curr2->rightX, curr2->topY,
                           curr2->upperLeftDiagonalX, curr2->lowerRightDiagonalX,
                           curr2->lowerLeftDiagonalX, curr2->upperRightDiagonalX);

    (*result)[2] = curr1->normalize();
    (*result)[1] = curr2->normalize();
  }

  curr1 = (*result)[1];
  curr2 = (*result)[0];

  if (!(curr1->isEmpty() || curr2->isEmpty()) &&
      curr2->rightX - curr2->leftXValue(curr2->bottomY) >
          curr2->bottomY - curr1->lowerYValue(curr2->rightX)) {
    // switch the horizontal lower left divide line to vertical
    curr2 = new (jrt::kGeom) IntOctagon(jrt::Math::min(curr2->leftX, curr1->leftX), curr1->bottomY,
                           curr2->rightX, curr2->topY, curr2->upperLeftDiagonalX,
                           curr1->lowerRightDiagonalX, curr1->lowerLeftDiagonalX,
                           curr2->upperRightDiagonalX);

    curr1 = new (jrt::kGeom) IntOctagon(curr2->rightX, curr1->bottomY, curr1->rightX, curr1->topY,
                           curr1->upperLeftDiagonalX, curr1->lowerRightDiagonalX,
                           curr1->lowerLeftDiagonalX, curr1->upperRightDiagonalX);

    (*result)[1] = curr1->normalize();
    (*result)[0] = curr2->normalize();
  }

  return toTileShapeArray(result);
}

jrt::Array<TileShape*>* IntOctagon::cutoutFrom(Simplex* simplex) {
  return this->toSimplex()->cutoutFrom(simplex);
}

std::string IntOctagon::toString() {
  return "IntOctagon(leftX=" + jrt::str(leftX) + ", bottomY=" + jrt::str(bottomY) +
         ", rightX=" + jrt::str(rightX) + ", topY=" + jrt::str(topY) +
         ", upperLeftDiagonalX=" + jrt::str(upperLeftDiagonalX) +
         ", lowerRightDiagonalX=" + jrt::str(lowerRightDiagonalX) +
         ", lowerLeftDiagonalX=" + jrt::str(lowerLeftDiagonalX) +
         ", upperRightDiagonalX=" + jrt::str(upperRightDiagonalX) + ")";
}

}  // namespace fr
