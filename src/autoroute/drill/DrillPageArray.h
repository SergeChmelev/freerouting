#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Describes the 2 dimensional array of pages of ExpansionDrill`s used in the maze search
 * algorithm. The pages are rectangles of about equal width and height covering the bounding box
 * of the board area.
 */
class DrillPageArray : public virtual jrt::Object {
 private:
  IntBox* bounds;

  /** The number of columns in the array. */
  int32_t columnCount;

  /** The number of rows in the array. */
  int32_t rowCount;

  /** The width of a single page in this array. */
  int32_t pageWidth;

  /** The height of a single page in this array. */
  int32_t pageHeight;

  jrt::Array<jrt::Array<DrillPage*>*>* pages;

 public:
  /** Creates a new instance of DrillPageArray. */
  DrillPageArray(RoutingBoard* board, int32_t maxPageWidth);

  /** Invalidates all drill pages intersecting with shape so they must be recalculated at the
   * next call of getDrills(). */
  virtual void invalidate(TileShape* shape);

  /** Collects all drill pages with a 2-dimensional overlap with shape. */
  virtual jrt::Collection<DrillPage*>* overlappingPages(TileShape* shape);

  /** Resets all drill pages for autorouting the next connection. */
  virtual void reset();

  /** Emits optional diagnostics for all drills. */
  virtual void emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity);
};

}  // namespace fr
