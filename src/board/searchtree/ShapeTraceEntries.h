#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * An entry point of a trace into the shape (Java: private static nested class
 * ShapeTraceEntries.EntryPoint). Pure data holder, fully implemented here.
 */
class ShapeTraceEntries_EntryPoint : public virtual jrt::Object {
 public:
  PolylineTrace* trace;
  int32_t traceLineNo;
  FloatPoint* entryApprox;
  int32_t edgeIndex;
  int32_t stackLevel;
  ShapeTraceEntries_EntryPoint* next = nullptr;

  ShapeTraceEntries_EntryPoint(PolylineTrace* trace, int32_t traceLineNo, int32_t edgeIndex,
                               FloatPoint* entryApprox)
      : trace(trace),
        traceLineNo(traceLineNo),
        entryApprox(entryApprox),
        edgeIndex(edgeIndex),
        stackLevel(-1) {}  // stackLevel -1: not yet calculated
};

/** Auxiliary class used by the shove functions. */
class ShapeTraceEntries : public virtual jrt::Object {
 private:
  static constexpr double c_offset_add = 1;

 public:
  jrt::Collection<Via*>* shoveViaList;  // a LinkedList

 private:
  TileShape* shape;
  int32_t layer;
  jrt::Array<int32_t>* ownNetNos;
  int32_t clearanceClassIndex;
  RoutingBoard* board;
  ShapeEntrySide* fromSide;
  ShapeTraceEntries_EntryPoint* listAnchor;
  int32_t tracePieceCount;
  int32_t maxStackLevel;
  bool shapeContainsTraceTails = false;
  Item* foundObstacle = nullptr;

 public:
  /**
   * Used for shoving traces and vias out of the input shape. fromSide.no is the side of shape, from
   * where the shove comes. if fromSide.no < 0, it will be calculated internally.
   */
  ShapeTraceEntries(TileShape* shape, int32_t layer, jrt::Array<int32_t>* ownNetNos,
                    int32_t clearanceClassIndex, ShapeEntrySide* fromSide, RoutingBoard* board);

  /** Cutout trace. */
  static void cutoutTrace(PolylineTrace* trace, ConvexShape* shape, int32_t clearanceClassIndex);

 private:
  /** Optimized function handling the performance critical standard cutout case. */
  static void fastCutoutTrace(PolylineTrace* trace, Polyline* startPiece, Polyline* endPiece);

  static bool netNosEqual(jrt::Array<int32_t>* netNos1, jrt::Array<int32_t>* netNos2);

 public:
  /**
   * Stores traces and vias in itemList. Returns false, if itemList contains obstacles, which cannot
   * be shoved aside. If isPadCheck, the check is for vias, otherwise it is for traces. If
   * copperSharingAllowed, overlaps with traces or pads of the own net are allowed.
   */
  virtual bool storeItems(jrt::Collection<Item*>* itemList, bool isPadCheck,
                          bool copperSharingAllowed);

  /**
   * Puts all obstacles in itemList into the internal sorted data structure. Returns false, if the
   * list contains obstacles, which cannot be shoved aside.
   */
  virtual bool storeItems(jrt::Collection<Item*>* itemList, bool isPadCheck,
                          bool copperSharingAllowed, bool holeOnly);

 private:
  bool overlapsCopper(Pin* pin);

 public:
  /**
   * Returns the next substitute trace piece (the pieces of the traces in the shape, to be shoved),
   * or null when there are no more.
   * PORT NOTE (stage E): `forInsert` (the caller inserts the piece into the board) allocates it as
   * board data; otherwise it is an operation temporary.
   */
  virtual PolylineTrace* nextSubstituteTracePiece(bool forInsert = false);

  virtual int32_t stackDepth();

  virtual int32_t substituteTraceCount();

  virtual bool traceTailsInShape();

  virtual void cutoutTraces(jrt::Collection<Item*>* itemList);

  virtual Item* getFoundObstacle();

 private:
  bool storeTrace(PolylineTrace* trace);

  void searchFromSide();

  void resort();

  bool calculateStackLevels();

  jrt::Array<ShapeTraceEntries_EntryPoint*>* popPiece();

  void insertEntryPoint(PolylineTrace* trace, int32_t traceLineNo, int32_t edgeIndex,
                        FloatPoint* entryApprox);

  void rotateEntryListAroundAnchor(ShapeTraceEntries_EntryPoint* newAnchor, int32_t edgeCount);
};

}  // namespace fr
