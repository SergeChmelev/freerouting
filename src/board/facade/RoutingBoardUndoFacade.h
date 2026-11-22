#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/*
 * ================================================================================================
 * DESIGN NOTES: copying a board without Java serialization (phase 2 implements this)
 * ================================================================================================
 *
 * Java copies boards in two ways, both through ObjectOutputStream/ObjectInputStream:
 *   (a) BoardSnapshotManager.serialize/deserialize (BasicBoard.clone, BoardHistory,
 *       AutorouteConnectionRouter strict-DRC snapshots): plain round trip, then
 *       BasicBoard.readObject runs.
 *   (b) RoutingBoardUndoFacade.deepCopy: the same round trip plus post-steps.
 * C++ replaces the round trip by BasicBoard::copyAsDeserialized(itemMap) (virtual, RoutingBoard
 * overrides it so the copy keeps the dynamic type):
 *
 *   1. copy = new <SameClass>(BasicBoard_CopyTag{}, source)
 *      BasicBoard part, non-transient state (Java copies it, so it must not be shared if mutable):
 *        - rules          : DEEP COPY (mutated while routing: setPinEdgeToTurnDist,
 *                           setIgnoreConduction, minHoleToHole; Nets hold a board pointer) ->
 *                           needs a BoardRules deep-copy facility from the rules group; then
 *                           copy->rules->nets->setBoard(copy).
 *        - components     : DEEP COPY incl. its undo stack (component positions are undoable).
 *        - communication  : NEW Communication sharing the immutable parts (coordinateTransform,
 *                           unit, resolution, specctraParserInfo) but with a DEEP COPY of
 *                           idGenerator (ItemIdGenerator counter: the copy must continue issuing
 *                           the same new ids independently) and observers = nullptr (transient!).
 *        - library, layerStructure : SHARED (PORT NOTE: not mutated after loading; Java copies
 *                           them, but sharing keeps padstack identity consistent between items,
 *                           rules' ViaInfos and the library).
 *        - boundingBox    : shared (IntBox is immutable).
 *        - preExistingClearanceViolationsCount, maxTraceHalfWidth, minTraceHalfWidth : copied.
 *      Transient members get JAVA DEFAULTS, not their initializers: revision 0, journal off and
 *      arrays null, pendingBox null, pendingFirstLayer = pendingLastLayer = 0 (!), pendingUnknown
 *      false, netAssignmentChangeCount 0, routingLedger_/caches/facades null,
 *      unfixableClearanceViolationsCount 0, ruleBlockedPins/tolerancePins null,
 *      clearanceBaseline null, updateBox nullptr (not IntBox::EMPTY()).
 *      RoutingBoard part: failureLog DEEP COPY (RoutingFailureLog is non-transient); changedArea,
 *      autorouteEngine, shoveFailingObstacle, shoveCacheObstacles, facades null;
 *      shoveFailingLayer = 0 (!), write counters 0.
 *   2. copy->copyItemListFrom(source, itemMap): a new UndoableObjects with the same state:
 *      every Storable (live ones and the ones held only in the undo/redo stacks) mapped once
 *      through an IdentityHashMap old->new created with Item::copy(item->getId()) (same id), then
 *      newItem->board = copy. Map order (by item compareTo = id), node levels, stackLevel,
 *      redoPossible and deletedObjectsStack are reproduced; version/contentVersion are transient
 *      (0). Needs an UndoableObjects copy hook from the datastructures group, e.g.
 *      `UndoableObjects* copyMapped(std::function<UndoableObjects_Storable*(Storable*)>)`.
 *      Phase 2 must check, per item class, that copy(id) carries every NON-transient field
 *      (fixed state, component id, name, ConductionArea isObstacle/isFilled, ...) and copy the
 *      rest explicitly; transient item caches (tree entries, precalculated tree shapes, autoroute
 *      info, exactCopperCache, fracture memo) start empty. Immutable geometry (Polyline,
 *      PolylineArea, PolygonShape, shapes) is SHARED with the source: this is what Java's
 *      AreaRecordingOutputStream / AreaRestoringInputStream achieved by handing the copies the
 *      already computed convex pieces of the written areas/polygons (reuseConvexPiecesOf), so the
 *      planes of a copy are not split into tiles again.
 *   3. copy->afterDeserialize() (= Java BasicBoard.readObject): minHoleToHole fixup, NEW
 *      SearchTreeManager (only the default uncompensated tree; clearanceCompensationUsed=false
 *      even if the source used compensation -- Java oddity, keep it), normalizeSuppressedNetNos
 *      = new HashSet, facades null, then for each item of getItems() IN ITEM-LIST ORDER:
 *      item->board = copy; searchTreeManager->insert(item). The insertion order shapes the
 *      MinAreaTree and thus the obstacle traversal order, so it must match Java exactly.
 *
 * deepCopy() = (b) in Java order:
 *   on the source: copy = source->copyAsDeserialized(itemMap); then, for every ConductionArea
 *   plane of the source (Java: in serialization write order; use item-list order),
 *   memo = plane->fractureMemoForCopy(); clearanceBaseline = source->clearanceBaseline;
 *   ledgerCounts = source->routingLedger()->counts() (creates the source ledger if absent).
 *   on the copy: copyOfPlane->adoptFractureMemo(memo) for each plane whose copy has the same id;
 *   copy->clearanceBaseline = clearanceBaseline; copy->clearAllItemTemporaryAutorouteData();
 *   copy->finishAutoroute(); copy->routingLedger()->seed(ledgerCounts). Java returns null after
 *   logging on any exception; C++ does not expect failures.
 *
 * PORT NOTE: Java's private static nested classes AreaRecordingOutputStream and
 * AreaRestoringInputStream (ObjectOutputStream/ObjectInputStream subclasses recording areas,
 * polygons and planes in write order) are dropped: sharing immutable geometry and mapping planes
 * through itemMap gives the same result. Their names remain forward-declared in fwd.h only.
 * Java `synchronized (board)` is dropped (single-threaded port).
 * ================================================================================================
 */

/**
 * Owns routing-board undo/redo side effects and deep-copy restoration.
 * (Java package-private methods are public in C++.)
 */
class RoutingBoardUndoFacade final : public virtual jrt::Object {
 private:
  RoutingBoard* board;

 public:
  explicit RoutingBoardUndoFacade(RoutingBoard* board) : board(board) {}

  bool undo(jrt::Set<int32_t>* changedNets);

  bool redo(jrt::Set<int32_t>* changedNets);

  /**
   * Copies the board (see the design notes above): an independent RoutingBoard with the same
   * items (ids and order), search trees rebuilt, clearance baseline, cleared autoroute data,
   * seeded routing ledger and adopted plane fracture memos.
   */
  RoutingBoard* deepCopy();

 private:
  void applyUndoRedoSideEffects(jrt::Collection<UndoableObjects_Storable*>* cancelledObjects,
                                jrt::Collection<UndoableObjects_Storable*>* restoredObjects,
                                jrt::Set<int32_t>* changedNets);
};

}  // namespace fr
