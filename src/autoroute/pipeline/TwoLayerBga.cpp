// Port of app.freerouting.autoroute.pipeline.TwoLayerBga
#include "autoroute/pipeline/TwoLayerBga.h"

#include "autoroute/pipeline/BallFields.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/LayerStructure.h"
#include "core/library/Padstack.h"
#include "geometry/planar/Circle.h"
#include "settings/FanoutSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

jrt::Set<std::string>* TwoLayerBga::MODES() {
  static jrt::Set<std::string>* v = jrt::onHeap([] {
    // PORT NOTE: Java Set.of (immutable, unordered); only contains() is used. On the heap: it
    // outlives the job that made it.
    auto* s = new (jrt::kBoard) jrt::HashSet<std::string>();
    s->add("off");
    s->add("no_fanout");
    s->add("direct_first");
    return s;
  });
  return v;
}

std::string TwoLayerBga::mode(RouterSettings* settings) {
  std::string mode =
      (settings->fanout != nullptr && settings->fanout->twoLayerBga.has_value())
          ? settings->fanout->twoLayerBga.value()
          : std::string("off");
  if (!MODES()->contains(mode)) {
    throw jrt::IllegalArgumentException(
        "router.fanout.two_layer_bga must be off, no_fanout or direct_first");
  }
  return mode;
}

bool TwoLayerBga::isBallGrid(jrt::Collection<Pin*>* pins) {
  if (pins == nullptr) {
    return false;
  }
  BallFields_Field* field = BallFields::fieldOf(pins);
  if (field == nullptr) {
    return false;
  }
  int32_t round = 0;
  for (Pin* p : *pins) {
    if (p->getPadstack() != nullptr &&
        jrt::fast_cast<Circle>(p->getPadstack()->getShape(field->layer())) != nullptr) {
      round++;
    }
  }
  return round >= BallFields::MIN_BALLS && round * 4 >= pins->size() * 3;
}

jrt::List<std::string>* TwoLayerBga::ballGridParts(BasicBoard* board) {
  jrt::List<std::string>* parts = new (jrt::kOp) jrt::ArrayList<std::string>();
  for (int32_t id = 1; id <= board->components->count(); id++) {
    if (isBallGrid(board->getComponentPins(id))) {
      std::string name =
          board->components->get(id) != nullptr ? board->components->get(id)->name : jrt::str(id);
      parts->add(name);
    }
  }
  return parts;
}

std::optional<std::string> TwoLayerBga::apply(RouterSettings* settings, BasicBoard* board) {
  std::string mode = TwoLayerBga::mode(settings);
  // Java: settings.fanout.enabled != Boolean.TRUE (identity; boxed booleans are the cached
  // Boolean.TRUE / FALSE instances) -> "not present-and-true".
  if ("off" == mode
      || settings->fanout == nullptr
      || !(settings->fanout->enabled.has_value() && settings->fanout->enabled.value() == true)
      || !(std::string("eager") ==
           (settings->fanout->strategy.has_value() ? settings->fanout->strategy.value()
                                                   : std::string("eager")))
      || board->layerStructure->signalLayerCount() != 2) {
    return std::nullopt;
  }
  jrt::List<std::string>* parts = ballGridParts(board);
  if (parts->isEmpty()) {
    return std::nullopt;
  }
  if ("no_fanout" == mode) {
    settings->fanout->enabled = false;
  } else {
    settings->fanout->strategy = std::string("direct_first");
  }
  std::string sb;
  sb += "Two signal layers and a ball grid array (";
  for (int32_t i = 0; i < jrt::Math::min(5, parts->size()); i++) {
    if (i > 0) {
      sb += ", ";
    }
    sb += parts->get(i);
  }
  if (parts->size() > 5) {
    sb += ", ...";
  }
  sb += "): the main attempt starts ";
  sb += "no_fanout" == mode ? "without the fanout" : "with the direct-first fanout";
  sb += " (router.fanout.two_layer_bga=";
  sb += mode;
  sb += ").";
  return sb;
}

}  // namespace fr
