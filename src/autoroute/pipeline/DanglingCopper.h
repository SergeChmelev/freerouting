#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * freerouting.net: removes the dangling copper the stages leave on the board that is written: a
 * via with copper of its net on one layer only and a trace with a free end, with the stub behind
 * each up to the next fork, pin or plane.
 */
class DanglingCopper : public virtual jrt::Object {
 private:
  /** At most this many removal passes: a removed stub can leave the via behind it with one layer of copper. */
  static constexpr int32_t MAX_PASSES = 16;

  DanglingCopper() = default;

 public:
  /** Removes the dangling copper on board. Returns the line to log, or empty (Java null) when there was none. */
  static std::optional<std::string> remove(RoutingBoard* board, const std::string& where);

 private:
  static int32_t open(RoutingBoard* board, jrt::Array<int32_t>* nets);
};

}  // namespace fr
