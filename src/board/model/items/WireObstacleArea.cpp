// Port of app.freerouting.board.model.items.WireObstacleArea.
#include "board/model/items/WireObstacleArea.h"

#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/model/items/Trace.h"
#include "util/TextManager.h"

namespace fr {

WireObstacleArea::WireObstacleArea(Area* area, int32_t layer, Vector* translation,
                                   double rotationInDegree, bool sideChanged,
                                   jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                                   int32_t id, int32_t groupId,
                                   const std::optional<std::string>& name, FixedState fixedState,
                                   BasicBoard* board)
    : ObstacleArea(area, layer, translation, rotationInDegree, sideChanged, netNumbers,
                   clearanceClassIndex, id, groupId, name, fixedState, board) {}

WireObstacleArea::WireObstacleArea(Area* area, int32_t layer, Vector* translation,
                                   double rotationInDegree, bool sideChanged,
                                   int32_t clearanceClassIndex, int32_t id, int32_t groupId,
                                   const std::optional<std::string>& name, FixedState fixedState,
                                   BasicBoard* board)
    : WireObstacleArea(area, layer, translation, rotationInDegree, sideChanged,
                       new (jrt::kOp) jrt::Array<int32_t>(0), clearanceClassIndex, id, groupId, name,
                       fixedState, board) {}

Item* WireObstacleArea::copy(int32_t id) {
  auto* copiedNetNos = new (jrt::kOp) jrt::Array<int32_t>(netNumbers->length);
  jrt::arraycopy(netNumbers, 0, copiedNetNos, 0, netNumbers->length);
  return new (jrt::kBoardData) WireObstacleArea(getRelativeArea(), getLayer(), getTranslation(),
                              getRotationInDegree(), getSideChanged(), copiedNetNos,
                              clearanceClassIndex(), id, getComponentId(), this->name,
                              getFixedState(), board);
}

bool WireObstacleArea::isObstacle(Item* other) {
  if (other == nullptr || other == this || other->sharesNet(this)) {
    return false;
  }
  return jrt::fast_cast<Trace>(other) != nullptr;
}

bool WireObstacleArea::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::KEEPOUT);
}

void WireObstacleArea::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  printer->appendBold(tm->getText("wire_keepout"));
  this->printShapeInfo(printer, locale);
  this->printClearanceInfo(printer, locale);
  this->printClearanceViolationInfo(printer, locale);
  printer->newline();
}

}  // namespace fr
