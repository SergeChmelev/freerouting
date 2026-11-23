// PORT NOTE: see GeomCopy.h.
#include "geometry/planar/GeomCopy.h"

#include <cstdio>
#include <cstdlib>

#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/RationalVector.h"
#include "geometry/planar/IntVector.h"
#include "geometry/planar/RationalPoint.h"
#include "geometry/planar/Simplex.h"

namespace fr::geomcopy {

namespace {

[[noreturn]] void unhandled(const char* what, jrt::Object* o) {
  std::fprintf(stderr, "geomcopy: cannot copy %s of type %s out of a geometry region\n", what,
               jrt::className(o).c_str());
  std::abort();
}

}  // namespace

Point* point(Point* p, const jrt::GeomRegion& from) {
  if (p == nullptr || !from.contains(p)) return p;
  if (auto* ip = jrt::fast_cast<IntPoint>(p)) return new (jrt::kGeom) IntPoint(ip->x, ip->y);
  if (auto* rp = jrt::fast_cast<RationalPoint>(p)) return new (jrt::kGeom) RationalPoint(rp->x, rp->y, rp->z);
  unhandled("point", p);
}

Line* line(Line* l, const jrt::GeomRegion& from) {
  if (l == nullptr || !from.contains(l)) return l;
  auto* ia = jrt::fast_cast<IntPoint>(l->a);
  auto* ib = jrt::fast_cast<IntPoint>(l->b);
  if (ia != nullptr && ib != nullptr) return Line::ofInts(ia->x, ia->y, ib->x, ib->y);
  return new (jrt::kGeom) Line(point(l->a, from), point(l->b, from));
}

jrt::Array<Line*>* lines(jrt::Array<Line*>* a, const jrt::GeomRegion& from) {
  if (a == nullptr) return a;
  bool inside = from.contains(a);
  if (!inside) {
    // An outside array can only hold outside lines (it was filled before the region or in its
    // holder's domain).
    return a;
  }
  auto* result = new (jrt::kGeom) jrt::Array<Line*>(a->length);
  for (int32_t i = 0; i < a->length; i++) (*result)[i] = line((*a)[i], from);
  return result;
}

jrt::Array<IntPoint*>* intPoints(jrt::Array<IntPoint*>* a, const jrt::GeomRegion& from) {
  if (a == nullptr) return a;
  jrt::Array<IntPoint*>* result = from.contains(a) ? new (jrt::kGeom) jrt::Array<IntPoint*>(a->length) : a;
  for (int32_t i = 0; i < a->length; i++) {
    IntPoint* p = (*a)[i];
    (*result)[i] = p != nullptr && from.contains(p) ? new (jrt::kGeom) IntPoint(p->x, p->y) : p;
  }
  return result;
}

IntOctagon* octagon(IntOctagon* s, const jrt::GeomRegion& from) {
  if (s == nullptr || !from.contains(s)) return s;
  return new (jrt::kGeom) IntOctagon(s->leftX, s->bottomY, s->rightX, s->topY, s->upperLeftDiagonalX,
                                     s->lowerRightDiagonalX, s->lowerLeftDiagonalX, s->upperRightDiagonalX);
}

TileShape* tileShape(TileShape* s, const jrt::GeomRegion& from) {
  if (s == nullptr || !from.contains(s)) return s;
  if (auto* o = jrt::fast_cast<IntOctagon>(s)) return octagon(o, from);
  if (auto* b = jrt::fast_cast<IntBox>(s)) return IntBox::ofInts(b->ll->x, b->ll->y, b->ur->x, b->ur->y);
  if (auto* x = jrt::fast_cast<Simplex>(s)) return new (jrt::kGeom) Simplex(lines(x->edgeLines(), from));
  unhandled("shape", s);
}

namespace {

bool pointLive(Point* p) { return p != nullptr && jrt::in_live_geom_region(p); }

Point* pointToHeap(Point* p) {
  if (!pointLive(p)) return p;
  if (auto* ip = jrt::fast_cast<IntPoint>(p)) return new (jrt::kGeom) IntPoint(ip->x, ip->y);
  if (auto* rp = jrt::fast_cast<RationalPoint>(p)) return new (jrt::kGeom) RationalPoint(rp->x, rp->y, rp->z);
  unhandled("point", p);
}

bool lineLive(Line* l) { return l != nullptr && (jrt::in_live_geom_region(l) || pointLive(l->a) || pointLive(l->b)); }

Line* lineToHeap(Line* l) {
  if (!lineLive(l)) return l;
  auto* ia = jrt::fast_cast<IntPoint>(l->a);
  auto* ib = jrt::fast_cast<IntPoint>(l->b);
  if (ia != nullptr && ib != nullptr) return Line::ofInts(ia->x, ia->y, ib->x, ib->y);
  return new (jrt::kGeom) Line(pointToHeap(l->a), pointToHeap(l->b));
}

}  // namespace

Point* keep(Point* x) {
  if (!pointLive(x)) return x;
  jrt::HeapScope heap;
  return pointToHeap(x);
}

Vector* keep(Vector* x) {
  if (x == nullptr || !jrt::in_live_geom_region(x)) return x;
  jrt::HeapScope heap;
  if (auto* iv = jrt::fast_cast<IntVector>(x)) return new (jrt::kGeom) IntVector(iv->x, iv->y);
  if (auto* rv = jrt::fast_cast<RationalVector>(x)) return new (jrt::kGeom) RationalVector(rv->x, rv->y, rv->z);
  unhandled("vector", x);
}

Polyline* keep(Polyline* x) {
  if (x == nullptr) return x;
  bool live = jrt::in_live_geom_region(x) || jrt::in_live_geom_region(x->lines);
  for (int32_t i = 0; !live && i < x->lines->length; i++) live = lineLive((*x->lines)[i]);
  if (!live) return x;
  jrt::HeapScope heap;
  auto* copy = new (jrt::kGeom) jrt::Array<Line*>(x->lines->length);
  for (int32_t i = 0; i < x->lines->length; i++) (*copy)[i] = lineToHeap((*x->lines)[i]);
  return Polyline::ofNormalizedLines(copy);
}

}  // namespace fr::geomcopy
