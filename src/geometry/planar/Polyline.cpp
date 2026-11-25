#include "geometry/planar/Polyline.h"

#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/LineSegment.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polygon.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

Polyline::Polyline(Polygon* polygon) {
  jrt::Array<Point*>* points = polygon->cornerArray();
  if (points->length < 2) {
    FRLogger::warn("Polyline: must contain at least 2 different points");
    lines = new (jrt::kGeom) jrt::Array<Line*>(0);
    return;
  }
  lines = new (jrt::kGeom) jrt::Array<Line*>(points->length + 1);
  for (int32_t i = 1; i < points->length; i++) {
    (*lines)[i] = new (jrt::kGeom) Line((*points)[i - 1], (*points)[i]);
  }
  // construct perpendicular lines at the start and at the end to represent
  // the first and the last point of points as intersection of lines.

  (*lines)[0] = endpointCap((*lines)[1], (*points)[0]);
  (*lines)[points->length] =
      endpointCap((*lines)[points->length - 1]->opposite(), (*points)[points->length - 1]);
}

// PORT NOTE (stage E): the temporary polygon is freed when the delegated constructor returns.
Polyline::Polyline(jrt::Array<Point*>* points)
    : Polyline(jrt::DisposeOnExit<Polygon>(new (jrt::kOp) Polygon(points)).get()) {}

Polyline::Polyline(Point* fromCorner, Point* toCorner) {
  if (fromCorner->equals(toCorner)) {
    lines = new (jrt::kGeom) jrt::Array<Line*>(0);
    return;
  }
  lines = new (jrt::kGeom) jrt::Array<Line*>(3);
  (*lines)[1] = new (jrt::kGeom) Line(fromCorner, toCorner);
  (*lines)[0] = endpointCap((*lines)[1], fromCorner);
  (*lines)[2] = endpointCap((*lines)[1], toCorner);
}

Polyline* Polyline::ofNormalizedLines(jrt::Array<Line*>* normalizedLines) {
  auto* p = new (jrt::kGeom) Polyline(new (jrt::kGeom) jrt::Array<Line*>(0));
  p->lines = normalizedLines;
  return p;
}

Polyline::Polyline(jrt::Array<Line*>* inputLines) {
  jrt::Array<Line*>* filteredLines = removeConsecutiveParallelLines(inputLines);
  filteredLines = removeOverlaps(filteredLines);
  if (filteredLines->length < 3) {
    this->lines = new (jrt::kGeom) jrt::Array<Line*>(0);
    return;
  }
  if (filteredLines == inputLines) {
    filteredLines = inputLines->clone();
  }
  precalculatedFloatCorners = new (jrt::kGeom) jrt::Array<FloatPoint*>(filteredLines->length - 1);

  // turn evtl the direction of the lines that they point always
  // from the previous corner to the next corner
  for (int32_t i = 1; i < filteredLines->length - 1; i++) {
    (*precalculatedFloatCorners)[i] =
        (*filteredLines)[i]->intersectionApprox((*filteredLines)[i + 1]);
    Side* sideOfLine = (*filteredLines)[i - 1]->sideOf((*precalculatedFloatCorners)[i]);
    if (sideOfLine != Side::COLLINEAR()) {
      Direction* d0 = (*filteredLines)[i - 1]->direction();
      Direction* d1 = (*filteredLines)[i]->direction();
      Side* side1 = d0->sideOf(d1);
      if (side1 != sideOfLine) {
        (*filteredLines)[i] = (*filteredLines)[i]->opposite();
      }
    }
  }
  this->lines = filteredLines;
}

Line* Polyline::endpointCap(Line* support, Point* point) {
  Line* cap = support->capAt(point);
  if (cap == nullptr) {
    throw jrt::IllegalArgumentException("The polyline endpoint has no bounded integer cap");
  }
  return cap;
}

jrt::Array<Line*>* Polyline::removeConsecutiveParallelLines(jrt::Array<Line*>* lines) {
  if (lines->length < 3) {
    // polyline must have at least 3 lines
    return lines;
  }
  auto* tmpArr = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  int32_t newLength = 0;
  (*tmpArr)[0] = (*lines)[0];
  for (int32_t i = 1; i < lines->length; i++) {
    // skip multiple lines
    if (!(*tmpArr)[newLength]->isParallel((*lines)[i])) {
      ++newLength;
      (*tmpArr)[newLength] = (*lines)[i];
    }
  }
  ++newLength;
  if (newLength == lines->length) {
    // nothing skipped
    return lines;
  }
  // at least 1 line is skipped, adjust the array
  if (newLength < 3) {
    return new (jrt::kGeom) jrt::Array<Line*>(0);
  }
  auto* result = new (jrt::kGeom) jrt::Array<Line*>(newLength);
  jrt::arraycopy(tmpArr, 0, result, 0, newLength);
  return result;
}

jrt::Array<Line*>* Polyline::removeOverlaps(jrt::Array<Line*>* lines) {
  if (lines->length < 4) {
    return lines;
  }
  int32_t newLength = 0;
  auto* tmpArr = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  (*tmpArr)[0] = (*lines)[0];
  if (!(*lines)[0]->isEqualOrOpposite((*lines)[2])) {
    ++newLength;
  }
  // else skip the first line
  (*tmpArr)[newLength] = (*lines)[1];
  ++newLength;
  for (int32_t i = 2; i < lines->length - 2; i++) {
    if (newLength > 0 && (*tmpArr)[newLength - 1]->isEqualOrOpposite((*lines)[i + 1])) {
      // skip 2 lines
      --newLength;
    } else {
      (*tmpArr)[newLength] = (*lines)[i];
      ++newLength;
    }
  }
  (*tmpArr)[newLength] = (*lines)[lines->length - 2];
  ++newLength;
  // Guard: newLength must be >= 2 before accessing tmpArr[newLength - 2].
  // If the loop decremented newLength all the way to 0 the index would be -1.
  if (newLength >= 2 && !(*lines)[lines->length - 1]->isEqualOrOpposite((*tmpArr)[newLength - 2])) {
    (*tmpArr)[newLength] = (*lines)[lines->length - 1];
    ++newLength;
  }
  // else skip the last line
  if (newLength == lines->length) {
    // nothing skipped
    return lines;
  }
  // at least 1 line is skipped, adjust the array
  if (newLength < 3) {
    return new (jrt::kGeom) jrt::Array<Line*>(0);
  }
  auto* result = new (jrt::kGeom) jrt::Array<Line*>(newLength);
  jrt::arraycopy(tmpArr, 0, result, 0, newLength);
  return result;
}

bool Polyline::isEmpty() { return lines->length < 3; }

bool Polyline::isPoint() {
  if (lines->length < 3) {
    return true;
  }
  Point* firstCorner = this->corner(0);
  for (int32_t i = 1; i < lines->length - 1; i++) {
    if (!this->corner(i)->equals(firstCorner)) {
      return false;
    }
  }
  return true;
}

bool Polyline::isOrthogonal() {
  for (int32_t i = 0; i < lines->length; i++) {
    if (!(*lines)[i]->isOrthogonal()) {
      return false;
    }
  }
  return true;
}

bool Polyline::isMultipleOf45Degree() {
  for (int32_t i = 0; i < lines->length; i++) {
    if (!(*lines)[i]->isMultipleOf45Degree()) {
      return false;
    }
  }
  return true;
}

Point* Polyline::firstCorner() { return corner(0); }

Point* Polyline::lastCorner() { return corner(lines->length - 2); }

jrt::Array<Point*>* Polyline::corners() {
  if (lines->length < 2) {
    return new (jrt::kGeom) jrt::Array<Point*>(0);
  }
  if (precalculatedCorners == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    // corner array is not yet allocated
    precalculatedCorners = new (jrt::kGeom) jrt::Array<Point*>(lines->length - 1);
  }
  for (int32_t i = 0; i < precalculatedCorners->length; i++) {
    if ((*precalculatedCorners)[i] == nullptr) {
      jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
      (*precalculatedCorners)[i] = (*lines)[i]->intersection((*lines)[i + 1]);
    }
  }
  return precalculatedCorners;
}

jrt::Array<FloatPoint*>* Polyline::cornerApproxArr() {
  if (lines->length < 2) {
    return new (jrt::kGeom) jrt::Array<FloatPoint*>(0);
  }
  if (precalculatedFloatCorners == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    // corner array is not yet allocated
    precalculatedFloatCorners = new (jrt::kGeom) jrt::Array<FloatPoint*>(lines->length - 1);
  }
  for (int32_t i = 0; i < precalculatedFloatCorners->length; i++) {
    if ((*precalculatedFloatCorners)[i] == nullptr) {
      jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
      (*precalculatedFloatCorners)[i] = (*lines)[i]->intersectionApprox((*lines)[i + 1]);
    }
  }
  return precalculatedFloatCorners;
}

FloatPoint* Polyline::cornerApprox(int32_t cornerIndex) {
  int32_t no;
  if (cornerIndex < 0) {
    FRLogger::warn("Polyline.corner_approx: no is < 0");
    no = 0;
  } else if (cornerIndex >= lines->length - 1) {
    FRLogger::warn("Polyline.corner_approx: no must be less than lines.length - 1");
    no = lines->length - 2;
  } else {
    no = cornerIndex;
  }
  if (precalculatedFloatCorners == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    // corner array is not yet allocated
    precalculatedFloatCorners = new (jrt::kGeom) jrt::Array<FloatPoint*>(lines->length - 1);
  }
  if ((*precalculatedFloatCorners)[no] == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    // corner is not yet calculated
    (*precalculatedFloatCorners)[no] = (*lines)[no]->intersectionApprox((*lines)[no + 1]);
  }
  return (*precalculatedFloatCorners)[no];
}

Point* Polyline::corner(int32_t cornerIndex) {
  if (lines->length < 2) {
    if (FRLogger::isTraceEnabled()) {
      FRLogger::trace("Polyline.corner: lines.length is < 2");
    }
    return nullptr;
  }
  int32_t no;
  if (cornerIndex < 0) {
    FRLogger::warn("Polyline.corner: no is < 0");
    no = 0;
  } else if (cornerIndex >= lines->length - 1) {
    FRLogger::warn("Polyline.corner: no must be less than lines.length - 1");
    no = lines->length - 2;
  } else {
    no = cornerIndex;
  }
  if (precalculatedCorners == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    // corner array is not yet allocated
    precalculatedCorners = new (jrt::kGeom) jrt::Array<Point*>(lines->length - 1);
  }
  if ((*precalculatedCorners)[no] == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    // corner is not yet calculated
    (*precalculatedCorners)[no] = (*lines)[no]->intersection((*lines)[no + 1]);
  }
  return (*precalculatedCorners)[no];
}

Polyline* Polyline::reverse() {
  auto* reversedLines = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  for (int32_t i = 0; i < lines->length; i++) {
    (*reversedLines)[i] = (*lines)[lines->length - i - 1]->opposite();
  }
  return new (jrt::kGeom) Polyline(reversedLines);
}

double Polyline::lengthApprox(int32_t requestedFromCorner, int32_t requestedToCorner) {
  int32_t fromCorner = jrt::Math::max(requestedFromCorner, 0);
  int32_t toCorner = jrt::Math::min(requestedToCorner, lines->length - 2);
  double result = 0;
  for (int32_t i = fromCorner; i < toCorner; i++) {
    result += this->cornerApprox(i + 1)->distance(this->cornerApprox(i));
  }
  return result;
}

double Polyline::lengthApprox() {
  if (precalculatedLength < 0) {
    precalculatedLength = lengthApprox(0, lines->length - 2);
  }
  return precalculatedLength;
}

jrt::Array<TileShape*>* Polyline::offsetShapes(int32_t halfWidth) {
  return offsetShapes(halfWidth, 0, lines->length - 1);
}

jrt::Array<TileShape*>* Polyline::offsetShapes(int32_t halfWidth, int32_t requestedFromNo,
                                               int32_t requestedToNo) {
  int32_t fromNo = jrt::Math::max(requestedFromNo, 0);
  int32_t toNo = jrt::Math::min(requestedToNo, this->lines->length - 1);
  int32_t shapeCount = jrt::Math::max(toNo - fromNo - 1, 0);
  auto* shapes = new (jrt::kGeom) jrt::Array<TileShape*>(shapeCount);
  if (shapeCount == 0) {
    return shapes;
  }
  Vector* prevDir = (*this->lines)[fromNo]->direction()->getVector();
  Vector* currentDirection = (*this->lines)[fromNo + 1]->direction()->getVector();
  for (int32_t i = fromNo + 1; i < toNo; i++) {
    Vector* nextDir = (*this->lines)[i + 1]->direction()->getVector();

    auto* offsetLines = new (jrt::kGeom) jrt::Array<Line*>(4);

    (*offsetLines)[0] = (*this->lines)[i]->translate(-halfWidth);
    // current center line translated to the right

    // create the front line of the offset shape
    Side* nextDirFromCurrDir = nextDir->sideOf(currentDirection);
    // left turn from currentLine to nextLine
    if (nextDirFromCurrDir == Side::ON_THE_LEFT()) {
      (*offsetLines)[1] = (*this->lines)[i + 1]->translate(-halfWidth);
      // next right line
    } else {
      (*offsetLines)[1] = (*this->lines)[i + 1]->opposite()->translate(-halfWidth);
      // next left line in opposite direction
    }

    (*offsetLines)[2] = (*this->lines)[i]->opposite()->translate(-halfWidth);
    // current left line in opposite direction

    // create the back line of the offset shape
    Side* currentDirFromPrevDir = currentDirection->sideOf(prevDir);
    // left turn from prevLine to currentLine
    if (currentDirFromPrevDir == Side::ON_THE_LEFT()) {
      (*offsetLines)[3] = (*this->lines)[i - 1]->translate(-halfWidth);
      // previous line translated to the right
    } else {
      (*offsetLines)[3] = (*this->lines)[i - 1]->opposite()->translate(-halfWidth);
      // previous left line in opposite direction
    }
    // cut off outstanding corners with following shapes
    FloatPoint* cornerToCheck = nullptr;
    Line* currentLine = (*offsetLines)[1];
    Line* checkLine;
    if (nextDirFromCurrDir == Side::ON_THE_LEFT()) {
      checkLine = (*offsetLines)[2];
    } else {
      checkLine = (*offsetLines)[0];
    }
    FloatPoint* checkDistanceCorner = cornerApprox(i);
    const double checkDistSquare = 2.0 * halfWidth * halfWidth;
    jrt::Collection<Line*>* cutDogEarLines = new (jrt::kOp) jrt::LinkedList<Line*>();
    Vector* tmpCurrDir = nextDir;
    bool directionChanged = false;
    for (int32_t j = i + 2; j < this->lines->length - 1; j++) {
      if (cornerApprox(j - 1)->distanceSquare(checkDistanceCorner) > checkDistSquare) {
        break;
      }
      if (!directionChanged) {
        cornerToCheck = currentLine->intersectionApprox(checkLine);
      }
      Vector* tmpNextDir = (*this->lines)[j]->direction()->getVector();
      Line* nextBorderLine;
      Side* tmpNextDirFromTmpCurrDir = tmpNextDir->sideOf(tmpCurrDir);
      directionChanged = tmpNextDirFromTmpCurrDir != nextDirFromCurrDir;
      if (!directionChanged) {
        if (tmpNextDirFromTmpCurrDir == Side::ON_THE_LEFT()) {
          nextBorderLine = (*this->lines)[j]->translate(-halfWidth);
        } else {
          nextBorderLine = (*this->lines)[j]->opposite()->translate(-halfWidth);
        }

        if (nextBorderLine->sideOf(cornerToCheck) == Side::ON_THE_LEFT() &&
            nextBorderLine->sideOf(this->corner(i)) == Side::ON_THE_RIGHT() &&
            nextBorderLine->sideOf(this->corner(i - 1)) == Side::ON_THE_RIGHT()) {
          // an outstanding corner
          cutDogEarLines->add(nextBorderLine);
        }
        tmpCurrDir = tmpNextDir;
        currentLine = nextBorderLine;
      }
    }
    // cut off outstanding corners with previous shapes
    checkDistanceCorner = cornerApprox(i - 1);
    if (currentDirFromPrevDir == Side::ON_THE_LEFT()) {
      checkLine = (*offsetLines)[2];
    } else {
      checkLine = (*offsetLines)[0];
    }
    currentLine = (*offsetLines)[3];
    tmpCurrDir = prevDir;
    directionChanged = false;
    for (int32_t j = i - 2; j >= 1; j--) {
      if (cornerApprox(j)->distanceSquare(checkDistanceCorner) > checkDistSquare) {
        break;
      }
      if (!directionChanged) {
        cornerToCheck = currentLine->intersectionApprox(checkLine);
      }
      Vector* tmpPrevDir = (*this->lines)[j]->direction()->getVector();
      Line* prevBorderLine;
      Side* tmpCurrDirFromTmpPrevDir = tmpCurrDir->sideOf(tmpPrevDir);
      directionChanged = tmpCurrDirFromTmpPrevDir != currentDirFromPrevDir;
      if (!directionChanged) {
        if (tmpCurrDir->sideOf(tmpPrevDir) == Side::ON_THE_LEFT()) {
          prevBorderLine = (*this->lines)[j]->translate(-halfWidth);
        } else {
          prevBorderLine = (*this->lines)[j]->opposite()->translate(-halfWidth);
        }
        if (prevBorderLine->sideOf(cornerToCheck) == Side::ON_THE_LEFT() &&
            prevBorderLine->sideOf(this->corner(i)) == Side::ON_THE_RIGHT() &&
            prevBorderLine->sideOf(this->corner(i - 1)) == Side::ON_THE_RIGHT()) {
          // an outstanding corner
          cutDogEarLines->add(prevBorderLine);
        }
        tmpCurrDir = tmpPrevDir;
        currentLine = prevBorderLine;
      }
    }
    TileShape* s1 = TileShape::getInstance(offsetLines);
    int32_t cutLineCount = cutDogEarLines->size();
    if (cutLineCount > 0) {
      auto* cutLines = new (jrt::kGeom) jrt::Array<Line*>(cutLineCount);
      jrt::Iterator<Line*>* it = cutDogEarLines->iterator();
      for (int32_t j = 0; j < cutLineCount; j++) {
        (*cutLines)[j] = it->next();
      }
      s1 = s1->intersection(TileShape::getInstance(cutLines));
    }
    int32_t currentShapeNo = i - fromNo - 1;
    TileShape* boundingShape;
    if (USE_BOUNDING_OCTAGON_FOR_OFFSET_SHAPES) {
      // intersect with the bounding octagon
      IntOctagon* surrOct = boundingOctagon(i - 1, i);
      boundingShape = surrOct->offset(halfWidth);

    } else {
      // intersect with the bounding box
      IntBox* surrBox = boundingBox(i - 1, i);
      IntBox* offsetBox = surrBox->offset(halfWidth);
      boundingShape = offsetBox->toSimplex();
    }
    (*shapes)[currentShapeNo] = boundingShape->intersectionWithSimplify(s1);
    if ((*shapes)[currentShapeNo]->isEmpty()) {
      FRLogger::warn("offset_shapes: shape is empty");
    }

    prevDir = currentDirection;
    currentDirection = nextDir;
  }
  return shapes;
}

TileShape* Polyline::offsetShape(int32_t halfWidth, int32_t no) {
  if (no < 0 || no > lines->length - 3) {
    FRLogger::warn("Polyline.offsetShape: no out of range");
    return nullptr;
  }
  jrt::Array<TileShape*>* result = offsetShapes(halfWidth, no, no + 2);
  return (*result)[0];
}

IntBox* Polyline::offsetBox(int32_t halfWidth, int32_t no) {
  LineSegment* currentLineSegment = new (jrt::kGeom) LineSegment(this, no + 1);
  return currentLineSegment->boundingBox()->offset(halfWidth);
}

Polyline* Polyline::translateBy(Vector* vector) {
  if (vector->equals(Vector::ZERO())) {
    return this;
  }
  auto* newArray = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  for (int32_t i = 0; i < newArray->length; i++) {
    (*newArray)[i] = (*lines)[i]->translateBy(vector);
  }
  return new (jrt::kGeom) Polyline(newArray);
}

Polyline* Polyline::turn90Degree(int32_t factor, IntPoint* pole) {
  auto* newArray = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  for (int32_t i = 0; i < newArray->length; i++) {
    (*newArray)[i] = (*lines)[i]->turn90Degree(factor, pole);
  }
  return new (jrt::kGeom) Polyline(newArray);
}

Polyline* Polyline::rotateApprox(double angle, FloatPoint* pole) {
  if (angle == 0) {
    return this;
  }
  // PORT NOTE: Java builds an IntPoint[] and passes it as Point[] (array covariance).
  auto* newCorners = new (jrt::kGeom) jrt::Array<Point*>(this->cornerCount());
  for (int32_t i = 0; i < newCorners->length; i++) {
    (*newCorners)[i] = this->cornerApprox(i)->rotate(angle, pole)->round();
  }
  return new (jrt::kGeom) Polyline(newCorners);
}

Polyline* Polyline::mirrorVertical(IntPoint* pole) {
  auto* newArray = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  for (int32_t i = 0; i < newArray->length; i++) {
    (*newArray)[i] = (*lines)[i]->mirrorVertical(pole);
  }
  return new (jrt::kGeom) Polyline(newArray);
}

Polyline* Polyline::mirrorHorizontal(IntPoint* pole) {
  auto* newArray = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  for (int32_t i = 0; i < newArray->length; i++) {
    (*newArray)[i] = (*lines)[i]->mirrorHorizontal(pole);
  }
  return new (jrt::kGeom) Polyline(newArray);
}

IntBox* Polyline::boundingBox(int32_t requestedFromCornerNo, int32_t requestedToCornerNo) {
  int32_t fromCornerNo = jrt::Math::max(requestedFromCornerNo, 0);
  int32_t toCornerNo = jrt::Math::min(requestedToCornerNo, lines->length - 2);
  double llx = jrt::Integer::MAX_VALUE;
  double lly = llx;
  double urx = jrt::Integer::MIN_VALUE;
  double ury = urx;
  for (int32_t i = fromCornerNo; i <= toCornerNo; i++) {
    FloatPoint* currentCorner = cornerApprox(i);
    llx = jrt::Math::min(llx, currentCorner->x);
    lly = jrt::Math::min(lly, currentCorner->y);
    urx = jrt::Math::max(urx, currentCorner->x);
    ury = jrt::Math::max(ury, currentCorner->y);
  }
  IntPoint* lowerLeft =
      new (jrt::kGeom) IntPoint(jrt::d2i(jrt::Math::floor(llx)), jrt::d2i(jrt::Math::floor(lly)));
  IntPoint* upperRight =
      new (jrt::kGeom) IntPoint(jrt::d2i(jrt::Math::ceil(urx)), jrt::d2i(jrt::Math::ceil(ury)));
  return new (jrt::kGeom) IntBox(lowerLeft, upperRight);
}

IntBox* Polyline::boundingBox() {
  if (precalculatedBoundingBox == nullptr) {
    jrt::DomainScope domain(this);  // PORT NOTE (stage E): cache in the holder's domain
    precalculatedBoundingBox = boundingBox(0, cornerCount() - 1);
  }
  return precalculatedBoundingBox;
}

IntOctagon* Polyline::boundingOctagon(int32_t requestedFromCornerNo,
                                      int32_t requestedToCornerNo) {
  int32_t fromCornerNo = jrt::Math::max(requestedFromCornerNo, 0);
  int32_t toCornerNo = jrt::Math::min(requestedToCornerNo, lines->length - 2);
  double lx = jrt::Integer::MAX_VALUE;
  double ly = jrt::Integer::MAX_VALUE;
  double rx = jrt::Integer::MIN_VALUE;
  double uy = jrt::Integer::MIN_VALUE;
  double ulx = jrt::Integer::MAX_VALUE;
  double lrx = jrt::Integer::MIN_VALUE;
  double llx = jrt::Integer::MAX_VALUE;
  double urx = jrt::Integer::MIN_VALUE;
  for (int32_t i = fromCornerNo; i <= toCornerNo; i++) {
    FloatPoint* current = cornerApprox(i);
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
  return new (jrt::kGeom) IntOctagon(jrt::d2i(jrt::Math::floor(lx)), jrt::d2i(jrt::Math::floor(ly)),
                        jrt::d2i(jrt::Math::ceil(rx)), jrt::d2i(jrt::Math::ceil(uy)),
                        jrt::d2i(jrt::Math::floor(ulx)), jrt::d2i(jrt::Math::ceil(lrx)),
                        jrt::d2i(jrt::Math::floor(llx)), jrt::d2i(jrt::Math::ceil(urx)));
}

FloatPoint* Polyline::nearestPointApprox(FloatPoint* fromPoint) {
  double minDistance = jrt::Double::MAX_VALUE;
  FloatPoint* nearestPoint = nullptr;
  // calculate the nearest corner point
  jrt::Array<FloatPoint*>* corners = cornerApproxArr();
  for (int32_t i = 0; i < corners->length; i++) {
    double currentDistance = (*corners)[i]->distance(fromPoint);
    if (currentDistance < minDistance) {
      minDistance = currentDistance;
      nearestPoint = (*corners)[i];
    }
  }
  const double ctolerance = 1;
  for (int32_t i = 1; i < lines->length - 1; i++) {
    FloatPoint* projection = fromPoint->projectionApprox((*lines)[i]);
    double currentDistance = projection->distance(fromPoint);
    if (currentDistance < minDistance) {
      // look, if the projection is inside the segment
      double segmentLength = (*corners)[i]->distance((*corners)[i - 1]);
      if (projection->distance((*corners)[i]) + projection->distance((*corners)[i - 1]) <
          segmentLength + ctolerance) {
        minDistance = currentDistance;
        nearestPoint = projection;
      }
    }
  }
  return nearestPoint;
}

double Polyline::distance(FloatPoint* fromPoint) {
  return fromPoint->distance(nearestPointApprox(fromPoint));
}

Polyline* Polyline::combine(Polyline* other) {
  if (other == nullptr || lines->length < 3 || other->lines->length < 3) {
    return this;
  }
  bool combineAtStart;
  bool combineOtherAtStart;
  if (firstCorner()->equals(other->firstCorner())) {
    combineAtStart = true;
    combineOtherAtStart = true;
  } else if (firstCorner()->equals(other->lastCorner())) {
    combineAtStart = true;
    combineOtherAtStart = false;
  } else if (lastCorner()->equals(other->firstCorner())) {
    combineAtStart = false;
    combineOtherAtStart = true;
  } else if (lastCorner()->equals(other->lastCorner())) {
    combineAtStart = false;
    combineOtherAtStart = false;
  } else {
    return this;  // no common endpoint
  }
  auto* newLines = new (jrt::kGeom) jrt::Array<Line*>(this->lines->length + other->lines->length - 2);
  if (combineAtStart) {
    // insert the lines of other in front
    if (combineOtherAtStart) {
      // insert in reverse order, skip the first line of other
      for (int32_t i = 0; i < other->lines->length - 1; i++) {
        (*newLines)[i] = (*other->lines)[other->lines->length - i - 1]->opposite();
      }
    } else {
      // skip the last line of other
      jrt::arraycopy(other->lines, 0, newLines, 0, other->lines->length - 1);
    }
    // append the lines of this polyline, skip the first line
    jrt::arraycopy(this->lines, 1, newLines, other->lines->length - 1, this->lines->length - 1);
  } else {
    // insert the lines of this polyline in front, skip the last line
    jrt::arraycopy(this->lines, 0, newLines, 0, this->lines->length - 1);
    if (combineOtherAtStart) {
      // skip the first line of other
      jrt::arraycopy(other->lines, 1, newLines, this->lines->length - 1, other->lines->length - 1);
    } else {
      // insert in reverse order, skip the last line of other
      for (int32_t i = 1; i < other->lines->length; i++) {
        (*newLines)[this->lines->length + i - 2] =
            (*other->lines)[other->lines->length - i - 1]->opposite();
      }
    }
  }
  return new (jrt::kGeom) Polyline(newLines);
}

jrt::Array<Polyline*>* Polyline::split(int32_t lineIndex, Line* endLine) {
  if (lineIndex < 1 || lineIndex > lines->length - 2) {
    FRLogger::warn("Polyline.split: lineIndex out of range");
    return nullptr;
  }
  if ((*this->lines)[lineIndex]->isParallel(endLine)) {
    return nullptr;
  }
  Point* newEndCorner = (*this->lines)[lineIndex]->intersection(endLine);
  if (FRLogger::isGranularTraceActive()) {
    FRLogger::trace(
        "Polyline.split", "compare_trace_split_called",
        "lineIndex=" + jrt::str(lineIndex) + ", lines.length=" + jrt::str(lines->length) +
            ", lines.length-2=" + jrt::str(lines->length - 2) +
            ", newEndCorner=" + debugPoint(newEndCorner) + " (type=" +
            jrt::className(newEndCorner) + ")" + ", lastCorner=" +
            debugPoint(this->lastCorner()) + " (type=" + jrt::className(this->lastCorner()) +
            ")" + ", equals=" + jrt::str(newEndCorner->equals(this->lastCorner())),
        "Polyline split lineIndex=" + jrt::str(lineIndex),
        jrt::Array<Point*>::of({this->firstCorner(), newEndCorner, this->lastCorner()}));
  }
  if (FRLogger::isGranularTraceActive()) {
    std::string sb = "    CORNERS:";
    for (int32_t i = 0; i < this->cornerCount(); i++) {
      sb += " ";
      sb += jrt::str(this->cornerApprox(i));
    }
    FRLogger::trace(
        "Polyline.split", "compare_trace_split_corners", sb,
        "Polyline split lineIndex=" + jrt::str(lineIndex),
        jrt::Array<Point*>::of({this->firstCorner(), newEndCorner, this->lastCorner()}));
  }
  if ((lineIndex == 1 && newEndCorner->equals(this->firstCorner())) ||
      (lineIndex >= lines->length - 2 && newEndCorner->equals(this->lastCorner()))) {
    // No split, if endLine does not intersect, but touches
    // only this Polyline at an end point.
    return nullptr;
  }
  jrt::Array<Line*>* firstPiece;
  if (this->corner(lineIndex - 1)->equals(newEndCorner)) {
    // skip line segment of length 0 at the end of the first piece
    firstPiece = new (jrt::kGeom) jrt::Array<Line*>(lineIndex + 1);
    jrt::arraycopy(this->lines, 0, firstPiece, 0, firstPiece->length);

  } else {
    firstPiece = new (jrt::kGeom) jrt::Array<Line*>(lineIndex + 2);
    jrt::arraycopy(this->lines, 0, firstPiece, 0, lineIndex + 1);
    (*firstPiece)[lineIndex + 1] = endLine;
  }
  jrt::Array<Line*>* secondPiece;
  if (this->corner(lineIndex)->equals(newEndCorner)) {
    // skip line segment of length 0 at the beginning of the second piece
    secondPiece = new (jrt::kGeom) jrt::Array<Line*>(lines->length - lineIndex);
    jrt::arraycopy(this->lines, lineIndex, secondPiece, 0, secondPiece->length);

  } else {
    secondPiece = new (jrt::kGeom) jrt::Array<Line*>(lines->length - lineIndex + 1);
    (*secondPiece)[0] = endLine;
    jrt::arraycopy(this->lines, lineIndex, secondPiece, 1, secondPiece->length - 1);
  }
  auto* result = new (jrt::kGeom) jrt::Array<Polyline*>(2);
  (*result)[0] = new (jrt::kGeom) Polyline(firstPiece);
  (*result)[1] = new (jrt::kGeom) Polyline(secondPiece);
  if ((*result)[0]->isPoint() || (*result)[1]->isPoint()) {
    return nullptr;
  }
  return result;
}

Polyline* Polyline::skipLines(int32_t fromNo, int32_t toNo) {
  if (fromNo < 0 || toNo > lines->length - 1 || fromNo > toNo) {
    return this;
  }
  auto* newLines = new (jrt::kGeom) jrt::Array<Line*>(lines->length - (toNo - fromNo + 1));
  jrt::arraycopy(this->lines, 0, newLines, 0, fromNo);
  jrt::arraycopy(this->lines, toNo + 1, newLines, fromNo, newLines->length - fromNo);
  return new (jrt::kGeom) Polyline(newLines);
}

bool Polyline::contains(Point* point) {
  for (int32_t i = 1; i < lines->length - 1; i++) {
    LineSegment* currentSegment = new (jrt::kGeom) LineSegment(this, i);
    if (currentSegment->contains(point)) {
      return true;
    }
  }
  return false;
}

std::string Polyline::debugPoint(Point* point) {
  if (auto* intPoint = jrt::fast_cast<IntPoint>(point)) {
    return "(" + jrt::str(intPoint->x) + "," + jrt::str(intPoint->y) + ")";
  }
  return jrt::str(point);
}

LineSegment* Polyline::projectionLine(Point* point) {
  if (point == nullptr) {
    FRLogger::warn(
        "Polyline.projectionLine: fromPoint is null; returning null. "
        "This indicates a degenerate routing connection was attempted with an "
        "uninitialized endpoint.");
    return nullptr;
  }
  FloatPoint* fromPoint = point->toFloat();
  double minDistance = jrt::Double::MAX_VALUE;
  Line* resultLine = nullptr;
  Line* nearestLine = nullptr;
  for (int32_t i = 1; i < lines->length - 1; i++) {
    FloatPoint* projection = fromPoint->projectionApprox((*lines)[i]);
    double currentDistance = projection->distance(fromPoint);
    if (currentDistance < minDistance) {
      Direction* directionTowardsLine = (*this->lines)[i]->perpendicularDirection(point);
      if (directionTowardsLine == nullptr) {
        continue;
      }
      Line* currentResultLine;
      try {
        currentResultLine = new (jrt::kGeom) Line(point, directionTowardsLine);
      } catch (const jrt::IllegalArgumentException& unsupportedProjection) {
        continue;
      }
      Point* prevCorner = this->corner(i - 1);
      Point* nextCorner = this->corner(i);
      Side* prevCornerSide = currentResultLine->sideOf(prevCorner);
      Side* nextCornerSide = currentResultLine->sideOf(nextCorner);
      if (prevCornerSide == nextCornerSide && prevCornerSide != Side::COLLINEAR()) {
        // the projection point is outside the line segment
        continue;
      }
      nearestLine = (*this->lines)[i];
      minDistance = currentDistance;
      resultLine = currentResultLine;
    }
  }
  if (nearestLine == nullptr) {
    return nullptr;
  }
  Line* startLine = jrt::fast_cast<IntPoint>(point) != nullptr
                        ? new (jrt::kGeom) Line(point, nearestLine->direction())
                        : resultLine->capAt(point);
  if (startLine == nullptr) {
    return nullptr;
  }
  return new (jrt::kGeom) LineSegment(startLine, resultLine, nearestLine);
}

Polyline* Polyline::shorten(int32_t newLineCount, double lastSegmentLength) {
  FloatPoint* lastCorner = this->cornerApprox(newLineCount - 2);
  FloatPoint* prevLastCorner = this->cornerApprox(newLineCount - 3);
  IntPoint* newLastCorner = prevLastCorner->changeLength(lastCorner, lastSegmentLength)->round();
  if (newLastCorner->equals(this->corner(this->cornerCount() - 2))) {
    // skip the last line
    return skipLines(newLineCount - 1, newLineCount - 1);
  }
  auto* newLines = new (jrt::kGeom) jrt::Array<Line*>(newLineCount);
  jrt::arraycopy(this->lines, 0, newLines, 0, newLineCount - 2);
  // create the last 2 lines of the new polyline
  Point* firstLinePoint = (*lines)[newLineCount - 2]->a;
  if (firstLinePoint->equals(newLastCorner)) {
    firstLinePoint = (*lines)[newLineCount - 2]->b;
  }
  Line* newPrevLastLine = new (jrt::kGeom) Line(firstLinePoint, static_cast<Point*>(newLastCorner));
  newLines->set(newLineCount - 2, newPrevLastLine);
  (*newLines)[newLineCount - 1] =
      Line::getInstance(newLastCorner, newPrevLastLine->direction()->turn45Degree(6));
  return new (jrt::kGeom) Polyline(newLines);
}

}  // namespace fr
