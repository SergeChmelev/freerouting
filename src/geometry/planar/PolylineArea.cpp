#include "geometry/planar/PolylineArea.h"

#include <typeinfo>

#include "datastructures/Stoppable.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

PolylineArea::PolylineArea(PolylineShape* borderShape, jrt::Array<PolylineShape*>* holeArr)
    : borderShape(borderShape), holeArr(holeArr) {}

void PolylineArea::cutoutHolePiece(TileShape* dividePiece, TileShape* holePiece,
                                   jrt::Collection<TileShape*>* pieces) {
  jrt::Array<TileShape*>* resultPieces = dividePiece->cutout(holePiece);
  for (int32_t i = 0; i < resultPieces->length; i++) {
    TileShape* currentPiece = (*resultPieces)[i];
    if (currentPiece->dimension() == 2) {
      pieces->add(currentPiece);
    }
  }
}

int32_t PolylineArea::dimension() { return borderShape->dimension(); }

bool PolylineArea::isBounded() { return borderShape->isBounded(); }

bool PolylineArea::isEmpty() { return borderShape->isEmpty(); }

bool PolylineArea::isContainedIn(IntBox* box) { return borderShape->isContainedIn(box); }

jrt::Array<Shape*>* PolylineArea::getHoles() {
  // PORT NOTE: Java returns holeArr itself (array covariance); C++ arrays are not covariant, so a
  // new Shape* array with the same elements is returned.
  auto* result = new (jrt::kOp) jrt::Array<Shape*>(holeArr->length);
  for (int32_t i = 0; i < holeArr->length; i++) {
    (*result)[i] = (*holeArr)[i];
  }
  return result;
}

IntBox* PolylineArea::boundingBox() { return borderShape->boundingBox(); }

IntOctagon* PolylineArea::boundingOctagon() { return borderShape->boundingOctagon(); }

bool PolylineArea::contains(FloatPoint* point) {
  if (!borderShape->contains(point)) {
    return false;
  }
  for (int32_t i = 0; i < holeArr->length; i++) {
    if ((*holeArr)[i]->contains(point)) {
      return false;
    }
  }
  return true;
}

bool PolylineArea::contains(Point* point) {
  if (!borderShape->contains(point)) {
    return false;
  }
  for (int32_t i = 0; i < holeArr->length; i++) {
    if ((*holeArr)[i]->containsInside(point)) {
      return false;
    }
  }
  return true;
}

FloatPoint* PolylineArea::nearestPointApprox(FloatPoint* fromPoint) {
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

PolylineArea* PolylineArea::translateBy(Vector* vector) {
  if (vector->equals(Vector::ZERO())) {
    return this;
  }
  PolylineShape* translatedBorder = borderShape->translateBy(vector);
  auto* translatedHoles = new (jrt::kGeom) jrt::Array<PolylineShape*>(holeArr->length);
  for (int32_t i = 0; i < holeArr->length; i++) {
    (*translatedHoles)[i] = (*holeArr)[i]->translateBy(vector);
  }
  return new (jrt::kGeom) PolylineArea(translatedBorder, translatedHoles);
}

jrt::Array<FloatPoint*>* PolylineArea::cornerApproxArr() {
  int32_t cornerCount = borderShape->borderLineCount();
  for (int32_t i = 0; i < holeArr->length; i++) {
    cornerCount += (*holeArr)[i]->borderLineCount();
  }
  auto* result = new (jrt::kGeom) jrt::Array<FloatPoint*>(cornerCount);
  jrt::Array<FloatPoint*>* currentCornerArr = borderShape->cornerApproxArr();
  jrt::arraycopy(currentCornerArr, 0, result, 0, currentCornerArr->length);
  int32_t destPos = currentCornerArr->length;
  for (int32_t i = 0; i < holeArr->length; i++) {
    currentCornerArr = (*holeArr)[i]->cornerApproxArr();
    jrt::arraycopy(currentCornerArr, 0, result, destPos, currentCornerArr->length);
    destPos += currentCornerArr->length;
  }
  return result;
}

void PolylineArea::reuseConvexPiecesOf(PolylineArea* source) {
  jrt::Array<TileShape*>* sourcePieces = source->precalculatedConvexPieces;
  if (this->precalculatedConvexPieces != nullptr || sourcePieces == nullptr ||
      this->holeArr->length != source->holeArr->length ||
      !sameBorder(this->borderShape, source->borderShape)) {
    return;
  }
  for (int32_t i = 0; i < this->holeArr->length; i++) {
    if (!sameBorder((*this->holeArr)[i], (*source->holeArr)[i])) {
      return;
    }
  }
  this->precalculatedConvexPieces = sourcePieces;
}

bool PolylineArea::sameBorder(PolylineShape* first, PolylineShape* second) {
  if (typeid(*first) != typeid(*second) ||
      first->borderLineCount() != second->borderLineCount()) {
    return false;
  }
  for (int32_t i = 0; i < first->borderLineCount(); i++) {
    if (!first->borderLine(i)->equals(second->borderLine(i))) {
      return false;
    }
  }
  return true;
}

jrt::Array<TileShape*>* PolylineArea::splitToConvex() { return splitToConvex(nullptr); }

jrt::Array<TileShape*>* PolylineArea::splitToConvex(Stoppable* stoppableThread) {
  if (precalculatedConvexPieces == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    jrt::Array<TileShape*>* convexBorderPieces = borderShape->splitToConvex();
    if (convexBorderPieces == nullptr) {
      return nullptr;
    }
    jrt::Collection<TileShape*>* currentPieceList = new (jrt::kOp) jrt::LinkedList<TileShape*>();
    for (int32_t i = 0; i < convexBorderPieces->length; i++) {
      currentPieceList->add((*convexBorderPieces)[i]);
    }
    for (int32_t i = 0; i < holeArr->length; i++) {
      if ((*holeArr)[i]->dimension() < 2) {
        FRLogger::warn("PolylineArea. split_to_convex: dimension 2 for hole expected");
        continue;
      }
      jrt::Array<TileShape*>* convexHolePieces = (*holeArr)[i]->splitToConvex();
      if (convexHolePieces == nullptr) {
        return nullptr;
      }
      for (int32_t j = 0; j < convexHolePieces->length; j++) {
        TileShape* currentHolePiece = (*convexHolePieces)[j];
        jrt::Collection<TileShape*>* newPieceList = new (jrt::kOp) jrt::LinkedList<TileShape*>();
        for (TileShape* currentDividePiece : *currentPieceList) {
          if (stoppableThread != nullptr && stoppableThread->isStopRequested()) {
            return nullptr;
          }
          cutoutHolePiece(currentDividePiece, currentHolePiece, newPieceList);
        }
        currentPieceList = newPieceList;
      }
    }
    precalculatedConvexPieces = new (jrt::kGeom) jrt::Array<TileShape*>(currentPieceList->size());
    jrt::Iterator<TileShape*>* it = currentPieceList->iterator();
    for (int32_t i = 0; i < precalculatedConvexPieces->length; i++) {
      (*precalculatedConvexPieces)[i] = it->next();
    }
  }
  return precalculatedConvexPieces;
}

PolylineArea* PolylineArea::turn90Degree(int32_t factor, IntPoint* pole) {
  PolylineShape* newBorder = borderShape->turn90Degree(factor, pole);
  auto* newHoleArr = new (jrt::kGeom) jrt::Array<PolylineShape*>(holeArr->length);
  for (int32_t i = 0; i < newHoleArr->length; i++) {
    (*newHoleArr)[i] = (*holeArr)[i]->turn90Degree(factor, pole);
  }
  return new (jrt::kGeom) PolylineArea(newBorder, newHoleArr);
}

PolylineArea* PolylineArea::rotateApprox(double angle, FloatPoint* pole) {
  PolylineShape* newBorder = borderShape->rotateApprox(angle, pole);
  auto* newHoleArr = new (jrt::kGeom) jrt::Array<PolylineShape*>(holeArr->length);
  for (int32_t i = 0; i < newHoleArr->length; i++) {
    (*newHoleArr)[i] = (*holeArr)[i]->rotateApprox(angle, pole);
  }
  return new (jrt::kGeom) PolylineArea(newBorder, newHoleArr);
}

PolylineArea* PolylineArea::mirrorVertical(IntPoint* pole) {
  PolylineShape* newBorder = borderShape->mirrorVertical(pole);
  auto* newHoleArr = new (jrt::kGeom) jrt::Array<PolylineShape*>(holeArr->length);
  for (int32_t i = 0; i < newHoleArr->length; i++) {
    (*newHoleArr)[i] = (*holeArr)[i]->mirrorVertical(pole);
  }
  return new (jrt::kGeom) PolylineArea(newBorder, newHoleArr);
}

PolylineArea* PolylineArea::mirrorHorizontal(IntPoint* pole) {
  PolylineShape* newBorder = borderShape->mirrorHorizontal(pole);
  auto* newHoleArr = new (jrt::kGeom) jrt::Array<PolylineShape*>(holeArr->length);
  for (int32_t i = 0; i < newHoleArr->length; i++) {
    (*newHoleArr)[i] = (*holeArr)[i]->mirrorHorizontal(pole);
  }
  return new (jrt::kGeom) PolylineArea(newBorder, newHoleArr);
}

}  // namespace fr
