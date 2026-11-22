#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Which neighbour calculation applies to a search tree (Java: package-private nested enum). */
enum class SortedRoomNeighbours_CalculationMode : int32_t { ORTHOGONAL, DEGREE_45, ANY_ANGLE };

inline std::string SortedRoomNeighbours_CalculationMode_name(SortedRoomNeighbours_CalculationMode e) {
  switch (e) {
    case SortedRoomNeighbours_CalculationMode::ORTHOGONAL: return "ORTHOGONAL";
    case SortedRoomNeighbours_CalculationMode::DEGREE_45: return "DEGREE_45";
    case SortedRoomNeighbours_CalculationMode::ANY_ANGLE: return "ANY_ANGLE";
  }
  return "";
}
inline std::string SortedRoomNeighbours_CalculationMode_toString(SortedRoomNeighbours_CalculationMode e) {
  return SortedRoomNeighbours_CalculationMode_name(e);
}
inline jrt::Array<SortedRoomNeighbours_CalculationMode>* SortedRoomNeighbours_CalculationMode_values() {
  return jrt::Array<SortedRoomNeighbours_CalculationMode>::of(
      {SortedRoomNeighbours_CalculationMode::ORTHOGONAL, SortedRoomNeighbours_CalculationMode::DEGREE_45,
       SortedRoomNeighbours_CalculationMode::ANY_ANGLE});
}
inline SortedRoomNeighbours_CalculationMode SortedRoomNeighbours_CalculationMode_valueOf(const std::string& s) {
  if (s == "ORTHOGONAL") return SortedRoomNeighbours_CalculationMode::ORTHOGONAL;
  if (s == "DEGREE_45") return SortedRoomNeighbours_CalculationMode::DEGREE_45;
  if (s == "ANY_ANGLE") return SortedRoomNeighbours_CalculationMode::ANY_ANGLE;
  throw jrt::IllegalArgumentException("No enum constant SortedRoomNeighbours.CalculationMode." + s);
}

/**
 * Helper class to sort the doors of an expansion room counterclockwise around the border of the
 * room shape. Java: private inner class (Comparable) -> explicit `outer` pointer.
 */
class SortedRoomNeighbours_SortedRoomNeighbour : public virtual jrt::Object {
 private:
  static constexpr double c_dist_tolerance = 1;

 public:
  SortedRoomNeighbours* outer;

  /** The search tree object of the neighbour room. */
  SearchTreeObject* searchTreeObject;

  /** The shape of the neighbour room. */
  TileShape* neighbourShape;

  /** The intersection of this ExpansionRoom shape with the neighbourShape. */
  TileShape* intersection;

  /** The side number of this room, where it touches the neighbour. */
  int32_t touchingSideNoOfRoom;

  /** The side number of the neighbour room, where it touches this room. */
  int32_t touchingSideNoOfNeighbourRoom;

  /** True, if the intersection of this room and the neighbour is equal to a corner of this room. */
  bool roomTouchIsCorner;

  /** True, if the intersection is equal to a corner of the neighbour room. */
  bool neighbourRoomTouchIsCorner;

 private:
  Point* precalculatedFirstCorner = nullptr;
  Point* precalculatedLastCorner = nullptr;

 public:
  SortedRoomNeighbours_SortedRoomNeighbour(SortedRoomNeighbours* outer,
                                           SearchTreeObject* searchTreeObject,
                                           TileShape* neighbourShape, TileShape* intersection,
                                           int32_t touchingSideNoOfRoom,
                                           int32_t touchingSideNoOfNeighbourRoom,
                                           bool roomTouchIsCorner, bool neighbourRoomTouchIsCorner)
      : outer(outer),
        searchTreeObject(searchTreeObject),
        neighbourShape(neighbourShape),
        intersection(intersection),
        touchingSideNoOfRoom(touchingSideNoOfRoom),
        touchingSideNoOfNeighbourRoom(touchingSideNoOfNeighbourRoom),
        roomTouchIsCorner(roomTouchIsCorner),
        neighbourRoomTouchIsCorner(neighbourRoomTouchIsCorner) {}

  /** Compare function for sorting the neighbours counterclockwise around the room border. */
  virtual int32_t compareTo(SortedRoomNeighbours_SortedRoomNeighbour* other);

  /** Returns the first corner of the intersection shape with the neighbour. */
  virtual Point* firstCorner();

  /** Returns the last corner of the intersection shape with the neighbour. */
  virtual Point* lastCorner();
};

/**
 * To calculate the neighbour rooms of an expansion room. The neighbour rooms will be sorted in
 * counterclockwise order around the border of the shape of the expansion room.
 */
class SortedRoomNeighbours final : public virtual jrt::Object {
  friend class SortedRoomNeighbours_SortedRoomNeighbour;

 private:
  ExpansionRoom* fromRoom;
  CompleteExpansionRoom* completedRoom;
  TileShape* roomShape;
  jrt::TreeSet<SortedRoomNeighbours_SortedRoomNeighbour*>* sortedNeighbours;
  jrt::Collection<ShapeTree_TreeEntry*>* ownNetObjects;

  SortedRoomNeighbours(ExpansionRoom* fromRoom, CompleteExpansionRoom* completedRoom);

 public:
  /**
   * To calculate the neighbour rooms of an expansion room. Returns the completed room, or null if
   * the room could not be completed.
   */
  static CompleteExpansionRoom* complete(ExpansionRoom* room, AutorouteEngine* autorouteEngine);

  /** (Java: package-private) */
  static SortedRoomNeighbours_CalculationMode selectCalculationMode(ShapeSearchTree* searchTree);

  /** Calculates the neighbours with the any-angle algorithm and completes the room. */
  static CompleteExpansionRoom* calculate(ExpansionRoom* room, AutorouteEngine* autorouteEngine);

 private:
  static void calculateIncompleteRoomsWithEmptyNeighbours(ObstacleExpansionRoom* room,
                                                          AutorouteEngine* autorouteEngine);

  static void calculateTargetDoors(CompleteFreeSpaceExpansionRoom* room,
                                   jrt::Collection<ShapeTree_TreeEntry*>* ownNetObjects,
                                   AutorouteEngine* autorouteEngine);

  static SortedRoomNeighbours* calculateNeighbours(ExpansionRoom* room, int32_t netNumber,
                                                   ShapeSearchTree* autorouteSearchTree,
                                                   int32_t roomIdNo);

 public:
  /** (Java: package-private) Checks if a door between room1 and room2 may be inserted. */
  static bool insertDoorOk(ExpansionRoom* room1, ExpansionRoom* room2, TileShape* doorShape);

 private:
  static bool insertDoorOk(ObstacleExpansionRoom* room, Line* doorLine);

  void addSortedNeighbour(SearchTreeObject* searchTreeObject, TileShape* neighbourShape,
                          TileShape* intersection, int32_t touchingSideNoOfRoom,
                          int32_t touchingSideNoOfNeighbourRoom, bool roomTouchIsCorner,
                          bool neighbourRoomTouchIsCorner);

  bool tryRemoveEdge(int32_t netNumber, ShapeSearchTree* autorouteSearchTree);

 public:
  /** Calculates the new incomplete rooms between the sorted neighbours. */
  void calculateNewIncompleteRooms(AutorouteEngine* autorouteEngine);
};

}  // namespace fr
