// Port of app.freerouting.board.model.items.ComponentObstacleArea.
#pragma once

#include "board/model/items/ObstacleArea.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes areas of the board, where components are not allowed. */
class ComponentObstacleArea : public ObstacleArea {
 public:
  /**
   * Creates a new instance of ComponentObstacleArea. If isObstacle is false, the new instance is
   * not regarded as obstacle and used only for displaying on the screen.
   */
  ComponentObstacleArea(Area* area, int32_t layer, Vector* translation, double rotationInDegree,
                        bool sideChanged, int32_t clearanceClassIndex, int32_t id,
                        int32_t componentId, const std::optional<std::string>& name,
                        FixedState fixedState, BasicBoard* board);

  using Item::isObstacle;
  using Item::isTraceObstacle;

  Item* copy(int32_t id) override;

  bool isObstacle(Item* other) override;

  bool isTraceObstacle(int32_t netNumber) override { return false; }

  bool isSelectedByFilter(ItemSelectionFilter* filter) override;

  /** IsFront. */
  virtual bool isFront();

  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;
};

}  // namespace fr
