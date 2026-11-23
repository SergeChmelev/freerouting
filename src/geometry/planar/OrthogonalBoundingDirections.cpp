#include "geometry/planar/OrthogonalBoundingDirections.h"

#include "geometry/planar/Circle.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/PolygonShape.h"
#include "geometry/planar/RegularTileShape.h"
#include "geometry/planar/Simplex.h"

namespace fr {

OrthogonalBoundingDirections* OrthogonalBoundingDirections::INSTANCE() {
  static OrthogonalBoundingDirections* v = jrt::onHeap([] { return new (jrt::kGeom) OrthogonalBoundingDirections(); });
  return v;
}

RegularTileShape* OrthogonalBoundingDirections::bounds(ConvexShape* shape) {
  return shape->boundingShape(this);
}

RegularTileShape* OrthogonalBoundingDirections::bounds(IntBox* box) { return box; }

RegularTileShape* OrthogonalBoundingDirections::bounds(IntOctagon* oct) {
  return oct->boundingBox();
}

RegularTileShape* OrthogonalBoundingDirections::bounds(Simplex* simplex) {
  return simplex->boundingBox();
}

RegularTileShape* OrthogonalBoundingDirections::bounds(Circle* circle) {
  return circle->boundingBox();
}

RegularTileShape* OrthogonalBoundingDirections::bounds(PolygonShape* polygon) {
  return polygon->boundingBox();
}

}  // namespace fr
