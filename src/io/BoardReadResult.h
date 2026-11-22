#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::io {

/**
 * Sealed result type for all outcomes of a board read operation (DSN, JSON, ...).
 *
 * PORT NOTE: Java sealed interface + records -> abstract class hierarchy. Callers replace the
 * pattern-matching switch with dynamic_cast chains:
 *   if (auto* s = jrt::fast_cast<BoardReadResult_Success>(r)) ... else if (... OutlineMissing ...)
 */
class BoardReadResult : public virtual jrt::Object {
 public:
  ~BoardReadResult() override = default;
};

/**
 * Full board + metadata are available. The board is fully constructed and routable.
 * warnings: non-fatal issues encountered during loading; never null, may be empty.
 */
class BoardReadResult_Success : public virtual BoardReadResult {
 public:
  BoardReadResult_Success(fr::BasicBoard* board, BoardMetadata* metadata,
                          jrt::List<std::string>* warnings)
      : board_(board), metadata_(metadata), warnings_(warnings) {}

  fr::BasicBoard* board() { return board_; }
  BoardMetadata* metadata() { return metadata_; }
  jrt::List<std::string>* warnings() { return warnings_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  fr::BasicBoard* board_;
  BoardMetadata* metadata_;
  jrt::List<std::string>* warnings_;
};

/**
 * The board was constructed but the outline (boundary) scope was absent from the input file. The
 * board reference is still valid and may be used with caution.
 */
class BoardReadResult_OutlineMissing : public virtual BoardReadResult {
 public:
  BoardReadResult_OutlineMissing(fr::BasicBoard* board, BoardMetadata* metadata,
                                 jrt::List<std::string>* warnings)
      : board_(board), metadata_(metadata), warnings_(warnings) {}

  fr::BasicBoard* board() { return board_; }
  BoardMetadata* metadata() { return metadata_; }
  jrt::List<std::string>* warnings() { return warnings_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  fr::BasicBoard* board_;
  BoardMetadata* metadata_;
  jrt::List<std::string>* warnings_;
};

/**
 * The input did not conform to the expected grammar/format.
 * location: human-readable description of where the error was detected; detail: the problem.
 */
class BoardReadResult_ParseError : public virtual BoardReadResult {
 public:
  BoardReadResult_ParseError(std::string location, std::string detail)
      : location_(std::move(location)), detail_(std::move(detail)) {}

  std::string location() { return location_; }
  std::string detail() { return detail_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  std::string location_;
  std::string detail_;
};

/**
 * An IOException occurred while reading the underlying stream.
 * PORT NOTE: the exception is stored by value; equals/hashCode stay identity based (Java's
 * IOException does not override equals/hashCode either).
 */
class BoardReadResult_IoError : public virtual BoardReadResult {
 public:
  explicit BoardReadResult_IoError(jrt::IOException cause) : cause_(std::move(cause)) {}

  jrt::IOException cause() { return cause_; }

  std::string toString() override;

 private:
  jrt::IOException cause_;
};

}  // namespace fr::io
