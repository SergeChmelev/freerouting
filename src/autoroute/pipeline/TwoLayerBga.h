#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Erik Gavriluk (freerouting.net) (router.fanout.two_layer_bga): how the main attempt starts on a
 * board with exactly two signal layers and a ball grid array. On two layers every escape via
 * closes both layers, and the eager fanout can wall in the inner balls.
 */
class TwoLayerBga : public virtual jrt::Object {
 public:
  /** Java: Set.of("off", "no_fanout", "direct_first"). */
  static jrt::Set<std::string>* MODES();

 private:
  TwoLayerBga() = default;

 public:
  /** The setting's value, checked. */
  static std::string mode(RouterSettings* settings);
  /**
   * Whether a part's pins are a ball grid: an area array (BallFields.fieldOf) whose pads are
   * round, all but a quarter of them.
   */
  static bool isBallGrid(jrt::Collection<Pin*>* pins);
  /** The names of the board's ball grid parts. */
  static jrt::List<std::string>* ballGridParts(BasicBoard* board);
  /**
   * Sets the main attempt's fanout for board as the setting says, in settings: the fanout off
   * (no_fanout) or direct-first (direct_first) where the board has exactly two signal layers, at
   * least one ball grid part and the default fanout. Returns the line to log, or empty (Java null).
   */
  static std::optional<std::string> apply(RouterSettings* settings, BasicBoard* board);
};

}  // namespace fr
