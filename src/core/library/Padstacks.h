// Port of app.freerouting.core.library.Padstacks
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes a library of padstacks for pins or vias. */
class Padstacks : public virtual jrt::Object {
 public:
  // Stage E: owns its padstacks (runs only when the object is disposed).
  ~Padstacks() override;
  /** Creates a new instance of Padstacks. */
  explicit Padstacks(LayerStructure* layerStructure);

  /**
   * Returns the padstack with the input name or null, if no such padstack exists. (Java's
   * defensive null check on name is dropped.)
   */
  Padstack* get(const std::string& name);

  /** Returns the padstack with the specified index. Padstack IDs start at 1. */
  Padstack* get(int32_t padstackId);

  /** Returns the count of padstacks in this object. */
  int32_t count();

  /** Returns the maximum drill radius among all padstacks in this library. */
  double getMaxDrillRadius();

  /** Appends a new padstack with the input shapes to this padstacks. */
  Padstack* add(const std::string& name, jrt::Array<ConvexShape*>* shapes,
                jrt::Array<jrt::Array<ConvexShape*>*>* auxiliaryShapes, bool drillAllowed,
                bool placedAbsolute);

  /** Appends a new padstack with the input shapes to this padstacks. */
  Padstack* add(const std::string& name, jrt::Array<ConvexShape*>* shapes, bool drillAllowed,
                bool placedAbsolute);

  /** Appends a new padstack with the input shapes. The name is generated internally. */
  Padstack* add(jrt::Array<ConvexShape*>* shapes);

  /** Appends a new padstack with shapes and auxiliary shapes. The name is generated internally. */
  Padstack* add(jrt::Array<ConvexShape*>* shapes,
                jrt::Array<jrt::Array<ConvexShape*>*>* auxiliaryShapes);

  /**
   * Appends a new padstack with the input shape from one layer to another and null on the other
   * layers. The padstack name is generated internally.
   */
  Padstack* add(ConvexShape* shape, int32_t fromLayer, int32_t toLayer);

  /** The layer structure of each padstack. */
  LayerStructure* boardLayerStructure;  // final

 private:
  /** The array of padstacks in this object (Java: Vector). */
  jrt::ArrayList<Padstack*>* padstacks;
};

}  // namespace fr
