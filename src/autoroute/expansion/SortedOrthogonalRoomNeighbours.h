#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Helper class to sort the doors of an expansion room counterclockwise around the border of the
 * room shape. Java: private inner class (Comparable) -> explicit `outer` pointer.
 */
class SortedOrthogonalRoomNeighbours_SortedRoomNeighbour : public virtual jrt::Object {
 public:
  SortedOrthogonalRoomNeighbours* outer;

  /** The shape of the neighbour room. */
  SearchTreeObject* searchTreeObject;
  IntBox* shape;

  /** The intersection of this ExpansionRoom shape with the neighbourShape. */
  IntBox* intersection;

  /** The first side of the room shape, where the neighbourShape touches. */
  int32_t firstTouchingSide;

  /** The last side of the room shape, where the neighbourShape touches. */
  int32_t lastTouchingSide;

  /** Creates a new instance; also marks edgeInteriorTouchesObstacle of the outer object. */
  SortedOrthogonalRoomNeighbours_SortedRoomNeighbour(SortedOrthogonalRoomNeighbours* outer,
                                                     SearchTreeObject* searchTreeObject,
                                                     IntBox* neighbourShape, IntBox* intersection);

  /** Compare function for sorting the neighbours counterclockwise around the room border. */
  virtual int32_t compareTo(SortedOrthogonalRoomNeighbours_SortedRoomNeighbour* other);
};

/**
 * Sorts the neighbours of an expansion room in counterclockwise order around its border, for the
 * 90-degree (orthogonal) autoroute search tree.
 */
class SortedOrthogonalRoomNeighbours final : public virtual jrt::Object {
  friend class SortedOrthogonalRoomNeighbours_SortedRoomNeighbour;

 public:
  CompleteExpansionRoom* completedRoom;
  jrt::TreeSet<SortedOrthogonalRoomNeighbours_SortedRoomNeighbour*>* sortedNeighbours;

 private:
  ExpansionRoom* fromRoom;
  bool isObstacleExpansionRoom;
  IntBox* roomShape;
  jrt::Array<bool>* edgeInteriorTouchesObstacle;

  SortedOrthogonalRoomNeighbours(ExpansionRoom* fromRoom, CompleteExpansionRoom* completedRoom);

 public:
  /** Calculates the neighbours of room and completes it. */
  static CompleteExpansionRoom* calculate(ExpansionRoom* room, AutorouteEngine* autorouteEngine);

 private:
  static void calculateIncompleteRoomsWithEmptyNeighbours(ObstacleExpansionRoom* room,
                                                          AutorouteEngine* autorouteEngine);

  static SortedOrthogonalRoomNeighbours* calculateNeighbours(ExpansionRoom* room,
                                                             int32_t netNumber,
                                                             ShapeSearchTree* autorouteSearchTree,
                                                             int32_t roomIdNo);

  static IntBox* removeBorderLine(IntBox* roomBox, int32_t removeEdgeNo);

  void calculateNewIncompleteRooms(AutorouteEngine* autorouteEngine);

  void insertIncompleteRoom(AutorouteEngine* autorouteEngine, int32_t llX, int32_t llY,
                            int32_t urX, int32_t urY);

  bool tryRemoveEdge(int32_t netNumber, ShapeSearchTree* autorouteSearchTree);

  void addSortedNeighbour(SearchTreeObject* searchTreeObject, IntBox* neighbourShape,
                          IntBox* intersection);
};

}  // namespace fr
