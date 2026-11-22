#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Describes a layer in a Specctra dsn file. */
class Layer : public virtual jrt::Object {
 public:
  /** All layers of the board: Layer("pcb", -1, false). */
  static Layer* PCB();

  /** The signal layers: Layer("signal", -1, true). */
  static Layer* SIGNAL();

  std::string name;
  int32_t no;
  bool isSignal;
  jrt::Collection<std::string>* netNames;

  /**
   * Creates a new instance of Layer. no is the physical layer number starting with 0 at the
   * component side and ending at the solder side. If isSignal, the layer is a signal layer,
   * otherwise it is a power layer. For power layers, netNames contains the nets of the plane.
   */
  Layer(const std::string& name, int32_t no, bool isSignal, jrt::Collection<std::string>* netNames)
      : name(name), no(no), isSignal(isSignal), netNames(netNames) {}

  /** Creates a new instance of Layer with an empty net name list. */
  Layer(const std::string& name, int32_t no, bool isSignal)
      : Layer(name, no, isSignal, new jrt::LinkedList<std::string>()) {}

  /** Writes a layer scope in the structure scope. Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter, int32_t layerIndex, bool writeRule);
};

}  // namespace fr::dsn
