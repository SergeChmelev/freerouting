// Port of app.freerouting.datastructures.Signum
#include "datastructures/Signum.h"

namespace fr {

Signum* Signum::POSITIVE() {
  static Signum* v = jrt::onHeap([&] { return new (jrt::kBoard) Signum("positive"); });
  return v;
}

Signum* Signum::NEGATIVE() {
  static Signum* v = jrt::onHeap([&] { return new (jrt::kBoard) Signum("negative"); });
  return v;
}

Signum* Signum::ZERO() {
  static Signum* v = jrt::onHeap([&] { return new (jrt::kBoard) Signum("zero"); });
  return v;
}

Signum* Signum::of(double value) {
  Signum* result;

  if (value > 0) {
    result = POSITIVE();
  } else if (value < 0) {
    result = NEGATIVE();
  } else {
    result = ZERO();
  }
  return result;
}

Signum* Signum::negate() {
  Signum* result;
  if (this == POSITIVE()) {
    result = NEGATIVE();
  } else if (this == NEGATIVE()) {
    result = POSITIVE();
  } else {
    result = this;
  }
  return result;
}

}  // namespace fr
