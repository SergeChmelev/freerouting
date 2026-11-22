#include "io/specctra/parser/LayerStructure.h"

#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "io/specctra/parser/Layer.h"

namespace fr::dsn {

LayerStructure::LayerStructure(jrt::Collection<Layer*>* layerList) {
  layers = new (jrt::kOp) jrt::Array<Layer*>(layerList->size());
  jrt::Iterator<Layer*>* it = layerList->iterator();
  for (int32_t i = 0; i < layers->length; i++) {
    (*layers)[i] = it->next();
  }
}

LayerStructure::LayerStructure(fr::LayerStructure* boardLayerStructure) {
  layers = new (jrt::kOp) jrt::Array<Layer*>(boardLayerStructure->layers->length);
  for (int32_t i = 0; i < layers->length; i++) {
    fr::Layer* boardLayer = (*boardLayerStructure->layers)[i];
    (*layers)[i] = new (jrt::kOp) Layer(boardLayer->name, i, boardLayer->isSignal);
  }
}

int32_t LayerStructure::getNo(const std::string& name) {
  for (int32_t i = 0; i < layers->length; i++) {
    if (name == (*layers)[i]->name) {
      return i;
    }
  }
  // check for special layers of the Electra autorouter used for the outline
  if (jrt::contains(name, "Top")) {
    return 0;
  }
  if (jrt::contains(name, "Bottom")) {
    return layers->length - 1;
  }
  return -1;
}

int32_t LayerStructure::signalLayerCount() {
  int32_t result = 0;
  for (Layer* currentLayer : *layers) {
    if (currentLayer->isSignal) {
      ++result;
    }
  }
  return result;
}

bool LayerStructure::containsPlane(const std::string& netName) {
  for (Layer* currentLayer : *layers) {
    if (!currentLayer->isSignal) {
      if (currentLayer->netNames->contains(netName)) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace fr::dsn
