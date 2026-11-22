#include "geometry/planar/Polygon.h"

#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

Polygon::Polygon(jrt::Array<Point*>* points) {
  corners = new (jrt::kOp) jrt::LinkedList<Point*>();
  if (points->length == 0) {
    return;
  }
  for (Point* p : *points) {
    corners->add(p);
  }

  bool cornerRemoved = true;
  while (cornerRemoved) {
    cornerRemoved = false;
    // remove multiple points

    if (corners->isEmpty()) {
      return;
    }
    jrt::Iterator<Point*>* i = corners->iterator();
    Point* currentObject = i->next();
    while (i->hasNext()) {
      Point* nextOb = i->next();
      if (nextOb->equals(currentObject)) {
        i->remove();
        cornerRemoved = true;
      } else {
        currentObject = nextOb;
      }
    }

    // remove points which are collinear with  the previous
    // and next point.
    i = corners->iterator();
    Point* prev = i->next();
    jrt::Iterator<Point*>* prevI = corners->iterator();
    if (!i->hasNext()) {
      continue;
    }
    Point* current = i->next();
    prevI->next();
    while (i->hasNext()) {
      Point* next = i->next();
      prevI->next();

      if (current->sideOf(prev, next) == Side::COLLINEAR()) {
        prevI->remove();
        cornerRemoved = true;
        break;
      }
      prev = current;
      current = next;
    }
  }
}

jrt::Array<Point*>* Polygon::withoutSpikes(jrt::Array<Point*>* corners) {
  auto* result = new (jrt::kOp) jrt::ArrayList<Point*>(corners->toVector());
  bool closed = result->size() > 1 && result->get(0)->equals(result->get(result->size() - 1));
  if (closed) {
    result->removeAt(result->size() - 1);
  }
  bool removed = true;
  while (removed && result->size() > 3) {
    removed = false;
    int32_t count = result->size();
    for (int32_t i = 0; i < count; i++) {
      if (isSpike(result->get((i + count - 1) % count), result->get(i),
                  result->get((i + 1) % count))) {
        result->removeAt(i);
        removed = true;
        break;
      }
    }
  }
  if (result->size() == corners->length - (closed ? 1 : 0)) {
    return corners;
  }
  if (closed) {
    result->add(result->get(0));
  }
  return new (jrt::kGeom) jrt::Array<Point*>(result->toVector());
}

bool Polygon::isSpike(Point* prevCorner, Point* corner, Point* nextCorner) {
  FloatPoint* prev = prevCorner->toFloat();
  FloatPoint* current = corner->toFloat();
  FloatPoint* next = nextCorner->toFloat();
  double inX = current->x - prev->x;
  double inY = current->y - prev->y;
  double outX = next->x - current->x;
  double outY = next->y - current->y;
  if (inX * outX + inY * outY >= 0) {
    return false;
  }
  double chordX = next->x - prev->x;
  double chordY = next->y - prev->y;
  double cross = inX * chordY - inY * chordX;
  return cross * cross <= chordX * chordX + chordY * chordY;
}

jrt::Array<Point*>* Polygon::cornerArray() {
  int32_t cornerCount = corners->size();
  auto* result = new (jrt::kGeom) jrt::Array<Point*>(cornerCount);
  jrt::Iterator<Point*>* it = corners->iterator();
  for (int32_t i = 0; i < cornerCount; i++) {
    (*result)[i] = it->next();
  }
  return result;
}

Polygon* Polygon::revertCorners() {
  jrt::Array<Point*>* corners = cornerArray();
  auto* reverseCornerArr = new (jrt::kGeom) jrt::Array<Point*>(corners->length);
  for (int32_t i = 0; i < corners->length; i++) {
    (*reverseCornerArr)[i] = (*corners)[corners->length - i - 1];
  }
  return new (jrt::kOp) Polygon(reverseCornerArr);
}

int32_t Polygon::windingNumberAfterClosing() {
  jrt::Array<Point*>* corners = cornerArray();
  if (corners->length < 2) {
    return 0;
  }
  Vector* firstSideVector = (*corners)[1]->differenceBy((*corners)[0]);
  Vector* prevSideVector = firstSideVector;
  int32_t cornerCount = corners->length;
  // Skip the last corner, if it is equal to the first corner.
  if ((*corners)[0]->equals((*corners)[cornerCount - 1])) {
    --cornerCount;
  }
  double angleSum = 0;
  for (int32_t i = 1; i < cornerCount - 1; i++) {
    Vector* nextSideVector = (*corners)[i + 1]->differenceBy((*corners)[i]);
    angleSum += prevSideVector->angleApprox(nextSideVector);
    prevSideVector = nextSideVector;
  }
  if (cornerCount > 1) {
    Vector* nextSideVector = (*corners)[0]->differenceBy((*corners)[cornerCount - 1]);
    angleSum += prevSideVector->angleApprox(nextSideVector);
    prevSideVector = nextSideVector;
  }
  angleSum += prevSideVector->angleApprox(firstSideVector);
  angleSum /= 2.0 * jrt::Math::PI;
  if (jrt::Math::abs(angleSum) < 0.5) {
    FRLogger::warn("Polygon.winding_number_after_closing: winding number != 0 expected");
  }
  return jrt::l2i(jrt::Math::round(angleSum));
}

}  // namespace fr
