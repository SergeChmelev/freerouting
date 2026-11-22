#include "geometry/planar/FloatLine.h"

#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Side.h"
#include "logger/FRLogger.h"

namespace fr {

FloatLine::FloatLine(FloatPoint* a, FloatPoint* b) {
  if (a == nullptr || b == nullptr) {
    FRLogger::debug("FloatLine: one or both endpoints are null (degenerate line segment)");
  }
  this->a = a;
  this->b = b;
}

FloatLine* FloatLine::opposite() { return new FloatLine(this->b, this->a); }

FloatLine* FloatLine::adjustDirection(FloatLine* other) {
  if (this->b->sideOf(this->a, other->a) == other->b->sideOf(this->a, other->a)) {
    return this;
  }
  return this->opposite();
}

FloatPoint* FloatLine::intersection(FloatLine* other) {
  double d1x = this->b->x - this->a->x;
  double d1y = this->b->y - this->a->y;
  double d2x = other->b->x - other->a->x;
  double d2y = other->b->y - other->a->y;
  double det1 = this->a->x * this->b->y - this->a->y * this->b->x;
  double det2 = other->a->x * other->b->y - other->a->y * other->b->x;
  double det = d2x * d1y - d2y * d1x;
  double isX;
  double isY;
  if (det == 0) {
    return nullptr;
  }
  isX = (d2x * det1 - d1x * det2) / det;
  isY = (d2y * det1 - d1y * det2) / det;
  return new FloatPoint(isX, isY);
}

FloatLine* FloatLine::translate(double dist) {
  double dx = b->x - a->x;
  double dy = b->y - a->y;
  double dxdx = dx * dx;
  double dydy = dy * dy;
  double length = jrt::Math::sqrt(dxdx + dydy);
  FloatPoint* newA;
  if (dxdx <= dydy) {
    // translate along the x axis
    double relX = (dist * length) / dy;
    newA = new FloatPoint(this->a->x - relX, this->a->y);
  } else {
    // translate along the y axis
    double relY = (dist * length) / dx;
    newA = new FloatPoint(this->a->x, this->a->y + relY);
  }
  FloatPoint* newB = new FloatPoint(newA->x + dx, newA->y + dy);
  return new FloatLine(newA, newB);
}

double FloatLine::signedDistance(FloatPoint* point) {
  double dx = this->b->x - this->a->x;
  double dy = this->b->y - this->a->y;
  double det = dy * (point->x - this->a->x) - dx * (point->y - this->a->y);
  // area of the parallelogramm spanned by the 3 points
  double length = jrt::Math::sqrt(dx * dx + dy * dy);
  return det / length;
}

FloatPoint* FloatLine::perpendicularProjection(FloatPoint* point) {
  double dx = b->x - a->x;
  double dy = b->y - a->y;
  if (dx == 0 && dy == 0) {
    return this->a;
  }

  double dxdx = dx * dx;
  double dydy = dy * dy;
  double dxdy = dx * dy;
  double denominator = dxdx + dydy;
  double det = a->x * b->y - b->x * a->y;

  double x = (point->x * dxdx + point->y * dxdy + det * dy) / denominator;
  double y = (point->x * dxdy + point->y * dydy - det * dx) / denominator;

  return new FloatPoint(x, y);
}

double FloatLine::segmentDistance(FloatPoint* point) {
  FloatPoint* projection = perpendicularProjection(point);
  double result;
  if (projection->isContainedInBox(this->a, this->b, 0.01)) {
    result = point->distance(projection);
  } else {
    result = jrt::Math::min(point->distance(a), point->distance(b));
  }
  return result;
}

FloatLine* FloatLine::segmentProjection(FloatLine* lineSegment) {
  if (this->b->scalarProduct(this->a, lineSegment->a) < 0) {
    return nullptr;
  }
  if (this->a->scalarProduct(this->b, lineSegment->b) < 0) {
    return nullptr;
  }
  FloatPoint* projectedA;
  if (this->a->scalarProduct(this->b, lineSegment->a) < 0) {
    projectedA = this->a;
  } else {
    projectedA = this->perpendicularProjection(lineSegment->a);
    if (jrt::Math::abs(projectedA->x) >= Limits::CRIT_INT ||
        jrt::Math::abs(projectedA->y) >= Limits::CRIT_INT) {
      return nullptr;
    }
  }
  FloatPoint* projectedB;
  if (this->b->scalarProduct(this->a, lineSegment->b) < 0) {
    projectedB = this->b;
  } else {
    projectedB = this->perpendicularProjection(lineSegment->b);
  }
  if (jrt::Math::abs(projectedB->x) >= Limits::CRIT_INT ||
      jrt::Math::abs(projectedB->y) >= Limits::CRIT_INT) {
    return nullptr;
  }
  return new FloatLine(projectedA, projectedB);
}

FloatLine* FloatLine::segmentProjection2(FloatLine* lineSegment) {
  if (lineSegment->a->scalarProduct(lineSegment->b, this->b) <= 0) {
    return nullptr;
  }
  if (lineSegment->b->scalarProduct(lineSegment->a, this->a) <= 0) {
    return nullptr;
  }
  FloatPoint* projectedA;
  if (lineSegment->a->scalarProduct(lineSegment->b, this->a) < 0) {
    FloatLine* currentPerpendicularLine =
        new FloatLine(lineSegment->a, lineSegment->b->turn90Degree(1, lineSegment->a));
    projectedA = currentPerpendicularLine->intersection(this);
    if (projectedA == nullptr || jrt::Math::abs(projectedA->x) >= Limits::CRIT_INT ||
        jrt::Math::abs(projectedA->y) >= Limits::CRIT_INT) {
      return nullptr;
    }
  } else {
    projectedA = this->a;
  }

  FloatPoint* projectedB;

  if (lineSegment->b->scalarProduct(lineSegment->a, this->b) < 0) {
    FloatLine* currentPerpendicularLine =
        new FloatLine(lineSegment->b, lineSegment->a->turn90Degree(1, lineSegment->b));
    projectedB = currentPerpendicularLine->intersection(this);
    if (projectedB == nullptr || jrt::Math::abs(projectedB->x) >= Limits::CRIT_INT ||
        jrt::Math::abs(projectedB->y) >= Limits::CRIT_INT) {
      return nullptr;
    }
  } else {
    projectedB = this->b;
  }
  return new FloatLine(projectedA, projectedB);
}

FloatLine* FloatLine::shrinkSegment(double offset) {
  double dx = b->x - a->x;
  double dy = b->y - a->y;
  if (dx == 0 && dy == 0) {
    return this;
  }
  double length = jrt::Math::sqrt(dx * dx + dy * dy);
  double effectiveOffset = jrt::Math::min(offset, length / 2);
  FloatPoint* newA = new FloatPoint(a->x + (dx * effectiveOffset) / length,
                                    a->y + (dy * effectiveOffset) / length);
  double newLength = length - effectiveOffset;
  FloatPoint* newB =
      new FloatPoint(a->x + (dx * newLength) / length, a->y + (dy * newLength) / length);
  return new FloatLine(newA, newB);
}

FloatPoint* FloatLine::nearestSegmentPoint(FloatPoint* fromPoint) {
  FloatPoint* projection = this->perpendicularProjection(fromPoint);
  if (projection->isContainedInBox(this->a, this->b, 0.01)) {
    return projection;
  }
  // Now the projection is outside the line segment.
  FloatPoint* result;
  if (fromPoint->distanceSquare(this->a) <= fromPoint->distanceSquare(this->b)) {
    result = this->a;
  } else {
    result = this->b;
  }
  return result;
}

jrt::Array<FloatLine*>* FloatLine::divideSegmentIntoSections(int32_t count) {
  if (count == 0) {
    return new jrt::Array<FloatLine*>(0);
  }
  if (count == 1) {
    auto* result = new jrt::Array<FloatLine*>(1);
    (*result)[0] = this;
    return result;
  }
  double lineLength = this->b->distance(this->a);
  auto* result = new jrt::Array<FloatLine*>(count);
  double sectionLength = lineLength / count;
  double dx = b->x - a->x;
  double dy = b->y - a->y;
  FloatPoint* currentA = this->a;
  for (int32_t i = 0; i < count; i++) {
    FloatPoint* currentB;
    if (i == count - 1) {
      currentB = this->b;
    } else {
      double currentDistance = (i + 1) * sectionLength;
      double currentX = a->x + (dx * currentDistance) / lineLength;
      double currentY = a->y + (dy * currentDistance) / lineLength;
      currentB = new FloatPoint(currentX, currentY);
    }
    (*result)[i] = new FloatLine(currentA, currentB);
    currentA = currentB;
  }
  return result;
}

}  // namespace fr
