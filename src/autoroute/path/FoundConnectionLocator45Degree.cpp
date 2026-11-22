#include "autoroute/path/FoundConnectionLocator45Degree.h"

#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/expansion/ExpansionDoor.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/maze/AutorouteControl.h"
#include "datastructures/Signum.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"

namespace fr {

FloatPoint* FoundConnectionLocator45Degree::roundToInteger(FloatPoint* point) {
  return point->round()->toFloat();
}

bool FoundConnectionLocator45Degree::calcHorizontalFirstFromDoor(ExpandableObject* fromDoor,
                                                                 FloatPoint* fromPoint,
                                                                 FloatPoint* toPoint) {
  TileShape* doorShape = fromDoor->getShape();
  IntBox* fromDoorBox = doorShape->boundingBox();
  if (fromDoor->getDimension() != 1) {
    return fromDoorBox->height() >= fromDoorBox->width();
  }

  FloatLine* doorLineSegment = doorShape->diagonalCornerSegment();
  FloatPoint* leftCorner;
  FloatPoint* rightCorner;
  if (doorLineSegment->a->x < doorLineSegment->b->x
      || (doorLineSegment->a->x == doorLineSegment->b->x
          && doorLineSegment->a->y <= doorLineSegment->b->y)) {
    leftCorner = doorLineSegment->a;
    rightCorner = doorLineSegment->b;
  } else {
    leftCorner = doorLineSegment->b;
    rightCorner = doorLineSegment->a;
  }
  double doorDx = rightCorner->x - leftCorner->x;
  double doorDy = rightCorner->y - leftCorner->y;
  double absDoorDy = jrt::Math::abs(doorDy);
  double doorMaxWidth = jrt::Math::max(doorDx, absDoorDy);
  bool result;
  double doorHalfMaxWidth = 0.5 * doorMaxWidth;
  if (fromDoorBox->width() <= doorHalfMaxWidth) {
    // door is about vertical
    result = true;
  } else if (fromDoorBox->height() <= doorHalfMaxWidth) {
    // door is about horizontal
    result = false;
  } else {
    double dx = toPoint->x - fromPoint->x;
    double dy = toPoint->y - fromPoint->y;
    if (leftCorner->y < rightCorner->y) {
      // door is about right diagonal
      if (Signum::of(dx) == Signum::of(dy)) {
        result = jrt::Math::abs(dx) > jrt::Math::abs(dy);
      } else {
        result = jrt::Math::abs(dx) < jrt::Math::abs(dy);
      }

    } else {
      // door is about left diagonal
      if (Signum::of(dx) == Signum::of(dy)) {
        result = jrt::Math::abs(dx) < jrt::Math::abs(dy);
      } else {
        result = jrt::Math::abs(dx) > jrt::Math::abs(dy);
      }
    }
  }
  return result;
}

jrt::Collection<FloatPoint*>* FoundConnectionLocator45Degree::calculateNextTraceCorners() {
  jrt::Collection<FloatPoint*>* result = new (jrt::kOp) jrt::LinkedList<FloatPoint*>();

  if (this->currentToDoorIndex > this->currentTargetDoorIndex) {
    if (this->ctrl->netNumber == 33 || this->ctrl->netNumber == 66 || this->ctrl->netNumber == 67) {
      FRLogger::trace("compare_trace_next_corners_raw net=" + jrt::str(this->ctrl->netNumber)
                      + ", mode=45, branch=NO_MORE_DOORS" + ", layer="
                      + jrt::str(this->currentTraceLayer)
                      + ", from_door=" + jrt::str(this->currentFromDoorIndex)
                      + ", to_door=" + jrt::str(this->currentToDoorIndex)
                      + ", target_door=" + jrt::str(this->currentTargetDoorIndex)
                      + ", result_size=" + jrt::str(result->size()));
    }
    return result;
  }

  FoundConnectionLocator_BacktrackElement* currentFromInfo =
      (*this->backtrackArray)[this->currentToDoorIndex - 1];

  if (currentFromInfo->nextRoom == nullptr) {
    FRLogger::warn("FoundConnectionLocator45Degree.calculate_next_trace_corners: nextRoom is null");
    return result;
  }

  TileShape* roomShape = currentFromInfo->nextRoom->getShape();

  int32_t traceHalfwidth = (*this->ctrl->compensatedTraceHalfWidth)[this->currentTraceLayer];
  int32_t shrinkOffset;
  if (jrt::fast_cast<ObstacleExpansionRoom>(currentFromInfo->nextRoom) != nullptr) {
    shrinkOffset = traceHalfwidth;
  } else {
    shrinkOffset = this->ctrl->getTraceClearanceOffset(this->currentTraceLayer);
  }
  int32_t traceHalfwidthAdd = shrinkOffset;

  auto* shrinkedRoomShape = jrt::fast_cast<TileShape>(roomShape->offset(-shrinkOffset));
  if (this->ctrl->netNumber == 33 || this->ctrl->netNumber == 66 || this->ctrl->netNumber == 67) {
    FRLogger::trace("compare_trace_room_shrink_raw net=" + jrt::str(this->ctrl->netNumber)
                    + ", mode=45" + ", layer=" + jrt::str(this->currentTraceLayer)
                    + ", from_door=" + jrt::str(this->currentFromDoorIndex)
                    + ", to_door=" + jrt::str(this->currentToDoorIndex)
                    + ", target_door=" + jrt::str(this->currentTargetDoorIndex)
                    + ", next_room_type=" + jrt::className(currentFromInfo->nextRoom)
                    + ", shrinkOffset=" + jrt::str(shrinkOffset)
                    + ", room_empty=" + jrt::str(roomShape->isEmpty())
                    + ", shrinked_empty=" + jrt::str(shrinkedRoomShape->isEmpty())
                    + ", current_from=" + jrt::str(this->currentFromPoint));
  }
  if (!shrinkedRoomShape->isEmpty()) {
    // enter the shrunk room shape by a 45-degree angle first
    FloatPoint* nearestRoomPoint = shrinkedRoomShape->nearestPointApprox(this->currentFromPoint);
    bool horizontalFirst =
        calcHorizontalFirstFromDoor(currentFromInfo->door, this->currentFromPoint, nearestRoomPoint);
    nearestRoomPoint = roundToInteger(nearestRoomPoint);
    result->add(calculateAdditionalCorner(this->currentFromPoint, nearestRoomPoint, horizontalFirst,
                                          this->angleRestriction));
    result->add(nearestRoomPoint);
    this->currentFromPoint = nearestRoomPoint;
  } else {
    shrinkedRoomShape = roomShape;
  }

  if (this->currentToDoorIndex == this->currentTargetDoorIndex) {
    FloatPoint* nearestPoint = this->currentTargetShape->nearestPointApprox(this->currentFromPoint);
    nearestPoint = roundToInteger(nearestPoint);
    FloatPoint* addCorner =
        calculateAdditionalCorner(this->currentFromPoint, nearestPoint, true, this->angleRestriction);
    if (!shrinkedRoomShape->contains(addCorner)) {
      addCorner = calculateAdditionalCorner(this->currentFromPoint, nearestPoint, false,
                                            this->angleRestriction);
    }
    result->add(addCorner);
    result->add(nearestPoint);
    ++this->currentToDoorIndex;
    if (this->ctrl->netNumber == 33 || this->ctrl->netNumber == 66 || this->ctrl->netNumber == 67) {
      FRLogger::trace("compare_trace_next_corners_raw net=" + jrt::str(this->ctrl->netNumber)
                      + ", mode=45, branch=TARGET_DOOR" + ", layer="
                      + jrt::str(this->currentTraceLayer)
                      + ", from_door=" + jrt::str(this->currentFromDoorIndex)
                      + ", to_door=" + jrt::str(this->currentToDoorIndex)
                      + ", target_door=" + jrt::str(this->currentTargetDoorIndex)
                      + ", result_size=" + jrt::str(result->size())
                      + ", nearestPoint=" + jrt::str(nearestPoint)
                      + ", addCorner=" + jrt::str(addCorner));
    }
    return result;
  }

  FoundConnectionLocator_BacktrackElement* currentToInfo =
      (*this->backtrackArray)[this->currentToDoorIndex];
  auto* currentToDoor = jrt::fast_cast<ExpansionDoor>(currentToInfo->door);
  if (currentToDoor == nullptr) {
    FRLogger::warn(
        "FoundConnectionLocator45Degree.calculate_next_trace_corners: ExpansionDoor expected");
    return result;
  }

  FloatPoint* nearestToDoorPoint;
  if (currentToDoor->dimension == 2) {
    // May not happen in free angle routing mode because then corners are cut off.
    TileShape* toDoorShape = currentToDoor->getShape();

    auto* shrinkedToDoorShape = jrt::fast_cast<TileShape>(toDoorShape->shrink(shrinkOffset));
    nearestToDoorPoint = shrinkedToDoorShape->nearestPointApprox(this->currentFromPoint);
    nearestToDoorPoint = roundToInteger(nearestToDoorPoint);
  } else {
    jrt::Array<FloatLine*>* lineSections = currentToDoor->getSectionSegments(traceHalfwidth);
    if (currentToInfo->sectionNoOfDoor >= lineSections->length) {
      FRLogger::warn(
          "FoundConnectionLocator45Degree.calculate_next_trace_corners: "
          "lineSections inconsistent");
      return result;
    }
    FloatLine* currentLineSection = (*lineSections)[currentToInfo->sectionNoOfDoor];
    double sectionLen = currentLineSection->b->distance(currentLineSection->a);
    if (sectionLen <= 2.5 * traceHalfwidthAdd) {
      nearestToDoorPoint = currentLineSection->a->middlePoint(currentLineSection->b);
    } else {
      FloatLine* safeLineSection = currentLineSection->shrinkSegment(traceHalfwidthAdd);
      nearestToDoorPoint = safeLineSection->nearestSegmentPoint(this->currentFromPoint);
    }

    bool nearestToDoorPointOk = true;
    if (currentToInfo->nextRoom != nullptr) {
      Simplex* nextRoomShape = currentToInfo->nextRoom->getShape()->toSimplex();
      // with IntBox or IntOctagon the next calculation will not work, because they have
      // border lines of length 0.
      jrt::Array<FloatPoint*>* nearestPoints =
          nextRoomShape->nearestBorderPointsApprox(nearestToDoorPoint, 2);
      if (nearestPoints->length >= 2) {
        nearestToDoorPointOk = (*nearestPoints)[1]->distance(nearestToDoorPoint) >= traceHalfwidthAdd;
      }
    }
    if (nearestToDoorPointOk) {
      Simplex* prevRoomShape = currentFromInfo->nextRoom->getShape()->toSimplex();
      jrt::Array<FloatPoint*>* prevNearestPoints =
          prevRoomShape->nearestBorderPointsApprox(nearestToDoorPoint, 2);
      if (prevNearestPoints->length >= 2) {
        nearestToDoorPointOk =
            (*prevNearestPoints)[1]->distance(nearestToDoorPoint) >= traceHalfwidthAdd;
      }
    }
    if (!nearestToDoorPointOk) {
      // may be the room has an acute (45 degree) angle at a corner of the door
      nearestToDoorPoint = currentLineSection->a->middlePoint(currentLineSection->b);
    }
  }
  nearestToDoorPoint = roundToInteger(nearestToDoorPoint);
  bool horizontalFirst =
      calcHorizontalFirstToDoor(currentToInfo->door, this->currentFromPoint, nearestToDoorPoint);
  result->add(calculateAdditionalCorner(this->currentFromPoint, nearestToDoorPoint, horizontalFirst,
                                        this->angleRestriction));
  result->add(nearestToDoorPoint);
  ++this->currentToDoorIndex;
  if (this->ctrl->netNumber == 33 || this->ctrl->netNumber == 66 || this->ctrl->netNumber == 67) {
    FRLogger::trace("compare_trace_next_corners_raw net=" + jrt::str(this->ctrl->netNumber)
                    + ", mode=45, branch=EXPANSION_DOOR" + ", layer="
                    + jrt::str(this->currentTraceLayer)
                    + ", from_door=" + jrt::str(this->currentFromDoorIndex)
                    + ", to_door=" + jrt::str(this->currentToDoorIndex)
                    + ", target_door=" + jrt::str(this->currentTargetDoorIndex)
                    + ", result_size=" + jrt::str(result->size())
                    + ", nearestToDoorPoint=" + jrt::str(nearestToDoorPoint)
                    + ", horizontalFirst=" + jrt::str(horizontalFirst));
  }
  return result;
}

bool FoundConnectionLocator45Degree::calcHorizontalFirstToDoor(ExpandableObject* toDoor,
                                                               FloatPoint* fromPoint,
                                                               FloatPoint* toPoint) {
  TileShape* doorShape = toDoor->getShape();
  IntBox* fromDoorBox = doorShape->boundingBox();
  if (toDoor->getDimension() != 1) {
    return fromDoorBox->height() <= fromDoorBox->width();
  }
  FloatLine* doorLineSegment = doorShape->diagonalCornerSegment();
  FloatPoint* leftCorner;
  FloatPoint* rightCorner;
  if (doorLineSegment->a->x < doorLineSegment->b->x
      || (doorLineSegment->a->x == doorLineSegment->b->x
          && doorLineSegment->a->y <= doorLineSegment->b->y)) {
    leftCorner = doorLineSegment->a;
    rightCorner = doorLineSegment->b;
  } else {
    leftCorner = doorLineSegment->b;
    rightCorner = doorLineSegment->a;
  }
  double doorDx = rightCorner->x - leftCorner->x;
  double doorDy = rightCorner->y - leftCorner->y;
  double absDoorDy = jrt::Math::abs(doorDy);
  double doorMaxWidth = jrt::Math::max(doorDx, absDoorDy);
  bool result;
  double doorHalfMaxWidth = 0.5 * doorMaxWidth;
  if (fromDoorBox->width() <= doorHalfMaxWidth) {
    // door is about vertical
    result = false;
  } else if (fromDoorBox->height() <= doorHalfMaxWidth) {
    // door is about horizontal
    result = true;
  } else {
    double dx = toPoint->x - fromPoint->x;
    double dy = toPoint->y - fromPoint->y;
    if (leftCorner->y < rightCorner->y) {
      // door is about right diagonal
      if (Signum::of(dx) == Signum::of(dy)) {
        result = jrt::Math::abs(dx) < jrt::Math::abs(dy);
      } else {
        result = jrt::Math::abs(dx) > jrt::Math::abs(dy);
      }

    } else {
      // door is about left diagonal
      if (Signum::of(dx) == Signum::of(dy)) {
        result = jrt::Math::abs(dx) > jrt::Math::abs(dy);
      } else {
        result = jrt::Math::abs(dx) < jrt::Math::abs(dy);
      }
    }
  }
  return result;
}

}  // namespace fr
