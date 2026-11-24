// Port of app.freerouting.board.model.structure.LayerStructure
#pragma once

#include "board/model/structure/Layer.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes the layer structure of the board. */
class LayerStructure : public virtual jrt::Object {
 public:
  /** Creates a new instance of LayerStructure. */
  explicit LayerStructure(jrt::Array<Layer*>* layers) : layers(layers) {}
  // Stage E: owns its layers (runs only when the object is disposed).
  ~LayerStructure() override {
    for (Layer* l : *layers) jrt::dispose(l);
    jrt::dispose(layers);
  }

  /** Returns the index of the layer with the name name in the layer array, or -1. */
  int32_t getNo(const std::string& name) {
    for (int32_t i = 0; i < layers->length; i++) {
      if (name == (*layers)[i]->name) {
        return i;
      }
    }
    return -1;
  }

  /** Returns the index of layer in the layer array, or -1, if layer does not belong to it. */
  int32_t getNo(Layer* layer) {
    for (int32_t i = 0; i < layers->length; i++) {
      if (layer == (*layers)[i]) {
        return i;
      }
    }
    return -1;
  }

  /** Returns the count of signal layers of this layer structure. */
  int32_t signalLayerCount() {
    int32_t foundSignalLayers = 0;
    for (int32_t i = 0; i < layers->length; i++) {
      if ((*layers)[i]->isSignal) {
        ++foundSignalLayers;
      }
    }
    return foundSignalLayers;
  }

  /** Gets the no-th signal layer of this layer structure (the last layer if not found). */
  Layer* getSignalLayer(int32_t no) {
    int32_t foundSignalLayers = 0;
    for (int32_t i = 0; i < layers->length; i++) {
      if ((*layers)[i]->isSignal) {
        if (no == foundSignalLayers) {
          return (*layers)[i];
        }
        ++foundSignalLayers;
      }
    }
    return (*layers)[layers->length - 1];
  }

  /** Gets the count of signal layers with a smaller number than layer, or -1 if not found. */
  int32_t getSignalLayerNo(Layer* layer) {
    int32_t foundSignalLayers = 0;
    for (int32_t i = 0; i < layers->length; i++) {
      if ((*layers)[i] == layer) {
        return foundSignalLayers;
      }
      if ((*layers)[i]->isSignal) {
        ++foundSignalLayers;
      }
    }
    return -1;
  }

  /** Gets the layer number of the signalLayerNo-th signal layer in this layer structure. */
  int32_t getLayerNo(int32_t signalLayerNo) {
    Layer* currentSignalLayer = getSignalLayer(signalLayerNo);
    return getNo(currentSignalLayer);
  }

  /** The layers of the board (final). */
  jrt::Array<Layer*>* layers;
};

}  // namespace fr
