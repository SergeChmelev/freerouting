// Port of app.freerouting.board.model.structure.Layer
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes the structure of a board layer. */
class Layer : public virtual jrt::Object {
 public:
  /** Creates a new instance of Layer. */
  Layer(const std::string& name, bool isSignal) : name(name), isSignal(isSignal) {}

  std::string toString() override { return name; }

  /** The name of the layer. */
  const std::string name;

  /** True, if this is a signal layer, which can be used for routing. Otherwise, it may be for
   * example a power ground layer. */
  const bool isSignal;
};

}  // namespace fr
