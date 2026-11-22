#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Expansion of maze list elements to drills and other layers (Java: package-private). */
class MazeExpansionEngine final : public virtual jrt::Object {
 private:
  MazeSearchEngine* search;

 public:
  explicit MazeExpansionEngine(MazeSearchEngine* search) : search(search) {}

  void expandToDrill(ExpansionDrill* drill, MazeListElement* fromElement, int32_t addCosts);

  void expandToDrillPage(DrillPage* drillPage, MazeListElement* fromElement);

  void expandToDrillsOfPage(MazeListElement* fromElement);

  void expandToOtherLayers(MazeListElement* listElement);

 private:
  ForcedPadRouter_CheckDrillResult checkLayerWithAnyMatchingVia(ExpansionDrill* drill,
                                                               int32_t layer,
                                                               TileShape* roomShape,
                                                               jrt::Array<int32_t>* netNumbers);
};

}  // namespace fr
