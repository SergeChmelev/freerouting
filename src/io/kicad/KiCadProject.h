#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

class JsonValue;

/**
 * The board settings of a KiCad project, read from its {@code .kicad_pro} file.
 *
 * <p>KiCad writes the net class widths into a Specctra design, but not the minimum track width of
 * the board ({@code Board Setup > Constraints}): the KiCad DRC reports every trace narrower than it
 * as a {@code track_width} error. The same page sets a minimum connection width, and the KiCad DRC
 * reports every trace narrower than that one as a {@code connection_width} error. This class reads
 * both back, so that Freerouting does not neck a trace down below either.
 */
// PORT NOTE: Gson is replaced by util/JsonReader (same semantics for what is read here).
class KiCadProject final : public virtual jrt::Object {
 private:
  std::string sourceName_;
  std::optional<double> minTrackWidthUm_;
  std::optional<double> minConnectionWidthUm_;

  KiCadProject(const std::string& sourceName, std::optional<double> minTrackWidthUm,
               std::optional<double> minConnectionWidthUm)
      : sourceName_(sourceName),
        minTrackWidthUm_(minTrackWidthUm),
        minConnectionWidthUm_(minConnectionWidthUm) {}

 public:
  /** The file name, for messages. */
  std::string sourceName() { return sourceName_; }

  /**
   * The minimum track width of the board in micrometres ({@code
   * board.design_settings.rules.min_track_width}), or null if the project does not set a positive
   * one.
   */
  std::optional<double> minTrackWidthUm() { return minTrackWidthUm_; }

  /**
   * The minimum connection width of the board in micrometres ({@code
   * board.design_settings.rules.min_connection}), or null if the project does not set a positive
   * one. The KiCad DRC checks it on traces too.
   */
  std::optional<double> minConnectionWidthUm() { return minConnectionWidthUm_; }

  /**
   * The narrowest trace the KiCad DRC accepts, in micrometres: the larger of the minimum track
   * width and the minimum connection width, or null if the project sets neither.
   */
  std::optional<double> narrowestTraceWidthUm();

  /** Reads a {@code .kicad_pro} file (throws IOException, NoSuchFileException). */
  static KiCadProject* read(const std::string& path);

  /**
   * Parses the text of a {@code .kicad_pro} file.
   *
   * @throws IllegalArgumentException if the text is not a JSON object
   */
  static KiCadProject* parse(const std::string& text, const std::string& sourceName);

 private:
  /** A length in millimetres as micrometres, or null if it is not a positive number. */
  static std::optional<double> positiveUm(JsonValue* value);

  static JsonValue* member(JsonValue* element, const std::string& name);
};

}  // namespace fr
