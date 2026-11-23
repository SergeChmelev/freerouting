#include "geometry/planar/FortyfiveDegreeBoundingDirections.h"

#include "geometry/planar/Circle.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/PolygonShape.h"
#include "geometry/planar/Simplex.h"

namespace fr {

FortyfiveDegreeBoundingDirections* FortyfiveDegreeBoundingDirections::INSTANCE() {
  static FortyfiveDegreeBoundingDirections* v = jrt::onHeap([] { return new (jrt::kGeom) FortyfiveDegreeBoundingDirections(); });
  return v;
}

RegularTileShape* FortyfiveDegreeBoundingDirections::bounds(ConvexShape* shape) {
  return shape->boundingShape(this);
}

RegularTileShape* FortyfiveDegreeBoundingDirections::bounds(IntBox* box) {
  return box->toIntOctagon();
}

RegularTileShape* FortyfiveDegreeBoundingDirections::bounds(IntOctagon* oct) { return oct; }

RegularTileShape* FortyfiveDegreeBoundingDirections::bounds(Simplex* simplex) {
  return simplex->boundingOctagon();
}

RegularTileShape* FortyfiveDegreeBoundingDirections::bounds(Circle* circle) {
  return circle->boundingOctagon();
}

RegularTileShape* FortyfiveDegreeBoundingDirections::bounds(PolygonShape* polygon) {
  return polygon->boundingOctagon();
}

}  // namespace fr
