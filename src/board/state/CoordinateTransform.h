// Port of app.freerouting.board.state.CoordinateTransform
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Transformation function between the board and the user coordinate system.
 * PORT NOTE: java.util.Locale -> std::string language tag (only passed on to PrintableShape).
 */
class CoordinateTransform : public virtual jrt::Object {
 public:
  /** Throws RuntimeException if userUnitFactor != 1.0. */
  CoordinateTransform(double userUnitFactor, Unit userUnit, double boardUnitFactor,
                      Unit boardUnit);

  /** Scale a value from the board to the user coordinate system. */
  double boardToUser(double value);

  /** Transforms a geometry.planar.FloatPoint to a tuple of doubles in the user system. */
  FloatPoint* boardToUser(FloatPoint* point);

  /** Transforms a geometry.planar.Shape from the board to the user coordinate system. */
  PrintableShape* boardToUser(Shape* shape, const std::string& locale);

  /** Transforms a board circle to a user circle. */
  PrintableShape_Circle* boardToUser(Circle* circle, const std::string& locale);

  /** Transforms a board rectangle to a user rectangle. */
  PrintableShape_Rectangle* boardToUser(IntBox* box, const std::string& locale);

  /** Transforms a board polygon to a user polygon. */
  PrintableShape_Polygon* boardToUser(PolylineShape* shape, const std::string& locale);

  /** Scale a value from the user to the board coordinate system. */
  double userToBoard(double value);

  /** Transforms a FloatPoint from the user to the board coordinate system. */
  FloatPoint* userToBoard(FloatPoint* point);

  /** The unit used for user coordinates. */
  const Unit userUnit;

  /** The factor of the user unit. */
  const double userUnitFactor;

  /** The unit used for board coordinates. */
  const Unit boardUnit;

  /** The factor of the board unit. */
  const double boardUnitFactor;

 private:
  /** The factor used for transforming coordinates between user coordinate space and board space. */
  const double scaleFactor;
};

}  // namespace fr
