#include "board/optimize/TraceTightener45.h"

#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/state/ChangedArea.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/Signum.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Limits.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"

namespace fr {

Polyline* TraceTightener45::pullTight(Polyline* polyline) {
  Polyline* newResult = avoidAcidTraps(polyline);
  Polyline* prevResult = nullptr;
  for (int32_t rounds = 0; newResult != prevResult && !mustStopAfter(rounds); ++rounds) {
    prevResult = newResult;
    Polyline* tmp1 = reduceCorners(prevResult);
    Polyline* tmp2 = smoothenCorners(tmp1);
    newResult = repositionLines(tmp2);
  }
  return newResult;
}

AngleRestriction TraceTightener45::getAngleRestriction() {
  return AngleRestriction::FORTYFIVE_DEGREE;
}

Polyline* TraceTightener45::reduceCorners(Polyline* polyline) {
  if (polyline->lines->length <= 4) {
    return polyline;
  }
  jrt::Array<Point*>* currentCorner = new (jrt::kGeom) jrt::Array<Point*>(4);
  for (int32_t i = 0; i < 4; i++) {
    (*currentCorner)[i] = polyline->corner(i);
    if (jrt::fast_cast<IntPoint>((*currentCorner)[i]) == nullptr) {
      return polyline;
    }
  }
  jrt::Array<bool>* currentCornerInClipShape = new (jrt::kOp) jrt::Array<bool>(4);

  for (int32_t i = 0; i < 4; i++) {
    if (currentClipShape == nullptr) {
      (*currentCornerInClipShape)[i] = true;
    } else {
      (*currentCornerInClipShape)[i] = !currentClipShape->isOutside((*currentCorner)[i]);
    }
  }

  bool polylineChanged = false;
  int32_t newCornerCount = 1;
  jrt::Array<Point*>* newCorners = new (jrt::kGeom) jrt::Array<Point*>(polyline->lines->length - 3);
  (*newCorners)[0] = (*currentCorner)[0];
  jrt::Array<Point*>* currentCheckPoints = new (jrt::kGeom) jrt::Array<Point*>(2);
  Point* newCorner = nullptr;
  int32_t cornerIndex = 3;
  while (cornerIndex < polyline->lines->length - 1) {
    (*currentCorner)[3] = polyline->corner(cornerIndex);
    if (jrt::fast_cast<IntPoint>((*currentCorner)[3]) == nullptr) {
      return polyline;
    }
    if ((*currentCorner)[1]->equals((*currentCorner)[2]) ||
        (cornerIndex < polyline->lines->length - 2 &&
         (*currentCorner)[3]->sideOf((*currentCorner)[1], (*currentCorner)[2]) ==
             Side::COLLINEAR())) {
      // corners in the middle af a line can be skipped
      ++cornerIndex;
      (*currentCorner)[2] = (*currentCorner)[3];
      (*currentCornerInClipShape)[2] = (*currentCornerInClipShape)[3];
      if (cornerIndex < polyline->lines->length - 1) {
        (*currentCorner)[3] = polyline->corner(cornerIndex);
        if (jrt::fast_cast<IntPoint>((*currentCorner)[3]) == nullptr) {
          return polyline;
        }
      }
      polylineChanged = true;
    }
    (*currentCornerInClipShape)[3] =
        currentClipShape == nullptr || !currentClipShape->isOutside((*currentCorner)[3]);
    bool cornerRemoved = false;
    if ((*currentCornerInClipShape)[1] && (*currentCornerInClipShape)[2] &&
        (*currentCornerInClipShape)[3]) {
      // translate the line from currentCorner[2] to currentCorner[1] to currentCorner[3]
      Vector* delta = (*currentCorner)[3]->differenceBy((*currentCorner)[2]);
      newCorner = (*currentCorner)[1]->translateBy(delta);
      if ((*currentCorner)[3]->equals((*currentCorner)[2])) {
        // just remove multiple corner
        cornerRemoved = true;
      } else if (newCorner->sideOf((*currentCorner)[0], (*currentCorner)[1]) ==
                 Side::COLLINEAR()) {
        (*currentCheckPoints)[0] = newCorner;
        (*currentCheckPoints)[1] = (*currentCorner)[1];
        Polyline* checkPolyline = new (jrt::kGeom) Polyline(currentCheckPoints);
        if (checkPolyline->lines->length == 3) {
          TileShape* shapeToCheck = checkPolyline->offsetShape(currentHalfWidth, 0);
          if (board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                     currentClearanceClassIndex, this->contactPins)) {
            (*currentCheckPoints)[1] = (*currentCorner)[3];
            if ((*currentCheckPoints)[0]->equals((*currentCheckPoints)[1])) {
              cornerRemoved = true;
            } else {
              checkPolyline = new (jrt::kGeom) Polyline(currentCheckPoints);
              if (checkPolyline->lines->length == 3) {
                shapeToCheck = checkPolyline->offsetShape(currentHalfWidth, 0);
                cornerRemoved =
                    board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                           currentClearanceClassIndex, this->contactPins);
              } else {
                cornerRemoved = true;
              }
            }
          }
        } else {
          cornerRemoved = true;
        }
      }
    }
    if (!cornerRemoved && (*currentCornerInClipShape)[0] && (*currentCornerInClipShape)[1] &&
        (*currentCornerInClipShape)[2]) {
      // the first try has failed. Try to translate the line from
      // corner2 to corner1 to corner_0
      Vector* delta = (*currentCorner)[0]->differenceBy((*currentCorner)[1]);
      newCorner = (*currentCorner)[2]->translateBy(delta);
      if ((*currentCorner)[0]->equals((*currentCorner)[1])) {
        // just remove multiple corner
        cornerRemoved = true;
      } else if (newCorner->sideOf((*currentCorner)[2], (*currentCorner)[3]) ==
                 Side::COLLINEAR()) {
        (*currentCheckPoints)[0] = newCorner;
        (*currentCheckPoints)[1] = (*currentCorner)[0];
        Polyline* checkPolyline = new (jrt::kGeom) Polyline(currentCheckPoints);
        if (checkPolyline->lines->length == 3) {
          TileShape* shapeToCheck = checkPolyline->offsetShape(currentHalfWidth, 0);
          if (board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                     currentClearanceClassIndex, this->contactPins)) {
            (*currentCheckPoints)[1] = (*currentCorner)[2];
            checkPolyline = new (jrt::kGeom) Polyline(currentCheckPoints);
            if (checkPolyline->lines->length == 3) {
              shapeToCheck = checkPolyline->offsetShape(currentHalfWidth, 0);
              cornerRemoved =
                  board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                         currentClearanceClassIndex, this->contactPins);
            } else {
              cornerRemoved = true;
            }
          }
        } else {
          cornerRemoved = true;
        }
      }
    }
    if (cornerRemoved) {
      polylineChanged = true;
      (*currentCorner)[1] = newCorner;
      (*currentCornerInClipShape)[1] =
          currentClipShape == nullptr || !currentClipShape->isOutside((*currentCorner)[1]);
      if (board->changedArea != nullptr) {
        board->changedArea->join(newCorner->toFloat(), currentLayer);
        board->changedArea->join((*currentCorner)[1]->toFloat(), currentLayer);
        board->changedArea->join((*currentCorner)[2]->toFloat(), currentLayer);
      }
    } else {
      (*newCorners)[newCornerCount] = (*currentCorner)[1];
      ++newCornerCount;
      (*currentCorner)[0] = (*currentCorner)[1];
      (*currentCorner)[1] = (*currentCorner)[2];
      (*currentCornerInClipShape)[0] = (*currentCornerInClipShape)[1];
      (*currentCornerInClipShape)[1] = (*currentCornerInClipShape)[2];
    }
    (*currentCorner)[2] = (*currentCorner)[3];
    (*currentCornerInClipShape)[2] = (*currentCornerInClipShape)[3];
    ++cornerIndex;
  }
  if (!polylineChanged) {
    return polyline;
  }
  jrt::Array<Point*>* adjustedCorners = new (jrt::kGeom) jrt::Array<Point*>(newCornerCount + 2);
  jrt::arraycopy(newCorners, 0, adjustedCorners, 0, newCornerCount);
  (*adjustedCorners)[newCornerCount] = (*currentCorner)[1];
  (*adjustedCorners)[newCornerCount + 1] = (*currentCorner)[2];
  return new (jrt::kGeom) Polyline(adjustedCorners);
}

Polyline* TraceTightener45::smoothenCorners(Polyline* polyline) {
  Polyline* result = polyline;
  bool polylineChanged = true;
  while (polylineChanged) {
    if (result->lines->length < 4) {
      return result;
    }
    polylineChanged = false;
    jrt::Array<Line*>* lines = new (jrt::kGeom) jrt::Array<Line*>(result->lines->length);
    jrt::arraycopy(result->lines, 0, lines, 0, lines->length);

    for (int32_t i = 1; i < lines->length - 2; i++) {
      Direction* d1 = (*lines)[i]->direction();
      Direction* d2 = (*lines)[i + 1]->direction();
      if (d1->isMultipleOf45Degree() && d2->isMultipleOf45Degree() &&
          d1->projection(d2) != Signum::POSITIVE()) {
        // there is a 90 degree or sharper angle
        Line* newLine = smoothenCorner(lines, i);
        if (newLine == nullptr) {
          // the greedy smoothening couldn't change the polyline
          newLine = smoothenSharpCorner(lines, i);
        }
        if (newLine != nullptr) {
          polylineChanged = true;
          // add the new line into the line array
          jrt::Array<Line*>* tmpLines = new (jrt::kGeom) jrt::Array<Line*>(lines->length + 1);
          jrt::arraycopy(lines, 0, tmpLines, 0, i + 1);
          (*tmpLines)[i + 1] = newLine;
          jrt::arraycopy(lines, i + 1, tmpLines, i + 2, tmpLines->length - (i + 2));
          lines = tmpLines;
          ++i;
        }
      }
    }
    if (polylineChanged) {
      result = new (jrt::kGeom) Polyline(lines);
    }
  }
  return result;
}

Line* TraceTightener45::smoothenSharpCorner(jrt::Array<Line*>* lines, int32_t no) {
  FloatPoint* currentCorner = (*lines)[no]->intersectionApprox((*lines)[no + 1]);
  if (currentCorner->x != jrt::d2i(currentCorner->x)) {
    // intersection of 2 diagonal lines is not integer
    Line* result = smoothenNonIntegerCorner(lines, no);
    {
      if (result != nullptr) {
        return result;
      }
    }
  }
  FloatPoint* prevCorner = (*lines)[no]->intersectionApprox((*lines)[no - 1]);
  FloatPoint* nextCorner = (*lines)[no + 1]->intersectionApprox((*lines)[no + 2]);

  Direction* prevDir = (*lines)[no]->direction();
  Direction* nextDir = (*lines)[no + 1]->direction();
  Direction* newLineDir = Direction::getInstance(prevDir->getVector()->add(nextDir->getVector()));
  Line* translateLine = Line::getInstance(currentCorner->round(), newLineDir);
  double translateDist = (Limits::sqrt2 - 1) * this->currentHalfWidth;
  double prevDist = jrt::Math::abs(translateLine->signedDistance(prevCorner));
  double nextDist = jrt::Math::abs(translateLine->signedDistance(nextCorner));
  translateDist = jrt::Math::min(translateDist, prevDist);
  translateDist = jrt::Math::min(translateDist, nextDist);
  if (translateDist < 0.99) {
    return nullptr;
  }
  translateDist = jrt::Math::max(translateDist - 1, 1.0);
  if (translateLine->sideOf(nextCorner) == Side::ON_THE_LEFT()) {
    translateDist = -translateDist;
  }
  Line* result = translateLine->translate(translateDist);
  if (board->changedArea != nullptr) {
    board->changedArea->join(currentCorner, currentLayer);
  }
  return result;
}

Line* TraceTightener45::smoothenNonIntegerCorner(jrt::Array<Line*>* lines, int32_t no) {
  Line* prevLine = (*lines)[no];
  Line* nextLine = (*lines)[no + 1];
  if (prevLine->isEqualOrOpposite(nextLine)) {
    return nullptr;
  }
  if (!(prevLine->isDiagonal() && nextLine->isDiagonal())) {
    return nullptr;
  }
  FloatPoint* currentCorner = prevLine->intersectionApprox(nextLine);
  FloatPoint* prevCorner = prevLine->intersectionApprox((*lines)[no - 1]);
  FloatPoint* nextCorner = nextLine->intersectionApprox((*lines)[no + 2]);
  int32_t newX = 0;
  int32_t newY = 0;
  bool newLineIsVertical = false;
  bool newLineIsHorizontal = false;
  if (prevCorner->x > currentCorner->x && nextCorner->x > currentCorner->x) {
    newX = jrt::d2i(jrt::Math::ceil(currentCorner->x));
    newY = jrt::d2i(jrt::Math::ceil(currentCorner->y));
    newLineIsVertical = true;
  } else if (prevCorner->x < currentCorner->x && nextCorner->x < currentCorner->x) {
    newX = jrt::d2i(jrt::Math::floor(currentCorner->x));
    newY = jrt::d2i(jrt::Math::floor(currentCorner->y));
    newLineIsVertical = true;
  } else if (prevCorner->y > currentCorner->y && nextCorner->y > currentCorner->y) {
    newX = jrt::d2i(jrt::Math::ceil(currentCorner->x));
    newY = jrt::d2i(jrt::Math::ceil(currentCorner->y));
    newLineIsHorizontal = true;
  } else if (prevCorner->y < currentCorner->y && nextCorner->y < currentCorner->y) {
    newX = jrt::d2i(jrt::Math::floor(currentCorner->x));
    newY = jrt::d2i(jrt::Math::floor(currentCorner->y));
    newLineIsHorizontal = true;
  }
  Direction* newLineDir;
  if (newLineIsVertical) {
    if (prevCorner->y < nextCorner->y) {
      newLineDir = Direction::UP();
    } else {
      newLineDir = Direction::DOWN();
    }
  } else if (newLineIsHorizontal) {
    if (prevCorner->x < nextCorner->x) {
      newLineDir = Direction::RIGHT();
    } else {
      newLineDir = Direction::LEFT();
    }
  } else {
    return nullptr;
  }

  Point* lineA = new (jrt::kGeom) IntPoint(newX, newY);
  return new (jrt::kGeom) Line(lineA, newLineDir);
}

Line* TraceTightener45::smoothenCorner(jrt::Array<Line*>* lines, int32_t no) {
  FloatPoint* prevCorner = (*lines)[no]->intersectionApprox((*lines)[no - 1]);
  FloatPoint* currentCorner = (*lines)[no]->intersectionApprox((*lines)[no + 1]);
  FloatPoint* nextCorner = (*lines)[no + 1]->intersectionApprox((*lines)[no + 2]);

  Direction* prevDir = (*lines)[no]->direction();
  Direction* nextDir = (*lines)[no + 1]->direction();
  Direction* newLineDir = Direction::getInstance(prevDir->getVector()->add(nextDir->getVector()));
  Line* translateLine = Line::getInstance(currentCorner->round(), newLineDir);
  double prevDist = jrt::Math::abs(translateLine->signedDistance(prevCorner));
  double nextDist = jrt::Math::abs(translateLine->signedDistance(nextCorner));
  if (prevDist == 0 || nextDist == 0) {
    return nullptr;
  }
  double maxTranslateDist;
  FloatPoint* nearestCorner;
  if (prevDist <= nextDist) {
    maxTranslateDist = prevDist;
    nearestCorner = prevCorner;
  } else {
    maxTranslateDist = nextDist;
    nearestCorner = nextCorner;
  }
  if (maxTranslateDist < 1) {
    return nullptr;
  }
  maxTranslateDist = jrt::Math::max(maxTranslateDist - 1, 1.0);
  if (translateLine->sideOf(nextCorner) == Side::ON_THE_LEFT()) {
    maxTranslateDist = -maxTranslateDist;
  }
  jrt::Array<Line*>* checkLines = new (jrt::kGeom) jrt::Array<Line*>(3);
  (*checkLines)[0] = (*lines)[no];
  (*checkLines)[2] = (*lines)[no + 1];
  double translateDist = maxTranslateDist;
  double deltaDist = maxTranslateDist;
  Side* sideOfNearestCorner = translateLine->sideOf(nearestCorner);
  int32_t sign = Signum::asInt(maxTranslateDist);
  Line* result = nullptr;
  while (jrt::Math::abs(deltaDist) > this->minTranslateDist) {
    bool checkOk = false;
    Line* newLine = translateLine->translate(translateDist);
    Side* newLineSideOfNearestCorner = newLine->sideOf(nearestCorner);
    if (newLineSideOfNearestCorner == sideOfNearestCorner ||
        newLineSideOfNearestCorner == Side::COLLINEAR()) {
      (*checkLines)[1] = newLine;
      Polyline* tmp = new (jrt::kGeom) Polyline(checkLines);

      if (tmp->lines->length == 3) {
        TileShape* shapeToCheck = tmp->offsetShape(currentHalfWidth, 0);
        checkOk = board->checkTraceShape(shapeToCheck, currentLayer, currentNetNumbers,
                                         currentClearanceClassIndex, this->contactPins);
      }
      deltaDist /= 2;
      if (checkOk) {
        result = (*checkLines)[1];
        if (translateDist == maxTranslateDist) {
          // biggest possible change
          break;
        }
        translateDist += deltaDist;
      } else {
        translateDist -= deltaDist;
      }
    } else {  // moved a little bit to far at the first time because of numerical inaccuracy
      double shortenValue = sign * 0.5;
      maxTranslateDist -= shortenValue;
      translateDist -= shortenValue;
      deltaDist -= shortenValue;
    }
  }
  if (result != nullptr && board->changedArea != nullptr) {
    FloatPoint* newPrevCorner = (*checkLines)[0]->intersectionApprox(result);
    FloatPoint* newNextCorner = (*checkLines)[2]->intersectionApprox(result);
    board->changedArea->join(newPrevCorner, currentLayer);
    board->changedArea->join(newNextCorner, currentLayer);
    board->changedArea->join(currentCorner, currentLayer);
  }
  return result;
}

Polyline* TraceTightener45::smoothenStartCornerAtTrace(PolylineTrace* trace) {
  bool acuteAngle = false;
  bool bend = false;
  FloatPoint* otherTraceCornerApprox = nullptr;
  Line* otherTraceLine = nullptr;
  Line* otherPrevTraceLine = nullptr;
  Polyline* tracePolyline = trace->polyline();
  Point* currentEndCorner = tracePolyline->corner(0);

  if (this->currentClipShape != nullptr && this->currentClipShape->isOutside(currentEndCorner)) {
    return nullptr;
  }

  Point* currentPrevEndCorner = tracePolyline->corner(1);
  Side* prevCornerSide = nullptr;
  Direction* lineDirection = (*tracePolyline->lines)[1]->direction();
  Direction* prevLineDirection = (*tracePolyline->lines)[2]->direction();

  jrt::DisposeOnExit<jrt::Collection<Item*>> contactListOwner(trace->getStartContacts());
  jrt::Collection<Item*>* contactList = contactListOwner.get();
  for (Item* currentContact : *contactList) {
    auto* contactTrace = jrt::fast_cast<PolylineTrace>(currentContact);
    if (contactTrace != nullptr && !currentContact->isShoveFixed()) {
      Polyline* contactTracePolyline = contactTrace->polyline();
      FloatPoint* currentOtherTraceCornerApprox;
      Line* currentOtherTraceLine;
      Line* currentOtherPrevTraceLine;
      if (contactTracePolyline->firstCorner()->equals(currentEndCorner)) {
        currentOtherTraceCornerApprox = contactTracePolyline->cornerApprox(1);
        currentOtherTraceLine = (*contactTracePolyline->lines)[1];
        currentOtherPrevTraceLine = (*contactTracePolyline->lines)[2];
      } else if (contactTracePolyline->lastCorner()->equals(currentEndCorner)) {
        int32_t currentCornerNo = contactTracePolyline->cornerCount() - 2;
        currentOtherTraceCornerApprox = contactTracePolyline->cornerApprox(currentCornerNo);
        currentOtherTraceLine = (*contactTracePolyline->lines)[currentCornerNo + 1]->opposite();
        currentOtherPrevTraceLine = (*contactTracePolyline->lines)[currentCornerNo];
      } else {
        return nullptr;
      }
      Side* currentPrevCornerSide = currentPrevEndCorner->sideOf(currentOtherTraceLine);
      Signum* currentProjection = lineDirection->projection(currentOtherTraceLine->direction());
      bool otherTraceFound = false;
      if (currentProjection == Signum::POSITIVE() && currentPrevCornerSide != Side::COLLINEAR()) {
        if (currentOtherTraceLine->direction()->isOrthogonal()) {
          acuteAngle = true;
          otherTraceFound = true;
        }
      } else if (currentProjection == Signum::ZERO() && tracePolyline->cornerCount() > 2) {
        if (prevLineDirection->projection(currentOtherTraceLine->direction()) ==
            Signum::POSITIVE()) {
          bend = true;
          otherTraceFound = true;
        }
      }
      if (otherTraceFound) {
        otherTraceCornerApprox = currentOtherTraceCornerApprox;
        otherTraceLine = currentOtherTraceLine;
        prevCornerSide = currentPrevCornerSide;
        otherPrevTraceLine = currentOtherPrevTraceLine;
      }
    } else {
      return nullptr;
    }
  }

  if (acuteAngle) {
    Direction* newLineDir;
    if (prevCornerSide == Side::ON_THE_LEFT()) {
      newLineDir = otherTraceLine->direction()->turn45Degree(2);
    } else {
      newLineDir = otherTraceLine->direction()->turn45Degree(6);
    }
    Line* translateLine = Line::getInstance(currentEndCorner->toFloat()->round(), newLineDir);
    double translateDist = (Limits::sqrt2 - 1) * this->currentHalfWidth;
    double prevCornerDist =
        jrt::Math::abs(translateLine->signedDistance(currentPrevEndCorner->toFloat()));
    double otherDist = jrt::Math::abs(translateLine->signedDistance(otherTraceCornerApprox));
    translateDist = jrt::Math::min(translateDist, prevCornerDist);
    translateDist = jrt::Math::min(translateDist, otherDist);
    if (translateDist >= getMinContactChamferRoom()) {
      translateDist = jrt::Math::max(translateDist - 1, 1.0);
      if (translateLine->sideOf(currentPrevEndCorner) == Side::ON_THE_LEFT()) {
        translateDist = -translateDist;
      }
      Line* addLine = translateLine->translate(translateDist);
      // construct the new trace polyline.
      jrt::Array<Line*>* newLines = new (jrt::kGeom) jrt::Array<Line*>(tracePolyline->lines->length + 1);
      (*newLines)[0] = otherTraceLine;
      (*newLines)[1] = addLine;
      jrt::arraycopy(tracePolyline->lines, 1, newLines, 2, newLines->length - 2);
      return new (jrt::kGeom) Polyline(newLines);
    }
  } else if (bend) {
    jrt::Array<Line*>* checkLineArr = new (jrt::kGeom) jrt::Array<Line*>(tracePolyline->lines->length + 1);
    (*checkLineArr)[0] = otherPrevTraceLine;
    (*checkLineArr)[1] = otherTraceLine;
    jrt::arraycopy(tracePolyline->lines, 1, checkLineArr, 2, checkLineArr->length - 2);
    Line* newLine = repositionLine(checkLineArr, 2);
    if (newLine != nullptr) {
      jrt::Array<Line*>* newLines = new (jrt::kGeom) jrt::Array<Line*>(tracePolyline->lines->length);
      (*newLines)[0] = otherTraceLine;
      (*newLines)[1] = newLine;
      jrt::arraycopy(tracePolyline->lines, 2, newLines, 2, newLines->length - 2);
      return new (jrt::kGeom) Polyline(newLines);
    }
  }
  return nullptr;
}

Polyline* TraceTightener45::smoothenEndCornerAtTrace(PolylineTrace* trace) {
  bool acuteAngle = false;
  bool bend = false;
  FloatPoint* otherTraceCornerApprox = nullptr;
  Line* otherTraceLine = nullptr;
  Line* otherPrevTraceLine = nullptr;
  Polyline* tracePolyline = trace->polyline();
  Point* currentEndCorner = tracePolyline->lastCorner();

  if (this->currentClipShape != nullptr && this->currentClipShape->isOutside(currentEndCorner)) {
    return nullptr;
  }

  Point* currentPrevEndCorner = tracePolyline->corner(tracePolyline->cornerCount() - 2);
  Side* prevCornerSide = nullptr;
  Direction* lineDirection =
      (*tracePolyline->lines)[tracePolyline->lines->length - 2]->direction()->opposite();
  Direction* prevLineDirection =
      (*tracePolyline->lines)[tracePolyline->lines->length - 3]->direction()->opposite();

  jrt::DisposeOnExit<jrt::Collection<Item*>> contactListOwner(trace->getEndContacts());
  jrt::Collection<Item*>* contactList = contactListOwner.get();
  for (Item* currentContact : *contactList) {
    auto* contactTrace = jrt::fast_cast<PolylineTrace>(currentContact);
    if (contactTrace != nullptr && !currentContact->isShoveFixed()) {
      Polyline* contactTracePolyline = contactTrace->polyline();
      FloatPoint* currentOtherTraceCornerApprox;
      Line* currentOtherTraceLine;
      Line* currentOtherPrevTraceLine;
      if (contactTracePolyline->firstCorner()->equals(currentEndCorner)) {
        currentOtherTraceCornerApprox = contactTracePolyline->cornerApprox(1);
        currentOtherTraceLine = (*contactTracePolyline->lines)[1];
        currentOtherPrevTraceLine = (*contactTracePolyline->lines)[2];
      } else if (contactTracePolyline->lastCorner()->equals(currentEndCorner)) {
        int32_t currentCornerNo = contactTracePolyline->cornerCount() - 2;
        currentOtherTraceCornerApprox = contactTracePolyline->cornerApprox(currentCornerNo);
        currentOtherTraceLine = (*contactTracePolyline->lines)[currentCornerNo + 1]->opposite();
        currentOtherPrevTraceLine = (*contactTracePolyline->lines)[currentCornerNo];
      } else {
        return nullptr;
      }
      Side* currentPrevCornerSide = currentPrevEndCorner->sideOf(currentOtherTraceLine);
      Signum* currentProjection = lineDirection->projection(currentOtherTraceLine->direction());
      bool otherTraceFound = false;
      if (currentProjection == Signum::POSITIVE() && currentPrevCornerSide != Side::COLLINEAR()) {
        if (currentOtherTraceLine->direction()->isOrthogonal()) {
          acuteAngle = true;
          otherTraceFound = true;
        }
      } else if (currentProjection == Signum::ZERO() && tracePolyline->cornerCount() > 2) {
        if (prevLineDirection->projection(currentOtherTraceLine->direction()) ==
            Signum::POSITIVE()) {
          bend = true;
          otherTraceFound = true;
        }
      }
      if (otherTraceFound) {
        otherTraceCornerApprox = currentOtherTraceCornerApprox;
        otherTraceLine = currentOtherTraceLine;
        prevCornerSide = currentPrevCornerSide;
        otherPrevTraceLine = currentOtherPrevTraceLine;
      }
    } else {
      return nullptr;
    }
  }

  if (acuteAngle) {
    Direction* newLineDir;
    if (prevCornerSide == Side::ON_THE_LEFT()) {
      newLineDir = otherTraceLine->direction()->turn45Degree(6);
    } else {
      newLineDir = otherTraceLine->direction()->turn45Degree(2);
    }
    Line* translateLine = Line::getInstance(currentEndCorner->toFloat()->round(), newLineDir);
    double translateDist = (Limits::sqrt2 - 1) * this->currentHalfWidth;
    double prevCornerDist =
        jrt::Math::abs(translateLine->signedDistance(currentPrevEndCorner->toFloat()));
    double otherDist = jrt::Math::abs(translateLine->signedDistance(otherTraceCornerApprox));
    translateDist = jrt::Math::min(translateDist, prevCornerDist);
    translateDist = jrt::Math::min(translateDist, otherDist);
    if (translateDist >= getMinContactChamferRoom()) {
      translateDist = jrt::Math::max(translateDist - 1, 1.0);
      if (translateLine->sideOf(currentPrevEndCorner) == Side::ON_THE_LEFT()) {
        translateDist = -translateDist;
      }
      Line* addLine = translateLine->translate(translateDist);
      // construct the new trace polyline.
      jrt::Array<Line*>* newLines = new (jrt::kGeom) jrt::Array<Line*>(tracePolyline->lines->length + 1);
      jrt::arraycopy(tracePolyline->lines, 0, newLines, 0, tracePolyline->lines->length - 1);
      (*newLines)[newLines->length - 2] = addLine;
      (*newLines)[newLines->length - 1] = otherTraceLine;
      return new (jrt::kGeom) Polyline(newLines);
    }
  } else if (bend) {
    jrt::Array<Line*>* checkLineArr = new (jrt::kGeom) jrt::Array<Line*>(tracePolyline->lines->length + 1);
    jrt::arraycopy(tracePolyline->lines, 0, checkLineArr, 0, tracePolyline->lines->length - 1);
    (*checkLineArr)[checkLineArr->length - 2] = otherTraceLine;
    (*checkLineArr)[checkLineArr->length - 1] = otherPrevTraceLine;
    Line* newLine = repositionLine(checkLineArr, tracePolyline->lines->length - 2);
    if (newLine != nullptr) {
      jrt::Array<Line*>* newLines = new (jrt::kGeom) jrt::Array<Line*>(tracePolyline->lines->length);
      jrt::arraycopy(tracePolyline->lines, 0, newLines, 0, newLines->length - 2);
      (*newLines)[newLines->length - 2] = newLine;
      (*newLines)[newLines->length - 1] = otherTraceLine;
      return new (jrt::kGeom) Polyline(newLines);
    }
  }
  return nullptr;
}

}  // namespace fr
