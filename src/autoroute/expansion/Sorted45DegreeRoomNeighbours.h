#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Helper class to sort the doors of an expansion room counterclockwise around the border of the
 * room shape. Java: private inner class (Comparable) -> explicit `outer` pointer.
 */
class Sorted45DegreeRoomNeighbours_SortedRoomNeighbour : public virtual jrt::Object {
 public:
  Sorted45DegreeRoomNeighbours* outer;

  /** The shape of the neighbour room. */
  SearchTreeObject* searchTreeObject;
  IntOctagon* shape;

  /** The intersection of this ExpansionRoom shape with the neighbourShape. */
  IntOctagon* intersection;

  /** The first side of the room shape, where the neighbourShape touches. */
  int32_t firstTouchingSide;

  /** The last side of the room shape, where the neighbourShape touches. */
  int32_t lastTouchingSide;

  /** Creates a new instance; also marks edgeInteriorTouchesObstacle of the outer object. */
  Sorted45DegreeRoomNeighbours_SortedRoomNeighbour(Sorted45DegreeRoomNeighbours* outer,
                                                   SearchTreeObject* searchTreeObject,
                                                   IntOctagon* neighbourShape,
                                                   IntOctagon* intersection);

  /** Compare function for sorting the neighbours counterclockwise around the room border. */
  virtual int32_t compareTo(Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* other);
};

/**
 * Sorts the neighbours of an expansion room in counterclockwise order around its border, for the
 * 45-degree autoroute search tree.
 */
class Sorted45DegreeRoomNeighbours final : public virtual jrt::Object {
  friend class Sorted45DegreeRoomNeighbours_SortedRoomNeighbour;

 public:
  CompleteExpansionRoom* completedRoom;
  jrt::TreeSet<Sorted45DegreeRoomNeighbours_SortedRoomNeighbour*>* sortedNeighbours;

 private:
  ExpansionRoom* fromRoom;
  IntOctagon* roomShape;
  jrt::Array<bool>* edgeInteriorTouchesObstacle;

  Sorted45DegreeRoomNeighbours(ExpansionRoom* fromRoom, CompleteExpansionRoom* completedRoom);

 public:
  /** Calculates the neighbours of room and completes it. */
  static CompleteExpansionRoom* calculate(ExpansionRoom* room, AutorouteEngine* autorouteEngine);

 private:
  static Sorted45DegreeRoomNeighbours* calculateNeighbours(ExpansionRoom* room, int32_t netNumber,
                                                           ShapeSearchTree* autorouteSearchTree,
                                                           int32_t roomIdNo);

  static IntOctagon* removeNotTouchingBorderLines(IntOctagon* roomOct,
                                                  jrt::Array<bool>* edgeInteriorTouchesObstacle);

  void addSortedNeighbour(SearchTreeObject* searchTreeObject, IntOctagon* neighbourShape,
                          IntOctagon* intersection);

  void calculateEdgeIncompleteRoomsOfObstacleExpansionRoom(int32_t fromSideIndex,
                                                           int32_t toSideIndex,
                                                           AutorouteEngine* autorouteEngine);

  bool tryRemoveEdgeLine(int32_t netNumber, ShapeSearchTree* autorouteSearchTree);

  static std::string describeBounds(IntBox* bounds);

  void insertIncompleteRoom(AutorouteEngine* autorouteEngine, int32_t leftX, int32_t bottomY,
                            int32_t rightX, int32_t topY, int32_t upperLeftDiagonalX,
                            int32_t lowerRightDiagonalX, int32_t lowerLeftDiagonalX,
                            int32_t upperRightDiagonalX);

  void calculateNewIncompleteRoomsForObstacleExpansionRoom(
      Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* prevNeighbour,
      Sorted45DegreeRoomNeighbours_SortedRoomNeighbour* nextNeighbour,
      AutorouteEngine* autorouteEngine);

  void calculateNewIncompleteRooms(AutorouteEngine* autorouteEngine);
};

}  // namespace fr
