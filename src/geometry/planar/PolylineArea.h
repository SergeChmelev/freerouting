#pragma once

#include "fwd.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/PolylineShape.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * A PolylineArea is an Area, where the outside border curve and the hole borders consist of
 * straight lines.
 */
class PolylineArea : public virtual Area {
 public:
  // PORT NOTE: package-private fields -> public.
  PolylineShape* borderShape;
  jrt::Array<PolylineShape*>* holeArr;

 private:
  jrt::Array<TileShape*>* precalculatedConvexPieces = nullptr;

 public:
  /** Creates a new instance of PolylineShapeWithHoles. */
  PolylineArea(PolylineShape* borderShape, jrt::Array<PolylineShape*>* holeArr);

 private:
  static void cutoutHolePiece(TileShape* dividePiece, TileShape* holePiece,
                              jrt::Collection<TileShape*>* pieces);

 public:
  int32_t dimension() override;
  bool isBounded() override;
  bool isEmpty() override;
  bool isContainedIn(IntBox* box) override;
  PolylineShape* getBorder() override { return borderShape; }

  /** PORT NOTE: Java returns PolylineShape[] (covariant array); C++ arrays are not covariant, so
   * this returns a new Array<Shape*>* with the elements of holeArr (all PolylineShape*). Use the
   * holeArr field for a typed view. */
  jrt::Array<Shape*>* getHoles() override;

  IntBox* boundingBox() override;
  IntOctagon* boundingOctagon() override;
  bool contains(FloatPoint* point) override;
  bool contains(Point* point) override;
  FloatPoint* nearestPointApprox(FloatPoint* fromPoint) override;
  PolylineArea* translateBy(Vector* vector) override;
  jrt::Array<FloatPoint*>* cornerApproxArr() override;

  /** Takes the convex pieces of source, if source is split already and has the same border and
   * holes, so that this area need not be split again. */
  virtual void reuseConvexPiecesOf(PolylineArea* source);

 private:
  static bool sameBorder(PolylineShape* first, PolylineShape* second);

 public:
  /** Splits this polygon shape with holes into convex pieces. The result is not exact, because
   * rounded intersections of lines are used in the result pieces. */
  jrt::Array<TileShape*>* splitToConvex() override;

  /** Splits this polygon shape with holes into convex pieces. Returns null, if
   * stoppableThread was requested to stop. */
  virtual jrt::Array<TileShape*>* splitToConvex(Stoppable* stoppableThread);

  PolylineArea* turn90Degree(int32_t factor, IntPoint* pole) override;
  PolylineArea* rotateApprox(double angle, FloatPoint* pole) override;
  PolylineArea* mirrorVertical(IntPoint* pole) override;
  PolylineArea* mirrorHorizontal(IntPoint* pole) override;
};

}  // namespace fr
