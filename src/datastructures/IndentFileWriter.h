// Port of app.freerouting.datastructures.IndentFileWriter
#pragma once

#include <ostream>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Handles the indenting of scopes while writing to an output text file.
 *
 * PORT NOTE: Java extends OutputStreamWriter(stream, UTF_8). The C++ version wraps a
 * std::ostream& owned by the caller (on the stack, see PORTING.md); strings are UTF-8 already.
 * The OutputStreamWriter methods used by callers are provided: write(String), flush(), close().
 * close() only flushes (the caller owns the stream).
 */
class IndentFileWriter : public virtual jrt::Object {
 public:
  /** Creates a new instance of IndentFileWriter. */
  explicit IndentFileWriter(std::ostream& stream) : stream_(&stream) {}

  /** OutputStreamWriter.write(String). */
  void write(const std::string& str) { (*stream_) << str; }

  /** OutputStreamWriter.flush(). */
  void flush() { stream_->flush(); }

  /** OutputStreamWriter.close(): flushes only, the stream is owned by the caller. */
  void close() { stream_->flush(); }

  /** Begins a new scope. */
  void startScope(bool newLine);

  /** Begins a new scope on a new line. */
  void startScope() { startScope(true); }

  /** Closes the latest open scope. */
  void endScope();

  /** Starts a new line inside a scope. */
  void newLine();

 private:
  static inline const std::string INDENT_STRING = "  ";
  static inline const std::string BEGIN_SCOPE = "(";
  static inline const std::string END_SCOPE = ")";

  std::ostream* stream_;
  int32_t currentIndentLevel = 0;
};

}  // namespace fr
