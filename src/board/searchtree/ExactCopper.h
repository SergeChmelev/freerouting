#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Copper as a convex core grown by a radius. A circle is its centre grown by its radius, an oval
 * pad its centre line grown by its half width; a pad polygon or a trace tile is its polygon grown
 * by nothing (or by a tree's clearance compensation and a drill's hole envelope). The search trees
 * keep copper as a bounding tile; the clearance query keeps the tiles as its fast test and
 * measures this shape where they overlap, so a legal position is not refused because of the tile.
 */
class ExactCopper final : public virtual jrt::Object {
 private:
  jrt::Array<FloatPoint*>* corners;
  TileShape* polygon;
  FloatPoint* centre;

 public:
  double radius;

 private:
  ExactCopper(TileShape* polygon, FloatPoint* centre, double radius);

  /** A segment grown by a radius: the copper of an oval pad. */
  ExactCopper(jrt::Array<FloatPoint*>* segment, double radius);

 public:
  /** The copper of shape; null for a shape without a convex core. */
  static ExactCopper* of(Shape* shape);

  /** The copper of shape grown by grow; null for a shape without a convex core. */
  static ExactCopper* of(Shape* shape, double grow);

  /**
   * The copper of a round or oval pad, every point within its radius of its centre line, grown by
   * grow. The pad's board shape is a polygon around it, which reaches past the round ends.
   */
  static ExactCopper* ofOval(PadOval* oval, double grow);

  /** The distance between the copper of a and b, 0 where they overlap. */
  static double distance(ExactCopper* a, ExactCopper* b);

 private:
  /**
   * The distance between two cores, one of them at least a segment: 0 where they cross, else the
   * smallest distance from a corner of one to the other.
   */
  static double segmentDistance(ExactCopper* a, ExactCopper* b);

  /** Whether the segments p1-p2 and q1-q2 cross at a point inside both. */
  static bool segmentsCross(FloatPoint* p1, FloatPoint* p2, FloatPoint* q1, FloatPoint* q2);

  static double cross(FloatPoint* from, FloatPoint* to, FloatPoint* point);

  static double polygonDistance(ExactCopper* a, ExactCopper* b);

 public:
  /** The distance of point to the convex polygon with these corners, 0 inside it. */
  static double pointDistance(jrt::Array<FloatPoint*>* corners, FloatPoint* point);
};

}  // namespace fr
