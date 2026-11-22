#pragma once

#include <istream>
#include <memory>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Opens a fresh stream over the same DSN input (used for the wiring pre-scan).
 *
 * PORT NOTE: Java returns an InputStream closed by try-with-resources. In C++ the opener returns an
 * owning std::unique_ptr<std::istream> (a std::istream is not a ported/GC object; destructors of
 * GC'd objects never run, so ownership must be explicit for the stream to be closed).
 */
class DsnReader_StreamOpener : public virtual jrt::Object {
 public:
  virtual std::unique_ptr<std::istream> open() = 0;
};

/**
 * Reads a Specctra DSN file and returns a fully constructed BasicBoard wrapped in a typed
 * BoardReadResult. No dependency on BoardManager, RoutingJob or any GUI class; board construction
 * happens internally via the minimal shim embedded in ReadScopeParameter.
 *
 * PORT NOTE: InputStream parameters -> std::istream&. Java's "inputStream == null" checks (which
 * return a ParseError) cannot occur and are dropped. Java "closes" the stream; in C++ the caller
 * owns the stream object (closeQuietly is a no-op).
 */
class DsnReader final : public virtual jrt::Object {
 public:
  /** Convenience overload: equivalent to readBoard(in, observers, idGenerator, null). */
  static fr::io::BoardReadResult* readBoard(std::istream& inputStream, fr::BoardObservers* observers,
                                            fr::IdGenerator* idGenerator);

  /**
   * Reads a DSN stream and returns a fully constructed board or a typed failure.
   * observers / idGenerator are nullable (defaults are created). designName: optional caller-supplied
   * filename (without path) used in log messages; when null or blank the pcb-name token is used.
   */
  static fr::io::BoardReadResult* readBoard(std::istream& inputStream, fr::BoardObservers* observers,
                                            fr::IdGenerator* idGenerator,
                                            const std::optional<std::string>& designName);

  /**
   * Reads a DSN using explicit item-allocation options resolved by the caller before import. A null
   * option uses DsnReadOptions::DEFAULT().
   */
  static fr::io::BoardReadResult* readBoard(std::istream& inputStream, fr::BoardObservers* observers,
                                            fr::IdGenerator* idGenerator,
                                            const std::optional<std::string>& designName,
                                            DsnReadOptions* options);

  /** Parses a Specctra DSN directly from an in-memory byte array without temporary files. */
  static fr::io::BoardReadResult* readBoard(jrt::Array<int8_t>* dsnBytes,
                                            fr::BoardObservers* observers,
                                            fr::IdGenerator* idGenerator,
                                            const std::optional<std::string>& designName);

  /** Parses a Specctra DSN from an in-memory byte array with explicit read options. */
  static fr::io::BoardReadResult* readBoard(jrt::Array<int8_t>* dsnBytes,
                                            fr::BoardObservers* observers,
                                            fr::IdGenerator* idGenerator,
                                            const std::optional<std::string>& designName,
                                            DsnReadOptions* options);

  /**
   * Parses only the (parser ...), (resolution ...) and (structure ...) scopes and returns a Success
   * with populated BoardMetadata (board may be null if the DSN had no valid outline), ParseError for
   * malformed headers or IoError for I/O failures. Stops reading after the structure scope.
   */
  static fr::io::BoardReadResult* readMetadata(std::istream& inputStream);

 private:
  DsnReader() = default;

  static fr::io::BoardReadResult* readBoardFromStreams(
      std::istream& parseStream, DsnReader_StreamOpener* scanStreamOpener,
      fr::BoardObservers* observers, fr::IdGenerator* idGenerator,
      const std::optional<std::string>& designName, DsnReadOptions* options);

  // PORT NOTE: closing a std::istream owned by the caller is a no-op.
  static void closeQuietly(std::istream& stream) {}

  /** Deletes the temporary spool file, ignoring errors. PORT NOTE: java.nio Path -> std::string. */
  static void deleteQuietly(const std::optional<std::string>& path);
};

}  // namespace fr::dsn
