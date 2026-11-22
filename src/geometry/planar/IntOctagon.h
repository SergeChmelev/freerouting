#pragma once

#include "fwd.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/Simplex.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Implements functionality for convex shapes, whose borderline directions are multiples of 45
 * degree and defined with integer coordinates.
 */
class IntOctagon final : public RegularTileShape {
 public:
  using RegularTileShape::contains;
  using TileShape::cutout;
  using TileShape::intersects;

  /** Reusable instance of an empty octagon. PORT NOTE: static field -> accessor. */
  static IntOctagon* EMPTY();

  /* Vertical boundaries (east/west) */
  // X-coordinate of the left vertical border
  int32_t leftX;
  // X-coordinate of the right vertical border
  int32_t rightX;

  /* Horizontal boundaries (north/south) */
  // Y-coordinate of the bottom horizontal border
  int32_t bottomY;
  // Y-coordinate of the top horizontal border
  int32_t topY;

  /* Diagonal boundaries at +45 degree angle */
  // X-axis intersection of lower-left diagonal border
  int32_t lowerLeftDiagonalX;
  // X-axis intersection of upper-right diagonal border
  int32_t upperRightDiagonalX;

  /* Diagonal boundaries at -45 degree angle */
  // X-axis intersection of upper-left diagonal border
  int32_t upperLeftDiagonalX;
  // X-axis intersection of lower-right diagonal border
  int32_t lowerRightDiagonalX;

 private:
  /** Result of toSimplex() memorized for performance reasons. */
  Simplex* precalculatedToSimplex = nullptr;

 public:
  /** Creates an IntOctagon from 8 integer boundary values (note the parameter order). */
  IntOctagon(int32_t leftX, int32_t bottomY, int32_t rightX, int32_t topY,
             int32_t upperLeftDiagonalX, int32_t lowerRightDiagonalX, int32_t lowerLeftDiagonalX,
             int32_t upperRightDiagonalX)
      : leftX(leftX),
        rightX(rightX),
        bottomY(bottomY),
        topY(topY),
        lowerLeftDiagonalX(lowerLeftDiagonalX),
        upperRightDiagonalX(upperRightDiagonalX),
        upperLeftDiagonalX(upperLeftDiagonalX),
        lowerRightDiagonalX(lowerRightDiagonalX) {}

  bool isEmpty() override;
  bool isIntOctagon() override { return true; }
  bool isBounded() override;
  bool cornerIsBounded(int32_t no) override;
  IntBox* boundingBox() override;
  IntOctagon* boundingOctagon() override { return this; }
  IntOctagon* boundingTile() override { return this; }
  int32_t dimension() override;
  IntPoint* corner(int32_t no) override;

  /** Returns a stable identifier for this octagon. */
  int32_t getId() override;

  /** Returns the y coordinate of the corner with index no. */
  virtual int32_t cornerY(int32_t no);

  /** Returns the x coordinate of the corner with index no. */
  virtual int32_t cornerX(int32_t no);

  double area() override;
  int32_t borderLineCount() override;
  Line* borderLine(int32_t no) override;
  IntOctagon* translateBy(Vector* relCoor) override;
  double maxWidth() override;
  double minWidth() override;
  IntOctagon* offset(double distance) override;
  IntOctagon* enlarge(double offset) override;
  bool contains(RegularTileShape* other) override;
  bool contains(FloatPoint* point) override;

  RegularTileShape* union_(RegularTileShape* other) override;
  IntOctagon* union_(IntOctagon* other) override;
  IntOctagon* union_(IntBox* other) override;

  TileShape* intersection(TileShape* other) override;
  Simplex* intersection(Simplex* other) override;
  IntOctagon* intersection(IntOctagon* other) override;
  IntOctagon* intersection(IntBox* other) override;

  /** Returns an equivalent octagon with all redundant bounds tightened. */
  virtual IntOctagon* normalize();

  /** Checks, if this IntOctagon is normalized. */
  virtual bool isNormalized();

  Simplex* toSimplex() override;
  RegularTileShape* boundingShape(ShapeBoundingDirections* dirs) override;

  /** Checks, if the point (x, y) is on the left, on the right or on the border line with index
   * borderLineNo of this octagon. */
  virtual Side* sideOfBorderLine(int32_t x, int32_t y, int32_t borderLineNo);

  /** Checks if this normalized octagon is contained in box. */
  bool isContainedIn(IntBox* box) override;
  bool isContainedIn(IntOctagon* other) override;

  bool intersects(Shape* other) override;
  bool intersects(IntBox* other) override;

  /** Checks if two normalized octagons intersect. */
  bool intersects(IntOctagon* other) override;
  bool intersects(Simplex* other) override;
  bool intersects(Circle* other) override;

  /** Returns true, if this octagon intersects with other and the intersection is
   * 2-dimensional. */
  virtual bool overlaps(IntOctagon* other);

  /** Computes the x value of the left boundary of this octagon at y. */
  virtual int32_t leftXValue(int32_t y);

  /** Computes the x value of the right boundary of this octagon at y. */
  virtual int32_t rightXValue(int32_t y);

  /** Computes the y value of the lower boundary of this octagon at x. */
  virtual int32_t lowerYValue(int32_t x);

  /** Computes the y value of the upper boundary of this octagon at x. */
  virtual int32_t upperYValue(int32_t x);

  Side* compare(RegularTileShape* other, int32_t edgeIndex) override;
  Side* compare(IntOctagon* other, int32_t edgeIndex) override;
  Side* compare(IntBox* other, int32_t edgeIndex) override;

  int32_t borderLineIndex(Line* line) override;

  /** Calculates the border point of this octagon from point into the 45 degree direction dir. If
   * this border point is not an IntPoint, the nearest outside IntPoint of the octagon is
   * returned. */
  virtual IntPoint* borderPoint(IntPoint* point, FortyfiveDegreeDirection dir);

  /** Calculates the sorted p_max_result_points nearest points on the border of this octagon in
   * the 45-degree directions. point is assumed to be located in the interior of this octagon. */
  virtual jrt::Array<IntPoint*>* nearestBorderProjections(IntPoint* point, int32_t maxResultPoints);

  /** Checks, if point is on the right of the border line with index lineIndex (with tolerance:
   * Side.COLLINEAR within tolerance). */
  virtual Side* borderLineSideOf(FloatPoint* point, int32_t lineIndex, double tolerance);

  /** Checks, if this octagon can be converted to an IntBox. */
  bool isIntBox() override;

  TileShape* simplify() override;

  jrt::Array<TileShape*>* cutout(TileShape* shape) override;

  /** Divide d minus this octagon into 8 convex pieces, from which 4 have cut off a corner.
   * PORT NOTE: Java returns IntOctagon[]; C++ arrays are not covariant -> Array<TileShape*>*
   * (all elements IntOctagon*). */
  jrt::Array<TileShape*>* cutoutFrom(IntBox* d) override;

  /** Divide d minus this octagon into 8 convex pieces without sharp angles. PORT NOTE: see
   * above (Java IntOctagon[]). */
  jrt::Array<TileShape*>* cutoutFrom(IntOctagon* d) override;

  /** PORT NOTE: Java returns Simplex[]. */
  jrt::Array<TileShape*>* cutoutFrom(Simplex* simplex) override;

  std::string toString() override;
};

}  // namespace fr
