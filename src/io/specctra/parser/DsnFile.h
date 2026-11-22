#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Result of reading a dsn file. */
enum class DsnFile_ReadResult : int32_t { OK, OUTLINE_MISSING, ERROR };

inline std::string DsnFile_ReadResult_name(DsnFile_ReadResult e) {
  switch (e) {
    case DsnFile_ReadResult::OK: return "OK";
    case DsnFile_ReadResult::OUTLINE_MISSING: return "OUTLINE_MISSING";
    case DsnFile_ReadResult::ERROR: return "ERROR";
  }
  return "";
}

inline std::string DsnFile_ReadResult_toString(DsnFile_ReadResult e) {
  return DsnFile_ReadResult_name(e);
}

inline jrt::Array<DsnFile_ReadResult>* DsnFile_ReadResult_values() {
  return jrt::Array<DsnFile_ReadResult>::of(
      {DsnFile_ReadResult::OK, DsnFile_ReadResult::OUTLINE_MISSING, DsnFile_ReadResult::ERROR});
}

inline DsnFile_ReadResult DsnFile_ReadResult_valueOf(const std::string& name) {
  for (DsnFile_ReadResult e : *DsnFile_ReadResult_values()) {
    if (DsnFile_ReadResult_name(e) == name) return e;
  }
  throw jrt::IllegalArgumentException(
      "No enum constant app.freerouting.io.specctra.parser.DsnFile.ReadResult." + name);
}

/** Class for reading and writing dsn-files (static helpers; the read path is in DsnReader). */
class DsnFile final : public virtual jrt::Object {
 public:
  static constexpr char CLASS_CLEARANCE_SEPARATOR = '-';

  /**
   * Turns board keepouts that lie entirely inside the copper of a through-hole pad into via
   * keepouts (KiCad exports plated-slot holes as keepouts on every signal layer). Returns the number
   * of converted keepouts. Called from DsnReader::readBoard once the components are placed.
   */
  static int32_t convertPlatedSlotKeepouts(fr::BasicBoard* routingBoard);

  /**
   * Removes unnamed board-level generic keepouts that enclose the center of a routable single-layer
   * (SMD) pin on the same layer (KiCad copper-pour-only rule areas). Named board keepouts are
   * preserved. Returns the number of removed keepouts.
   */
  static int32_t removePinEnclosingKeepouts(fr::BasicBoard* routingBoard);

  /**
   * Sets containsPlane to true for nets with a conductionArea covering a large part of a signal
   * layer. Returns true, if something was changed. Called from DsnReader::readBoard when the DSN
   * file contains no (autoroute ...) scope.
   */
  static bool adjustPlaneAutorouteSettings(fr::BasicBoard* routingBoard);

  static bool readOnOffScope(IJFlexScanner* scanner);

  static int32_t readIntegerScope(IJFlexScanner* scanner);

  static double readFloatScope(IJFlexScanner* scanner);

  /** Returns the string, or null (nullopt) on I/O error. */
  static std::optional<std::string> readStringScope(IJFlexScanner* scanner);

  /** Returns null if the closing bracket is missing. */
  static jrt::Array<std::string>* readStringListScope(IJFlexScanner* scanner);

 private:
  DsnFile() = default;

  static bool enclosesRoutableSmdPinCenter(fr::ObstacleArea* keepout, fr::BasicBoard* routingBoard);

  static bool contains(fr::IntBox* outer, fr::IntBox* inner);

  static bool isInsideThroughHolePad(fr::ObstacleArea* keepout, fr::BasicBoard* routingBoard);
};

}  // namespace fr::dsn
