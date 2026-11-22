#pragma once

#include <istream>
#include <optional>
#include <string>

#include "core/events/BoardFileDetailsUpdatedEventListener.h"  // for the inline ArrayList
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Stores file data, metadata, and derived board statistics. */
// PORT NOTE: java.io.File / java.nio.file.Path -> std::string paths; ByteArrayInputStream ->
// the byte array itself; CRC32 -> its int64_t value; gson toString()/saveAs() -> a simplified
// hand-written JSON. `volatile` dropped (single-threaded).
class BoardFileDetails : public virtual jrt::Object {
 protected:
  jrt::List<BoardFileDetailsUpdatedEventListener*>* updatedEventListeners =
      new jrt::ArrayList<BoardFileDetailsUpdatedEventListener*>();

 public:
  // "size": the size of the file in bytes
  int64_t size = 0;
  // "crc32": the CRC32 checksum of the data
  int64_t crc32 = 0;
  // "format": the format of the file (initialised to FileFormat::UNKNOWN in the constructors)
  io::FileFormat format{};
  // "statistics" (initialised to new BoardStatistics() in the constructors)
  BoardStatistics* statistics = nullptr;

 protected:
  // "filename": the filename only without the path
  std::string filename = "";
  // "path": the absolute path to the directory of the file
  std::string directoryPath = "";
  jrt::Array<int8_t>* dataBytes = new jrt::Array<int8_t>(0);

 public:
  /** Creates empty file details. */
  BoardFileDetails();
  /** Creates a new BoardFileDetails object from a file (Java: BoardFileDetails(File)). */
  explicit BoardFileDetails(const std::string& file);
  /** Creates a new BoardFileDetails object from a board (statistics only). */
  explicit BoardFileDetails(BasicBoard* board);

  /** Calculates the CRC32 checksum of the supplied stream (Java returns the CRC32 object). */
  static int64_t calculateCrc32(std::istream& inputStream);

  /** Saves this object to a UTF-8 JSON file. */
  void saveAs(const std::string& filename);

  std::string getAbsolutePath();

  /** Java returns a ByteArrayInputStream over the data; the port returns the bytes. */
  jrt::Array<int8_t>* getData();

  /** Replaces the file data and updates its metadata and statistics. */
  void setData(jrt::Array<int8_t>* data);

  /**
   * Discards unavailable output bytes while retaining the selected format, destination, and
   * listeners. No update event is fired.
   */
  void invalidateData();

  /** Returns a JSON representation of this object. */
  std::string toString() override;

  /** Returns the file path represented by these details, or null if no filename is set. */
  std::optional<std::string> getFile();

  /** Returns the directory containing the file. */
  std::string getDirectoryPath() { return this->directoryPath; }

  /** Returns the filename without its directory. */
  std::string getFilename() { return this->filename; }

  /** Sets both the filename and the path (null clears both). */
  void setFilename(const std::optional<std::string>& filename);

  /** Returns the filename without its final extension. */
  std::string getFilenameWithoutExtension();

  /** Registers a listener for changes to these details. */
  void addUpdatedEventListener(BoardFileDetailsUpdatedEventListener* listener);

  /** Notifies all registered listeners of an update. */
  void fireUpdatedEvent();
};

}  // namespace fr
