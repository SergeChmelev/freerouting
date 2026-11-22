#pragma once

#include <istream>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Finds the copper areas of the (wiring ...) scope before the board is built.
 *
 * The layer types are decided in the (structure ...) scope, which comes before the wiring. Some
 * exporters (KiCad since 2026-07) write the copper of a zone as (wire (polygon <layer> ...) (net
 * <name>)) in the wiring, even on a (type power) layer, instead of a (plane ...) scope. Without
 * looking ahead, such a layer looks empty when the structure is read and is turned into a routable
 * signal layer.
 */
class PolygonWireScan final : public virtual jrt::Object {
 public:
  /**
   * Returns, for every layer that receives at least one (wire (polygon ...)), the names of the nets
   * of those wires, in file order (LinkedHashMap / LinkedHashSet). Wires without a net are counted
   * with no net name. Returns an empty map if the file cannot be scanned.
   */
  static jrt::Map<std::string, jrt::Set<std::string>*>* netsByLayer(std::istream& dsn);

 private:
  PolygonWireScan() = default;
};

}  // namespace fr::dsn
