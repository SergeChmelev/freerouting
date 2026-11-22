#include "io/specctra/DsnReadOptions.h"

namespace fr::dsn {

DsnReadOptions* DsnReadOptions::INPUT_ORDER() {
  static DsnReadOptions* v = new DsnReadOptions(false);
  return v;
}

DsnReadOptions* DsnReadOptions::REFERENCE_NAME() {
  static DsnReadOptions* v = new DsnReadOptions(true);
  return v;
}

DsnReadOptions* DsnReadOptions::DEFAULT() { return REFERENCE_NAME(); }

}  // namespace fr::dsn
