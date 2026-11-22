#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Strategy for updating board state during routing passes. */
enum class BoardUpdateStrategy : int32_t { GLOBAL_OPTIMAL };

inline std::string BoardUpdateStrategy_name(BoardUpdateStrategy e) {
  switch (e) {
    case BoardUpdateStrategy::GLOBAL_OPTIMAL: return "GLOBAL_OPTIMAL";
  }
  return "";
}
inline std::string BoardUpdateStrategy_toString(BoardUpdateStrategy e) { return BoardUpdateStrategy_name(e); }
inline jrt::Array<BoardUpdateStrategy>* BoardUpdateStrategy_values() {
  return jrt::Array<BoardUpdateStrategy>::of({BoardUpdateStrategy::GLOBAL_OPTIMAL});
}
inline BoardUpdateStrategy BoardUpdateStrategy_valueOf(const std::string& s) {
  if (s == "GLOBAL_OPTIMAL") return BoardUpdateStrategy::GLOBAL_OPTIMAL;
  throw jrt::IllegalArgumentException("No enum constant BoardUpdateStrategy." + s);
}

}  // namespace fr
