// Port of app.freerouting.board.model.items.PrintableShape (+ nested Circle, Rectangle, Polygon).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Shape class used for printing a geometry.planar.Shape after transforming it to user
 * coordinates.
 */
class PrintableShape : public virtual jrt::Object {
 protected:
  std::string locale;  // final; PORT NOTE: java.util.Locale -> language tag string

  /** PrintableShape. */
  explicit PrintableShape(const std::string& locale) : locale(locale) {}

 public:
  /** Returns text information about the PrintableShape. */
  std::string toString() override = 0;
};

class PrintableShape_Circle : public PrintableShape {
 public:
  FloatPoint* center;  // final
  double radius;       // final

  /** Creates a Circle from the input coordinates. */
  PrintableShape_Circle(FloatPoint* center, double radius, const std::string& locale)
      : PrintableShape(locale), center(center), radius(radius) {}

  std::string toString() override;
};

class PrintableShape_Rectangle : public PrintableShape {
 public:
  FloatPoint* lowerLeft;   // final
  FloatPoint* upperRight;  // final

  PrintableShape_Rectangle(FloatPoint* lowerLeft, FloatPoint* upperRight, const std::string& locale)
      : PrintableShape(locale), lowerLeft(lowerLeft), upperRight(upperRight) {}

  std::string toString() override;
};

class PrintableShape_Polygon : public PrintableShape {
 public:
  jrt::Array<FloatPoint*>* corners;  // final

  PrintableShape_Polygon(jrt::Array<FloatPoint*>* corners, const std::string& locale)
      : PrintableShape(locale), corners(corners) {}

  std::string toString() override;
};

}  // namespace fr
