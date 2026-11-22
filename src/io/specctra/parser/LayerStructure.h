#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Describes a layer structure read from a dsn file. */
class LayerStructure : public virtual jrt::Object {
 public:
  jrt::Array<Layer*>* layers;

  /** Creates a new instance of LayerStructure from a list of layers. */
  explicit LayerStructure(jrt::Collection<Layer*>* layerList);

  /** Creates a dsn-LayerStructure from a board LayerStructure. */
  explicit LayerStructure(fr::LayerStructure* boardLayerStructure);

  /** Returns the number of the named layer, or -1 when it does not exist. */
  virtual int32_t getNo(const std::string& name);

  virtual int32_t signalLayerCount();

  /** Returns, if the net with name netName contains a power plane. */
  virtual bool containsPlane(const std::string& netName);
};

}  // namespace fr::dsn
