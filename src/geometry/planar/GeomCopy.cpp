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

// "Foreign" geometry: lies in a live region that is not the holder's domain `home`.
bool foreign(const void* p, std::uint32_t home) {
  if (p == nullptr) return false;
  std::uint32_t d = jrt::geom_domain_of(p);
  return d != 0 && d != home;
}

Point* pointCopy(Point* p, std::uint32_t home) {
  if (!foreign(p, home)) return p;
  if (auto* ip = jrt::fast_cast<IntPoint>(p)) return new (jrt::kGeom) IntPoint(ip->x, ip->y);
  if (auto* rp = jrt::fast_cast<RationalPoint>(p)) return new (jrt::kGeom) RationalPoint(rp->x, rp->y, rp->z);
  unhandled("point", p);
}

bool lineForeign(Line* l, std::uint32_t home) {
  return l != nullptr && (foreign(l, home) || foreign(l->a, home) || foreign(l->b, home));
}

Line* lineCopy(Line* l, std::uint32_t home) {
  if (!lineForeign(l, home)) return l;
  auto* ia = jrt::fast_cast<IntPoint>(l->a);
  auto* ib = jrt::fast_cast<IntPoint>(l->b);
  if (ia != nullptr && ib != nullptr) return Line::ofInts(ia->x, ia->y, ib->x, ib->y);
  return new (jrt::kGeom) Line(pointCopy(l->a, home), pointCopy(l->b, home));
}

// Allocates the copy in the holder's domain: its region while that is active, else the heap.
struct HomeTarget {
  jrt::GeomTarget target;
  explicit HomeTarget(std::uint32_t home) : target(home != 0 ? jrt::active_geom_region(home) : nullptr) {}
};

}  // namespace

Point* keep(Point* x, const void* owner) {
  std::uint32_t home = jrt::geom_domain_of(owner);
  if (!foreign(x, home)) return x;
  HomeTarget t(home);
  return pointCopy(x, home);
}

Vector* keep(Vector* x, const void* owner) {
  std::uint32_t home = jrt::geom_domain_of(owner);
  if (!foreign(x, home)) return x;
  HomeTarget t(home);
  if (auto* iv = jrt::fast_cast<IntVector>(x)) return new (jrt::kGeom) IntVector(iv->x, iv->y);
  if (auto* rv = jrt::fast_cast<RationalVector>(x)) return new (jrt::kGeom) RationalVector(rv->x, rv->y, rv->z);
  unhandled("vector", x);
}

namespace {

bool shapeForeign(TileShape* s, std::uint32_t home) {
  if (s == nullptr) return false;
  if (foreign(s, home)) return true;
  if (auto* b = jrt::fast_cast<IntBox>(s)) return foreign(b->ll, home) || foreign(b->ur, home);
  if (auto* x = jrt::fast_cast<Simplex>(s)) {
    jrt::Array<Line*>* ls = x->edgeLines();
    if (foreign(ls, home)) return true;
    for (int32_t i = 0; i < ls->length; i++) {
      if (lineForeign((*ls)[i], home)) return true;
    }
    return false;
  }
  if (jrt::fast_cast<IntOctagon>(s) != nullptr) return false;
  return false;  // other shape types: only the object itself is checked (it aborts if foreign)
}

// Copy into the current domain (caller sets HomeTarget).
TileShape* shapeCopy(TileShape* s, std::uint32_t home) {
  if (auto* o = jrt::fast_cast<IntOctagon>(s)) {
    return new (jrt::kGeom) IntOctagon(o->leftX, o->bottomY, o->rightX, o->topY, o->upperLeftDiagonalX,
                                       o->lowerRightDiagonalX, o->lowerLeftDiagonalX, o->upperRightDiagonalX);
  }
  if (auto* b = jrt::fast_cast<IntBox>(s)) return IntBox::ofInts(b->ll->x, b->ll->y, b->ur->x, b->ur->y);
  if (auto* x = jrt::fast_cast<Simplex>(s)) {
    jrt::Array<Line*>* ls = x->edgeLines();
    auto* copy = new (jrt::kGeom) jrt::Array<Line*>(ls->length);
    for (int32_t i = 0; i < ls->length; i++) (*copy)[i] = lineCopy((*ls)[i], home);
    return new (jrt::kGeom) Simplex(copy);
  }
  unhandled("shape", s);
}

}  // namespace

TileShape* keep(TileShape* x, const void* owner) {
  std::uint32_t home = jrt::geom_domain_of(owner);
  if (!shapeForeign(x, home)) return x;
  HomeTarget t(home);
  return shapeCopy(x, home);
}

jrt::Array<TileShape*>* keep(jrt::Array<TileShape*>* x, const void* owner) {
  if (x == nullptr) return x;
  std::uint32_t home = jrt::geom_domain_of(owner);
  bool needed = foreign(x, home);
  for (int32_t i = 0; !needed && i < x->length; i++) needed = shapeForeign((*x)[i], home);
  if (!needed) return x;
  HomeTarget t(home);
  jrt::Array<TileShape*>* result = foreign(x, home) ? new (jrt::kGeom) jrt::Array<TileShape*>(x->length) : x;
  for (int32_t i = 0; i < x->length; i++) {
    TileShape* s = (*x)[i];
    (*result)[i] = shapeForeign(s, home) ? shapeCopy(s, home) : s;
  }
  return result;
}

Polyline* keep(Polyline* x, const void* owner) {
  if (x == nullptr) return x;
  std::uint32_t home = jrt::geom_domain_of(owner);
  bool needed = foreign(x, home) || foreign(x->lines, home);
  for (int32_t i = 0; !needed && i < x->lines->length; i++) needed = lineForeign((*x->lines)[i], home);
  if (!needed) return x;
  HomeTarget t(home);
  auto* copy = new (jrt::kGeom) jrt::Array<Line*>(x->lines->length);
  for (int32_t i = 0; i < x->lines->length; i++) (*copy)[i] = lineCopy((*x->lines)[i], home);
  return Polyline::ofNormalizedLines(copy);
}

}  // namespace fr::geomcopy
