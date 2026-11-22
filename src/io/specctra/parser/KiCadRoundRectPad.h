#pragma once

#include <cmath>
#include <regex>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Repairs the rounded-rectangle pad polygons written by the Specctra exporter of KiCad 10.0 and
 * earlier (padstack name RoundRect[<layers>]Pad_<W>x<H>_<R>_um_<chamfer ratio>_<chamfer corners>).
 * The legacy exporter enlarged the pad by e = round(r * (1 - cos(PI / 36))) and approximated the
 * corners with inscribed chords (KiCad issue 25751, fixed in KiCad 11). For such a legacy padstack
 * the polygon is replaced by one with the straight edges at the nominal positions and the corners
 * circumscribed around the real radius. Anything else is left untouched.
 */
class KiCadRoundRectPad final : public virtual jrt::Object {
 public:
  /**
   * Returns the corrected shape of a padstack, or shape itself if the padstack is not a
   * rounded-rectangle pad of a legacy KiCad export. shape is in DSN coordinates relative to the
   * padstack origin; dsnUnit is the unit of the DSN coordinates (nullable in Java -> optional).
   */
  static Shape* correct(const std::string& padstackName, Shape* shape,
                        std::optional<fr::Unit> dsnUnit,
                        fr::io::CoordinateTransform* coordinateTransform);

  /** The real corner radius of a legacy export from the radius written in the name. */
  static double legacyRealRadiusUm(double nameRadiusUm);

  /**
   * Returns true if the polygon corners coor lie on the rounded-rectangle outline once around
   * (within tolerance).
   */
  static bool isOnOutlineOnceAround(jrt::Array<double>* coor, double centreX, double centreY,
                                    double halfWidth, double halfHeight, double radius,
                                    double tolerance);

  /** Smallest segment count whose circumscribed corner exceeds the arc by at most 0.5 um. */
  static int32_t segmentsPerQuarter(double radiusUm);

  /** The corners of the rounded rectangle with circumscribed arcs (n segments per quarter). */
  static jrt::Array<double>* corners(double left, double bottom, double right, double top,
                                     double radius, int32_t n, double margin);

 private:
  KiCadRoundRectPad() = default;

  /**
   * The padstack name pattern. PORT NOTE: java.util.regex.Pattern -> std::regex (ECMAScript);
   * Matcher.matches() -> std::regex_match.
   */
  static std::regex* NAME();

  /** 1 - cos(PI / 36), the radius compensation of the legacy KiCad exporter. */
  static inline const double LEGACY_COMPENSATION = 1.0 - std::cos(jrt::Math::PI / 36.0);

  /** Largest excess of the circumscribed corner over the real arc, in micrometres. */
  static constexpr double MAX_CORNER_EXCESS_UM = 0.5;

  /** The DSN coordinate of the nearest board grid line at or beyond dsn in sign direction. */
  static double outwards(double dsn, int32_t sign,
                         fr::io::CoordinateTransform* coordinateTransform);
};

}  // namespace fr::dsn
