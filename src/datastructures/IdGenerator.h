// Port of app.freerouting.datastructures.IdGenerator
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Interface for creating unique identification numbers. */
class IdGenerator : public virtual jrt::Object {
 public:
  /** Creates a new unique identification number. */
  virtual int32_t newId() = 0;

  /** Returns the maximum generated ID number so far. */
  virtual int32_t maxGeneratedId() = 0;
};

}  // namespace fr
