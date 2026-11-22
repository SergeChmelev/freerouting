#include "geometry/planar/PlanarArea.h"

#include <geos_c.h>

#include <vector>

#include "geometry/planar/Area.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/Shape.h"

// PORT NOTE: Java wraps a JTS Geometry. The port uses the GEOS C API (GEOS 3.14 is the C++ port
// of JTS 1.20). Differences that cannot be avoided:
//  * JTS 1.20 Geometry.union/difference/intersection default to the "old" overlay engine
//    (OverlayOp + snapping) unless the jts.overlay=ng property is set, while GEOS uses OverlayNG.
//    Results are topologically equal but vertex order / polygon order may differ in rare cases.
//  * JTS signals failures with exceptions (TopologyException etc.); GEOS returns NULL (or 2 for
//    predicates). Every such failure is turned into a jrt::RuntimeException so the Java
//    exception fallbacks (repairTopology paths) run in the same situations.
// GEOS geometries are allocated with C++ new inside libgeos, i.e. in the collected heap; they
// are never destroyed explicitly.

namespace fr {

struct PlanarAreaGeometry {
  GEOSGeometry* g;
  explicit PlanarAreaGeometry(GEOSGeometry* g) : g(g) {}
};

namespace {

GEOSContextHandle_t geosContext() {
  static GEOSContextHandle_t ctx = GEOS_init_r();
  return ctx;
}

class GeosException : public jrt::RuntimeException {
 public:
  explicit GeosException(std::string_view m) : jrt::RuntimeException(m) {}
};

GEOSGeometry* checked(GEOSGeometry* g, const char* what) {
  if (g == nullptr) {
    throw GeosException(what);
  }
  return g;
}

PlanarAreaGeometry* wrap(GEOSGeometry* g) { return new PlanarAreaGeometry(g); }

// GEOMETRY_FACTORY.createPolygon()
GEOSGeometry* createEmptyPolygon() {
  return checked(GEOSGeom_createEmptyPolygon_r(geosContext()), "createPolygon failed");
}

struct Coord {
  double x;
  double y;
};

// GEOMETRY_FACTORY.createLinearRing(coords); throws like JTS on an invalid ring.
GEOSGeometry* createLinearRing(const std::vector<Coord>& coords) {
  GEOSContextHandle_t ctx = geosContext();
  GEOSCoordSequence* seq =
      GEOSCoordSeq_create_r(ctx, static_cast<unsigned int>(coords.size()), 2);
  if (seq == nullptr) {
    throw GeosException("createLinearRing: coordinate sequence");
  }
  for (unsigned int i = 0; i < coords.size(); i++) {
    if (GEOSCoordSeq_setXY_r(ctx, seq, i, coords[i].x, coords[i].y) == 0) {
      throw GeosException("createLinearRing: setXY");
    }
  }
  return checked(GEOSGeom_createLinearRing_r(ctx, seq), "createLinearRing failed");
}

// GEOMETRY_FACTORY.createPolygon(shell)
GEOSGeometry* createPolygon(GEOSGeometry* shell) {
  return checked(GEOSGeom_createPolygon_r(geosContext(), shell, nullptr, 0),
                 "createPolygon failed");
}

bool geomIsEmpty(GEOSGeometry* g) {
  char r = GEOSisEmpty_r(geosContext(), g);
  if (r == 2) {
    throw GeosException("isEmpty failed");
  }
  return r == 1;
}

double geomArea(GEOSGeometry* g) {
  double area = 0.0;
  if (GEOSArea_r(geosContext(), g, &area) == 0) {
    throw GeosException("getArea failed");
  }
  return area;
}

bool geomIsValid(GEOSGeometry* g) {
  char r = GEOSisValid_r(geosContext(), g);
  if (r == 2) {
    throw GeosException("isValid failed");
  }
  return r == 1;
}

GEOSGeometry* geomDifference(GEOSGeometry* a, GEOSGeometry* b) {
  return checked(GEOSDifference_r(geosContext(), a, b), "difference failed");
}

GEOSGeometry* geomUnion(GEOSGeometry* a, GEOSGeometry* b) {
  return checked(GEOSUnion_r(geosContext(), a, b), "union failed");
}

GEOSGeometry* geomIntersection(GEOSGeometry* a, GEOSGeometry* b) {
  return checked(GEOSIntersection_r(geosContext(), a, b), "intersection failed");
}

// UnaryUnionOp.union(Collection<Geometry>)
GEOSGeometry* unaryUnion(const std::vector<GEOSGeometry*>& geoms) {
  GEOSContextHandle_t ctx = geosContext();
  // The collection takes ownership of its members, so it is built from clones.
  std::vector<GEOSGeometry*> members;
  members.reserve(geoms.size());
  for (GEOSGeometry* g : geoms) {
    members.push_back(checked(GEOSGeom_clone_r(ctx, g), "clone failed"));
  }
  GEOSGeometry* collection = checked(
      GEOSGeom_createCollection_r(ctx, GEOS_GEOMETRYCOLLECTION, members.data(),
                                  static_cast<unsigned int>(members.size())),
      "createCollection failed");
  return checked(GEOSUnaryUnion_r(ctx, collection), "unaryUnion failed");
}

// LinearRing.getCoordinates() -> List<FloatPoint>
jrt::List<FloatPoint*>* ringToPoints(const GEOSGeometry* ring) {
  GEOSContextHandle_t ctx = geosContext();
  if (ring == nullptr || GEOSisEmpty_r(ctx, ring) == 1) {
    return new jrt::ArrayList<FloatPoint*>();
  }
  const GEOSCoordSequence* seq = GEOSGeom_getCoordSeq_r(ctx, ring);
  unsigned int size = 0;
  if (seq == nullptr || GEOSCoordSeq_getSize_r(ctx, seq, &size) == 0) {
    throw GeosException("getCoordinates failed");
  }
  auto* points = new jrt::ArrayList<FloatPoint*>();
  for (unsigned int i = 0; i < size; i++) {
    double x = 0.0;
    double y = 0.0;
    GEOSCoordSeq_getXY_r(ctx, seq, i, &x, &y);
    points->add(new FloatPoint(x, y));
  }
  return points;
}

}  // namespace

PlanarArea* PlanarArea::EMPTY() {
  static PlanarArea* v = new PlanarArea(wrap(createEmptyPolygon()));
  return v;
}

PlanarArea::PlanarArea(PlanarAreaGeometry* geometry) {
  this->geometry =
      (geometry != nullptr && geometry->g != nullptr) ? geometry : wrap(createEmptyPolygon());
}

PlanarArea* PlanarArea::empty() { return EMPTY(); }

PlanarArea* PlanarArea::fromBox(IntBox* box) {
  if (box == nullptr || box->width() <= 0 || box->height() <= 0) {
    return empty();
  }
  std::vector<Coord> coords = {
      {static_cast<double>(box->ll->x), static_cast<double>(box->ll->y)},
      {static_cast<double>(box->ur->x), static_cast<double>(box->ll->y)},
      {static_cast<double>(box->ur->x), static_cast<double>(box->ur->y)},
      {static_cast<double>(box->ll->x), static_cast<double>(box->ur->y)},
      {static_cast<double>(box->ll->x), static_cast<double>(box->ll->y)}};
  return new PlanarArea(wrap(createPolygon(createLinearRing(coords))));
}

PlanarArea* PlanarArea::fromCircle(Circle* circle, int32_t numPoints) {
  if (circle == nullptr || circle->radius <= 0) {
    return empty();
  }
  int32_t n = jrt::Math::max(numPoints, 32);
  double circumscribedRadius = circle->radius / jrt::Math::cos(jrt::Math::PI / n);
  // PORT NOTE: JTS GeometricShapeFactory(GEOMETRY_FACTORY) with setCentre / setSize /
  // setNumPoints and createCircle() (= createEllipse()) is reproduced inline, following the
  // JTS 1.20 algorithm: envelope from centre and size, nPts points starting at angle 0 and
  // proceeding counter-clockwise with ang = i * (2 * PI / nPts), closing point = first point,
  // floating precision model (makePrecise is a no-op), rotation angle 0.
  double centreInX = circle->center->x;
  double centreInY = circle->center->y;
  double width = 2.0 * circumscribedRadius;
  double height = 2.0 * circumscribedRadius;
  int32_t nPts = n;
  // Dimensions.getEnvelope(): new Envelope(centre.x - width/2, centre.x + width/2, ...)
  double envX1 = centreInX - width / 2;
  double envX2 = centreInX + width / 2;
  double envY1 = centreInY - height / 2;
  double envY2 = centreInY + height / 2;
  double envMinX = envX1 < envX2 ? envX1 : envX2;
  double envMaxX = envX1 < envX2 ? envX2 : envX1;
  double envMinY = envY1 < envY2 ? envY1 : envY2;
  double envMaxY = envY1 < envY2 ? envY2 : envY1;
  double xRadius = (envMaxX - envMinX) / 2.0;
  double yRadius = (envMaxY - envMinY) / 2.0;
  double centreX = envMinX + xRadius;
  double centreY = envMinY + yRadius;
  std::vector<Coord> pts(static_cast<size_t>(nPts) + 1);
  int32_t iPt = 0;
  for (int32_t i = 0; i < nPts; i++) {
    double ang = i * (2 * jrt::Math::PI / nPts);
    double x = xRadius * jrt::Math::cos(ang) + centreX;
    double y = yRadius * jrt::Math::sin(ang) + centreY;
    pts[iPt++] = Coord{x, y};
  }
  pts[iPt] = pts[0];
  return new PlanarArea(wrap(createPolygon(createLinearRing(pts))));
}

PlanarArea* PlanarArea::fromPolylineShape(PolylineShape* poly) {
  if (poly == nullptr || !poly->isBounded()) {
    return empty();
  }
  int32_t count = poly->borderLineCount();
  if (count < 3) {
    return empty();
  }
  std::vector<Coord> coords(static_cast<size_t>(count) + 1);
  for (int32_t i = 0; i < count; i++) {
    FloatPoint* p = poly->cornerApprox(i);
    coords[i] = Coord{p->x, p->y};
  }
  coords[count] = Coord{coords[0].x, coords[0].y};
  try {
    GEOSGeometry* shell = createLinearRing(coords);
    return new PlanarArea(wrap(createPolygon(shell)));
  } catch (const jrt::Exception&) {
    return empty();
  }
}

PlanarArea* PlanarArea::fromShape(Shape* shape) {
  if (shape == nullptr) {
    return empty();
  }
  if (auto* circle = dynamic_cast<Circle*>(shape)) {
    return fromCircle(circle, 32);
  }
  if (auto* box = dynamic_cast<IntBox*>(shape)) {
    return fromBox(box);
  }
  if (auto* poly = dynamic_cast<PolylineShape*>(shape)) {
    return fromPolylineShape(poly);
  }
  return empty();
}

PlanarArea* PlanarArea::fromArea(Area* area) {
  if (area == nullptr || area->isEmpty()) {
    return empty();
  }
  if (auto* circle = dynamic_cast<Circle*>(area)) {
    return fromCircle(circle, 32);
  }
  Shape* borderShape = area->getBorder();
  auto* border = dynamic_cast<PolylineShape*>(borderShape);
  if (border == nullptr || !border->isBounded()) {
    return empty();
  }
  PlanarArea* result = fromPolylineShape(border);
  jrt::Array<Shape*>* holes = area->getHoles();
  if (holes != nullptr && holes->length > 0) {
    jrt::List<PlanarArea*>* holeAreas = new jrt::ArrayList<PlanarArea*>();
    for (Shape* hole : *holes) {
      PlanarArea* ha = fromShape(hole);
      if (ha != nullptr && !ha->isEmpty()) {
        holeAreas->add(ha);
      }
    }
    if (!holeAreas->isEmpty()) {
      result = result->subtractAll(holeAreas);
    }
  }
  return result;
}

PlanarArea* PlanarArea::createRotatedSpoke(double centerX, double centerY, double spokeWidth,
                                           double length, double angleRadians) {
  double halfW = spokeWidth / 2.0;
  double halfL = length / 2.0;
  double cos = jrt::Math::cos(angleRadians);
  double sin = jrt::Math::sin(angleRadians);

  double localPts[4][2] = {{-halfW, -halfL}, {halfW, -halfL}, {halfW, halfL}, {-halfW, halfL}};

  std::vector<Coord> coords(5);
  for (int32_t i = 0; i < 4; i++) {
    double rx = localPts[i][0] * cos - localPts[i][1] * sin;
    double ry = localPts[i][0] * sin + localPts[i][1] * cos;
    coords[i] = Coord{centerX + rx, centerY + ry};
  }
  coords[4] = Coord{coords[0].x, coords[0].y};

  try {
    GEOSGeometry* shell = createLinearRing(coords);
    return new PlanarArea(wrap(createPolygon(shell)));
  } catch (const jrt::Exception&) {
    return empty();
  }
}

bool PlanarArea::isEmpty() {
  return geometry == nullptr || geomIsEmpty(geometry->g) || geomArea(geometry->g) <= 0.0;
}

PlanarAreaGeometry* PlanarArea::repairTopology(PlanarAreaGeometry* g) {
  if (g == nullptr || geomIsEmpty(g->g)) {
    return wrap(createEmptyPolygon());
  }
  return geomIsValid(g->g)
             ? g
             : wrap(checked(GEOSBuffer_r(geosContext(), g->g, 0, 8), "buffer failed"));
}

PlanarArea* PlanarArea::difference(PlanarArea* other) {
  if (isEmpty()) {
    return empty();
  }
  if (other == nullptr || other->isEmpty()) {
    return this;
  }
  try {
    return new PlanarArea(wrap(geomDifference(this->geometry->g, other->geometry->g)));
  } catch (const jrt::Exception& ex) {
    try {
      PlanarAreaGeometry* fixedThis = repairTopology(this->geometry);
      PlanarAreaGeometry* fixedOther = repairTopology(other->geometry);
      return new PlanarArea(wrap(geomDifference(fixedThis->g, fixedOther->g)));
    } catch (const jrt::Exception& retryEx) {
      throw jrt::IllegalStateException("Failed to compute planar geometry difference");
    }
  }
}

PlanarArea* PlanarArea::union_(PlanarArea* other) {
  if (isEmpty()) {
    return other != nullptr ? other : empty();
  }
  if (other == nullptr || other->isEmpty()) {
    return this;
  }
  try {
    return new PlanarArea(wrap(geomUnion(this->geometry->g, other->geometry->g)));
  } catch (const jrt::Exception& ex) {
    try {
      PlanarAreaGeometry* fixedThis = repairTopology(this->geometry);
      PlanarAreaGeometry* fixedOther = repairTopology(other->geometry);
      return new PlanarArea(wrap(geomUnion(fixedThis->g, fixedOther->g)));
    } catch (const jrt::Exception& retryEx) {
      throw jrt::IllegalStateException("Failed to compute planar geometry union");
    }
  }
}

PlanarArea* PlanarArea::intersection(PlanarArea* other) {
  if (isEmpty() || other == nullptr || other->isEmpty()) {
    return empty();
  }
  try {
    return new PlanarArea(wrap(geomIntersection(this->geometry->g, other->geometry->g)));
  } catch (const jrt::Exception& ex) {
    try {
      PlanarAreaGeometry* fixedThis = repairTopology(this->geometry);
      PlanarAreaGeometry* fixedOther = repairTopology(other->geometry);
      return new PlanarArea(wrap(geomIntersection(fixedThis->g, fixedOther->g)));
    } catch (const jrt::Exception& retryEx) {
      throw jrt::IllegalStateException("Failed to compute planar geometry intersection");
    }
  }
}

PlanarArea* PlanarArea::unionAll(jrt::List<PlanarArea*>* areas) {
  if (areas == nullptr || areas->isEmpty()) {
    return empty();
  }
  std::vector<GEOSGeometry*> geoms;
  geoms.reserve(static_cast<size_t>(areas->size()));
  for (PlanarArea* a : *areas) {
    if (a != nullptr && !a->isEmpty()) {
      geoms.push_back(a->geometry->g);
    }
  }
  if (geoms.empty()) {
    return empty();
  }
  try {
    return new PlanarArea(wrap(unaryUnion(geoms)));
  } catch (const jrt::Exception& ex) {
    try {
      std::vector<GEOSGeometry*> sanitized;
      sanitized.reserve(geoms.size());
      for (GEOSGeometry* g : geoms) {
        sanitized.push_back(repairTopology(wrap(g))->g);
      }
      return new PlanarArea(wrap(unaryUnion(sanitized)));
    } catch (const jrt::Exception& retryEx) {
      PlanarAreaGeometry* result = repairTopology(wrap(geoms[0]));
      for (int32_t i = 1; i < static_cast<int32_t>(geoms.size()); i++) {
        try {
          result = wrap(geomUnion(result->g, repairTopology(wrap(geoms[i]))->g));
        } catch (const jrt::Exception& pairwiseEx) {
          throw jrt::IllegalStateException(
              "Failed to compute planar geometry unionAll at index " + jrt::str(i));
        }
      }
      return new PlanarArea(result);
    }
  }
}

PlanarArea* PlanarArea::subtractAll(jrt::List<PlanarArea*>* cutouts) {
  if (isEmpty() || cutouts == nullptr || cutouts->isEmpty()) {
    return this;
  }
  PlanarArea* combined = unionAll(cutouts);
  return difference(combined);
}

jrt::List<PlanarArea*>* PlanarArea::decomposeIntoIslands() {
  if (isEmpty()) {
    // PORT NOTE: Collections.emptyList() -> new empty list.
    return new jrt::ArrayList<PlanarArea*>();
  }
  jrt::List<PlanarArea*>* islands = new jrt::ArrayList<PlanarArea*>();
  collectPolygons(this->geometry, islands);
  return islands;
}

void PlanarArea::collectPolygons(PlanarAreaGeometry* geom, jrt::List<PlanarArea*>* list) {
  if (geom == nullptr || geom->g == nullptr || geomIsEmpty(geom->g)) {
    return;
  }
  GEOSContextHandle_t ctx = geosContext();
  int typeId = GEOSGeomTypeId_r(ctx, geom->g);
  if (typeId == GEOS_POLYGON) {
    if (!geomIsEmpty(geom->g) && geomArea(geom->g) > 0.0) {
      list->add(new PlanarArea(geom));
    }
  } else if (typeId == GEOS_MULTIPOINT || typeId == GEOS_MULTILINESTRING ||
             typeId == GEOS_MULTIPOLYGON || typeId == GEOS_GEOMETRYCOLLECTION) {
    int numGeometries = GEOSGetNumGeometries_r(ctx, geom->g);
    for (int i = 0; i < numGeometries; i++) {
      // The sub-geometry is owned by its parent, which is never destroyed; a clone keeps the
      // island independent of the parent anyway.
      const GEOSGeometry* sub = GEOSGetGeometryN_r(ctx, geom->g, i);
      collectPolygons(wrap(checked(GEOSGeom_clone_r(ctx, sub), "clone failed")), list);
    }
  }
}

bool PlanarArea::contains(double x, double y) {
  if (isEmpty()) {
    return false;
  }
  try {
    GEOSGeometry* pt = checked(GEOSGeom_createPointFromXY_r(geosContext(), x, y), "createPoint");
    char r = GEOSContains_r(geosContext(), this->geometry->g, pt);
    if (r == 2) {
      throw GeosException("contains failed");
    }
    return r == 1;
  } catch (const jrt::Exception&) {
    return false;
  }
}

double PlanarArea::getArea() { return isEmpty() ? 0.0 : geomArea(this->geometry->g); }

jrt::Array<double>* PlanarArea::getCenter() {
  if (isEmpty()) {
    return jrt::Array<double>::of({0.0, 0.0});
  }
  GEOSContextHandle_t ctx = geosContext();
  double minX = 0, minY = 0, maxX = 0, maxY = 0;
  GEOSGeom_getXMin_r(ctx, this->geometry->g, &minX);
  GEOSGeom_getYMin_r(ctx, this->geometry->g, &minY);
  GEOSGeom_getXMax_r(ctx, this->geometry->g, &maxX);
  GEOSGeom_getYMax_r(ctx, this->geometry->g, &maxY);
  // Envelope.centre(): (minx + maxx) / 2.0, (miny + maxy) / 2.0
  return jrt::Array<double>::of({(minX + maxX) / 2.0, (minY + maxY) / 2.0});
}

jrt::Array<double>* PlanarArea::getBoundingBox() {
  if (isEmpty()) {
    return jrt::Array<double>::of({0.0, 0.0, 0.0, 0.0});
  }
  GEOSContextHandle_t ctx = geosContext();
  double minX = 0, minY = 0, maxX = 0, maxY = 0;
  GEOSGeom_getXMin_r(ctx, this->geometry->g, &minX);
  GEOSGeom_getYMin_r(ctx, this->geometry->g, &minY);
  GEOSGeom_getXMax_r(ctx, this->geometry->g, &maxX);
  GEOSGeom_getYMax_r(ctx, this->geometry->g, &maxY);
  return jrt::Array<double>::of({minX, minY, maxX - minX, maxY - minY});
}

jrt::List<PlanarArea_Contour*>* PlanarArea::toContours() {
  if (isEmpty()) {
    // PORT NOTE: Collections.emptyList() -> new empty list.
    return new jrt::ArrayList<PlanarArea_Contour*>();
  }
  GEOSContextHandle_t ctx = geosContext();
  jrt::List<PlanarArea_Contour*>* contours = new jrt::ArrayList<PlanarArea_Contour*>();
  for (PlanarArea* island : *decomposeIntoIslands()) {
    if (GEOSGeomTypeId_r(ctx, island->geometry->g) == GEOS_POLYGON) {
      GEOSGeometry* poly = island->geometry->g;
      jrt::List<FloatPoint*>* outer = ringToPoints(GEOSGetExteriorRing_r(ctx, poly));
      jrt::List<jrt::List<FloatPoint*>*>* holes = new jrt::ArrayList<jrt::List<FloatPoint*>*>();
      int numInteriorRings = GEOSGetNumInteriorRings_r(ctx, poly);
      for (int i = 0; i < numInteriorRings; i++) {
        holes->add(ringToPoints(GEOSGetInteriorRingN_r(ctx, poly, i)));
      }
      contours->add(new PlanarArea_Contour(outer, holes));
    }
  }
  return contours;
}

}  // namespace fr
