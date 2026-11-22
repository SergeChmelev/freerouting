#pragma once

#include <functional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"
#include "datastructures/MinAreaTree.h"

namespace fr {

/**
 * Created for sorting Items according to their clearance to clearanceClassIndex on layer layer.
 * (Java: private static nested class ShapeSearchTree.EntrySortedByClearance.)
 */
class ShapeSearchTree_EntrySortedByClearance : public virtual jrt::Object {
 public:
  ShapeTree_Leaf* leaf;
  int32_t clearance;

  ShapeSearchTree_EntrySortedByClearance(ShapeTree_Leaf* leaf, int32_t clearance)
      : leaf(leaf), clearance(clearance) {}

  /**
   * Orders by clearance, then in leaf order. Leaves of items never compare equal, so no entry is
   * dropped, and the order does not depend on the order of insertion.
   */
  virtual int32_t compareTo(ShapeSearchTree_EntrySortedByClearance* other);
};

/**
 * Elementary geometric search functions making direct use of the MinAreaTree in the package
 * datastructures.
 *
 * PORT NOTE: the Java read/write locks of the tree are dropped (single-threaded port); the
 * `...Unlocked` private methods are kept so the structure matches Java, the public wrappers just
 * forward to them.
 */
class ShapeSearchTree : public MinAreaTree {
 private:
  static constexpr int32_t DRILL_HOLE_CLEARANCE_MARGIN = 10;

  /**
   * Reusable result list for spatial overlap queries (Java: ThreadLocal<List<Leaf>>; one shared
   * list in the single-threaded port).
   */
  static jrt::ArrayList<ShapeTree_Leaf*>* QUERY_LEAF_BUFFER();

 public:
  /**
   * The clearance class number for which the shapes of this tree is compensated. If
   * compensatedClearanceClassNo = 0, the shapes are not compensated.
   */
  int32_t compensatedClearanceClassNo;

  /**
   * PORT NOTE: Java computes key in the base constructor via getClass() (runtime class). In C++
   * the dynamic type inside the base constructor is ShapeSearchTree, so every derived constructor
   * recomputes `key = getKey(this, directions, compensatedClearanceClassNo)` in its body.
   */
  std::string key;

 protected:
  BasicBoard* board;

 public:
  /**
   * Creates a new ShapeSearchTree. compensatedClearanceClassNo is the clearance class number for
   * which the shapes of this tree is compensated. If 0, the shapes are not compensated.
   * (Java package-private.)
   */
  ShapeSearchTree(ShapeBoundingDirections* directions, BasicBoard* board,
                  int32_t compensatedClearanceClassNo);

  /** GetKey: "<TreeClass>_<Directions without 'BoundingDirections'>_cc<clearanceClass>". */
  static std::string getKey(ShapeSearchTree* searchTree, ShapeBoundingDirections* directions,
                            int32_t clearanceClass);

  std::string toString() override { return key; }

  /** Returns, if for the shapes stored in this tree clearance compensation is used. */
  virtual bool isClearanceCompensationUsed() { return this->compensatedClearanceClassNo > 0; }

  /**
   * Return the clearance compensation value of clearanceClassIndex to the clearance compensation
   * class of this search tree on layer layer. Returns 0, if no clearance compensation is used.
   */
  virtual int32_t clearanceCompensationValue(int32_t clearanceClassIndex, int32_t layer);

  /**
   * Clearance still needed between two shapes already prepared for this tree. Never shrink
   * prepared shapes when they already provide more clearance than required.
   */
  virtual int32_t additionalClearance(int32_t firstClass, int32_t secondClass, int32_t layer);

  /**
   * Changes the tree entries from keepAtStartCount + 1 to newShapeCount - 1 - keepAtEndCount to
   * changedEntries. Special implementation for change_trace for performance reasons.
   */
  virtual void changeEntries(PolylineTrace* obj, Polyline* newPolyline, int32_t keepAtStartCount,
                             int32_t keepAtEndCount);

 private:
  void changeEntriesUnlocked(PolylineTrace* obj, Polyline* newPolyline, int32_t keepAtStartCount,
                             int32_t keepAtEndCount);

 public:
  /** Merges the tree entries from fromTrace in front of toTrace (combine trace). */
  virtual void mergeEntriesInFront(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                   Polyline* joinedPolyline, int32_t fromEntryNo,
                                   int32_t toEntryNo);

 private:
  void mergeEntriesInFrontUnlocked(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                   Polyline* joinedPolyline, int32_t fromEntryNo,
                                   int32_t toEntryNo);

 public:
  /** Merges the tree entries from fromTrace to the end of toTrace (combine trace). */
  virtual void mergeEntriesAtEnd(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                 Polyline* joinedPolyline, int32_t fromEntryNo,
                                 int32_t toEntryNo);

 private:
  void mergeEntriesAtEndUnlocked(PolylineTrace* fromTrace, PolylineTrace* toTrace,
                                 Polyline* joinedPolyline, int32_t fromEntryNo,
                                 int32_t toEntryNo);

 public:
  /** Transfers tree entries from fromTrace to start and endPiece after a middle piece was cut out. */
  virtual void reuseEntriesAfterCutout(PolylineTrace* fromTrace, PolylineTrace* startPiece,
                                       PolylineTrace* endPiece);

 private:
  void reuseEntriesAfterCutoutUnlocked(PolylineTrace* fromTrace, PolylineTrace* startPiece,
                                       PolylineTrace* endPiece);

 public:
  /**
   * Puts all items in the tree overlapping with shape on layer layer into obstacles. If layer < 0,
   * the layer is ignored. (Entries are added in leaf order unless obstacles is a naturally ordered
   * SortedSet.)
   */
  virtual void overlappingObjects(ConvexShape* shape, int32_t layer,
                                  jrt::Array<int32_t>* ignoreNetNos,
                                  jrt::Set<SearchTreeObject*>* obstacles);

  /**
   * Returns the objects on layer layer which overlap shape and for which accept answers true.
   * accept runs before the exact intersection test, so it must depend on nothing but its argument.
   */
  virtual jrt::Set<SearchTreeObject*>* overlappingObjects(
      ConvexShape* shape, int32_t layer, std::function<bool(SearchTreeObject*)> accept);

  /** Returns all SearchTreeObjects on layer layer, which overlap with shape (layer < 0: all). */
  virtual jrt::Set<SearchTreeObject*>* overlappingObjects(ConvexShape* shape, int32_t layer);

  /**
   * Returns the objects with a shape on layer layer which overlaps a tree shape of area containing
   * one of their points, or null if area is not in this tree. points gives the points of an object,
   * or null to leave the object out. An object for which byShape is true is kept instead when one
   * of its tree shapes on layer overlaps a tree shape of area with a positive area.
   */
  virtual jrt::Set<SearchTreeObject*>* overlappingObjectsWithPointIn(
      Item* area, int32_t layer, std::function<jrt::Array<Point*>*(SearchTreeObject*)> points,
      std::function<bool(SearchTreeObject*)> byShape);

  /** Puts all tree entries overlapping with shape on layer layer into treeEntries. */
  virtual void overlappingTreeEntries(ConvexShape* shape, int32_t layer,
                                      jrt::Collection<ShapeTree_TreeEntry*>* treeEntries);

  /**
   * Puts all tree entries overlapping with shape on layer layer into treeEntries. Entries with an
   * object containing a net number of ignoreNetNos are ignored.
   */
  virtual void overlappingTreeEntries(ConvexShape* shape, int32_t layer,
                                      jrt::Array<int32_t>* ignoreNetNos,
                                      jrt::Collection<ShapeTree_TreeEntry*>* treeEntries);

 private:
  void overlappingTreeEntriesUnlocked(ConvexShape* shape, int32_t layer,
                                      jrt::Array<int32_t>* ignoreNetNos,
                                      jrt::Collection<ShapeTree_TreeEntry*>* treeEntries);

  void overlappingTreeEntriesUnlocked(ConvexShape* shape, int32_t layer,
                                      jrt::Array<int32_t>* ignoreNetNos,
                                      jrt::Collection<ShapeTree_TreeEntry*>* treeEntries,
                                      Item* obstaclesOf);

  /**
   * Puts the tree entries overlapping with shape into treeEntries, in leaf order when inLeafOrder
   * is set, in tree traversal order otherwise.
   */
  void overlappingTreeEntriesUnlocked(ConvexShape* shape, int32_t layer,
                                      jrt::Array<int32_t>* ignoreNetNos,
                                      jrt::Collection<ShapeTree_TreeEntry*>* treeEntries,
                                      Item* obstaclesOf, bool inLeafOrder);

 public:
  /**
   * Appends entries overlapping the query, including clearance, without clearing the destination.
   * The shape must use this tree's geometry convention (raw for an uncompensated tree, already
   * compensated for a compensated tree). If layer is negative, all layers are queried.
   *
   * PORT NOTE: Java disambiguates the null overloads with `(Item) null`; in C++ pass
   * `static_cast<Item*>(nullptr)` / `static_cast<Shape*>(nullptr)`.
   */
  virtual void overlappingTreeEntriesWithClearance(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries);

  /** Overload with exactShape copper. */
  virtual void overlappingTreeEntriesWithClearance(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
      Shape* exactShape);

  /** Overload that filters out items that are not obstacles to obstaclesOf. */
  virtual void overlappingTreeEntriesWithClearance(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
      Item* obstaclesOf);

  /** Overload with obstaclesOf and exactQuery. */
  virtual void overlappingTreeEntriesWithClearance(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
      Item* obstaclesOf, ExactCopper* exactQuery);

 private:
  void overlappingTreeEntriesWithClearanceUnlocked(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
      Item* obstaclesOf);

  void overlappingTreeEntriesWithClearanceUnlocked(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex, jrt::Collection<ShapeTree_TreeEntry*>* obstacleEntries,
      Item* obstaclesOf, ExactCopper* exactQuery);

 public:
  /**
   * Whether the copper of a clearance query and of a tree entry whose tiles overlap (enlarged by
   * half the clearance each) keep clearance apart all the same. The holders are 1-element arrays
   * (lazily computed query copper).
   */
  virtual bool copperApart(ConvexShape* queryShape, bool queryIsCopper,
                           jrt::Array<ExactCopper*>* queryCopperHolder,
                           jrt::Array<bool>* queryCopperKnownHolder, jrt::Object* entryObject,
                           int32_t entryIndex, TileShape* entryTile, int32_t clearance);

  /**
   * The copper behind the tree shape of a pin or via, grown as its tree shape is; null for other
   * objects, whose tree shape is their copper's own tile. Cached in DrillItem.exactCopperCache.
   */
  virtual ExactCopper* exactCopperOf(SearchTreeObject* treeObject, int32_t index);

 private:
  /** The bounding octagon of a tile shape, or null for other shapes and unbounded tiles. */
  static IntOctagon* boundingOctagonOf(ConvexShape* shape);

  static constexpr int32_t ENLARGED_OCTAGON_MARGIN = 2;

 public:
  /**
   * Whether boundingOctagonsApart is sure to hold between a query tile with bounding octagon
   * queryOctagon and entryShape enlarged by halfClearance, judged without enlarging the entry.
   * (Java package-private.)
   */
  static bool enlargedSimplexApart(IntOctagon* queryOctagon, TileShape* entryShape,
                                   int32_t halfClearance);

  /**
   * Returns true when the bounding octagons of two shapes, one of them a simplex, share no point.
   * (Java package-private.)
   */
  static bool boundingOctagonsApart(ConvexShape* a, ConvexShape* b);

  /** Returns all TreeEntries which overlap with shape on layer layer inclusive clearance. */
  virtual jrt::Collection<ShapeTree_TreeEntry*>* overlappingTreeEntriesWithClearance(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex);

  /** ... and are obstacles to obstaclesOf. */
  virtual jrt::Collection<ShapeTree_TreeEntry*>* overlappingTreeEntriesWithClearance(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex, Item* obstaclesOf);

  /** ... measured with exactQuery copper. */
  virtual jrt::Collection<ShapeTree_TreeEntry*>* overlappingTreeEntriesWithClearance(
      ConvexShape* shape, int32_t layer, jrt::Array<int32_t>* ignoreNetNos,
      int32_t clearanceClassIndex, Item* obstaclesOf, ExactCopper* exactQuery);

  /** Appends overlapping objects, using the same tree-prepared query geometry. */
  virtual void overlappingObjectsWithClearance(ConvexShape* shape, int32_t layer,
                                               jrt::Array<int32_t>* ignoreNetNos,
                                               int32_t clearanceClassIndex,
                                               jrt::Set<SearchTreeObject*>* obstacles);

  /** Appends overlapping board items (non-item routing rooms are excluded). */
  virtual void overlappingObjectsWithClearance(ConvexShape* shape, int32_t layer,
                                               jrt::Array<int32_t>* ignoreNetNos,
                                               int32_t clearanceClassIndex,
                                               jrt::Collection<Item*>* obstacles);

  /** Appends overlapping board items with exactShape copper. */
  virtual void overlappingObjectsWithClearance(ConvexShape* shape, int32_t layer,
                                               jrt::Array<int32_t>* ignoreNetNos,
                                               int32_t clearanceClassIndex,
                                               jrt::Collection<Item*>* obstacles,
                                               Shape* exactShape);

  /** Returns all items in the tree, which overlap with shape on layer layer inclusive clearance. */
  virtual jrt::Set<Item*>* overlappingItemsWithClearance(ConvexShape* shape, int32_t layer,
                                                         jrt::Array<int32_t>* ignoreNetNos,
                                                         int32_t clearanceClassIndex);

  /** Returns all items in the tree with exactShape copper. */
  virtual jrt::Set<Item*>* overlappingItemsWithClearance(ConvexShape* shape, int32_t layer,
                                                         jrt::Array<int32_t>* ignoreNetNos,
                                                         int32_t clearanceClassIndex,
                                                         Shape* exactShape);

  /**
   * Calculates a new incomplete room with a maximal TileShape contained in the shape of room, which
   * may overlap only with items of the input net on the input layer. If that is not possible,
   * several rooms are returned. If ignoreShape != null, CompleteFreeSpaceExpansionRooms whose
   * intersection with the room is contained in ignoreShape are ignored.
   */
  virtual jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* completeShape(
      IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
      TileShape* ignoreShape);

 private:
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* completeShapeUnlocked(
      IncompleteFreeSpaceExpansionRoom* room, int32_t netNumber, SearchTreeObject* ignoreObject,
      TileShape* ignoreShape);

  /**
   * Restrains the shape of incompleteRoom to a TileShape, which does not intersect with the
   * interior of obstacleShape (recursive).
   */
  jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* restrainShape(
      IncompleteFreeSpaceExpansionRoom* incompleteRoom, TileShape* obstacleShape);

 public:
  /**
   * Reduces the first or last shape of trace at a tie pin, so that the autorouter algorithm can
   * find a connection for a different net.
   */
  virtual void reduceTraceShapeAtTiePin(Pin* tiePin, PolylineTrace* trace);

 private:
  void reduceTraceShapeAtTiePinUnlocked(Pin* tiePin, PolylineTrace* trace);

 public:
  /** Changes the shape with index shapeIndex of item to newShape and updates the tree entry. */
  virtual void changeItemShape(Item* item, int32_t shapeIndex, TileShape* newShape);

 private:
  void changeItemShapeUnlocked(Item* item, int32_t shapeIndex, TileShape* newShape);

 public:
  virtual jrt::Array<TileShape*>* calculateTreeShapes(DrillItem* drillItem);

  virtual jrt::Array<TileShape*>* calculateTreeShapes(ObstacleArea* obstacleArea);

  virtual jrt::Array<TileShape*>* calculateTreeShapes(BoardOutline* boardOutline);

  virtual jrt::Array<TileShape*>* calculateTreeShapes(PolylineTrace* trace);

 protected:
  /**
   * Synthesized obstacle for copper layers where a drilled item has NO pad shape: the drill hole
   * still passes through. Returns null when the hole-clearance rule is disabled or no drill radius
   * is known.
   */
  virtual Shape* drillHoleObstacle(DrillItem* drillItem);

 public:
  /**
   * Extra obstacle inflation so that copper of other nets stays holeClearance away from this item's
   * drill hole (not just its copper pad). Returns 0 when the hole-clearance rule is disabled.
   */
  virtual int32_t drillHoleClearanceDelta(DrillItem* drillItem, Shape* shape, int32_t layer);

  /**
   * Used for creating the shapes of a polyline_trace for this tree. Overwritten in derived classes.
   * (Java package-private.)
   */
  virtual TileShape* offsetShape(Polyline* polyline, int32_t halfWidth, int32_t no);

  /** Used for creating the shapes of a polyline_trace for this tree. Overwritten in derived classes. */
  virtual jrt::Array<TileShape*>* offsetShapes(Polyline* polyline, int32_t halfWidth,
                                               int32_t fromNo, int32_t toNo);

 protected:
  /**
   * Makes sure that on each layer there will be more than 1 IncompleteFreeSpaceExpansionRoom, even
   * if there are no objects on the layer. Otherwise, the maze search algorithm gets problems with
   * vias.
   */
  virtual jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* divideLargeRoom(
      jrt::Collection<IncompleteFreeSpaceExpansionRoom*>* roomList, IntBox* boardBoundingBox);

 public:
  virtual bool validateEntries(Item* item);
};

}  // namespace fr
