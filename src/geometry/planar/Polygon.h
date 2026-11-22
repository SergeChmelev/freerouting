#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * A Polygon is a list of points in the plane, where no 2 consecutive points may be equal and no 3
 * consecutive points collinear.
 */
class Polygon : public virtual jrt::Object {
 private:
  jrt::Collection<Point*>* corners;

 public:
  /** Creates a polygon from points. Multiple points and points, which are collinear with its
   * previous and next point will be removed. */
  explicit Polygon(jrt::Array<Point*>* points);

  /** Returns corners, the corners of a closed border, without the corners at which the border
   * turns back on itself (spikes). */
  static jrt::Array<Point*>* withoutSpikes(jrt::Array<Point*>* corners);

 private:
  static bool isSpike(Point* prevCorner, Point* corner, Point* nextCorner);

 public:
  /** Returns the array of corners of this polygon. */
  virtual jrt::Array<Point*>* cornerArray();

  /** Reverts the order of the corners of this polygon. */
  virtual Polygon* revertCorners();

  /** Returns the winding number of this polygon, treated as closed. It will be > 0, if the
   * corners are in counterclock sense, and < 0, if the corners are in clockwise sense. */
  virtual int32_t windingNumberAfterClosing();
};

}  // namespace fr
