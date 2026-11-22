#include "geometry/planar/LineSegment.h"

#include "datastructures/Signum.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/RationalPoint.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

LineSegment::LineSegment(Line* startLine, Line* middleLine, Line* endLine) {
  start = startLine;
  middle = middleLine;
  end = endLine;
}

LineSegment::LineSegment(Polyline* polyline, int32_t no) {
  if (no <= 0 || no >= polyline->lines->length - 1) {
    FRLogger::warn("LineSegment from Polyline: no out of range");
    start = nullptr;
    middle = nullptr;
    end = nullptr;
    return;
  }
  start = (*polyline->lines)[no - 1];
  middle = (*polyline->lines)[no];
  end = (*polyline->lines)[no + 1];
}

LineSegment::LineSegment(PolylineShape* shape, int32_t no) {
  int32_t lineCount = shape->borderLineCount();
  if (no < 0 || no >= lineCount) {
    FRLogger::warn("LineSegment from TileShape: no out of range");
    start = nullptr;
    middle = nullptr;
    end = nullptr;
    return;
  }
  if (no == 0) {
    start = shape->borderLine(lineCount - 1);
  } else {
    start = shape->borderLine(no - 1);
  }
  middle = shape->borderLine(no);
  if (no == lineCount - 1) {
    end = shape->borderLine(0);
  } else {
    end = shape->borderLine(no + 1);
  }
}

Point* LineSegment::startPoint() {
  if (precalculatedStartPoint == nullptr) {
    precalculatedStartPoint = middle->intersection(start);
  }
  return precalculatedStartPoint;
}

Point* LineSegment::endPoint() {
  if (precalculatedEndPoint == nullptr) {
    precalculatedEndPoint = middle->intersection(end);
  }
  return precalculatedEndPoint;
}

FloatPoint* LineSegment::startPointApprox() {
  FloatPoint* result;
  if (precalculatedStartPoint != nullptr) {
    result = precalculatedStartPoint->toFloat();
  } else {
    result = this->start->intersectionApprox(this->middle);
  }
  return result;
}

FloatPoint* LineSegment::endPointApprox() {
  FloatPoint* result;
  if (precalculatedEndPoint != nullptr) {
    result = precalculatedEndPoint->toFloat();
  } else {
    result = this->end->intersectionApprox(this->middle);
  }
  return result;
}

LineSegment* LineSegment::opposite() {
  return new (jrt::kGeom) LineSegment(end->opposite(), middle->opposite(), start->opposite());
}

Polyline* LineSegment::toPolyline() {
  auto* lines = new (jrt::kGeom) jrt::Array<Line*>(3);
  (*lines)[0] = start;
  (*lines)[1] = middle;
  (*lines)[2] = end;
  return new (jrt::kGeom) Polyline(lines);
}

Simplex* LineSegment::toSimplex() {
  auto* lines = new (jrt::kGeom) jrt::Array<Line*>(4);
  if (this->endPoint()->sideOf(this->start) == Side::ON_THE_RIGHT()) {
    (*lines)[0] = this->start->opposite();
  } else {
    (*lines)[0] = this->start;
  }
  (*lines)[1] = this->middle;
  (*lines)[2] = this->middle->opposite();
  if (this->startPoint()->sideOf(this->end) == Side::ON_THE_RIGHT()) {
    (*lines)[3] = this->end->opposite();
  } else {
    (*lines)[3] = this->end;
  }
  return Simplex::getInstance(lines);
}

Line* LineSegment::splitLine(Point* point) {
  if (!contains(point)) {
    return nullptr;
  }
  if (jrt::fast_cast<IntPoint>(point) != nullptr) {
    return new (jrt::kGeom) Line(point, middle->direction()->turn45Degree(2));
  }
  if (start->sideOf(point) == Side::COLLINEAR()) {
    return start;
  }
  if (end->sideOf(point) == Side::COLLINEAR()) {
    return end;
  }
  RationalPoint* rational = jrt::fast_cast<RationalPoint>(point);
  jrt::BigInteger baseX = rational->x.divide(rational->z);
  jrt::BigInteger baseY = rational->y.divide(rational->z);
  if (!withinIntegerBounds(baseX) || !withinIntegerBounds(baseY)) {
    return nullptr;
  }
  static const int32_t offsets[5][2] = {{0, 0}, {1, 0}, {0, 1}, {-1, 0}, {0, -1}};
  for (const auto& offset : offsets) {
    jrt::BigInteger ax = baseX.add(jrt::BigInteger::valueOf(offset[0]));
    jrt::BigInteger ay = baseY.add(jrt::BigInteger::valueOf(offset[1]));
    if (!withinIntegerBounds(ax) || !withinIntegerBounds(ay)) {
      continue;
    }
    jrt::BigInteger dx = rational->x.subtract(ax.multiply(rational->z));
    jrt::BigInteger dy = rational->y.subtract(ay.multiply(rational->z));
    jrt::BigInteger divisor = dx.gcd(dy);
    if (divisor.signum() == 0) {
      continue;
    }
    dx = dx.divide(divisor);
    dy = dy.divide(divisor);
    for (int32_t sign : {1, -1}) {
      jrt::BigInteger bx = ax.add(dx.multiply(jrt::BigInteger::valueOf(sign)));
      jrt::BigInteger by = ay.add(dy.multiply(jrt::BigInteger::valueOf(sign)));
      if (!withinIntegerBounds(bx) || !withinIntegerBounds(by)) {
        continue;
      }
      Line* cap = new (jrt::kGeom) Line(ax.intValueExact(), ay.intValueExact(), bx.intValueExact(),
                           by.intValueExact());
      if (!middle->isParallel(cap) && middle->intersection(cap)->equals(point)) {
        return cap;
      }
    }
  }
  return nullptr;
}

bool LineSegment::withinIntegerBounds(const jrt::BigInteger& coordinate) {
  return coordinate.abs().compareTo(Limits::CRIT_INT_BIG()) <= 0;
}

bool LineSegment::contains(Point* point) {
  if (point == nullptr || point->isInfinite()) {
    return false;
  }
  if (middle->sideOf(point) != Side::COLLINEAR()) {
    return false;
  }
  Point* first = startPoint();
  Point* last = endPoint();
  return point->compareX(first) * point->compareX(last) <= 0 &&
         point->compareY(first) * point->compareY(last) <= 0;
}

IntBox* LineSegment::boundingBox() {
  FloatPoint* startCorner = middle->intersectionApprox(start);
  FloatPoint* endCorner = middle->intersectionApprox(end);
  double llx = jrt::Math::min(startCorner->x, endCorner->x);
  double lly = jrt::Math::min(startCorner->y, endCorner->y);
  double urx = jrt::Math::max(startCorner->x, endCorner->x);
  double ury = jrt::Math::max(startCorner->y, endCorner->y);
  IntPoint* lowerLeft =
      new (jrt::kGeom) IntPoint(jrt::d2i(jrt::Math::floor(llx)), jrt::d2i(jrt::Math::floor(lly)));
  IntPoint* upperRight =
      new (jrt::kGeom) IntPoint(jrt::d2i(jrt::Math::ceil(urx)), jrt::d2i(jrt::Math::ceil(ury)));
  return new (jrt::kGeom) IntBox(lowerLeft, upperRight);
}

IntOctagon* LineSegment::boundingOctagon() {
  FloatPoint* startCorner = middle->intersectionApprox(start);
  FloatPoint* endCorner = middle->intersectionApprox(end);
  double lx = jrt::Math::floor(jrt::Math::min(startCorner->x, endCorner->x));
  double ly = jrt::Math::floor(jrt::Math::min(startCorner->y, endCorner->y));
  double rx = jrt::Math::ceil(jrt::Math::max(startCorner->x, endCorner->x));
  double uy = jrt::Math::ceil(jrt::Math::max(startCorner->y, endCorner->y));
  double startXminusY = startCorner->x - startCorner->y;
  double endXminusY = endCorner->x - endCorner->y;
  double ulx = jrt::Math::floor(jrt::Math::min(startXminusY, endXminusY));
  double lrx = jrt::Math::ceil(jrt::Math::max(startXminusY, endXminusY));
  double startXplusY = startCorner->x + startCorner->y;
  double endXplusY = endCorner->x + endCorner->y;
  double llx = jrt::Math::floor(jrt::Math::min(startXplusY, endXplusY));
  double urx = jrt::Math::ceil(jrt::Math::max(startXplusY, endXplusY));
  IntOctagon* result =
      new (jrt::kGeom) IntOctagon(jrt::d2i(lx), jrt::d2i(ly), jrt::d2i(rx), jrt::d2i(uy), jrt::d2i(ulx),
                     jrt::d2i(lrx), jrt::d2i(llx), jrt::d2i(urx));
  return result->normalize();
}

LineSegment* LineSegment::changeLengthApprox(double newLength) {
  FloatPoint* newEndPoint = startPointApprox()->changeLength(endPointApprox(), newLength);
  Direction* perpendicularDirection = this->middle->direction()->turn45Degree(2);
  Line* newEndLine = new (jrt::kGeom) Line(newEndPoint->round(), perpendicularDirection);
  return new (jrt::kGeom) LineSegment(this->start, this->middle, newEndLine);
}

jrt::Array<Line*>* LineSegment::intersection(LineSegment* other) {
  if (!this->boundingBox()->intersects(other->boundingBox())) {
    return new (jrt::kGeom) jrt::Array<Line*>(0);
  }
  Side* startPointSide = startPoint()->sideOf(other->middle);
  Side* endPointSide = endPoint()->sideOf(other->middle);
  if (startPointSide == Side::COLLINEAR() && endPointSide == Side::COLLINEAR()) {
    // there may be an overlap
    LineSegment* thisSorted = this->sortEndpointsInXY();
    LineSegment* otherSorted = other->sortEndpointsInXY();
    LineSegment* leftLine;
    LineSegment* rightLine;
    if (thisSorted->startPoint()->compareXY(otherSorted->startPoint()) <= 0) {
      leftLine = thisSorted;
      rightLine = otherSorted;
    } else {
      leftLine = otherSorted;
      rightLine = thisSorted;
    }
    int32_t cmp = leftLine->endPoint()->compareXY(rightLine->startPoint());
    if (cmp < 0) {
      // end point of the left line is to the left of the start point of the right line
      return new (jrt::kGeom) jrt::Array<Line*>(0);
    }
    if (cmp == 0) {
      // end point of the left line is equal to the start point of the right line
      auto* result = new (jrt::kGeom) jrt::Array<Line*>(1);
      (*result)[0] = leftLine->end;
      return result;
    }
    // now there is a real overlap
    auto* result = new (jrt::kGeom) jrt::Array<Line*>(2);
    (*result)[0] = rightLine->start;
    if (rightLine->endPoint()->compareXY(leftLine->endPoint()) >= 0) {
      (*result)[1] = leftLine->end;
    } else {
      (*result)[1] = rightLine->end;
    }
    return result;
  }
  if (startPointSide == endPointSide ||
      other->startPoint()->sideOf(this->middle) == other->endPoint()->sideOf(this->middle)) {
    return new (jrt::kGeom) jrt::Array<Line*>(0);  // no intersection possible
  }
  // now both start points and both end points are on different sides of the middle
  // line of the other segment.
  auto* result = new (jrt::kGeom) jrt::Array<Line*>(1);
  (*result)[0] = other->middle;
  return result;
}

bool LineSegment::intersects(LineSegment* other) {
  jrt::Array<Line*>* intersections = this->intersection(other);
  return intersections->length > 0;
}

bool LineSegment::overlaps(LineSegment* other) {
  jrt::Array<Line*>* intersections = this->intersection(other);
  return intersections->length > 1;
}

jrt::Array<IntPoint*>* LineSegment::stairApproximation(double width, bool toTheRight) {
  IntPoint* startPoint = this->startPoint()->toFloat()->round();
  IntPoint* endPoint = this->endPoint()->toFloat()->round();
  if (startPoint->equals(endPoint)) {
    return new (jrt::kGeom) jrt::Array<IntPoint*>(0);
  }

  if (startPoint->x == endPoint->x || startPoint->y == endPoint->y) {
    auto* result = new (jrt::kGeom) jrt::Array<IntPoint*>(2);
    (*result)[0] = startPoint;
    (*result)[1] = endPoint;
    return result;
  }

  int32_t dx = endPoint->x - startPoint->x;
  int32_t dy = endPoint->y - startPoint->y;
  int32_t absDx = jrt::Math::abs(dx);
  int32_t absDy = jrt::Math::abs(dy);
  bool functionOfX = absDx >= absDy;
  // use otherwise function of y for better numerical  stability

  int32_t stairWidth;
  int32_t stairCount;

  if (functionOfX) {
    stairWidth = jrt::Math::max(
        1, jrt::l2i(jrt::Math::round((width * static_cast<double>(absDx)) /
                                     static_cast<double>(absDy))));
    stairCount = (absDx - 1) / stairWidth + 1;
    if (endPoint->x < startPoint->x) {
      stairWidth = -stairWidth;
    }
  } else {
    stairWidth = jrt::Math::max(
        1, jrt::l2i(jrt::Math::round((width * static_cast<double>(absDy)) /
                                     static_cast<double>(absDx))));
    stairCount = (absDy - 1) / stairWidth + 1;
    if (endPoint->y < startPoint->y) {
      stairWidth = -stairWidth;
    }
  }
  auto* result = new (jrt::kGeom) jrt::Array<IntPoint*>(2 * stairCount + 1);

  (*result)[0] = startPoint;
  double det = static_cast<double>(dx) * static_cast<double>(dy);
  bool changeXfirst = (toTheRight && det > 0) || (!toTheRight && det < 0);
  int32_t currentIndex = 0;

  int32_t prevLinePointX = startPoint->x;
  int32_t prevLinePointY = startPoint->y;
  for (int32_t i = 1; i < stairCount; i++) {
    int32_t currentLinePointX;
    int32_t currentLinePointY;
    if (functionOfX) {
      currentLinePointX = startPoint->x + i * stairWidth;
      currentLinePointY =
          jrt::l2i(jrt::Math::round(this->getLine()->functionValueApprox(currentLinePointX)));
    } else {
      currentLinePointY = startPoint->y + i * stairWidth;
      currentLinePointX =
          jrt::l2i(jrt::Math::round(this->getLine()->functionInYValueApprox(currentLinePointY)));
    }
    ++currentIndex;
    if (changeXfirst) {
      (*result)[currentIndex] = new (jrt::kGeom) IntPoint(currentLinePointX, prevLinePointY);
    } else {
      (*result)[currentIndex] = new (jrt::kGeom) IntPoint(prevLinePointX, currentLinePointY);
    }
    ++currentIndex;
    (*result)[currentIndex] = new (jrt::kGeom) IntPoint(currentLinePointX, currentLinePointY);
    prevLinePointX = currentLinePointX;
    prevLinePointY = currentLinePointY;
  }
  ++currentIndex;
  if (changeXfirst) {
    (*result)[currentIndex] = new (jrt::kGeom) IntPoint(endPoint->x, prevLinePointY);
  } else {
    (*result)[currentIndex] = new (jrt::kGeom) IntPoint(prevLinePointX, endPoint->y);
  }
  ++currentIndex;
  (*result)[currentIndex] = endPoint;
  return result;
}

jrt::Array<IntPoint*>* LineSegment::stairApproximation45(double width, bool toTheRight) {
  IntPoint* startPoint = this->startPoint()->toFloat()->round();
  IntPoint* endPoint = this->endPoint()->toFloat()->round();
  if (startPoint->equals(endPoint)) {
    return new (jrt::kGeom) jrt::Array<IntPoint*>(0);
  }
  IntVector* delta = endPoint->differenceBy(startPoint);
  if (delta->isMultipleOf45Degree()) {
    auto* result = new (jrt::kGeom) jrt::Array<IntPoint*>(2);
    (*result)[0] = startPoint;
    (*result)[1] = endPoint;
    return result;
  }
  IntVector* absDelta = new (jrt::kGeom) IntVector(jrt::Math::abs(delta->x), jrt::Math::abs(delta->y));
  bool functionOfX = absDelta->x >= absDelta->y;
  // use otherwise function of y for better numerical  stability
  double det = static_cast<double>(delta->x) * static_cast<double>(delta->y);
  int32_t stairWidth;
  int32_t stairCount;
  if (functionOfX) {
    stairWidth = jrt::Math::max(
        1, jrt::l2i(jrt::Math::round((width * static_cast<double>(absDelta->x)) /
                                     static_cast<double>(absDelta->y))));
    stairCount = (absDelta->x - 1) / stairWidth + 1;
    if (endPoint->x < startPoint->x) {
      stairWidth = -stairWidth;
    }
  } else {
    stairWidth = jrt::Math::max(
        1, jrt::l2i(jrt::Math::round((width * static_cast<double>(absDelta->y)) /
                                     static_cast<double>(absDelta->x))));
    stairCount = (absDelta->x - 1) / stairWidth + 1;
    if (endPoint->y < startPoint->y) {
      stairWidth = -stairWidth;
    }
  }
  auto* result = new (jrt::kGeom) jrt::Array<IntPoint*>(2 * stairCount + 1);
  (*result)[0] = startPoint;
  IntPoint* prevLinePoint = startPoint;
  int32_t currentIndex = 0;
  for (int32_t i = 1; i <= stairCount; i++) {
    IntPoint* currentLinePoint;
    int32_t currentX;
    int32_t currentY;
    if (i == stairCount) {
      currentLinePoint = endPoint;
    } else {
      if (functionOfX) {
        currentX = startPoint->x + i * stairWidth;
        currentY = jrt::l2i(jrt::Math::round(this->getLine()->functionValueApprox(currentX)));
      } else {
        currentY = startPoint->y + i * stairWidth;
        currentX = jrt::l2i(jrt::Math::round(this->getLine()->functionInYValueApprox(currentY)));
      }
      currentLinePoint = new (jrt::kGeom) IntPoint(currentX, currentY);
    }
    if (functionOfX) {
      bool diagonalFirst = (toTheRight && det < 0) || (!toTheRight && det > 0);

      if (diagonalFirst) {
        currentX = prevLinePoint->x +
                   Signum::asInt(stairWidth) *
                       jrt::Math::abs(currentLinePoint->y - prevLinePoint->y);
        currentY = currentLinePoint->y;
      } else {
        // horizontal first
        currentX = currentLinePoint->x -
                   Signum::asInt(stairWidth) *
                       jrt::Math::abs(currentLinePoint->y - prevLinePoint->y);
        currentY = prevLinePoint->y;
      }
    } else {
      // function of y
      bool diagonalFirst = (toTheRight && det > 0) || (!toTheRight && det < 0);

      if (diagonalFirst) {
        currentX = currentLinePoint->x;
        currentY = prevLinePoint->y +
                   Signum::asInt(stairWidth) *
                       jrt::Math::abs(currentLinePoint->x - prevLinePoint->x);
      } else {
        currentX = prevLinePoint->x;
        currentY = currentLinePoint->y -
                   Signum::asInt(stairWidth) *
                       jrt::Math::abs(currentLinePoint->x - prevLinePoint->x);
      }
    }
    ++currentIndex;
    (*result)[currentIndex] = new (jrt::kGeom) IntPoint(currentX, currentY);
    ++currentIndex;
    (*result)[currentIndex] = currentLinePoint;
    prevLinePoint = currentLinePoint;
  }
  return result;
}

jrt::Array<int32_t>* LineSegment::borderIntersections(TileShape* shape) {
  auto* emptyResult = new (jrt::kOp) jrt::Array<int32_t>(0);
  if (!this->boundingBox()->intersects(shape->boundingBox())) {
    return emptyResult;
  }

  int32_t edgeCount = shape->borderLineCount();
  Line* prevLine = shape->borderLine(edgeCount - 1);
  Line* currentLine = shape->borderLine(0);
  auto* result = new (jrt::kOp) jrt::Array<int32_t>(2);
  auto* intersection = new (jrt::kGeom) jrt::Array<Point*>(2);
  int32_t intersectionCount = 0;
  Point* lineStart = this->startPoint();
  Point* lineEnd = this->endPoint();

  for (int32_t edgeLineNo = 0; edgeLineNo < edgeCount; edgeLineNo++) {
    Line* nextLine;
    if (edgeLineNo == edgeCount - 1) {
      nextLine = shape->borderLine(0);
    } else {
      nextLine = shape->borderLine(edgeLineNo + 1);
    }

    Side* startPointSide = currentLine->sideOf(lineStart);
    Side* endPointSide = currentLine->sideOf(lineEnd);
    if (startPointSide == Side::ON_THE_LEFT() && endPointSide == Side::ON_THE_LEFT()) {
      // both endpoints are outside the borderLine,
      // no intersection possible
      return emptyResult;
    }

    if (startPointSide == Side::COLLINEAR()) {
      // the start is on currentLine, check that the end point is inside
      // the halfplane, because touches count only, if the interior
      // is entered
      if (endPointSide != Side::ON_THE_RIGHT()) {
        return emptyResult;
      }
    }

    if (endPointSide == Side::COLLINEAR()) {
      // the end is on currentLine, check that the start point is inside
      // the halfplane, because touches count only, if the interior
      // is entered
      if (startPointSide != Side::ON_THE_RIGHT()) {
        return emptyResult;
      }
    }

    if (startPointSide != Side::ON_THE_RIGHT() || endPointSide != Side::ON_THE_RIGHT()) {
      // not both points are inside the halplane defined by currentLine
      Point* is = this->middle->intersection(currentLine);
      Side* prevLineSideOfIs = prevLine->sideOf(is);
      Side* nextLineSideOfIs = nextLine->sideOf(is);
      if (prevLineSideOfIs != Side::ON_THE_LEFT() && nextLineSideOfIs != Side::ON_THE_LEFT()) {
        // this line segment intersects currentLine between the
        // previous and the next corner of simplex

        if (prevLineSideOfIs == Side::COLLINEAR()) {
          // this line segment goes through the previous
          // corner of simplex. Check, that the intersection
          // isn't merely a touch.
          Point* prevPrevCorner;
          if (edgeLineNo == 0) {
            prevPrevCorner = shape->corner(edgeCount - 1);
          } else {
            prevPrevCorner = shape->corner(edgeLineNo - 1);
          }

          Point* nextCorner;
          if (edgeLineNo == edgeCount - 1) {
            nextCorner = shape->corner(0);
          } else {
            nextCorner = shape->corner(edgeLineNo + 1);
          }
          // check, that prevPrevCorner and nextCorner
          // are on different sides of this line segment.
          Side* prevPrevCornerSide = this->middle->sideOf(prevPrevCorner);
          Side* nextCornerSide = this->middle->sideOf(nextCorner);
          if (prevPrevCornerSide == Side::COLLINEAR() || nextCornerSide == Side::COLLINEAR() ||
              prevPrevCornerSide == nextCornerSide) {
            return emptyResult;
          }
        }
        if (nextLineSideOfIs == Side::COLLINEAR()) {
          // this line segment goes through the next
          // corner of simplex. Check, that the intersection
          // isn't merely a touch.
          Point* prevCorner = shape->corner(edgeLineNo);
          Point* nextNextCorner;

          if (edgeLineNo == edgeCount - 2) {
            nextNextCorner = shape->corner(0);
          } else if (edgeLineNo == edgeCount - 1) {
            nextNextCorner = shape->corner(1);
          } else {
            nextNextCorner = shape->corner(edgeLineNo + 2);
          }
          // check, that prevCorner and nextNextCorner
          // are on different sides of this line segment.
          Side* prevCornerSide = this->middle->sideOf(prevCorner);
          Side* nextNextCornerSide = this->middle->sideOf(nextNextCorner);
          if (prevCornerSide == Side::COLLINEAR() || nextNextCornerSide == Side::COLLINEAR() ||
              prevCornerSide == nextNextCornerSide) {
            return emptyResult;
          }
        }
        bool intersectionAlreadyHandled = false;
        for (int32_t i = 0; i < intersectionCount; i++) {
          if (is->equals((*intersection)[i])) {
            intersectionAlreadyHandled = true;
            break;
          }
        }
        if (!intersectionAlreadyHandled) {
          if (intersectionCount < result->length) {
            // a new intersection is found
            (*result)[intersectionCount] = edgeLineNo;
            (*intersection)[intersectionCount] = is;
            ++intersectionCount;
          } else {
            FRLogger::warn("border_intersections: intersection_count (" +
                           jrt::str(intersectionCount) + ") is too big!");
          }
        }
      }
    }

    prevLine = currentLine;
    currentLine = nextLine;
  }

  if (intersectionCount == 0) {
    return emptyResult;
  }

  if (intersectionCount == 2) {
    // assure the correct order
    FloatPoint* is0 = (*intersection)[0]->toFloat();
    FloatPoint* is1 = (*intersection)[1]->toFloat();
    FloatPoint* currentStart = lineStart->toFloat();
    if (currentStart->distanceSquare(is1) < currentStart->distanceSquare(is0)) {
      // swap the result points
      int32_t tmp = (*result)[0];
      (*result)[0] = (*result)[1];
      (*result)[1] = tmp;
    }

    return result;
  }

  if (intersectionCount != 1) {
    FRLogger::warn("LineSegment.border_intersections: intersectionCount 1 expected");
  }

  auto* normalisedResult = new (jrt::kOp) jrt::Array<int32_t>(1);
  (*normalisedResult)[0] = (*result)[0];
  return normalisedResult;
}

LineSegment* LineSegment::sortEndpointsInXY() {
  bool swapEndlines = startPoint()->compareXY(endPoint()) > 0;
  LineSegment* result;

  if (swapEndlines) {
    result = new (jrt::kGeom) LineSegment(this->end, this->middle, this->start);
    result->precalculatedStartPoint = this->precalculatedEndPoint;
    result->precalculatedEndPoint = this->precalculatedStartPoint;
  } else {
    result = this;
  }

  return result;
}

}  // namespace fr
