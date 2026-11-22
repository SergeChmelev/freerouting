// Port of app.freerouting.datastructures.Signum
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Implements the mathematical signum function. */
class Signum final : public virtual jrt::Object {
 public:
  static Signum* POSITIVE();
  static Signum* NEGATIVE();
  static Signum* ZERO();

  /** Returns the signum of value. Values are Signum::POSITIVE, NEGATIVE and ZERO. */
  static Signum* of(double value);

  /** Returns the signum of value as an int. Values are +1, 0 and -1. */
  static int32_t asInt(double value) {
    int32_t result;
    if (value > 0) {
      result = 1;
    } else if (value < 0) {
      result = -1;
    } else {
      result = 0;
    }
    return result;
  }

  /** Returns the string of this instance. */
  std::string toString() override { return name; }

  /** Returns the opposite Signum of this Signum. */
  Signum* negate();

 private:
  explicit Signum(const std::string& name) : name(name) {}

  const std::string name;
};

}  // namespace fr
