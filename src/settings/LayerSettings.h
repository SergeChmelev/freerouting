#pragma once

#include <optional>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Settings configuration for a single board layer. */
// PORT NOTE: Serializable/Cloneable, gson and swagger annotations dropped.
class LayerSettings : public virtual jrt::Object {
 public:
  /** "routable": whether this layer is routable by the autorouter. */
  std::optional<bool> routable;
  /** "preferred_direction_horizontal": preferred direction horizontal (false = vertical). */
  std::optional<bool> preferredDirectionHorizontal;
  /**
   * "bend_cost": per-layer bend cost added to the maze expansion value at each direction change.
   * Null means "use the board default". Valid range: 0.0 to 9.9.
   */
  std::optional<double> bendCost;
  /** "preferred_direction_trace_cost": cost per mm in the preferred direction; null = computed. */
  std::optional<double> preferredDirectionTraceCost;
  /** "undesired_direction_trace_cost": cost per mm against the preferred direction. */
  std::optional<double> undesiredDirectionTraceCost;

  /** Default constructor required for reflection and serialization. */
  LayerSettings() = default;

  /** Convenience constructor. */
  LayerSettings(std::optional<bool> routable, std::optional<bool> preferredDirectionHorizontal)
      : routable(routable), preferredDirectionHorizontal(preferredDirectionHorizontal) {}

  /** Full constructor. */
  LayerSettings(std::optional<bool> routable, std::optional<bool> preferredDirectionHorizontal,
                std::optional<double> bendCost)
      : routable(routable),
        preferredDirectionHorizontal(preferredDirectionHorizontal),
        bendCost(bendCost) {}

  /** Java clone(): shallow field copy. */
  virtual LayerSettings* clone();

  bool equals(jrt::Object* other) override;
  int32_t hashCode() override;

  /** PORT NOTE: explicit replacement of ReflectionUtil.copyFields(source, this). */
  int32_t copyNewValuesFrom(LayerSettings* source);
};

}  // namespace fr
