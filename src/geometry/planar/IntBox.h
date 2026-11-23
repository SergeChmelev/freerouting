#pragma once

#include "fwd.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/Simplex.h"
#include "jrt/jrt.h"

namespace fr {

/** Implements functionality of orthogonal rectangles in the plane with integer coordinates. */
class IntBox final : public RegularTileShape {
 public:
  using RegularTileShape::contains;
  using TileShape::containsInside;
  using TileShape::cutout;
  using TileShape::intersects;
  using TileShape::nearestPoint;
  using TileShape::shrink;

  /** Standard implementation of an empty box. PORT NOTE: static field -> accessor. */
  static IntBox* EMPTY();

  /** Stores the coordinates of the lower-left corner. */
  IntPoint* ll;

  /** Stores the coordinates of the upper-right corner. */
  IntPoint* ur;

  /** Creates an IntBox from its lower left and upper right corners. */
  IntBox(IntPoint* ll, IntPoint* ur) : ll(ll), ur(ur) {}

  /** Creates an IntBox from the coordinates of its lower-left and upper-right corners. */
  IntBox(int32_t lowerLeftX, int32_t lowerLeftY, int32_t upperRightX, int32_t upperRightY);

  std::string toString() override;

  bool isIntOctagon() override { return true; }

  /** Returns true, if the box is empty. */
  bool isEmpty() override;

  int32_t borderLineCount() override;

  /** Returns the horizontal extension of the box. */
  virtual int32_t width();

  /** Returns the vertical extension of the box. */
  virtual int32_t height();

  double maxWidth() override;
  double minWidth() override;
  double area() override;
  double circumference() override;
  IntPoint* corner(int32_t no) override;
  int32_t dimension() override;

  /** Checks, if point is located in the interior of this box. */
  virtual bool containsInside(IntPoint* point);

  bool isIntBox() override;
  TileShape* simplify() override;

  /** Calculates the nearest point of this box to fromPoint. */
  virtual FloatPoint* nearestPoint(FloatPoint* fromPoint);

  /** Calculates the sorted maxResultPoints nearest points on the border of this box. point is
   * assumed to be located in the interior of this box. */
  virtual jrt::Array<IntPoint*>* nearestBorderProjections(IntPoint* point, int32_t maxResultPoints);

  /** Calculates distance of this box to fromPoint. */
  double distance(FloatPoint* fromPoint) override;

  /** Computes the weighted distance to the box other. */
  virtual double weightedDistance(IntBox* other, double horizontalWeight, double verticalWeight);

  IntBox* boundingBox() override { return this; }
  int32_t getId() override;
  IntOctagon* boundingOctagon() override;
  bool isBounded() override;
  IntBox* boundingTile() override { return this; }
  bool cornerIsBounded(int32_t no) override;

  RegularTileShape* union_(RegularTileShape* other) override;
  IntBox* union_(IntBox* other) override;
  IntOctagon* union_(IntOctagon* other) override;

  /** Returns the intersection of this box with an IntBox. */
  IntBox* intersection(IntBox* other) override;

  /** Returns the intersection of this box with a ConvexShape. */
  TileShape* intersection(TileShape* other) override;
  IntOctagon* intersection(IntOctagon* other) override;
  Simplex* intersection(Simplex* other) override;

  bool intersects(Shape* other) override;
  bool intersects(IntBox* other) override;
  bool intersects(IntOctagon* other) override;
  bool intersects(Simplex* other) override;
  bool intersects(Circle* other) override;

  /** Returns true, if this box intersects with other and the intersection is 2-dimensional. */
  virtual bool overlaps(IntBox* other);

  bool contains(RegularTileShape* other) override;
  RegularTileShape* boundingShape(ShapeBoundingDirections* dirs) override;

  /** Enlarges the box by offset (result is an IntOctagon). */
  IntOctagon* enlarge(double offset) override;

  IntBox* translateBy(Vector* relCoor) override;
  IntBox* turn90Degree(int32_t factor, IntPoint* pole) override;
  Line* borderLine(int32_t no) override;
  bool borderLineCoordinates(int32_t no, int32_t* c) override;
  int32_t borderLineIndex(Line* line) override;

  /** Returns the box offseted by dist. If dist > 0, the offset is to the outside, else to the
   * inside. */
  IntBox* offset(double dist) override;

  /** Returns the box, where the horizontal boundary is offseted by dist. If dist > 0, the offset
   * is to the outside, else to the inside. */
  virtual IntBox* horizontalOffset(double dist);

  /** Returns the box, where the vertical boundary is offseted by dist. If dist > 0, the offset is
   * to the outside, else to the inside. */
  virtual IntBox* verticalOffset(double dist);

  /** Shrinks the width and height of the box by the input width. The box will not vanish
   * completely. */
  virtual IntBox* shrink(int32_t width);

  Side* compare(RegularTileShape* other, int32_t edgeIndex) override;
  Side* compare(IntBox* other, int32_t edgeIndex) override;
  Side* compare(IntOctagon* other, int32_t edgeIndex) override;

  /** Returns an object of class IntOctagon defining the same shape. */
  virtual IntOctagon* toIntOctagon();

  /** Returns an object of class Simplex defining the same shape. */
  Simplex* toSimplex() override;

  bool isContainedIn(IntBox* other) override;
  bool isContainedIn(IntOctagon* other) override;

  /** Return true, if other is contained in the interior of this box. */
  virtual bool containsInInterior(IntBox* other);

  /** Calculates the part of fromBox, which has minimal distance to this box. */
  virtual IntBox* nearestPart(IntBox* fromBox);

  /** Divides this box into sections with width and height at most maxSectionWidth of about equal
   * size. PORT NOTE: Java returns IntBox[]; C++ arrays are not covariant -> Array<TileShape*>*
   * (all elements IntBox*). */
  jrt::Array<TileShape*>* divideIntoSections(double maxSectionWidth) override;

  jrt::Array<TileShape*>* cutout(TileShape* shape) override;

  /** Divides d minus this box into convex pieces. PORT NOTE: Java returns IntBox[] -> elements
   * are IntBox*. */
  jrt::Array<TileShape*>* cutoutFrom(IntBox* d) override;

  /** PORT NOTE: Java returns Simplex[]. */
  jrt::Array<TileShape*>* cutoutFrom(Simplex* simplex) override;

  /** PORT NOTE: Java returns IntOctagon[]. */
  jrt::Array<TileShape*>* cutoutFrom(IntOctagon* oct) override;
};

}  // namespace fr
