#pragma once

#include <functional>
#include <istream>
#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Builder for constructing HeadlessRoutingRequest instances (Java: HeadlessRoutingRequest.Builder). */
// PORT NOTE: the builder's fields collide with its setter names, so the fields get a trailing `_`.
class HeadlessRoutingRequest_Builder : public virtual jrt::Object {
 public:
  HeadlessRoutingRequest_Builder();

  /** Sets the input DSN file content as raw bytes. */
  HeadlessRoutingRequest_Builder* dsnBytes(jrt::Array<int8_t>* dsnBytes);
  /** Sets the input DSN file content from a UTF-8 string (null ignored). */
  HeadlessRoutingRequest_Builder* dsnContent(const std::optional<std::string>& dsnContent);
  /** Reads the input DSN file content from an input stream (Java: null stream ignored). */
  HeadlessRoutingRequest_Builder* dsnStream(std::istream* stream);
  /** Sets the design name hint used in session headers and log messages. */
  HeadlessRoutingRequest_Builder* designName(const std::optional<std::string>& designName);
  /** Sets the optional RULES file content as raw bytes. */
  HeadlessRoutingRequest_Builder* rulesBytes(jrt::Array<int8_t>* rulesBytes);
  /** Sets the optional RULES file content from a UTF-8 string (null ignored). */
  HeadlessRoutingRequest_Builder* rulesContent(const std::optional<std::string>& rulesContent);
  /** Sets the filename of the optional RULES file. */
  HeadlessRoutingRequest_Builder* rulesName(const std::optional<std::string>& rulesName);
  /** Sets custom router settings overrides (applied at highest priority). */
  HeadlessRoutingRequest_Builder* settings(RouterSettings* settings);
  /** Sets the cooperative cancellation token. */
  HeadlessRoutingRequest_Builder* cancellationToken(CancellationToken* cancellationToken);
  /** Sets an optional progress callback listener. */
  HeadlessRoutingRequest_Builder* progressListener(
      HeadlessRoutingProgressListener* progressListener);
  /** Whether to perform authoritative DRC checks upon completion (default true). */
  HeadlessRoutingRequest_Builder* runDrc(bool runDrc);
  /** Whether to serialize the final board to Specctra SES format (default true). */
  HeadlessRoutingRequest_Builder* computeSes(bool computeSes);
  /** PORT EXTENSION (not in Java): observer of the live board, see HeadlessRoutingRequest. */
  HeadlessRoutingRequest_Builder* boardObserver(
      std::function<void(RoutingBoard*, const std::string&)> observer) {
    boardObserver_ = std::move(observer);
    return this;
  }
  /** PORT EXTENSION: route an already loaded board (see HeadlessRoutingRequest::presetBoard). */
  HeadlessRoutingRequest_Builder* presetBoard(RoutingBoard* board) {
    presetBoard_ = board;
    return this;
  }
  /** Builds the immutable HeadlessRoutingRequest. */
  HeadlessRoutingRequest* build();

 private:
  RoutingBoard* presetBoard_ = nullptr;
  std::function<void(RoutingBoard*, const std::string&)> boardObserver_;
  jrt::Array<int8_t>* dsnBytes_ = nullptr;
  std::optional<std::string> designName_ = std::string("design.dsn");
  jrt::Array<int8_t>* rulesBytes_ = nullptr;
  std::optional<std::string> rulesName_;
  RouterSettings* settings_ = nullptr;
  CancellationToken* cancellationToken_ = nullptr;  // CancellationToken.none() in the constructor
  HeadlessRoutingProgressListener* progressListener_ = nullptr;
  bool runDrc_ = true;
  bool computeSes_ = true;
};

/**
 * Immutable configuration request for FreeroutingHeadlessEngine (Java record): in-memory DSN
 * content, optional rules file, settings overrides, progress listener, cancellation token, and
 * output generation options.
 */
class HeadlessRoutingRequest : public virtual jrt::Object {
 public:
  /**
   * Validates inputs and defensively copies byte buffers and settings: throws
   * IllegalArgumentException for null/empty dsnBytes; blank designName -> "design.dsn"; null
   * cancellationToken -> CancellationToken::none().
   */
  HeadlessRoutingRequest(jrt::Array<int8_t>* dsnBytes,
                         const std::optional<std::string>& designName,
                         jrt::Array<int8_t>* rulesBytes,
                         const std::optional<std::string>& rulesName, RouterSettings* settings,
                         CancellationToken* cancellationToken,
                         HeadlessRoutingProgressListener* progressListener, bool runDrc,
                         bool computeSes);

  /** Returns a copy of the DSN bytes. */
  jrt::Array<int8_t>* dsnBytes();
  /** Never null after construction. */
  std::string designName() { return *designName_; }
  /** Returns a copy of the rules bytes, or null. */
  jrt::Array<int8_t>* rulesBytes();
  std::optional<std::string> rulesName() { return rulesName_; }
  /** Returns a clone of the settings, or null. */
  RouterSettings* settings();
  CancellationToken* cancellationToken() { return cancellationToken_; }
  HeadlessRoutingProgressListener* progressListener() { return progressListener_; }
  bool runDrc() { return runDrc_; }
  bool computeSes() { return computeSes_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

  /**
   * PORT EXTENSION (not in Java): if set, called on the routing thread with the live board
   * (job.board) and a short reason ("loaded", "board updated", "routing finished",
   * "optimization finished", ...) whenever the pipeline reports progress. Used by front ends to
   * take render snapshots. Must not modify the board.
   */
  std::function<void(RoutingBoard*, const std::string&)> boardObserver;
  /** PORT EXTENSION: route this board instead of parsing dsnBytes (which still feed the settings). */
  RoutingBoard* presetBoard = nullptr;

  /** Creates a new builder for HeadlessRoutingRequest. */
  static HeadlessRoutingRequest_Builder* builder();
  /** Creates a request with default settings for the given raw DSN bytes. */
  static HeadlessRoutingRequest* of(jrt::Array<int8_t>* dsnBytes);
  /** Creates a request with default settings for the given DSN string content. */
  static HeadlessRoutingRequest* of(const std::string& dsnContent);
  /** Creates a request with default settings by reading all bytes from the DSN stream. */
  static HeadlessRoutingRequest* of(std::istream& dsnStream);

 private:
  jrt::Array<int8_t>* dsnBytes_;
  std::optional<std::string> designName_;
  jrt::Array<int8_t>* rulesBytes_;
  std::optional<std::string> rulesName_;
  RouterSettings* settings_;
  CancellationToken* cancellationToken_;
  HeadlessRoutingProgressListener* progressListener_;
  bool runDrc_;
  bool computeSes_;
};

}  // namespace fr
