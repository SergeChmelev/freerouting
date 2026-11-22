#include "geometry/planar/PolygonShape.h"

#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polygon.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/ShapeBoundingDirections.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

jrt::Random* PolygonShape::randomGenerator() {
  static jrt::Random* v = new (jrt::kBoard) jrt::Random(seed);
  return v;
}

PolygonShape::PolygonShape(Polygon* polygon) {
  Polygon* currentPolygon = polygon;
  if (polygon->windingNumberAfterClosing() < 0) {
    // the corners of the polygon are in clockwise sense
    currentPolygon = polygon->revertCorners();
  }
  jrt::Array<Point*>* currentCorners = currentPolygon->cornerArray();
  int32_t lastCornerNo = currentCorners->length - 1;

  if (lastCornerNo > 0) {
    if ((*currentCorners)[0]->equals((*currentCorners)[lastCornerNo])) {
      // skip last point
      --lastCornerNo;
    }
  }

  bool lastPointCollinear = false;

  if (lastCornerNo >= 2) {
    lastPointCollinear = (*currentCorners)[lastCornerNo]->sideOf(
                             (*currentCorners)[lastCornerNo - 1], (*currentCorners)[0]) ==
                         Side::COLLINEAR();
  }
  if (lastPointCollinear) {
    // skip last point
    --lastCornerNo;
  }

  int32_t firstCornerNo = 0;
  bool firstPointCollinear = false;

  if (lastCornerNo - firstCornerNo >= 2) {
    firstPointCollinear = (*currentCorners)[0]->sideOf((*currentCorners)[1],
                                                       (*currentCorners)[lastCornerNo]) ==
                          Side::COLLINEAR();
  }

  if (firstPointCollinear) {
    // skip first point
    ++firstCornerNo;
  }
  // search the point with the lowest y and then with the lowest x
  int32_t startCornerNo = firstCornerNo;
  FloatPoint* startCorner = (*currentCorners)[startCornerNo]->toFloat();
  for (int32_t i = startCornerNo + 1; i <= lastCornerNo; i++) {
    FloatPoint* currentCorner = (*currentCorners)[i]->toFloat();
    if (currentCorner->y < startCorner->y ||
        (currentCorner->y == startCorner->y && currentCorner->x < startCorner->x)) {
      startCornerNo = i;
      startCorner = currentCorner;
    }
  }
  int32_t newCornerCount = lastCornerNo - firstCornerNo + 1;
  auto* result = new (jrt::kGeom) jrt::Array<Point*>(newCornerCount);
  int32_t currentCornerNo = 0;
  for (int32_t i = startCornerNo; i <= lastCornerNo; i++) {
    (*result)[currentCornerNo] = (*currentCorners)[i];
    ++currentCornerNo;
  }
  for (int32_t i = firstCornerNo; i < startCornerNo; i++) {
    (*result)[currentCornerNo] = (*currentCorners)[i];
    ++currentCornerNo;
  }
  corners = result;
}

PolygonShape::PolygonShape(jrt::Array<Point*>* corners) : PolygonShape(new (jrt::kOp) Polygon(corners)) {}

Point* PolygonShape::corner(int32_t no) {
  if (no < 0 || no >= corners->length) {
    FRLogger::warn("PolygonShape.corner: no out of range");
    return nullptr;
  }
  return (*corners)[no];
}

int32_t PolygonShape::borderLineCount() { return corners->length; }

bool PolygonShape::cornerIsBounded(int32_t no) { return true; }

bool PolygonShape::intersects(Shape* shape) {
  if (auto* other = jrt::fast_cast<PolygonShape>(shape)) {
    jrt::Array<TileShape*>* convexPieces = splitToConvex();
    jrt::Array<TileShape*>* otherPieces = other->splitToConvex();
    for (TileShape* piece : *convexPieces) {
      for (TileShape* otherPiece : *otherPieces) {
        if (piece->intersects(static_cast<Shape*>(otherPiece))) {
          return true;
        }
      }
    }
    return false;
  }
  return shape->intersects(static_cast<Shape*>(this));
}

bool PolygonShape::intersects(Circle* circle) {
  jrt::Array<TileShape*>* convexPieces = splitToConvex();
  for (int32_t i = 0; i < convexPieces->length; i++) {
    if ((*convexPieces)[i]->intersects(circle)) {
      return true;
    }
  }
  return false;
}

bool PolygonShape::intersects(Simplex* simplex) {
  jrt::Array<TileShape*>* convexPieces = splitToConvex();
  for (int32_t i = 0; i < convexPieces->length; i++) {
    if ((*convexPieces)[i]->intersects(simplex)) {
      return true;
    }
  }
  return false;
}

bool PolygonShape::intersects(IntOctagon* oct) {
  jrt::Array<TileShape*>* convexPieces = splitToConvex();
  for (int32_t i = 0; i < convexPieces->length; i++) {
    if ((*convexPieces)[i]->intersects(oct)) {
      return true;
    }
  }
  return false;
}

bool PolygonShape::intersects(IntBox* box) {
  jrt::Array<TileShape*>* convexPieces = splitToConvex();
  for (int32_t i = 0; i < convexPieces->length; i++) {
    if ((*convexPieces)[i]->intersects(box)) {
      return true;
    }
  }
  return false;
}

jrt::Array<Polyline*>* PolygonShape::cutout(Polyline* polyline) {
  FRLogger::warn("PolygonShape.cutout not yet implemented");
  return nullptr;
}

PolygonShape* PolygonShape::enlarge(double offset) {
  if (offset == 0) {
    return this;
  }
  FRLogger::warn("PolygonShape.enlarge not yet implemented");
  return nullptr;
}

double PolygonShape::borderDistance(FloatPoint* point) {
  if (corners->length == 0) {
    return 0;
  }
  FloatPoint* prevCorner = (*corners)[corners->length - 1]->toFloat();
  double result = prevCorner->distance(point);
  for (Point* corner : *corners) {
    FloatPoint* currentCorner = corner->toFloat();
    result =
        jrt::Math::min(result, (new (jrt::kGeom) FloatLine(prevCorner, currentCorner))->segmentDistance(point));
    prevCorner = currentCorner;
  }
  return result;
}

double PolygonShape::smallestRadius() { return borderDistance(centreOfGravity()); }

bool PolygonShape::contains(FloatPoint* point) {
  jrt::Array<TileShape*>* convexPieces = splitToConvex();
  for (int32_t i = 0; i < convexPieces->length; i++) {
    if ((*convexPieces)[i]->contains(point)) {
      return true;
    }
  }
  return false;
}

bool PolygonShape::contains(Point* point) { return !isOutside(point); }

bool PolygonShape::containsInside(Point* point) {
  if (containsOnBorder(point)) {
    return false;
  }
  return !isOutside(point);
}

bool PolygonShape::isOutside(Point* point) {
  jrt::Array<TileShape*>* convexPieces = splitToConvex();
  for (int32_t i = 0; i < convexPieces->length; i++) {
    if (!(*convexPieces)[i]->isOutside(point)) {
      return false;
    }
  }
  return true;
}

bool PolygonShape::containsOnBorder(Point* point) {
  for (int32_t i = 0; i < corners->length; i++) {
    Point* a = (*corners)[i];
    Point* b = (*corners)[(i + 1) % corners->length];
    if (point->equals(a)) {
      return true;
    }
    if (point->sideOf(a, b) != Side::COLLINEAR()) {
      continue;
    }
    if (point->compareX(a) * point->compareX(b) <= 0 &&
        point->compareY(a) * point->compareY(b) <= 0) {
      return true;
    }
  }
  return false;
}

double PolygonShape::distance(FloatPoint* point) {
  FRLogger::warn("PolygonShape.distance not yet implemented");
  return 0;
}

PolygonShape* PolygonShape::translateBy(Vector* vector) {
  if (vector->equals(Vector::ZERO())) {
    return this;
  }
  auto* newCorners = new (jrt::kGeom) jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*newCorners)[i] = (*corners)[i]->translateBy(vector);
  }
  return new (jrt::kGeom) PolygonShape(newCorners);
}

RegularTileShape* PolygonShape::boundingShape(ShapeBoundingDirections* dirs) {
  return dirs->bounds(this);
}

IntBox* PolygonShape::boundingBox() {
  if (precalculatedBoundingBox == nullptr) {
    double llx = jrt::Integer::MAX_VALUE;
    double lly = jrt::Integer::MAX_VALUE;
    double urx = jrt::Integer::MIN_VALUE;
    double ury = jrt::Integer::MIN_VALUE;
    for (int32_t i = 0; i < corners->length; i++) {
      FloatPoint* current = (*corners)[i]->toFloat();
      llx = jrt::Math::min(llx, current->x);
      lly = jrt::Math::min(lly, current->y);
      urx = jrt::Math::max(urx, current->x);
      ury = jrt::Math::max(ury, current->y);
    }
    IntPoint* lowerLeft =
        new (jrt::kGeom) IntPoint(jrt::d2i(jrt::Math::floor(llx)), jrt::d2i(jrt::Math::floor(lly)));
    IntPoint* upperRight =
        new (jrt::kGeom) IntPoint(jrt::d2i(jrt::Math::ceil(urx)), jrt::d2i(jrt::Math::ceil(ury)));
    precalculatedBoundingBox = new (jrt::kGeom) IntBox(lowerLeft, upperRight);
  }
  return precalculatedBoundingBox;
}

IntOctagon* PolygonShape::boundingOctagon() {
  if (precalculatedBoundingOctagon == nullptr) {
    double lx = jrt::Integer::MAX_VALUE;
    double ly = jrt::Integer::MAX_VALUE;
    double rx = jrt::Integer::MIN_VALUE;
    double uy = jrt::Integer::MIN_VALUE;
    double ulx = jrt::Integer::MAX_VALUE;
    double lrx = jrt::Integer::MIN_VALUE;
    double llx = jrt::Integer::MAX_VALUE;
    double urx = jrt::Integer::MIN_VALUE;
    for (int32_t i = 0; i < corners->length; i++) {
      FloatPoint* current = (*corners)[i]->toFloat();
      lx = jrt::Math::min(lx, current->x);
      ly = jrt::Math::min(ly, current->y);
      rx = jrt::Math::max(rx, current->x);
      uy = jrt::Math::max(uy, current->y);

      double tmp = current->x - current->y;
      ulx = jrt::Math::min(ulx, tmp);
      lrx = jrt::Math::max(lrx, tmp);

      tmp = current->x + current->y;
      llx = jrt::Math::min(llx, tmp);
      urx = jrt::Math::max(urx, tmp);
    }
    precalculatedBoundingOctagon = new (jrt::kGeom) IntOctagon(
        jrt::d2i(jrt::Math::floor(lx)), jrt::d2i(jrt::Math::floor(ly)),
        jrt::d2i(jrt::Math::ceil(rx)), jrt::d2i(jrt::Math::ceil(uy)),
        jrt::d2i(jrt::Math::floor(ulx)), jrt::d2i(jrt::Math::ceil(lrx)),
        jrt::d2i(jrt::Math::floor(llx)), jrt::d2i(jrt::Math::ceil(urx)));
  }
  return precalculatedBoundingOctagon;
}

bool PolygonShape::isConvex() {
  if (corners->length <= 2) {
    return true;
  }
  Point* prevPoint = (*corners)[corners->length - 1];
  Point* currentPoint = (*corners)[0];
  Point* nextPoint = (*corners)[1];

  for (int32_t ind = 0; ind < corners->length; ind++) {
    if (nextPoint->sideOf(prevPoint, currentPoint) == Side::ON_THE_RIGHT()) {
      return false;
    }
    prevPoint = currentPoint;
    currentPoint = nextPoint;
    if (ind == corners->length - 2) {
      nextPoint = (*corners)[0];
    } else if (ind == corners->length - 1) {
      nextPoint = (*corners)[1];
    } else {
      nextPoint = (*corners)[ind + 2];
    }
  }
  // check, if the sum of the interior angles is at most 2 * pi

  Line* firstLine = new (jrt::kGeom) Line((*corners)[corners->length - 1], (*corners)[0]);
  Line* currentLine = new (jrt::kGeom) Line((*corners)[0], (*corners)[1]);
  IntDirection* firstDirection = jrt::fast_cast<IntDirection>(firstLine->direction());
  IntDirection* currentDirection = jrt::fast_cast<IntDirection>(currentLine->direction());
  double lastDet = firstDirection->determinant(currentDirection);

  for (int32_t ind2 = 2; ind2 < corners->length; ind2++) {
    currentLine = new (jrt::kGeom) Line(currentLine->b, (*corners)[ind2]);
    currentDirection = jrt::fast_cast<IntDirection>(currentLine->direction());
    double currentDet = firstDirection->determinant(currentDirection);
    if (lastDet <= 0 && currentDet > 0) {
      return false;
    }
    lastDet = currentDet;
  }

  return true;
}

PolygonShape* PolygonShape::convexHull() {
  if (corners->length <= 2) {
    return this;
  }
  Point* prevPoint = (*corners)[corners->length - 1];
  Point* currentPoint = (*corners)[0];
  Point* nextPoint;
  for (int32_t ind = 0; ind < corners->length; ind++) {
    if (ind == corners->length - 1) {
      nextPoint = (*corners)[0];
    } else {
      nextPoint = (*corners)[ind + 1];
    }
    if (nextPoint->sideOf(prevPoint, currentPoint) != Side::ON_THE_LEFT()) {
      // skip currentPoint;
      auto* newCorners = new (jrt::kGeom) jrt::Array<Point*>(corners->length - 1);
      jrt::arraycopy(corners, 0, newCorners, 0, ind);
      if (ind < newCorners->length) {
        // copy remaining elements if present
        jrt::arraycopy(corners, ind + 1, newCorners, ind, newCorners->length - ind);
      }
      PolygonShape* result = new (jrt::kGeom) PolygonShape(newCorners);
      return result->convexHull();
    }
    prevPoint = currentPoint;
    currentPoint = nextPoint;
  }
  return this;
}

TileShape* PolygonShape::boundingTile() {
  PolygonShape* hull = convexHull();
  auto* boundingLines = new (jrt::kGeom) jrt::Array<Line*>(hull->corners->length);
  for (int32_t i = 0; i < boundingLines->length - 1; i++) {
    (*boundingLines)[i] = new (jrt::kGeom) Line((*hull->corners)[i], (*hull->corners)[i + 1]);
  }
  (*boundingLines)[boundingLines->length - 1] =
      new (jrt::kGeom) Line((*hull->corners)[hull->corners->length - 1], (*hull->corners)[0]);
  return TileShape::getInstance(boundingLines);
}

double PolygonShape::area() {
  if (dimension() < 2) {
    return 0;
  }
  // calculate half of the absolute value of
  // x0 (y1 - yn-1) + x1 (y2 - y0) + x2 (y3 - y1) + ...+ xn-1( y0 - yn-2)
  // where xi, yi are the coordinates of the i-th corner of this polygon.

  double result = 0;
  FloatPoint* prevCorner = (*corners)[corners->length - 2]->toFloat();
  FloatPoint* currentCorner = (*corners)[corners->length - 1]->toFloat();
  for (int32_t i = 0; i < corners->length; i++) {
    FloatPoint* nextCorner = (*corners)[i]->toFloat();
    result += currentCorner->x * (nextCorner->y - prevCorner->y);
    prevCorner = currentCorner;
    currentCorner = nextCorner;
  }
  return 0.5 * jrt::Math::abs(result);
}

int32_t PolygonShape::dimension() {
  if (corners->length == 0) {
    return -1;
  }
  if (corners->length == 1) {
    return 0;
  }
  if (corners->length == 2) {
    return 1;
  }
  return 2;
}

bool PolygonShape::isBounded() { return true; }

bool PolygonShape::isEmpty() { return corners->length == 0; }

Line* PolygonShape::borderLine(int32_t no) {
  if (no < 0 || no >= corners->length) {
    FRLogger::warn("PolygonShape.borderLine: no out of range");
    return nullptr;
  }
  Point* nextCorner;
  if (no == corners->length - 1) {
    nextCorner = (*corners)[0];
  } else {
    nextCorner = (*corners)[no + 1];
  }
  return new (jrt::kGeom) Line((*corners)[no], nextCorner);
}

FloatPoint* PolygonShape::nearestPointApprox(FloatPoint* fromPoint) {
  double minDist = jrt::Double::MAX_VALUE;
  FloatPoint* result = nullptr;
  jrt::Array<TileShape*>* convexShapes = splitToConvex();
  for (int32_t i = 0; i < convexShapes->length; i++) {
    FloatPoint* currentNearestPoint = (*convexShapes)[i]->nearestPointApprox(fromPoint);
    double currentDistance = currentNearestPoint->distanceSquare(fromPoint);
    if (currentDistance < minDist) {
      minDist = currentDistance;
      result = currentNearestPoint;
    }
  }
  return result;
}

PolygonShape* PolygonShape::turn90Degree(int32_t factor, IntPoint* pole) {
  auto* newCorners = new (jrt::kGeom) jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*newCorners)[i] = (*corners)[i]->turn90Degree(factor, pole);
  }
  return new (jrt::kGeom) PolygonShape(newCorners);
}

PolygonShape* PolygonShape::rotateApprox(double angle, FloatPoint* pole) {
  if (angle == 0) {
    return this;
  }
  auto* newCorners = new (jrt::kGeom) jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*newCorners)[i] = (*corners)[i]->toFloat()->rotate(angle, pole)->round();
  }
  return new (jrt::kGeom) PolygonShape(newCorners);
}

PolygonShape* PolygonShape::mirrorVertical(IntPoint* pole) {
  auto* newCorners = new (jrt::kGeom) jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*newCorners)[i] = (*corners)[i]->mirrorVertical(pole);
  }
  return new (jrt::kGeom) PolygonShape(newCorners);
}

PolygonShape* PolygonShape::mirrorHorizontal(IntPoint* pole) {
  auto* newCorners = new (jrt::kGeom) jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*newCorners)[i] = (*corners)[i]->mirrorHorizontal(pole);
  }
  return new (jrt::kGeom) PolygonShape(newCorners);
}

void PolygonShape::reuseConvexPiecesOf(PolygonShape* source) {
  jrt::Array<TileShape*>* sourcePieces = source->precalculatedConvexPieces;
  if (this->precalculatedConvexPieces == nullptr && sourcePieces != nullptr) {
    // Arrays.equals(this.corners, source.corners)
    bool sameCorners = this->corners == source->corners;
    if (!sameCorners && this->corners != nullptr && source->corners != nullptr &&
        this->corners->length == source->corners->length) {
      sameCorners = true;
      for (int32_t i = 0; i < this->corners->length; i++) {
        Point* a = (*this->corners)[i];
        Point* b = (*source->corners)[i];
        if (!(a == nullptr ? b == nullptr : a->equals(b))) {
          sameCorners = false;
          break;
        }
      }
    }
    if (sameCorners) {
      this->precalculatedConvexPieces = sourcePieces;
    }
  }
}

jrt::Array<TileShape*>* PolygonShape::splitToConvex() {
  if (this->precalculatedConvexPieces == nullptr) {
    // not yet precalculated
    // use a fixed seed to get reproducible result
    randomGenerator()->setSeed(seed);
    jrt::Collection<PolygonShape*>* convexPieces = splitToConvexRecu();
    if (convexPieces == nullptr) {
      // split failed, maybe the polygon has selfontersections
      return nullptr;
    }
    precalculatedConvexPieces = new (jrt::kGeom) jrt::Array<TileShape*>(convexPieces->size());
    jrt::Iterator<PolygonShape*>* it = convexPieces->iterator();
    for (int32_t i = 0; i < precalculatedConvexPieces->length; i++) {
      PolygonShape* currentPiece = it->next();
      (*precalculatedConvexPieces)[i] = TileShape::getInstance(currentPiece->corners);
    }
  }
  return this->precalculatedConvexPieces;
}

jrt::Collection<PolygonShape*>* PolygonShape::splitToConvexRecu() {
  // start with a hashed corner and search the first concave corner
  int32_t startCornerNo = randomGenerator()->nextInt(corners->length);
  Point* currentCorner = (*corners)[startCornerNo];
  Point* prevCorner;
  if (startCornerNo != 0) {
    prevCorner = (*corners)[startCornerNo - 1];
  } else {
    prevCorner = (*corners)[corners->length - 1];
  }

  Point* nextCorner;

  // search for the next concave corner from here
  int32_t concaveCornerNo = -1;
  for (int32_t i = 0; i < corners->length; i++) {
    if (startCornerNo < corners->length - 1) {
      nextCorner = (*corners)[startCornerNo + 1];
    } else {
      nextCorner = (*corners)[0];
    }
    if (nextCorner->sideOf(prevCorner, currentCorner) == Side::ON_THE_RIGHT()) {
      // concave corner found
      concaveCornerNo = startCornerNo;
      break;
    }
    prevCorner = currentCorner;
    currentCorner = nextCorner;
    startCornerNo = (startCornerNo + 1) % corners->length;
  }
  jrt::Collection<PolygonShape*>* result = new (jrt::kOp) jrt::LinkedList<PolygonShape*>();
  if (concaveCornerNo < 0) {
    // no concave corner found, this shape is already convex
    result->add(this);
    return result;
  }
  PolygonShape_DivisionPoint* d = new (jrt::kGeom) PolygonShape_DivisionPoint(this, concaveCornerNo);
  if (d->projection == nullptr) {
    // projection not found, maybe polygon has selfintersections
    return nullptr;
  }

  // construct the result pieces from polygon and the division point
  int32_t cornerCount = d->cornerNoAfterProjection - concaveCornerNo;

  if (cornerCount < 0) {
    cornerCount += corners->length;
  }
  ++cornerCount;
  auto* firstArr = new (jrt::kGeom) jrt::Array<Point*>(cornerCount);
  int32_t cornerInd = concaveCornerNo;

  for (int32_t i = 0; i < cornerCount - 1; i++) {
    (*firstArr)[i] = (*corners)[cornerInd];
    cornerInd = (cornerInd + 1) % corners->length;
  }
  (*firstArr)[cornerCount - 1] = d->projection->round();
  cornerCount = concaveCornerNo - d->cornerNoAfterProjection;
  if (cornerCount < 0) {
    cornerCount += corners->length;
  }
  cornerCount += 2;
  auto* lastArr = new (jrt::kGeom) jrt::Array<Point*>(cornerCount);
  (*lastArr)[0] = d->projection->round();
  cornerInd = d->cornerNoAfterProjection;
  for (int32_t i = 1; i < cornerCount; i++) {
    (*lastArr)[i] = (*corners)[cornerInd];
    cornerInd = (cornerInd + 1) % corners->length;
  }
  PolygonShape* lastPiece = new (jrt::kGeom) PolygonShape(lastArr);
  PolygonShape* firstPiece = new (jrt::kGeom) PolygonShape(firstArr);
  jrt::Collection<PolygonShape*>* c1 = firstPiece->splitToConvexRecu();
  if (c1 == nullptr) {
    return nullptr;
  }
  jrt::Collection<PolygonShape*>* c2 = lastPiece->splitToConvexRecu();
  if (c2 == nullptr) {
    return nullptr;
  }
  result->addAll(c1);
  result->addAll(c2);
  return result;
}

PolygonShape_DivisionPoint::PolygonShape_DivisionPoint(PolygonShape* outer,
                                                       int32_t concaveCornerNo)
    : outer(outer) {
  jrt::Array<Point*>* corners = outer->corners;
  FloatPoint* concaveCorner = (*corners)[concaveCornerNo]->toFloat();
  FloatPoint* beforeConcaveCorner;

  if (concaveCornerNo != 0) {
    beforeConcaveCorner = (*corners)[concaveCornerNo - 1]->toFloat();
  } else {
    beforeConcaveCorner = (*corners)[corners->length - 1]->toFloat();
  }

  FloatPoint* afterConcaveCorner;

  if (concaveCornerNo == corners->length - 1) {
    afterConcaveCorner = (*corners)[0]->toFloat();
  } else {
    afterConcaveCorner = (*corners)[concaveCornerNo + 1]->toFloat();
  }

  bool searchRight =
      beforeConcaveCorner->y > concaveCorner->y || concaveCorner->y > afterConcaveCorner->y;

  bool searchLeft =
      beforeConcaveCorner->y < concaveCorner->y || concaveCorner->y < afterConcaveCorner->y;

  bool searchUp =
      beforeConcaveCorner->x < concaveCorner->x || concaveCorner->x < afterConcaveCorner->x;

  bool searchDown =
      beforeConcaveCorner->x > concaveCorner->x || concaveCorner->x > afterConcaveCorner->x;

  double minProjectionDist = jrt::Integer::MAX_VALUE;
  FloatPoint* minProjection = nullptr;
  int32_t cornerNoAfterMinProjection = 0;

  int32_t cornerNoAfterCurrProjection = (concaveCornerNo + 2) % corners->length;

  Point* cornerBeforeCurrProjection;
  if (cornerNoAfterCurrProjection != 0) {
    cornerBeforeCurrProjection = (*corners)[cornerNoAfterCurrProjection - 1];
  } else {
    cornerBeforeCurrProjection = (*corners)[corners->length - 1];
  }
  FloatPoint* cornerBeforeProjectionApprox = cornerBeforeCurrProjection->toFloat();

  double currentDistance;
  int32_t loopEnd = corners->length - 2;

  for (int32_t i = 0; i < loopEnd; i++) {
    Point* cornerAfterCurrProjection = (*corners)[cornerNoAfterCurrProjection];
    FloatPoint* cornerAfterProjectionApprox = cornerAfterCurrProjection->toFloat();
    if (cornerBeforeProjectionApprox->y != cornerAfterProjectionApprox->y) {
      // try a horizontal division
      double minY;
      double maxY;

      if (cornerAfterProjectionApprox->y > cornerBeforeProjectionApprox->y) {
        minY = cornerBeforeProjectionApprox->y;
        maxY = cornerAfterProjectionApprox->y;
      } else {
        minY = cornerAfterProjectionApprox->y;
        maxY = cornerBeforeProjectionApprox->y;
      }

      if (concaveCorner->y >= minY && concaveCorner->y <= maxY) {
        Line* currentLine = new (jrt::kGeom) Line(cornerBeforeCurrProjection, cornerAfterCurrProjection);
        double xintersection = currentLine->functionInYValueApprox(concaveCorner->y);
        currentDistance = jrt::Math::abs(xintersection - concaveCorner->x);
        // Make sure, that the new shape will not be concave at the projection point.
        // That might happen, if the boundary curve runs back in itself.
        bool projectionOk =
            currentDistance < minProjectionDist &&
            ((searchRight && xintersection > concaveCorner->x &&
              concaveCorner->y <= cornerAfterProjectionApprox->y) ||
             (searchLeft && xintersection < concaveCorner->x &&
              concaveCorner->y >= cornerAfterProjectionApprox->y));
        if (projectionOk) {
          minProjectionDist = currentDistance;
          cornerNoAfterMinProjection = cornerNoAfterCurrProjection;
          minProjection = new (jrt::kGeom) FloatPoint(xintersection, concaveCorner->y);
        }
      }
    }

    if (cornerBeforeProjectionApprox->x != cornerAfterProjectionApprox->x) {
      // try a vertical division
      double minX;
      double maxX;
      if (cornerAfterProjectionApprox->x > cornerBeforeProjectionApprox->x) {
        minX = cornerBeforeProjectionApprox->x;
        maxX = cornerAfterProjectionApprox->x;
      } else {
        minX = cornerAfterProjectionApprox->x;
        maxX = cornerBeforeProjectionApprox->x;
      }
      if (concaveCorner->x >= minX && concaveCorner->x <= maxX) {
        Line* currentLine = new (jrt::kGeom) Line(cornerBeforeCurrProjection, cornerAfterCurrProjection);
        double yintersection = currentLine->functionValueApprox(concaveCorner->x);
        currentDistance = jrt::Math::abs(yintersection - concaveCorner->y);
        // make sure, that the new shape will be convex at the projection point
        bool projectionOk =
            currentDistance < minProjectionDist &&
            ((searchUp && yintersection > concaveCorner->y &&
              concaveCorner->x >= cornerAfterProjectionApprox->x) ||
             (searchDown && yintersection < concaveCorner->y &&
              concaveCorner->x <= cornerAfterProjectionApprox->x));

        if (projectionOk) {
          minProjectionDist = currentDistance;
          cornerNoAfterMinProjection = cornerNoAfterCurrProjection;
          minProjection = new (jrt::kGeom) FloatPoint(concaveCorner->x, yintersection);
        }
      }
    }
    cornerBeforeCurrProjection = cornerAfterCurrProjection;
    cornerBeforeProjectionApprox = cornerAfterProjectionApprox;
    if (cornerNoAfterCurrProjection == corners->length - 1) {
      cornerNoAfterCurrProjection = 0;
    } else {
      ++cornerNoAfterCurrProjection;
    }
  }
  if (minProjectionDist == jrt::Integer::MAX_VALUE) {
    // Arrays.toString(corners)
    std::string cornersString = "[";
    for (int32_t i = 0; i < corners->length; i++) {
      if (i > 0) {
        cornersString += ", ";
      }
      cornersString += jrt::str((*corners)[i]);
    }
    cornersString += "]";
    FRLogger::warn("PolygonShape.DivisionPoint: projection not found for concave corner #" +
                   jrt::str(concaveCornerNo) + " at (" + jrt::str(concaveCorner->x) + ", " +
                   jrt::str(concaveCorner->y) + ") in polygon with " +
                   jrt::str(corners->length) + " corners: " + cornersString);
  }

  projection = minProjection;
  cornerNoAfterProjection = cornerNoAfterMinProjection;
}

}  // namespace fr
