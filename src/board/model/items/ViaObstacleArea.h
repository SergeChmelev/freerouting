// Port of app.freerouting.board.model.items.ViaObstacleArea.
#pragma once

#include "board/model/items/ObstacleArea.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes Areas on the board, where vias are not allowed. */
class ViaObstacleArea : public ObstacleArea {
 public:
  /** Creates a new area item which may belong to several nets. */
  ViaObstacleArea(Area* area, int32_t layer, Vector* translation, double rotationInDegree,
                  bool sideChanged, jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                  int32_t id, int32_t groupId, const std::optional<std::string>& name,
                  FixedState fixedState, BasicBoard* board);

  /** Creates a new area item without net. */
  ViaObstacleArea(Area* area, int32_t layer, Vector* translation, double rotationInDegree,
                  bool sideChanged, int32_t clearanceClassIndex, int32_t id, int32_t groupId,
                  const std::optional<std::string>& name, FixedState fixedState,
                  BasicBoard* board);

  using Item::isObstacle;
  using Item::isTraceObstacle;

  Item* copy(int32_t id) override;

  bool isObstacle(Item* other) override;

  bool isTraceObstacle(int32_t netNumber) override { return false; }

  bool isSelectedByFilter(ItemSelectionFilter* filter) override;

  void printInfo(ItemInfoPrinter* printer, const std::string& locale) override;
};

}  // namespace fr
