#pragma once

#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Owns board snapshots and the binary serialization helpers.
 *
 * PORT NOTE: C++ has no object serialization. The "serialized board" is a frozen, fully
 * independent copy of the board (BasicBoard::copyAsDeserialized), never mutated afterwards;
 * "deserialize" copies it again, so one snapshot can be restored any number of times, each time
 * into an independent board, like Java's byte[]. Java `byte[]` snapshot values therefore become
 * `BasicBoard*` in all callers (BoardHistory, AutorouteConnectionRouter, ...). Java catches every
 * Throwable here and returns null; the C++ copy does not fail.
 */
class BoardSnapshotManager final : public virtual jrt::Object {
 private:
  BasicBoard* board;

 public:
  explicit BoardSnapshotManager(BasicBoard* board) : board(board) {}

  /**
   * Serializes either the complete board or the historical basic trace profile. PORT NOTE:
   * returns board->copyAsDeserialized(nullptr) for both profiles (the basic profile only fed
   * getHash in Java).
   */
  BasicBoard* serialize(bool basicProfile);

  /**
   * Deserializes a board from its snapshot: snapshot->copyAsDeserialized(nullptr), nullptr for
   * nullptr.
   */
  static BasicBoard* deserialize(BasicBoard* snapshot);

  /**
   * Returns an MD5 hash (32 lowercase hex digits) of the board trace-state profile. PORT NOTE:
   * Java hashes the serialized bytes of (getTraces(), getVias(), itemList); the C++ port hashes a
   * canonical text dump of the same state (see basicProfileDump), so values differ from Java but
   * equality of hashes still means equality of trace/via/item state. Only hash equality is used by
   * callers (BoardHistory, events).
   */
  std::string getHash();

 private:
  /**
   * PORT NOTE (new): canonical dump of the trace-state profile in item-list order: for every item
   * its class, id, nets, layers, clearance class, fixed state and geometry (trace corners and half
   * width, via padstack and center, other items' bounding boxes). The undo history is not part
   * of the dump (UndoableObjects exposes no read access to it).
   */
  std::string basicProfileDump();

 public:
  /** Makes the current item/component state restorable by undo. */
  void generateSnapshot();

  /** Removes the top snapshot from the undo stack. */
  bool popSnapshot();

  /** Returns the number of trace IDs that differ from another board. */
  int32_t diffTraces(BasicBoard* compareTo);
};

}  // namespace fr
