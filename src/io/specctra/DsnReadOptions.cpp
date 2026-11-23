#include "io/specctra/DsnReadOptions.h"

namespace fr::dsn {

DsnReadOptions* DsnReadOptions::INPUT_ORDER() {
  static DsnReadOptions* v = jrt::onHeap([&] { return new (jrt::kBoard) DsnReadOptions(false); });
  return v;
}

DsnReadOptions* DsnReadOptions::REFERENCE_NAME() {
  static DsnReadOptions* v = jrt::onHeap([&] { return new (jrt::kBoard) DsnReadOptions(true); });
  return v;
}

DsnReadOptions* DsnReadOptions::DEFAULT() { return REFERENCE_NAME(); }

}  // namespace fr::dsn
