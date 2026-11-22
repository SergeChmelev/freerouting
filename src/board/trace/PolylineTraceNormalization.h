// Port of app.freerouting.board.trace.PolylineTraceNormalization (package-private final class).
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Normalizes a PolylineTrace after board edits. The collaborator owns only the recursive
 * split/combine normalization walk; recursion is capped at MAX_NORMALIZATION_DEPTH (returns false).
 */
class PolylineTraceNormalization final : public virtual jrt::Object {
 private:
  static constexpr int32_t MAX_NORMALIZATION_DEPTH = 16;

  PolylineTraceNormalization() {}

 public:
  static bool normalize(PolylineTrace* trace, IntOctagon* clipShape) {
    return normalize(trace, clipShape, 0);
  }

 private:
  static bool normalize(PolylineTrace* trace, IntOctagon* clipShape, int32_t normalizationDepth);
};

}  // namespace fr
