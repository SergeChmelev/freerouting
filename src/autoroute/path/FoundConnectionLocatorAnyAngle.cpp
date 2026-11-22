#include "autoroute/path/FoundConnectionLocatorAnyAngle.h"

#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/maze/AutorouteControl.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Side.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

FloatPoint* FoundConnectionLocatorAnyAngle::calcDoorLeftCorner(
    FoundConnectionLocator_BacktrackElement* toInfo) {
  CompleteExpansionRoom* fromRoom = toInfo->door->otherRoom(toInfo->nextRoom);
  FloatPoint* pole = fromRoom->getShape()->centreOfGravity();
  TileShape* currentToDoorShape = toInfo->door->getShape();
  int32_t leftMostCornerNo = currentToDoorShape->indexOfLeftMostCorner(pole);
  return currentToDoorShape->cornerApprox(leftMostCornerNo);
}

FloatPoint* FoundConnectionLocatorAnyAngle::calcDoorRightCorner(
    FoundConnectionLocator_BacktrackElement* toInfo) {
  CompleteExpansionRoom* fromRoom = toInfo->door->otherRoom(toInfo->nextRoom);
  FloatPoint* pole = fromRoom->getShape()->centreOfGravity();
  TileShape* currentToDoorShape = toInfo->door->getShape();
  int32_t rightMostCornerNo = currentToDoorShape->indexOfRightMostCorner(pole);
  return currentToDoorShape->cornerApprox(rightMostCornerNo);
}

jrt::Collection<FloatPoint*>* FoundConnectionLocatorAnyAngle::calculateNextTraceCorners() {
  jrt::Collection<FloatPoint*>* result = new jrt::LinkedList<FloatPoint*>();
  if (this->currentToDoorIndex >= this->currentTargetDoorIndex) {
    if (this->currentToDoorIndex == this->currentTargetDoorIndex) {
      FloatPoint* nearestPoint =
          this->currentTargetShape->nearestPoint(this->currentFromPoint->round())->toFloat();
      ++this->currentToDoorIndex;
      result->add(nearestPoint);
    }
    return result;
  }

  double traceHalfwidthExact = (*this->ctrl->compensatedTraceHalfWidth)[this->currentTraceLayer];
  double traceHalfwidthMax = this->ctrl->getTraceClearanceOffset(this->currentTraceLayer);
  const double traceHalfwidthMiddle = traceHalfwidthExact + cTolerance;

  FoundConnectionLocator_BacktrackElement* currentToInfo =
      (*this->backtrackArray)[this->currentToDoorIndex];
  FloatPoint* doorLeftCorner = calcDoorLeftCorner(currentToInfo);
  FloatPoint* doorRightCorner = calcDoorRightCorner(currentToInfo);
  if (this->currentFromPoint->sideOf(doorLeftCorner, doorRightCorner) != Side::ON_THE_RIGHT()) {
    // the door is already crossed at this.fromPoint
    if (this->currentFromPoint->scalarProduct(this->previousFromPoint, doorLeftCorner) >= 0) {
      // Also the left corner of the door is passed.
      // That may not be the case if the door line is crossed almost parallel.
      doorLeftCorner = nullptr;
    }
    if (this->currentFromPoint->scalarProduct(this->previousFromPoint, doorRightCorner) >= 0) {
      // Also the right corner of the door is passed.
      doorRightCorner = nullptr;
    }
    if (doorLeftCorner == nullptr && doorRightCorner == nullptr) {
      // The door is completely passed.
      ++this->currentToDoorIndex;
      result->add(this->currentFromPoint);
      return result;
    }
  }

  // Calculate the visibility range for a trace line from currentFromPoint
  // through the interval from left_most_visible_point to right_most_visible_point,
  // by advancing the door index as far as possible, so that still something is visible.

  bool endOfTrace = false;
  FloatPoint* leftTangentPoint;
  FloatPoint* rightTangentPoint;
  int32_t newDoorInd = this->currentToDoorIndex;
  int32_t leftInd = newDoorInd;
  int32_t rightInd = newDoorInd;
  int32_t currentDoorInd = this->currentToDoorIndex + 1;
  FloatPoint* resultCorner = nullptr;

  // construct a maximum length straight line through the doors

  for (;;) {
    leftTangentPoint = this->currentFromPoint->rightTangentialPoint(doorLeftCorner, traceHalfwidthMax);
    if (doorLeftCorner != nullptr && leftTangentPoint == nullptr) {
      FRLogger::trace(
          "FoundConnectionLocator.calculate_next_trace_corner: left tangent point is null");
      leftTangentPoint = doorLeftCorner;
    }
    rightTangentPoint =
        this->currentFromPoint->leftTangentialPoint(doorRightCorner, traceHalfwidthMax);
    if (doorRightCorner != nullptr && rightTangentPoint == nullptr) {
      FRLogger::trace(
          "FoundConnectionLocator.calculate_next_trace_corner: right tangent point is null");
      rightTangentPoint = doorRightCorner;
    }
    if (leftTangentPoint != nullptr && rightTangentPoint != nullptr
        && rightTangentPoint->sideOf(this->currentFromPoint, leftTangentPoint)
               != Side::ON_THE_RIGHT()) {
      // The gap between  left_most_visible_point and right_most_visible_point is too small
      // for a trace with the current half width.

      double leftCornerDistance = doorLeftCorner->distance(this->currentFromPoint);
      double rightCornerDistance = doorRightCorner->distance(this->currentFromPoint);
      if (leftCornerDistance <= rightCornerDistance) {
        newDoorInd = leftInd;
        resultCorner = leftTurnNextCorner(this->currentFromPoint, traceHalfwidthMax, doorLeftCorner,
                                          doorRightCorner);
      } else {
        newDoorInd = rightInd;
        resultCorner = rightTurnNextCorner(this->currentFromPoint, traceHalfwidthMax,
                                           doorRightCorner, doorLeftCorner);
      }
      break;
    }
    if (currentDoorInd >= this->currentTargetDoorIndex) {
      endOfTrace = true;
      break;
    }
    FoundConnectionLocator_BacktrackElement* nextToInfo = (*this->backtrackArray)[currentDoorInd];
    FloatPoint* nextLeftCorner = calcDoorLeftCorner(nextToInfo);
    FloatPoint* nextRightCorner = calcDoorRightCorner(nextToInfo);
    if (this->currentFromPoint->sideOf(nextLeftCorner, nextRightCorner) != Side::ON_THE_RIGHT()) {
      // the door may be already crossed at this.fromPoint
      if (doorLeftCorner == nullptr
          && this->currentFromPoint->scalarProduct(this->previousFromPoint, nextLeftCorner) >= 0) {
        // Also the left corner of the door is passed.
        // That may not be the case if the door line is crossed almost parallel.
        nextLeftCorner = nullptr;
      }
      if (doorRightCorner == nullptr
          && this->currentFromPoint->scalarProduct(this->previousFromPoint, nextRightCorner) >= 0) {
        // Also the right corner of the door is passed.
        nextRightCorner = nullptr;
      }
      if (nextLeftCorner == nullptr && nextRightCorner == nullptr) {
        // The door is completely passed.
        // Should not happen because the previous door was not passed completely.
        FRLogger::trace(
            "FoundConnectionLocator.calculate_next_trace_corner: next door passed unexpected");
        ++this->currentToDoorIndex;
        result->add(this->currentFromPoint);
        return result;
      }
    }
    if (doorLeftCorner != nullptr && doorRightCorner != nullptr) {
      // otherwise the following sideOf conditions may not be correct
      // even if all parameter points are defined
      if (nextLeftCorner->sideOf(this->currentFromPoint, doorRightCorner) == Side::ON_THE_RIGHT()) {
        // bend to the right
        newDoorInd = rightInd + 1;
        resultCorner = rightTurnNextCorner(this->currentFromPoint, traceHalfwidthMax,
                                           doorRightCorner, nextLeftCorner);
        break;
      }

      if (nextRightCorner->sideOf(this->currentFromPoint, doorLeftCorner) == Side::ON_THE_LEFT()) {
        // bend to the left
        newDoorInd = leftInd + 1;
        resultCorner = leftTurnNextCorner(this->currentFromPoint, traceHalfwidthMax,
                                          doorLeftCorner, nextRightCorner);
        break;
      }
    }
    bool visabilityRangeGetsSmallerOnTheRightSide = doorRightCorner == nullptr;
    if (doorRightCorner != nullptr
        && nextRightCorner->sideOf(this->currentFromPoint, doorRightCorner)
               != Side::ON_THE_RIGHT()) {
      FloatPoint* currentTangentialPoint =
          this->currentFromPoint->leftTangentialPoint(nextRightCorner, traceHalfwidthMax);
      if (currentTangentialPoint != nullptr) {
        FloatLine* checkLine = new FloatLine(this->currentFromPoint, currentTangentialPoint);
        if (checkLine->segmentDistance(doorRightCorner) >= traceHalfwidthMax) {
          visabilityRangeGetsSmallerOnTheRightSide = true;
        }
      }
    }
    if (visabilityRangeGetsSmallerOnTheRightSide) {
      // The visibility range gets smaller on the right side.
      doorRightCorner = nextRightCorner;
      rightInd = currentDoorInd;
    }
    bool visabilityRangeGetsSmallerOnTheLeftSide = doorLeftCorner == nullptr;
    if (doorLeftCorner != nullptr
        && nextLeftCorner->sideOf(this->currentFromPoint, doorLeftCorner) != Side::ON_THE_LEFT()) {
      FloatPoint* currentTangentialPoint =
          this->currentFromPoint->rightTangentialPoint(nextLeftCorner, traceHalfwidthMax);
      if (currentTangentialPoint != nullptr) {
        FloatLine* checkLine = new FloatLine(this->currentFromPoint, currentTangentialPoint);
        if (checkLine->segmentDistance(doorLeftCorner) >= traceHalfwidthMax) {
          visabilityRangeGetsSmallerOnTheLeftSide = true;
        }
      }
    }
    if (visabilityRangeGetsSmallerOnTheLeftSide) {
      // The visibility range gets smaller on the left side.
      doorLeftCorner = nextLeftCorner;
      leftInd = currentDoorInd;
    }
    ++currentDoorInd;
  }

  if (endOfTrace) {
    FloatPoint* nearestPoint =
        this->currentTargetShape->nearestPoint(this->currentFromPoint->round())->toFloat();
    resultCorner = nearestPoint;
    if (leftTangentPoint != nullptr
        && nearestPoint->sideOf(this->currentFromPoint, leftTangentPoint) == Side::ON_THE_LEFT()) {
      // The nearest target point is to the left of the visible range, add another corner
      newDoorInd = leftInd + 1;
      FloatPoint* targetRightCorner = this->currentTargetShape->cornerApprox(
          this->currentTargetShape->indexOfRightMostCorner(this->currentFromPoint));
      FloatPoint* currentCorner = rightLeftTangentialPoint(this->currentFromPoint, targetRightCorner,
                                                           doorLeftCorner, traceHalfwidthMax);
      if (currentCorner != nullptr) {
        resultCorner = currentCorner;
        endOfTrace = false;
      }
    } else if (rightTangentPoint != nullptr
               && nearestPoint->sideOf(this->currentFromPoint, rightTangentPoint)
                      == Side::ON_THE_RIGHT()) {
      // The nearest target point is to the right of the visible range, add another corner
      FloatPoint* targetLeftCorner = this->currentTargetShape->cornerApprox(
          this->currentTargetShape->indexOfLeftMostCorner(this->currentFromPoint));
      newDoorInd = rightInd + 1;
      FloatPoint* currentCorner = leftRightTangentialPoint(this->currentFromPoint, targetLeftCorner,
                                                           doorRightCorner, traceHalfwidthMax);
      if (currentCorner != nullptr) {
        resultCorner = currentCorner;
        endOfTrace = false;
      }
    }
  }
  if (endOfTrace) {
    newDoorInd = this->currentTargetDoorIndex;
  }

  // Check clearance violation with the previous door shapes
  // and correct them in this case.

  FloatLine* checkLine = new FloatLine(this->currentFromPoint, resultCorner);
  int32_t checkFromDoorIndex =
      jrt::Math::max(this->currentToDoorIndex - 5, this->currentFromDoorIndex + 1);
  FloatPoint* correctedResult = nullptr;
  int32_t correctedDoorInd = 0;
  for (int32_t i = checkFromDoorIndex; i < newDoorInd; i++) {
    FloatPoint* currentLeftCorner = calcDoorLeftCorner((*this->backtrackArray)[i]);
    double currentDistance = checkLine->segmentDistance(currentLeftCorner);
    if (jrt::Math::abs(currentDistance) < traceHalfwidthMiddle) {
      FloatPoint* currentCorrectedResult =
          rightLeftTangentialPoint(checkLine->a, checkLine->b, currentLeftCorner, traceHalfwidthMax);
      if (currentCorrectedResult != nullptr) {
        if (correctedResult == nullptr
            || currentCorrectedResult->sideOf(this->currentFromPoint, correctedResult)
                   == Side::ON_THE_RIGHT()) {
          correctedDoorInd = i;
          correctedResult = currentCorrectedResult;
        }
      }
    }
    FloatPoint* currentRightCorner = calcDoorRightCorner((*this->backtrackArray)[i]);
    currentDistance = checkLine->segmentDistance(currentRightCorner);
    if (jrt::Math::abs(currentDistance) < traceHalfwidthMiddle) {
      FloatPoint* currentCorrectedResult = leftRightTangentialPoint(
          checkLine->a, checkLine->b, currentRightCorner, traceHalfwidthMax);
      if (currentCorrectedResult != nullptr) {
        if (correctedResult == nullptr
            || currentCorrectedResult->sideOf(this->currentFromPoint, correctedResult)
                   == Side::ON_THE_LEFT()) {
          correctedDoorInd = i;
          correctedResult = currentCorrectedResult;
        }
      }
    }
  }
  if (correctedResult != nullptr) {
    resultCorner = correctedResult;
    newDoorInd = jrt::Math::max(correctedDoorInd, this->currentToDoorIndex);
  }

  this->currentToDoorIndex = newDoorInd;
  if (resultCorner != nullptr && resultCorner != this->currentFromPoint) {
    result->add(resultCorner);
  }
  if (this->ctrl->netNumber == 33 || this->ctrl->netNumber == 66 || this->ctrl->netNumber == 67) {
    FRLogger::trace("compare_trace_next_corners_raw net=" + jrt::str(this->ctrl->netNumber)
                    + ", layer=" + jrt::str(this->currentTraceLayer)
                    + ", from_door=" + jrt::str(this->currentFromDoorIndex)
                    + ", to_door=" + jrt::str(this->currentToDoorIndex)
                    + ", target_door=" + jrt::str(this->currentTargetDoorIndex)
                    + ", endOfTrace=" + jrt::str(endOfTrace)
                    + ", corrected=" + jrt::str(correctedResult != nullptr)
                    + ", result_size=" + jrt::str(result->size())
                    + ", resultCorner=" + jrt::str(resultCorner)
                    + ", current_from=" + jrt::str(this->currentFromPoint));
  }
  return result;
}

FloatPoint* FoundConnectionLocatorAnyAngle::rightTurnNextCorner(FloatPoint* fromCorner, double dist,
                                                                FloatPoint* toCorner,
                                                                FloatPoint* nextCorner) {
  FloatPoint* currentTangentialPoint = fromCorner->leftTangentialPoint(toCorner, dist);
  if (currentTangentialPoint == nullptr) {
    FRLogger::trace("FoundConnectionLocator.right_turn_next_corner: left tangential point is null");
    return fromCorner;
  }
  FloatLine* const firstLine = new FloatLine(fromCorner, currentTangentialPoint);
  currentTangentialPoint = toCorner->rightTangentialPoint(nextCorner, 2 * dist + cTolerance);
  if (currentTangentialPoint == nullptr) {
    FRLogger::trace("FoundConnectionLocator.right_turn_next_corner: right tangential point is null");
    return fromCorner;
  }
  FloatLine* secondLine = new FloatLine(toCorner, currentTangentialPoint);
  secondLine = secondLine->translate(dist);
  return firstLine->intersection(secondLine);
}

FloatPoint* FoundConnectionLocatorAnyAngle::leftTurnNextCorner(FloatPoint* fromCorner, double dist,
                                                               FloatPoint* toCorner,
                                                               FloatPoint* nextCorner) {
  FloatPoint* currentTangentialPoint = fromCorner->rightTangentialPoint(toCorner, dist);
  if (currentTangentialPoint == nullptr) {
    FRLogger::trace("FoundConnectionLocator.left_turn_next_corner: right tangential point is null");
    return fromCorner;
  }
  FloatLine* const firstLine = new FloatLine(fromCorner, currentTangentialPoint);
  currentTangentialPoint = toCorner->leftTangentialPoint(nextCorner, 2 * dist + cTolerance);
  if (currentTangentialPoint == nullptr) {
    FRLogger::trace("FoundConnectionLocator.left_turn_next_corner: left tangential point is null");
    return fromCorner;
  }
  FloatLine* secondLine = new FloatLine(toCorner, currentTangentialPoint);
  secondLine = secondLine->translate(-dist);
  return firstLine->intersection(secondLine);
}

FloatPoint* FoundConnectionLocatorAnyAngle::rightLeftTangentialPoint(FloatPoint* fromPoint,
                                                                     FloatPoint* toPoint,
                                                                     FloatPoint* center,
                                                                     double dist) {
  FloatPoint* currentTangentialPoint = fromPoint->rightTangentialPoint(center, dist);
  if (currentTangentialPoint == nullptr) {
    FRLogger::trace(
        "FoundConnectionLocator. right_left_tangential_point: right tangential point is null");
    return nullptr;
  }
  FloatLine* firstLine = new FloatLine(fromPoint, currentTangentialPoint);
  currentTangentialPoint = toPoint->leftTangentialPoint(center, dist);
  if (currentTangentialPoint == nullptr) {
    FRLogger::trace(
        "FoundConnectionLocator. right_left_tangential_point: left tangential point is null");
    return nullptr;
  }
  FloatLine* secondLine = new FloatLine(toPoint, currentTangentialPoint);
  return firstLine->intersection(secondLine);
}

FloatPoint* FoundConnectionLocatorAnyAngle::leftRightTangentialPoint(FloatPoint* fromPoint,
                                                                     FloatPoint* toPoint,
                                                                     FloatPoint* center,
                                                                     double dist) {
  FloatPoint* currentTangentialPoint = fromPoint->leftTangentialPoint(center, dist);
  if (currentTangentialPoint == nullptr) {
    FRLogger::trace(
        "FoundConnectionLocator. left_right_tangential_point: left tangential point is null");
    return nullptr;
  }
  FloatLine* firstLine = new FloatLine(fromPoint, currentTangentialPoint);
  currentTangentialPoint = toPoint->rightTangentialPoint(center, dist);
  if (currentTangentialPoint == nullptr) {
    FRLogger::trace(
        "FoundConnectionLocator. left_right_tangential_point: right tangential point is null");
    return nullptr;
  }
  FloatLine* secondLine = new FloatLine(toPoint, currentTangentialPoint);
  return firstLine->intersection(secondLine);
}

}  // namespace fr
