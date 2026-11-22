#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Private history entry of BoardHistory (Java: BoardHistory.BoardHistoryEntry).
 *
 * PORT NOTE: Java stores the board as a serialized byte[] (board.serialize(false)) and restores
 * it with BasicBoard.deserialize(). The port stores the frozen snapshot returned by
 * board->serialize(false) (field `board`), and restoreBoard() hands out a fresh copy of it via
 * BasicBoard::deserialize(snapshot), so the stored snapshot is never mutated by callers.
 * `clearanceBaseline` is kept by reference like the Java transient field.
 */
class BoardHistory_BoardHistoryEntry : public virtual jrt::Object {
 public:
  BasicBoard* board;  // Java: byte[] board (serialized snapshot)
  std::string hash;
  float score;
  ClearanceBaseline* clearanceBaseline;
  int32_t restoreCount;

  BoardHistory_BoardHistoryEntry(RoutingBoard* board, RouterSettings* routerSettings);
};

/**
 * Records, manages and ranks the boards that were generated during the routing process.
 *
 * <p>The history is bounded to MAX_HISTORY_SIZE entries. When the cap is reached, the
 * lowest-scoring entry is evicted before adding the new one.
 */
class BoardHistory : public virtual jrt::Object {
 public:
  /** Maximum number of board snapshots retained in memory at any time. */
  static constexpr int32_t MAX_HISTORY_SIZE = 30;

 private:
  int32_t maxHistorySize;
  jrt::List<BoardHistory_BoardHistoryEntry*>* boards =
      new jrt::ArrayList<BoardHistory_BoardHistoryEntry*>();
  RouterSettings* routerSettings;
  // PORT NOTE: ReadWriteLock rwLock dropped (single-threaded port).

 public:
  /** Constructs a BoardHistory with default maximum history size. */
  explicit BoardHistory(RouterSettings* routerSettings);

  /** Package-private constructor that allows a custom cap. Intended for unit tests only. */
  BoardHistory(RouterSettings* routerSettings, int32_t maxHistorySize);

  /** Compatibility constructor for callers that explicitly select legacy scoring. */
  explicit BoardHistory(RoutingCostSettings* scoringSettings);

  /** Compatibility constructor for callers that explicitly select legacy scoring. */
  BoardHistory(RoutingCostSettings* scoringSettings, int32_t maxHistorySize);

  /** Adds a routing board to history if it improves overall score or space permits. */
  virtual void add(RoutingBoard* board);

 private:
  static RouterSettings* createLegacyRouterSettings(RoutingCostSettings* scoringSettings);

 public:
  /** Clears all boards from history. */
  virtual void clear();

  /** Returns true if the board snapshot is already present in history. */
  virtual bool contains(RoutingBoard* board);

  /** Removes the specified board snapshot from history. */
  virtual void remove(RoutingBoard* board);

  /** Returns the highest score among all boards currently in history. */
  virtual float getMaxScore();

  /**
   * Returns the best board in the history that has a restore count less than or equal to
   * maxAllowedRestoreCount. Returns null if no such board exists.
   */
  virtual RoutingBoard* restoreBoard(int32_t maxAllowedRestoreCount);

  /** Restores the highest-scoring board in history. */
  virtual RoutingBoard* restoreBestBoard();

  /** Returns the number of board snapshots in history. */
  virtual int32_t size();

  /** Returns the rank (1-indexed) of the specified board, or -1 if not found. */
  virtual int32_t getRank(RoutingBoard* board);
};

}  // namespace fr
