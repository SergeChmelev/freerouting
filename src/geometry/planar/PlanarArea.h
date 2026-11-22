#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * PORT NOTE: Java wraps an org.locationtech.jts Geometry (JTS is not available in C++). The
 * geometry representation is an opaque type defined in PlanarArea.cpp (phase 2: GEOS or an own
 * polygon-boolean implementation reproducing the JTS results). It is not one of the ported Java
 * types and never leaves this class.
 */
struct PlanarAreaGeometry;

/**
 * Java record PlanarArea.Contour: a closed polygon contour with an outer boundary and optional
 * hole rings.
 */
class PlanarArea_Contour : public virtual jrt::Object {
 public:
  PlanarArea_Contour(jrt::List<FloatPoint*>* outer, jrt::List<jrt::List<FloatPoint*>*>* holes)
      : outer_(outer), holes_(holes) {}

  jrt::List<FloatPoint*>* outer() { return outer_; }
  jrt::List<jrt::List<FloatPoint*>*>* holes() { return holes_; }

  bool equals(jrt::Object* o) override {
    if (o == this) return true;
    auto* other = dynamic_cast<PlanarArea_Contour*>(o);
    if (other == nullptr) return false;
    return jrt::equalsOf(outer_, other->outer_) && jrt::equalsOf(holes_, other->holes_);
  }
  int32_t hashCode() override {
    int32_t h = 0;
    h = jrt::hashCombine(h, jrt::hashOf(outer_));
    h = jrt::hashCombine(h, jrt::hashOf(holes_));
    return h;
  }
  std::string toString() override {
    return "Contour[outer=" + jrt::str(outer_) + ", holes=" + jrt::str(holes_) + "]";
  }

 private:
  jrt::List<FloatPoint*>* outer_;
  jrt::List<jrt::List<FloatPoint*>*>* holes_;
};

/**
 * Pure-Java 2D planar polygon area supporting constructive solid geometry (CSG) boolean
 * operations (difference, union, intersection), island decomposition and containment queries.
 * PORT NOTE: Java `union` is a C++ keyword -> `union_`.
 */
class PlanarArea final : public virtual jrt::Object {
 private:
  /** PORT NOTE: Java statics GEOMETRY_FACTORY / EMPTY -> accessor (GEOMETRY_FACTORY is folded
   * into the opaque geometry implementation). */
  static PlanarArea* EMPTY();

  PlanarAreaGeometry* geometry;

 public:
  /** Constructs a PlanarArea wrapping the specified geometry (null -> empty polygon). */
  explicit PlanarArea(PlanarAreaGeometry* geometry);

  /** Returns an empty planar area. */
  static PlanarArea* empty();

  /** Creates a PlanarArea from an IntBox. */
  static PlanarArea* fromBox(IntBox* box);

  /** Creates a PlanarArea approximating a Circle with a conservative circumscribed polygon. */
  static PlanarArea* fromCircle(Circle* circle, int32_t numPoints);

  /** Creates a PlanarArea from a PolylineShape. */
  static PlanarArea* fromPolylineShape(PolylineShape* poly);

  /** Creates a PlanarArea from a generic Shape. */
  static PlanarArea* fromShape(Shape* shape);

  /** Creates a PlanarArea from a Freerouting domain Area (border minus holes). */
  static PlanarArea* fromArea(Area* area);

  /** Creates a rotated spoke rectangle centered at (centerX, centerY). */
  static PlanarArea* createRotatedSpoke(double centerX, double centerY, double spokeWidth,
                                        double length, double angleRadians);

  /** Returns true if this area is empty or has zero area. */
  bool isEmpty();

 private:
  static PlanarAreaGeometry* repairTopology(PlanarAreaGeometry* g);

 public:
  /** Computes the geometric difference (this minus other). */
  PlanarArea* difference(PlanarArea* other);

  /** Computes the geometric union of this and other. */
  PlanarArea* union_(PlanarArea* other);

  /** Computes the geometric intersection of this and other. */
  PlanarArea* intersection(PlanarArea* other);

  /** Computes the union of a collection of planar areas using an optimized spatial union. */
  static PlanarArea* unionAll(jrt::List<PlanarArea*>* areas);

  /** Subtracts all given cutout areas from this area. */
  PlanarArea* subtractAll(jrt::List<PlanarArea*>* cutouts);

  /** Decomposes this planar area into contiguous, disjoint island polygons. */
  jrt::List<PlanarArea*>* decomposeIntoIslands();

 private:
  static void collectPolygons(PlanarAreaGeometry* geom, jrt::List<PlanarArea*>* list);

 public:
  /** Tests if the specified point (x, y) is contained within this area. */
  bool contains(double x, double y);

  /** Returns the total area in square board units. */
  double getArea();

  /** Returns the center coordinate of this area's bounding envelope. */
  jrt::Array<double>* getCenter();

  /** Returns bounding box as [minX, minY, width, height]. */
  jrt::Array<double>* getBoundingBox();

  /** Converts all disjoint islands and holes into lists of FloatPoint contours for rendering or
   * export. */
  jrt::List<PlanarArea_Contour*>* toContours();

  // PORT NOTE: Java `private static List<FloatPoint> ringToPoints(LinearRing ring)` operates on a
  // JTS ring; its C++ counterpart is an implementation detail of PlanarArea.cpp.
};

}  // namespace fr
