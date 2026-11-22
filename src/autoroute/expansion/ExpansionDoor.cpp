#include "autoroute/expansion/ExpansionDoor.h"

#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/CompleteFreeSpaceExpansionRoom.h"
#include "autoroute/expansion/ExpansionRoom.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "autoroute/maze/MazeSearchElement.h"
#include "geometry/planar/FloatLine.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/TileShape.h"

namespace fr {

ExpansionDoor::ExpansionDoor(ExpansionRoom* firstRoom, ExpansionRoom* secondRoom)
    : firstRoom(firstRoom), secondRoom(secondRoom) {
  this->dimension = firstRoom->getShape()->intersection(secondRoom->getShape())->dimension();
}

TileShape* ExpansionDoor::getShape() {
  TileShape* firstShape = firstRoom->getShape();
  TileShape* secondShape = secondRoom->getShape();
  TileShape* cached = cachedShape;
  if (cached != nullptr && firstShape == cachedFirstRoomShape && secondShape == cachedSecondRoomShape) {
    return cached;
  }
  TileShape* result = firstShape->intersection(secondShape);
  cachedFirstRoomShape = firstShape;
  cachedSecondRoomShape = secondShape;
  cachedShape = result;
  return result;
}

ExpansionRoom* ExpansionDoor::otherRoom(ExpansionRoom* room) {
  ExpansionRoom* result;
  if (room == firstRoom) {
    result = secondRoom;
  } else if (room == secondRoom) {
    result = firstRoom;
  } else {
    result = nullptr;
  }
  return result;
}

CompleteExpansionRoom* ExpansionDoor::otherRoom(CompleteExpansionRoom* room) {
  ExpansionRoom* roomAsExpansionRoom = room;
  ExpansionRoom* result;
  if (roomAsExpansionRoom == firstRoom) {
    result = secondRoom;
  } else if (roomAsExpansionRoom == secondRoom) {
    result = firstRoom;
  } else {
    result = nullptr;
  }
  if (jrt::fast_cast<CompleteExpansionRoom>(result) == nullptr) {
    result = nullptr;
  }
  return jrt::fast_cast<CompleteExpansionRoom>(result);
}

jrt::Array<FloatLine*>* ExpansionDoor::getSectionSegments(double offsetParam) {
  TileShape* doorShape = this->getShape();
  jrt::Array<FloatLine*>* cached = cachedSectionSegments;
  if (cached != nullptr && doorShape == cachedSectionShape &&
      jrt::Double::doubleToLongBits(offsetParam) == jrt::Double::doubleToLongBits(cachedSectionOffset)) {
    if (cachedSectionCount > 0) {
      this->allocateSections(cachedSectionCount);
    }
    return cached;
  }
  double offset = offsetParam + AutorouteEngine::TRACE_WIDTH_TOLERANCE;
  if (doorShape->isEmpty()) {
    return new jrt::Array<FloatLine*>(0);
  }
  FloatLine* doorLineSegment;
  FloatLine* shrinkedLineSegment;
  if (this->dimension == 1) {
    doorLineSegment = doorShape->diagonalCornerSegment();
    shrinkedLineSegment = doorLineSegment->shrinkSegment(offset);
  } else if (this->dimension == 2 && jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(this->firstRoom) != nullptr &&
             jrt::fast_cast<CompleteFreeSpaceExpansionRoom>(this->secondRoom) != nullptr) {
    // Overlapping doors at a corner possible in case of 90- or 45-degree routing.
    // In case of freeangle routing the corners are cut off.
    doorLineSegment = calcDoorLineSegment(doorShape);
    if (doorLineSegment == nullptr) {
      // CompleteFreeSpaceExpansionRoom inside other room
      return new jrt::Array<FloatLine*>(0);
    }
    if (doorLineSegment->b->distanceSquare(doorLineSegment->a) < 4 * offset * offset) {
      // door is small, 2 dimensional small doors are not yet expanded.
      return new jrt::Array<FloatLine*>(0);
    }
    shrinkedLineSegment = doorLineSegment->shrinkSegment(offset);
  } else {
    FloatPoint* gravityPoint = doorShape->centreOfGravity();
    doorLineSegment = new FloatLine(gravityPoint, gravityPoint);
    shrinkedLineSegment = doorLineSegment;
  }
  const double maxDoorSectionWidth = 10 * offset;
  int32_t sectionCount = jrt::d2i(doorLineSegment->b->distance(doorLineSegment->a) / maxDoorSectionWidth) + 1;
  this->allocateSections(sectionCount);
  jrt::Array<FloatLine*>* result = shrinkedLineSegment->divideSegmentIntoSections(sectionCount);
  cachedSectionShape = doorShape;
  cachedSectionOffset = offsetParam;
  cachedSectionCount = sectionCount;
  cachedSectionSegments = result;
  return result;
}

FloatLine* ExpansionDoor::calcDoorLineSegment(TileShape* doorShape) {
  TileShape* firstRoomShape = this->firstRoom->getShape();
  TileShape* secondRoomShape = this->secondRoom->getShape();
  Point* firstCorner = nullptr;
  Point* secondCorner = nullptr;
  int32_t cornerCount = doorShape->borderLineCount();
  for (int32_t i = 0; i < cornerCount; i++) {
    Point* currentCorner = doorShape->corner(i);
    if (!firstRoomShape->containsInside(currentCorner) && !secondRoomShape->containsInside(currentCorner)) {
      // currentCorner is on the border of both room shapes.
      if (firstCorner == nullptr) {
        firstCorner = currentCorner;
      } else if (!firstCorner->equals(currentCorner)) {
        secondCorner = currentCorner;
        break;
      }
    }
  }
  if (firstCorner == nullptr || secondCorner == nullptr) {
    return nullptr;
  }
  return new FloatLine(firstCorner->toFloat(), secondCorner->toFloat());
}

void ExpansionDoor::reset() {
  if (sectionArr != nullptr) {
    for (MazeSearchElement* currentSection : *sectionArr) {
      currentSection->reset();
    }
  }
}

int32_t ExpansionDoor::getId() {
  int32_t id1 = firstRoom->getId();
  int32_t id2 = secondRoom->getId();
  // Use a stable combination of room IDs. Note: min/max ensures order-independence.
  return jrt::Math::min(id1, id2) * 31 + jrt::Math::max(id1, id2);
}

void ExpansionDoor::allocateSections(int32_t sectionCount) {
  if (sectionArr != nullptr && sectionArr->length == sectionCount) {
    return;  // already allocated
  }
  sectionArr = new jrt::Array<MazeSearchElement*>(sectionCount);
  for (int32_t i = 0; i < sectionArr->length; i++) {
    (*sectionArr)[i] = new MazeSearchElement();
  }
}

}  // namespace fr
