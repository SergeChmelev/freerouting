#pragma once

// PORT EXTENSION (not a Java class): java.nio.file.Files.readString(path, UTF_8) and
// Path.getFileName(), as the KiCad file readers (io/kicad) use them.

#include <string>

#include "jrt/jrt.h"

namespace fr {

/** java.nio.file.NoSuchFileException: the file does not exist. */
class NoSuchFileException : public jrt::IOException {
 public:
  explicit NoSuchFileException(std::string_view m) : jrt::IOException(m) {}
};

class FileText final {
 public:
  /**
   * The whole content of a file (Files.readString). Throws NoSuchFileException if it does not
   * exist, IOException if it cannot be read (a directory, no permission...).
   * PORT NOTE: the bytes are returned as they are; Java would reject malformed UTF-8.
   */
  static std::string readString(const std::string& path);

  /** Path.of(path).getFileName().toString(): the last component of the path. */
  static std::string fileName(const std::string& path);

  /** Path.of(path).toString(): the path, separators collapsed and a trailing one removed. */
  static std::string pathString(const std::string& path);
};

}  // namespace fr
