// PORT NOTE (MEMORY_MODEL.md stage E, "region + copy"): not a Java type.
//
// Copy-out of immutable geometry from a jrt::GeomRegion that is about to be released: every
// function returns its argument unchanged when it does not lie in `from`, otherwise a deep copy
// allocated in the current kGeom domain (the caller selects it, usually the region's enclosing
// domain). The copy has the same values; lazily cached data is not copied (it is recomputed).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::geomcopy {

Point* point(Point* p, const jrt::GeomRegion& from);
Line* line(Line* l, const jrt::GeomRegion& from);
jrt::Array<Line*>* lines(jrt::Array<Line*>* a, const jrt::GeomRegion& from);
/** IntOctagon, IntBox and Simplex; any other shape type in the region aborts (an unhandled
 * escape would read released memory). */
TileShape* tileShape(TileShape* s, const jrt::GeomRegion& from);
IntOctagon* octagon(IntOctagon* s, const jrt::GeomRegion& from);
/** The array and every point in it (an outside array is updated in place). */
jrt::Array<IntPoint*>* intPoints(jrt::Array<IntPoint*>* a, const jrt::GeomRegion& from);

// Copy-at-store (holders outside every region, e.g. board items): returns x itself when no part
// of it lies in a live region of this thread, else a deep copy on the heap.
Point* keep(Point* x);
Vector* keep(Vector* x);
Polyline* keep(Polyline* x);

}  // namespace fr::geomcopy
