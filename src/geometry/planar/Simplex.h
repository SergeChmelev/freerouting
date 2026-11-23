#pragma once

#include "fwd.h"
#include "geometry/planar/TileShape.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Convex shape defined as intersection of half-planes. A half-plane is defined as the positive
 * side of a directed line.
 */
class Simplex final : public TileShape {
 public:
  using TileShape::getInstance;
  using TileShape::indexOfRightMostCorner;
  using TileShape::cutout;
  using TileShape::intersects;

  /** Standard implementation for an empty Simplex. PORT NOTE: static field -> accessor. */
  static Simplex* EMPTY();

 private:
  jrt::Array<Line*>* lines;

  /** Stores precalculated data for this simplex. */
  jrt::Array<Point*>* precalculatedCorners = nullptr;
  jrt::Array<FloatPoint*>* precalculatedFloatCorners = nullptr;
  IntBox* precalculatedBoundingBox = nullptr;
  IntOctagon* precalculatedBoundingOctagon = nullptr;

 public:
  /** Constructs a Simplex from the directed lines in lines. The simplex will not be normalized.
   * To get a normalized simplex use TileShape.getInstance. */
  explicit Simplex(jrt::Array<Line*>* lines) : lines(lines) {}
  /** PORT NOTE (stage E): the edge lines, for copying a simplex out of a geometry region. */
  jrt::Array<Line*>* edgeLines() { return lines; }

  /** Creates a Simplex as intersection of the half-planes defined by directed lines. */
  static Simplex* getInstance(jrt::Array<Line*>* lines);

  /** Return true, if this simplex is empty. */
  bool isEmpty() override;

  /** Converts the physical instance of this shape to a simpler physical instance, if possible
   * (to IntBox or IntOctagon). */
  TileShape* simplify() override;

  int32_t getId() override;

  bool cornerIsBounded(int32_t cornerIndex) override;

  /** Returns true, if the shape of this simplex is contained in a sufficiently large box. */
  bool isBounded() override;

  /** Returns the number of edge lines defining this simplex. */
  int32_t borderLineCount() override;

  /** Returns the intersection of the p_no -1-th with the p_no-th line of this simplex. If the
   * simplex is not bounded at this corner, the coordinates of the result will be set to
   * Integer.MAX_VALUE. */
  Point* corner(int32_t cornerIndex) override;

  /** Returns an approximation of the intersection of the p_no -1-th with the p_no-th line of this
   * simplex by a FloatPoint. */
  FloatPoint* cornerApprox(int32_t cornerIndex) override;

  jrt::Array<FloatPoint*>* cornerApproxArr() override;

  /** Returns the edgeIndex-th edge line of this simplex. The edge lines are sorted in
   * counterclock sense. */
  Line* borderLine(int32_t edgeIndex) override;

  /** Returns the dimension of this simplex. The result may be 2, 1, 0, or -1 (if the simplex is
   * empty). */
  int32_t dimension() override;

  /** Returns the length of the longest side of this simplex. */
  double maxWidth() override;

  /** Returns the width of the simplex (approximation of the minimum diameter). */
  double minWidth() override;

  /** Checks if this simplex can be converted into an IntBox. */
  bool isIntBox() override;

  /** Checks if this simplex can be converted into an IntOctagon. */
  bool isIntOctagon() override;

  /** Converts this simplex to an IntOctagon. Returns null, if that is not possible, because not
   * all lines of this simplex are 45 degree. */
  IntOctagon* toIntOctagon();

  /** Returns the simplex that results from translating its lines by vector. */
  Simplex* translateBy(Vector* vector) override;

  /** Returns the smallest box with int coordinates containing all corners of this simplex. The
   * coordinates of the result will be Integer.MAX_VALUE, if the simplex is not bounded. */
  IntBox* boundingBox() override;

  /** Calculates a bounding octagon of the Simplex. Returns null, if the Simplex is not
   * bounded. */
  IntOctagon* boundingOctagon() override;

  Simplex* boundingTile() override { return this; }

  RegularTileShape* boundingShape(ShapeBoundingDirections* dirs) override;

  /** Returns the simplex offseted by width. If width < 0, the simplex will be shrunk. */
  Simplex* offset(double width) override;

  Simplex* enlarge(double offset) override;

  /** Returns the number of the rightmost corner seen from fromPoint. No other point of this
   * simplex may be to the right of the line from fromPoint to the result corner. */
  virtual int32_t indexOfRightMostCorner(Point* fromPoint);

  /** Returns the intersection of box with this simplex. */
  Simplex* intersection(IntBox* box) override;
  Simplex* intersection(IntOctagon* other) override;

  /** Returns the intersection of this simplex and other. */
  Simplex* intersection(Simplex* other) override;

  /** Returns the intersection of this simplex and the shape other. */
  TileShape* intersection(TileShape* other) override;

  bool intersects(Shape* other) override;
  bool intersects(Simplex* other) override;
  bool intersects(IntBox* box) override;
  bool intersects(IntOctagon* octagon) override;
  bool intersects(Circle* circle) override;

  /** Returns the edge number if line is a border line of this simplex, otherwise -1. */
  int32_t borderLineIndex(Line* line) override;

  /** Constructs a Simplex from the lines of this simplex without the no-th line. */
  virtual Simplex* removeBorderLine(int32_t no);

  Simplex* toSimplex() override { return this; }

  jrt::Array<TileShape*>* cutout(TileShape* shape) override;

  /** Divides outerSimplex minus this simplex into convex pieces. PORT NOTE: Java returns
   * Simplex[]; C++ arrays are not covariant, so the override returns Array<TileShape*>* whose
   * elements are all Simplex*. */
  jrt::Array<TileShape*>* cutoutFrom(Simplex* outerSimplex) override;
  jrt::Array<TileShape*>* cutoutFrom(IntOctagon* oct) override;
  jrt::Array<TileShape*>* cutoutFrom(IntBox* box) override;

  /** Removes lines, which are redundant in the definition of the shape of this simplex. Assumes
   * that the lines of this simplex are sorted. */
  virtual Simplex* removeRedundantLines();

 private:
  /** Calculates the division lines for cutting outerSimplex minus this simplex at the inner
   * corner innerCornerNo. */
  jrt::Array<Line*>* calcDivisionLines(int32_t innerCornerNo, Simplex* outerSimplex);
};

}  // namespace fr
