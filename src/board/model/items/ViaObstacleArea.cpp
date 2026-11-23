// Port of app.freerouting.board.model.items.ViaObstacleArea.
#include "board/model/items/ViaObstacleArea.h"

#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/model/items/Via.h"
#include "util/TextManager.h"

namespace fr {

ViaObstacleArea::ViaObstacleArea(Area* area, int32_t layer, Vector* translation,
                                 double rotationInDegree, bool sideChanged,
                                 jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                                 int32_t id, int32_t groupId,
                                 const std::optional<std::string>& name, FixedState fixedState,
                                 BasicBoard* board)
    : ObstacleArea(area, layer, translation, rotationInDegree, sideChanged, netNumbers,
                   clearanceClassIndex, id, groupId, name, fixedState, board) {}

ViaObstacleArea::ViaObstacleArea(Area* area, int32_t layer, Vector* translation,
                                 double rotationInDegree, bool sideChanged,
                                 int32_t clearanceClassIndex, int32_t id, int32_t groupId,
                                 const std::optional<std::string>& name, FixedState fixedState,
                                 BasicBoard* board)
    : ViaObstacleArea(area, layer, translation, rotationInDegree, sideChanged,
                      new (jrt::kOp) jrt::Array<int32_t>(0), clearanceClassIndex, id, groupId, name,
                      fixedState, board) {}

Item* ViaObstacleArea::copy(int32_t id) {
  auto* copiedNetNos = new (jrt::kOp) jrt::Array<int32_t>(netNumbers->length);
  jrt::arraycopy(netNumbers, 0, copiedNetNos, 0, netNumbers->length);
  return new (jrt::kBoardData) ViaObstacleArea(getRelativeArea(), getLayer(), getTranslation(),
                             getRotationInDegree(), getSideChanged(), copiedNetNos,
                             clearanceClassIndex(), id, getComponentId(), this->name,
                             getFixedState(), board);
}

bool ViaObstacleArea::isObstacle(Item* other) {
  if (other->sharesNet(this)) {
    return false;
  }
  return jrt::fast_cast<Via>(other) != nullptr;
}

bool ViaObstacleArea::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::VIA_KEEPOUT);
}

void ViaObstacleArea::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  printer->appendBold(tm->getText("via_keepout"));
  this->printShapeInfo(printer, locale);
  this->printClearanceInfo(printer, locale);
  this->printClearanceViolationInfo(printer, locale);
  printer->newline();
}

}  // namespace fr
