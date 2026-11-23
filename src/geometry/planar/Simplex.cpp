#include "geometry/planar/Simplex.h"

#include "geometry/planar/Circle.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/ShapeBoundingDirections.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {
// Java Arrays.sort(Line[]) uses the natural order Line.compareTo (stable).
void sortLines(jrt::Array<Line*>* lines) {
  jrt::Arrays::sort<Line*>(lines, [](Line* a, Line* b) { return a->compareTo(b); });
}
}  // namespace

Simplex* Simplex::EMPTY() {
  static Simplex* v = new (jrt::kGeom) Simplex(new (jrt::kGeom) jrt::Array<Line*>(0));
  return v;
}

Simplex* Simplex::getInstance(jrt::Array<Line*>* lines) {
  if (lines->length == 0) {
    return Simplex::EMPTY();
  }
  auto* currentArr = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  jrt::arraycopy(lines, 0, currentArr, 0, lines->length);
  // sort the lines in ascending direction
  sortLines(currentArr);
  Simplex* currentSimplex = new (jrt::kGeom) Simplex(currentArr);
  return currentSimplex->removeRedundantLines();
}

bool Simplex::isEmpty() { return lines->length == 0; }

TileShape* Simplex::simplify() {
  TileShape* result = this;
  if (this->isEmpty()) {
    result = Simplex::EMPTY();
  } else if (this->isIntBox()) {
    result = this->boundingBox();
  } else if (this->isIntOctagon()) {
    result = this->toIntOctagon();
  }
  return result;
}

int32_t Simplex::getId() {
  int32_t result = 0;
  for (Line* current : *lines) {
    result = 31 * result + current->getId();
  }
  return result;
}

bool Simplex::cornerIsBounded(int32_t cornerIndex) {
  int32_t no;
  if (cornerIndex < 0) {
    FRLogger::warn("corner: no is < 0");
    no = 0;
  } else if (cornerIndex >= lines->length) {
    FRLogger::warn("corner: index must be less than lines.length - 1");
    no = lines->length - 1;
  } else {
    no = cornerIndex;
  }
  if (lines->length == 1) {
    return false;
  }
  int32_t prevNo;
  if (no == 0) {
    prevNo = lines->length - 1;
  } else {
    prevNo = no - 1;
  }
  Direction* prevLineDir = (*lines)[prevNo]->direction();
  Direction* currentLineDir = (*lines)[no]->direction();
  // PORT NOTE: integer directions are used directly instead of through new IntVectors (same
  // value as IntVector::determinant).
  auto* prevInt = jrt::fast_cast<IntDirection>(prevLineDir);
  auto* currentInt = jrt::fast_cast<IntDirection>(currentLineDir);
  if (prevInt != nullptr && currentInt != nullptr) {
    return static_cast<int64_t>(prevInt->x) * currentInt->y -
               static_cast<int64_t>(prevInt->y) * currentInt->x >
           0;
  }
  IntVector* prevDir = jrt::fast_cast<IntVector>(prevLineDir->getVector());
  IntVector* currentDirection = jrt::fast_cast<IntVector>(currentLineDir->getVector());
  return prevDir->determinant(currentDirection) > 0;
}

bool Simplex::isBounded() {
  if (lines->length == 0) {
    return true;
  }
  if (lines->length < 3) {
    return false;
  }
  for (int32_t i = 0; i < lines->length; i++) {
    if (!cornerIsBounded(i)) {
      return false;
    }
  }
  return true;
}

int32_t Simplex::borderLineCount() { return lines->length; }

Point* Simplex::corner(int32_t cornerIndex) {
  int32_t no;
  if (cornerIndex < 0) {
    FRLogger::warn("Simplex.corner: no is < 0");
    no = 0;
  } else if (cornerIndex >= lines->length) {
    FRLogger::warn("Simplex.corner: no must be less than lines.length - 1");
    no = lines->length - 1;
  } else {
    no = cornerIndex;
  }
  if (precalculatedCorners == nullptr) {
    // corner array is not yet allocated
    precalculatedCorners = new (jrt::kGeom) jrt::Array<Point*>(lines->length);
  }
  if ((*precalculatedCorners)[no] == nullptr) {
    // corner is not yet calculated
    Line* prev;
    if (no == 0) {
      prev = (*lines)[lines->length - 1];
    } else {
      prev = (*lines)[no - 1];
    }
    (*precalculatedCorners)[no] = (*lines)[no]->intersection(prev);
  }
  return (*precalculatedCorners)[no];
}

FloatPoint* Simplex::cornerApprox(int32_t cornerIndex) {
  if (lines->length == 0) {
    return nullptr;
  }
  int32_t no;
  if (cornerIndex < 0) {
    FRLogger::warn("Simplex.corner_approx: no is < 0");
    no = 0;
  } else if (cornerIndex >= lines->length) {
    FRLogger::warn("Simplex.corner_approx: no must be less than lines.length - 1");
    no = lines->length - 1;
  } else {
    no = cornerIndex;
  }
  if (precalculatedFloatCorners == nullptr) {
    // corner array is not yet allocated
    precalculatedFloatCorners = new (jrt::kGeom) jrt::Array<FloatPoint*>(lines->length);
  }
  if ((*precalculatedFloatCorners)[no] == nullptr) {
    // corner is not yet calculated
    Line* prev;
    if (no == 0) {
      prev = (*lines)[lines->length - 1];
    } else {
      prev = (*lines)[no - 1];
    }
    (*precalculatedFloatCorners)[no] = (*lines)[no]->intersectionApprox(prev);
  }
  return (*precalculatedFloatCorners)[no];
}

jrt::Array<FloatPoint*>* Simplex::cornerApproxArr() {
  if (precalculatedFloatCorners == nullptr) {
    // corner array is not yet allocated
    precalculatedFloatCorners = new (jrt::kGeom) jrt::Array<FloatPoint*>(lines->length);
  }
  for (int32_t i = 0; i < precalculatedFloatCorners->length; i++) {
    if ((*precalculatedFloatCorners)[i] == nullptr) {
      // corner is not yet calculated
      Line* prev;
      if (i == 0) {
        prev = (*lines)[lines->length - 1];
      } else {
        prev = (*lines)[i - 1];
      }
      (*precalculatedFloatCorners)[i] = (*lines)[i]->intersectionApprox(prev);
    }
  }
  return precalculatedFloatCorners->clone();
}

Line* Simplex::borderLine(int32_t edgeIndex) {
  if (lines->length == 0) {
    FRLogger::warn("Simplex.borderLine : simplex is empty");
    return nullptr;
  }
  int32_t no;
  if (edgeIndex < 0) {
    FRLogger::warn("Simplex.borderLine : no is < 0");
    no = 0;
  } else if (edgeIndex >= lines->length) {
    FRLogger::warn("Simplex.borderLine: no must be less than lines.length - 1");
    no = lines->length - 1;
  } else {
    no = edgeIndex;
  }
  return (*lines)[no];
}

int32_t Simplex::dimension() {
  if (lines->length == 0) {
    return -1;
  }
  if (lines->length > 4) {
    return 2;
  }
  if (lines->length == 1) {
    // we have a half plane
    return 2;
  }
  if (lines->length == 2) {
    if ((*lines)[0]->overlaps((*lines)[1])) {
      return 1;
    }
    return 2;
  }
  if (lines->length == 3) {
    if ((*lines)[0]->overlaps((*lines)[1]) || (*lines)[0]->overlaps((*lines)[2]) ||
        (*lines)[1]->overlaps((*lines)[2])) {
      // simplex is 1 dimensional and unbounded at one side
      return 1;
    }
    Point* intersection = (*lines)[1]->intersection((*lines)[2]);
    Side* sideOfLine0 = (*lines)[0]->sideOf(intersection);
    if (sideOfLine0 == Side::ON_THE_RIGHT()) {
      return 2;
    }
    if (sideOfLine0 == Side::ON_THE_LEFT()) {
      FRLogger::debug("empty Simplex not normalized");
      return -1;
    }
    // now the 3 lines intersect in the same point
    return 0;
  }
  // now the simplex has 4 edge lines
  // check if opposing lines are collinear
  bool collinear02 = (*lines)[0]->overlaps((*lines)[2]);
  bool collinear13 = (*lines)[1]->overlaps((*lines)[3]);
  if (collinear02 && collinear13) {
    return 0;
  }
  if (collinear02 || collinear13) {
    return 1;
  }
  return 2;
}

double Simplex::maxWidth() {
  if (!this->isBounded()) {
    return jrt::Integer::MAX_VALUE;
  }
  double maxDistance = jrt::Integer::MIN_VALUE;
  double maxDistance2 = jrt::Integer::MIN_VALUE;
  FloatPoint* gravityPoint = this->centreOfGravity();

  for (int32_t i = 0; i < borderLineCount(); i++) {
    double currentDistance = jrt::Math::abs((*lines)[i]->signedDistance(gravityPoint));

    if (currentDistance > maxDistance) {
      maxDistance2 = maxDistance;
      maxDistance = currentDistance;
    } else if (currentDistance > maxDistance2) {
      maxDistance2 = currentDistance;
    }
  }
  return maxDistance + maxDistance2;
}

double Simplex::minWidth() {
  if (!this->isBounded()) {
    return jrt::Integer::MAX_VALUE;
  }
  double minDistance = jrt::Integer::MAX_VALUE;
  double minDistance2 = jrt::Integer::MAX_VALUE;
  FloatPoint* gravityPoint = this->centreOfGravity();

  for (int32_t i = 0; i < borderLineCount(); i++) {
    double currentDistance = jrt::Math::abs((*lines)[i]->signedDistance(gravityPoint));

    if (currentDistance < minDistance) {
      minDistance2 = minDistance;
      minDistance = currentDistance;
    } else if (currentDistance < minDistance2) {
      minDistance2 = currentDistance;
    }
  }
  return minDistance + minDistance2;
}

bool Simplex::isIntBox() {
  for (int32_t i = 0; i < lines->length; i++) {
    Line* currentLine = (*lines)[i];
    if (!(jrt::fast_cast<IntPoint>(currentLine->a) != nullptr &&
          jrt::fast_cast<IntPoint>(currentLine->b) != nullptr)) {
      return false;
    }
    if (!currentLine->isOrthogonal()) {
      return false;
    }
    if (!cornerIsBounded(i)) {
      return false;
    }
  }
  return true;
}

bool Simplex::isIntOctagon() {
  for (int32_t i = 0; i < lines->length; i++) {
    Line* currentLine = (*lines)[i];
    if (!(jrt::fast_cast<IntPoint>(currentLine->a) != nullptr &&
          jrt::fast_cast<IntPoint>(currentLine->b) != nullptr)) {
      return false;
    }
    if (!currentLine->isMultipleOf45Degree()) {
      return false;
    }
    if (!cornerIsBounded(i)) {
      return false;
    }
  }
  return true;
}

IntOctagon* Simplex::toIntOctagon() {
  // this function is at the moment only implemented for lines
  // consisting of IntPoints.
  // The general implementation is still missing.
  if (!isIntOctagon()) {
    return nullptr;
  }
  if (isEmpty()) {
    return IntOctagon::EMPTY();
  }

  // initialise to the biggest octagon values

  int32_t rx = Limits::CRIT_INT;
  int32_t uy = Limits::CRIT_INT;
  int32_t lrx = Limits::CRIT_INT;
  int32_t urx = Limits::CRIT_INT;
  int32_t lx = -Limits::CRIT_INT;
  int32_t ly = -Limits::CRIT_INT;
  int32_t llx = -Limits::CRIT_INT;
  int32_t ulx = -Limits::CRIT_INT;
  for (int32_t i = 0; i < lines->length; i++) {
    Line* currentLine = (*lines)[i];
    IntPoint* a = jrt::fast_cast<IntPoint>(currentLine->a);
    IntPoint* b = jrt::fast_cast<IntPoint>(currentLine->b);
    if (a->y == b->y) {
      if (b->x >= a->x) {
        // lower boundary line
        ly = a->y;
      }
      if (b->x <= a->x) {
        // upper boundary line
        uy = a->y;
      }
    }
    if (a->x == b->x) {
      if (b->y >= a->y) {
        // right boundary line
        rx = a->x;
      }
      if (b->y <= a->y) {
        // left boundary line
        lx = a->x;
      }
    }
    if (a->y < b->y) {
      if (a->x < b->x) {
        // lower right boundary line
        lrx = a->x - a->y;
      } else if (a->x > b->x) {
        // upper right boundary line
        urx = a->x + a->y;
      }
    } else if (a->y > b->y) {
      if (a->x < b->x) {
        // lower left boundary line
        llx = a->x + a->y;
      } else if (a->x > b->x) {
        // upper left boundary line
        ulx = a->x - a->y;
      }
    }
  }
  IntOctagon* result = new (jrt::kGeom) IntOctagon(lx, ly, rx, uy, ulx, lrx, llx, urx);
  return result->normalize();
}

Simplex* Simplex::translateBy(Vector* vector) {
  if (vector->equals(Vector::ZERO())) {
    return this;
  }
  auto* newArray = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  for (int32_t i = 0; i < lines->length; i++) {
    (*newArray)[i] = (*lines)[i]->translateBy(vector);
  }
  return new (jrt::kGeom) Simplex(newArray);
}

IntBox* Simplex::boundingBox() {
  if (lines->length == 0) {
    return IntBox::EMPTY();
  }
  if (precalculatedBoundingBox == nullptr) {
    double llx = jrt::Integer::MAX_VALUE;
    double lly = jrt::Integer::MAX_VALUE;
    double urx = jrt::Integer::MIN_VALUE;
    double ury = jrt::Integer::MIN_VALUE;
    for (int32_t i = 0; i < lines->length; i++) {
      FloatPoint* current = cornerApprox(i);
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

IntOctagon* Simplex::boundingOctagon() {
  if (precalculatedBoundingOctagon == nullptr) {
    double lx = jrt::Integer::MAX_VALUE;
    double ly = jrt::Integer::MAX_VALUE;
    double rx = jrt::Integer::MIN_VALUE;
    double uy = jrt::Integer::MIN_VALUE;
    double ulx = jrt::Integer::MAX_VALUE;
    double lrx = jrt::Integer::MIN_VALUE;
    double llx = jrt::Integer::MAX_VALUE;
    double urx = jrt::Integer::MIN_VALUE;
    for (int32_t i = 0; i < lines->length; i++) {
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
    if (jrt::Math::min(lx, ly) < -Limits::CRIT_INT || jrt::Math::max(rx, uy) > Limits::CRIT_INT ||
        jrt::Math::min(ulx, llx) < -Limits::CRIT_INT ||
        jrt::Math::max(lrx, urx) > Limits::CRIT_INT) {
      // result is not bounded
      return nullptr;
    }
    precalculatedBoundingOctagon = new (jrt::kGeom) IntOctagon(
        jrt::d2i(jrt::Math::floor(lx)), jrt::d2i(jrt::Math::floor(ly)),
        jrt::d2i(jrt::Math::ceil(rx)), jrt::d2i(jrt::Math::ceil(uy)),
        jrt::d2i(jrt::Math::floor(ulx)), jrt::d2i(jrt::Math::ceil(lrx)),
        jrt::d2i(jrt::Math::floor(llx)), jrt::d2i(jrt::Math::ceil(urx)));
  }
  return precalculatedBoundingOctagon;
}

RegularTileShape* Simplex::boundingShape(ShapeBoundingDirections* dirs) {
  return dirs->bounds(this);
}

Simplex* Simplex::offset(double width) {
  if (width == 0) {
    return this;
  }
  auto* newArray = new (jrt::kGeom) jrt::Array<Line*>(lines->length);
  for (int32_t i = 0; i < lines->length; i++) {
    (*newArray)[i] = (*lines)[i]->translate(-width);
  }
  Simplex* offsetSimplex = new (jrt::kGeom) Simplex(newArray);
  if (width < 0) {
    offsetSimplex = offsetSimplex->removeRedundantLines();
  }
  return offsetSimplex;
}

Simplex* Simplex::enlarge(double offset) {
  if (offset == 0) {
    return this;
  }
  Simplex* offsetSimplex = this->offset(offset);
  IntOctagon* boundingOct = this->boundingOctagon();
  if (boundingOct == nullptr) {
    return Simplex::EMPTY();
  }
  IntOctagon* offsetOct = boundingOct->offset(offset);
  return offsetSimplex->intersection(offsetOct->toSimplex());
}

int32_t Simplex::indexOfRightMostCorner(Point* fromPoint) {
  Point* pole = fromPoint;
  Point* rightMostCorner = corner(0);
  int32_t result = 0;
  for (int32_t i = 1; i < lines->length; i++) {
    Point* currentCorner = corner(i);
    if (currentCorner->sideOf(pole, rightMostCorner) == Side::ON_THE_RIGHT()) {
      rightMostCorner = currentCorner;
      result = i;
    }
  }
  return result;
}

Simplex* Simplex::intersection(IntBox* box) { return intersection(box->toSimplex()); }

Simplex* Simplex::intersection(IntOctagon* other) { return intersection(other->toSimplex()); }

Simplex* Simplex::intersection(Simplex* other) {
  if (this->isEmpty() || other->isEmpty()) {
    return EMPTY();
  }
  auto* newArray = new (jrt::kGeom) jrt::Array<Line*>(lines->length + other->lines->length);
  jrt::arraycopy(this->lines, 0, newArray, 0, lines->length);
  jrt::arraycopy(other->lines, 0, newArray, lines->length, other->lines->length);
  sortLines(newArray);
  Simplex* result = new (jrt::kGeom) Simplex(newArray);
  return result->removeRedundantLines();
}

TileShape* Simplex::intersection(TileShape* other) { return other->intersection(this); }

bool Simplex::intersects(Shape* other) { return other->intersects(this); }

bool Simplex::intersects(Simplex* other) {
  ConvexShape* is = intersection(other);
  return !is->isEmpty();
}

bool Simplex::intersects(IntBox* box) { return intersects(box->toSimplex()); }

bool Simplex::intersects(IntOctagon* octagon) { return intersects(octagon->toSimplex()); }

bool Simplex::intersects(Circle* circle) { return circle->intersects(this); }

int32_t Simplex::borderLineIndex(Line* line) {
  for (int32_t i = 0; i < lines->length; i++) {
    if (line->equals((*lines)[i])) {
      return i;
    }
  }
  return -1;
}

Simplex* Simplex::removeBorderLine(int32_t no) {
  if (no < 0 || no >= lines->length) {
    return this;
  }
  auto* newArray = new (jrt::kGeom) jrt::Array<Line*>(this->lines->length - 1);
  jrt::arraycopy(this->lines, 0, newArray, 0, no);
  jrt::arraycopy(this->lines, no + 1, newArray, no, newArray->length - no);
  return new (jrt::kGeom) Simplex(newArray);
}

jrt::Array<TileShape*>* Simplex::cutout(TileShape* shape) { return shape->cutoutFrom(this); }

jrt::Array<TileShape*>* Simplex::cutoutFrom(Simplex* outerSimplex) {
  if (this->dimension() < 2) {
    FRLogger::warn("Simplex.cutout_from only implemented for 2-dim simplex");
    return nullptr;
  }
  Simplex* innerSimplex = this->intersection(outerSimplex);
  if (innerSimplex->dimension() < 2) {
    // nothing to cutout from outerSimplex
    auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(1);
    (*result)[0] = outerSimplex;
    return result;
  }
  int32_t innerCornerCount = innerSimplex->lines->length;
  auto* divisionLineArr = new (jrt::kGeom) jrt::Array<jrt::Array<Line*>*>(innerCornerCount);
  for (int32_t innerCornerNo = 0; innerCornerNo < innerCornerCount; innerCornerNo++) {
    (*divisionLineArr)[innerCornerNo] = innerSimplex->calcDivisionLines(innerCornerNo, outerSimplex);
    if ((*divisionLineArr)[innerCornerNo] == nullptr) {
      FRLogger::warn("Simplex.cutout_from: division line is null");
      auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(1);
      (*result)[0] = outerSimplex;
      return result;
    }
  }
  bool checkCrossFirstLine = false;
  Line* prevDivisionLine = nullptr;
  Line* firstDivisionLine = (*(*divisionLineArr)[0])[0];
  IntDirection* firstDirection = jrt::fast_cast<IntDirection>(firstDivisionLine->direction());
  jrt::Collection<Simplex*>* resultList = new (jrt::kOp) jrt::LinkedList<Simplex*>();

  for (int32_t innerCornerNo = 0; innerCornerNo < innerCornerCount; innerCornerNo++) {
    int32_t nextCornerNo = (innerCornerNo + 1) % innerCornerCount;
    Line* nextDivisionLine = (*(*divisionLineArr)[nextCornerNo])[0];
    jrt::Array<Line*>* currentDivisionLines = (*divisionLineArr)[innerCornerNo];
    if (currentDivisionLines->length == 2) {
      // 2 division lines are necessary (sharp corner).
      // Construct an unbounded simplex from
      // currentDivisionLines[1] and currentDivisionLines[0]
      // and intersect it with the outer simplex
      IntDirection* currentDirection =
          jrt::fast_cast<IntDirection>((*currentDivisionLines)[0]->direction());
      bool mergePrevDivisionLine = false;
      bool mergeFirstDivisionLine = false;
      if (prevDivisionLine != nullptr) {
        IntDirection* prevDir = jrt::fast_cast<IntDirection>(prevDivisionLine->direction());
        if (currentDirection->determinant(prevDir) > 0) {
          // the previous division line may intersect
          //  currentDivisionLines[0] inside divideSimplex
          mergePrevDivisionLine = true;
        }
      }
      if (!checkCrossFirstLine) {
        checkCrossFirstLine =
            innerCornerNo > 0 && currentDirection->determinant(firstDirection) > 0;
      }
      if (checkCrossFirstLine) {
        IntDirection* currentDir2 =
            jrt::fast_cast<IntDirection>((*currentDivisionLines)[1]->direction());
        if (currentDir2->determinant(firstDirection) < 0) {
          // The current piece has an intersection area with the first
          // piece.
          // Add a line to tmpPolyline to prevent this
          mergeFirstDivisionLine = true;
        }
      }
      int32_t pieceLineCount = 2;
      if (mergePrevDivisionLine) {
        ++pieceLineCount;
      }
      if (mergeFirstDivisionLine) {
        ++pieceLineCount;
      }
      auto* pieceLines = new (jrt::kGeom) jrt::Array<Line*>(pieceLineCount);
      (*pieceLines)[0] = new (jrt::kGeom) Line((*currentDivisionLines)[1]->b, (*currentDivisionLines)[1]->a);
      (*pieceLines)[1] = (*currentDivisionLines)[0];
      int32_t currentLineNo = 1;
      if (mergePrevDivisionLine) {
        ++currentLineNo;
        (*pieceLines)[currentLineNo] = prevDivisionLine;
      }
      if (mergeFirstDivisionLine) {
        ++currentLineNo;
        (*pieceLines)[currentLineNo] = new (jrt::kGeom) Line(firstDivisionLine->b, firstDivisionLine->a);
      }
      Simplex* currentPiece = new (jrt::kGeom) Simplex(pieceLines);
      resultList->add(currentPiece->intersection(outerSimplex));
    }
    // construct an unbounded simplex from nextDivisionLine,
    // innerSimplex.line [innerCornerNo] and the last current division line
    // and intersect it with the outer simplex
    bool mergeNextDivisionLine = !nextDivisionLine->b->equals(nextDivisionLine->a);
    Line* lastCurrDivisionLine = (*currentDivisionLines)[currentDivisionLines->length - 1];
    IntDirection* lastCurrDir = jrt::fast_cast<IntDirection>(lastCurrDivisionLine->direction());
    bool mergeLastCurrDivisionLine = !lastCurrDivisionLine->b->equals(lastCurrDivisionLine->a);
    bool mergePrevDivisionLine = false;
    bool mergeFirstDivisionLine = false;
    if (prevDivisionLine != nullptr) {
      IntDirection* prevDir = jrt::fast_cast<IntDirection>(prevDivisionLine->direction());
      if (lastCurrDir->determinant(prevDir) > 0) {
        // the previous division line may intersect
        //  the last current division line inside divideSimplex
        mergePrevDivisionLine = true;
      }
    }
    if (!checkCrossFirstLine) {
      checkCrossFirstLine =
          innerCornerNo > 0 && lastCurrDir->determinant(firstDirection) > 0 &&
          lastCurrDir->getVector()->scalarProduct(firstDirection->getVector()) < 0;
      // scalarProduct checked to ignore backcrossing at
      // small innerCornerNo
    }
    if (checkCrossFirstLine) {
      IntDirection* nextDir = jrt::fast_cast<IntDirection>(nextDivisionLine->direction());
      if (nextDir->determinant(firstDirection) < 0) {
        // The current piece has an intersection area with the first piece.
        // Add a line to tmpPolyline to prevent this
        mergeFirstDivisionLine = true;
      }
    }
    int32_t pieceLineCount = 1;
    if (mergeNextDivisionLine) {
      ++pieceLineCount;
    }
    if (mergeLastCurrDivisionLine) {
      ++pieceLineCount;
    }
    if (mergePrevDivisionLine) {
      ++pieceLineCount;
    }
    if (mergeFirstDivisionLine) {
      ++pieceLineCount;
    }
    auto* pieceLines = new (jrt::kGeom) jrt::Array<Line*>(pieceLineCount);
    Line* currentLine = (*innerSimplex->lines)[innerCornerNo];
    (*pieceLines)[0] = new (jrt::kGeom) Line(currentLine->b, currentLine->a);
    int32_t currentLineNo = 0;
    if (mergeNextDivisionLine) {
      ++currentLineNo;
      (*pieceLines)[currentLineNo] = new (jrt::kGeom) Line(nextDivisionLine->b, nextDivisionLine->a);
    }
    if (mergeLastCurrDivisionLine) {
      ++currentLineNo;
      (*pieceLines)[currentLineNo] = lastCurrDivisionLine;
    }
    if (mergePrevDivisionLine) {
      ++currentLineNo;
      (*pieceLines)[currentLineNo] = prevDivisionLine;
    }
    if (mergeFirstDivisionLine) {
      ++currentLineNo;
      (*pieceLines)[currentLineNo] = new (jrt::kGeom) Line(firstDivisionLine->b, firstDivisionLine->a);
    }
    Simplex* currentPiece = new (jrt::kGeom) Simplex(pieceLines);
    resultList->add(currentPiece->intersection(outerSimplex));
    nextDivisionLine = prevDivisionLine;  // (sic: Java assigns the local, not prevDivisionLine)
  }
  auto* result = new (jrt::kGeom) jrt::Array<TileShape*>(resultList->size());
  jrt::Iterator<Simplex*>* it = resultList->iterator();
  for (int32_t i = 0; i < result->length; i++) {
    (*result)[i] = it->next();
  }
  return result;
}

jrt::Array<TileShape*>* Simplex::cutoutFrom(IntOctagon* oct) {
  return cutoutFrom(oct->toSimplex());
}

jrt::Array<TileShape*>* Simplex::cutoutFrom(IntBox* box) { return cutoutFrom(box->toSimplex()); }

Simplex* Simplex::removeRedundantLines() {
  auto* lines = new (jrt::kGeom) jrt::Array<Line*>(this->lines->length);
  // copy the sorted lines of arr into lines while skipping
  // multiple lines
  int32_t newLength = 1;
  (*lines)[0] = (*this->lines)[0];
  Line* prev = (*this->lines)[0];
  for (int32_t i = 1; i < this->lines->length; i++) {
    if (!(*this->lines)[i]->fastEquals(prev)) {
      (*lines)[newLength] = (*this->lines)[i];
      prev = (*lines)[newLength];
      ++newLength;
    }
  }

  auto* intersectionSides = new (jrt::kGeom) jrt::Array<Side*>(newLength);
  // precalculated array , on which side of this line the previous and the
  // next line do intersect

  bool tryAgain = newLength > 2;
  int32_t indexOfLastRemovedLine = newLength;
  while (tryAgain) {
    tryAgain = false;
    int32_t prevInd = newLength - 1;
    int32_t nextInd;
    Line* prevLine = (*lines)[prevInd];
    Line* currentLine = (*lines)[0];
    Line* nextLine;
    for (int32_t ind = 0; ind < newLength; ind++) {
      if (ind == newLength - 1) {
        nextInd = 0;
      } else {
        nextInd = ind + 1;
      }
      nextLine = (*lines)[nextInd];

      bool removeLine = false;
      IntDirection* prevDir = jrt::fast_cast<IntDirection>(prevLine->direction());
      IntDirection* nextDir = jrt::fast_cast<IntDirection>(nextLine->direction());
      double det = prevDir->determinant(nextDir);
      if (det != 0) {  // prevLine and nextLine are not parallel
        if ((*intersectionSides)[ind] == nullptr) {
          // intersectionSides [ind] not precalculated
          (*intersectionSides)[ind] = currentLine->sideOfIntersection(prevLine, nextLine);
        }
        if (det > 0) {  // direction of nextLine is bigger than direction of prevLine
          // if the intersection of prevLine and nextLine
          // is on the left of currentLine, currentLine does not
          // contribute to the shape of the simplex
          removeLine = (*intersectionSides)[ind] != Side::ON_THE_LEFT();
        } else {  // direction of nextLine is smaller than direction of prevLine

          if ((*intersectionSides)[ind] == Side::ON_THE_LEFT()) {
            IntDirection* currentDirection = jrt::fast_cast<IntDirection>(currentLine->direction());
            if (prevDir->determinant(currentDirection) > 0) {
              // direction of currentLine is bigger than direction of prevLine
              // the halfplane defined by currentLine does not intersect
              // with the simplex defined by prevLine and nex_line,
              // hence this simplex must be empty
              newLength = 0;
              tryAgain = false;
              break;
            }
          }
        }
      } else {  // prevLine and nextLine are parallel
        if (prevLine->sideOf(nextLine->a) == Side::ON_THE_LEFT()) {
          // prevLine is to the left of nextLine; the half-planes defined by
          // prevLine and nextLine do not intersect.
          newLength = 0;
          tryAgain = false;
          break;
        }
      }
      if (removeLine) {
        tryAgain = true;
        --newLength;
        for (int32_t i = ind; i < newLength; i++) {
          (*lines)[i] = (*lines)[i + 1];
          (*intersectionSides)[i] = (*intersectionSides)[i + 1];
        }

        if (newLength < 3) {
          tryAgain = false;
          break;
        }
        // reset 3 precalculated intersectionSides
        if (ind == 0) {
          prevInd = newLength - 1;
        }
        (*intersectionSides)[prevInd] = nullptr;
        if (ind >= newLength) {
          nextInd = 0;
        } else {
          nextInd = ind;
        }
        (*intersectionSides)[nextInd] = nullptr;
        --ind;
        indexOfLastRemovedLine = ind;
      } else {
        prevLine = currentLine;
        prevInd = ind;
      }
      currentLine = nextLine;
      if (!tryAgain && ind >= indexOfLastRemovedLine) {
        // tried all lines without removing one
        break;
      }
    }
  }

  if (newLength == 2) {
    if ((*lines)[0]->isParallel((*lines)[1])) {
      if ((*lines)[0]->direction()->equals((*lines)[1]->direction())) {
        // one of the two remaining lines is redundant
        if ((*lines)[1]->sideOf((*lines)[0]->a) == Side::ON_THE_LEFT()) {
          (*lines)[0] = (*lines)[1];
        }
        --newLength;
      } else {
        // the two remaining lines have opposite direction; the simplex may be empty.
        if ((*lines)[1]->sideOf((*lines)[0]->a) == Side::ON_THE_LEFT()) {
          newLength = 0;
        }
      }
    }
  }
  if (newLength == lines->length) {
    return this;  // nothing removed
  }
  if (newLength == 0) {
    return Simplex::EMPTY();
  }
  auto* result = new (jrt::kGeom) jrt::Array<Line*>(newLength);
  jrt::arraycopy(lines, 0, result, 0, newLength);
  return new (jrt::kGeom) Simplex(result);
}

jrt::Array<Line*>* Simplex::calcDivisionLines(int32_t innerCornerNo, Simplex* outerSimplex) {
  Line* currentInnerLine = (*this->lines)[innerCornerNo];
  Line* prevInnerLine;
  if (innerCornerNo != 0) {
    prevInnerLine = (*this->lines)[innerCornerNo - 1];
  } else {
    prevInnerLine = (*this->lines)[lines->length - 1];
  }
  FloatPoint* intersection = currentInnerLine->intersectionApprox(prevInnerLine);
  if (intersection->x >= jrt::Integer::MAX_VALUE) {
    FRLogger::warn("Simplex.calc_division_lines: intersection expected");
    return nullptr;
  }
  IntPoint* innerCorner = intersection->round();
  double ctolerance = 0.0001;
  bool isExact = jrt::Math::abs(innerCorner->x - intersection->x) < ctolerance &&
                 jrt::Math::abs(innerCorner->y - intersection->y) < ctolerance;

  if (!isExact) {
    // it is assumed, that the corners of the original inner simplex are
    // exact and the not exact corners come from the intersection of
    // the inner simplex with the outer simplex.
    // Because these corners lie on the border of the outer simplex,
    // no division is necessary
    auto* result = new (jrt::kGeom) jrt::Array<Line*>(1);
    (*result)[0] = prevInnerLine;
    return result;
  }
  IntDirection* firstProjectionDir = Direction::NULL_();
  IntDirection* secondProjectionDir = Direction::NULL_();
  IntDirection* prevInnerDir = jrt::fast_cast<IntDirection>(prevInnerLine->direction()->opposite());
  IntDirection* nextInnerDir = jrt::fast_cast<IntDirection>(currentInnerLine->direction());
  int32_t outerLineNo = 0;

  // search the first outer line, so that
  // the perpendicular projection of the inner corner onto this
  // line is visible from innerCorner to the left of prevInnerLine.

  double minDistance = jrt::Integer::MAX_VALUE;

  for (int32_t ind = 0; ind < outerSimplex->lines->length; ind++) {
    Line* outerLine = (*outerSimplex->lines)[outerLineNo];
    IntDirection* currentProjectionDir =
        jrt::fast_cast<IntDirection>(innerCorner->perpendicularDirection(outerLine));
    if (currentProjectionDir == Direction::NULL_()) {
      auto* result = new (jrt::kGeom) jrt::Array<Line*>(1);
      (*result)[0] = new (jrt::kGeom) Line(innerCorner, innerCorner);
      return result;
    }
    bool projectionVisible = prevInnerDir->determinant(currentProjectionDir) >= 0;
    if (projectionVisible) {
      double currentDistance =
          jrt::Math::abs(outerLine->signedDistance(innerCorner->toFloat()));
      bool secondDivisionNecessary = currentProjectionDir->determinant(nextInnerDir) < 0;
      // may occur at a sharp angle
      IntDirection* currentSecondProjectionDir = currentProjectionDir;

      if (secondDivisionNecessary) {
        // search the first projection_dir between currentProjectionDir
        // and nextInnerDir, that is visible from next_inner_line
        bool secondProjectionVisible = false;
        int32_t tmpOuterLineNo = outerLineNo;
        while (!secondProjectionVisible) {
          if (tmpOuterLineNo == outerSimplex->lines->length - 1) {
            tmpOuterLineNo = 0;
          } else {
            ++tmpOuterLineNo;
          }
          currentSecondProjectionDir = jrt::fast_cast<IntDirection>(
              innerCorner->perpendicularDirection((*outerSimplex->lines)[tmpOuterLineNo]));

          if (currentSecondProjectionDir == Direction::NULL_()) {
            // inner corner is on outerLine
            auto* result = new (jrt::kGeom) jrt::Array<Line*>(1);
            (*result)[0] = new (jrt::kGeom) Line(innerCorner, innerCorner);
            return result;
          }
          if (currentProjectionDir->determinant(currentSecondProjectionDir) < 0) {
            // currentSecondProjectionDir not found;
            // the angle between currentProjectionDir and
            // currentSecondProjectionDir would be already bigger
            // than 180 degree
            currentDistance = jrt::Integer::MAX_VALUE;
            break;
          }

          secondProjectionVisible = currentSecondProjectionDir->determinant(nextInnerDir) >= 0;
        }
        currentDistance += jrt::Math::abs(
            (*outerSimplex->lines)[tmpOuterLineNo]->signedDistance(innerCorner->toFloat()));
      }
      if (currentDistance < minDistance) {
        minDistance = currentDistance;
        firstProjectionDir = currentProjectionDir;
        secondProjectionDir = currentSecondProjectionDir;
      }
    }
    if (outerLineNo == outerSimplex->lines->length - 1) {
      outerLineNo = 0;
    } else {
      ++outerLineNo;
    }
  }
  if (minDistance == jrt::Integer::MAX_VALUE) {
    FRLogger::warn("Simplex.calc_division_lines: division not found");
    return nullptr;
  }
  jrt::Array<Line*>* result;
  if (firstProjectionDir->equals(secondProjectionDir)) {
    result = new (jrt::kGeom) jrt::Array<Line*>(1);
    (*result)[0] = new (jrt::kGeom) Line(innerCorner, firstProjectionDir);
  } else {
    result = new (jrt::kGeom) jrt::Array<Line*>(2);
    (*result)[0] = new (jrt::kGeom) Line(innerCorner, firstProjectionDir);
    (*result)[1] = new (jrt::kGeom) Line(innerCorner, secondProjectionDir);
  }
  return result;
}

}  // namespace fr
