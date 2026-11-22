#pragma once

#include "fwd.h"
#include "geometry/planar/Shape.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Abstract class with functions for shapes whose borders consist of straight lines.
 */
class PolylineShape : public virtual Shape {
 public:
  using Shape::intersects;

  /** Returns true if the shape has no infinite part at this corner. */
  virtual bool cornerIsBounded(int32_t no) = 0;

  /** Returns the number of borderlines of the shape. */
  virtual int32_t borderLineCount() = 0;

  /** Returns the no-th corner of this shape for no between 0 and borderLineCount() - 1. The
   * corners are sorted starting with the smallest y-coordinate in counterclock sense around the
   * shape. If there are several corners with the smallest y-coordinate, the corner with the
   * smallest x-coordinate comes first. Consecutive corners may be equal. */
  virtual Point* corner(int32_t no) = 0;

  /** Turns this shape by factor times 90 degree around pole. */
  PolylineShape* turn90Degree(int32_t factor, IntPoint* pole) override = 0;

  /** Rotates this shape around pole by angle. The result may be not exact. */
  PolylineShape* rotateApprox(double angle, FloatPoint* pole) override = 0;

  /** Mirrors this shape at the horizontal line through pole. */
  PolylineShape* mirrorHorizontal(IntPoint* pole) override = 0;

  /** Mirrors this shape at the vertical line through pole. */
  PolylineShape* mirrorVertical(IntPoint* pole) override = 0;

  /** Returns the affine translation of the area by vector. */
  PolylineShape* translateBy(Vector* vector) override = 0;

  /** Return all unbounded corners of this shape. */
  virtual jrt::Array<Point*>* boundedCorners();

  /** Returns an approximation of the no-th corner of this shape for no between 0 and
   * borderLineCount() - 1. If the shape is not bounded at this corner, the coordinates of the
   * result will be set to Integer.MAX_VALUE. */
  virtual FloatPoint* cornerApprox(int32_t no);

  /** Returns an approximation of the all corners of this shape. If the shape is not bounded at a
   * corner, the coordinates will be set to Integer.MAX_VALUE. */
  jrt::Array<FloatPoint*>* cornerApproxArr() override;

  /** If point is equal to a corner of this shape, the number of that corner is returned; -1
   * otherwise. */
  virtual int32_t equalsCorner(Point* point);

  /** Returns the cumulative border line length of the shape. If the shape is unbounded,
   * Integer.MAX_VALUE is returned. */
  double circumference() override;

  /** Returns the arithmetic middle of the corners of this shape. */
  FloatPoint* centreOfGravity() override;

  /** Checks if this shape is completely contained in box. */
  bool isContainedIn(IntBox* box) override;

  /** Returns the index of the corner of the shape, so that all other points of the shape are to
   * the right of the line from fromPoint to this corner. */
  virtual int32_t indexOfLeftMostCorner(FloatPoint* fromPoint);

  /** Returns the index of the corner of the shape, so that all other points of the shape are to
   * the left of the line from fromPoint to this corner. */
  virtual int32_t indexOfRightMostCorner(FloatPoint* fromPoint);

  /** Returns a FloatLine result, so that result.a is an approximation of the left most corner of
   * this shape when viewed from fromPoint, and result.b is an approximation of the right most
   * corner. */
  virtual FloatLine* polarLineSegment(FloatPoint* fromPoint);

  /** Returns the no-th border line of this shape. */
  virtual Line* borderLine(int32_t no) = 0;

  /** Returns the previous border line or corner number of this shape. */
  virtual int32_t prevNo(int32_t no);

  /** Returns the next border line or corner number of this shape. */
  virtual int32_t nextNo(int32_t no);

  PolylineShape* getBorder() override { return this; }

  jrt::Array<Shape*>* getHoles() override;

  /** Checks, if this shape and line have a common point. */
  virtual bool intersects(Line* line);

  /** Calculates the left most corner of this shape, when looked at from fromPoint. */
  virtual Point* leftMostCorner(Point* fromPoint);

  /** Calculates the right most corner of this shape, when looked at from fromPoint. */
  virtual Point* rightMostCorner(Point* fromPoint);
};

}  // namespace fr
