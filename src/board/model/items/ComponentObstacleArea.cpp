// Port of app.freerouting.board.model.items.ComponentObstacleArea.
#include "board/model/items/ComponentObstacleArea.h"

#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "util/TextManager.h"

namespace fr {

ComponentObstacleArea::ComponentObstacleArea(Area* area, int32_t layer, Vector* translation,
                                             double rotationInDegree, bool sideChanged,
                                             int32_t clearanceClassIndex, int32_t id,
                                             int32_t componentId,
                                             const std::optional<std::string>& name,
                                             FixedState fixedState, BasicBoard* board)
    : ObstacleArea(area, layer, translation, rotationInDegree, sideChanged,
                   new jrt::Array<int32_t>(0), clearanceClassIndex, id, componentId, name,
                   fixedState, board) {}

Item* ComponentObstacleArea::copy(int32_t id) {
  // PORT NOTE (deep copy): like Java, the copy drops netNumbers (always empty unless assignNetNo
  // was called on this keepout).
  return new ComponentObstacleArea(getRelativeArea(), getLayer(), getTranslation(),
                                   getRotationInDegree(), getSideChanged(), clearanceClassIndex(),
                                   id, getComponentId(), this->name, getFixedState(), board);
}

bool ComponentObstacleArea::isObstacle(Item* other) {
  return other != this && jrt::fast_cast<ComponentObstacleArea>(other) != nullptr &&
         other->getComponentId() != this->getComponentId();
}

bool ComponentObstacleArea::isSelectedByFilter(ItemSelectionFilter* filter) {
  if (!this->isSelectedByFixedFilter(filter)) {
    return false;
  }
  return filter->isSelected(ItemSelectionFilter_SelectableChoices::COMPONENT_KEEPOUT);
}

bool ComponentObstacleArea::isFront() {
  Component* component = board->components->get(this->getComponentId());
  return component == nullptr || component->placedOnFront();
}

void ComponentObstacleArea::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager(jrt::className(this), locale);

  printer->appendBold(tm->getText("component_keepout"));
  this->printShapeInfo(printer, locale);
  this->printClearanceInfo(printer, locale);
  this->printClearanceViolationInfo(printer, locale);
  printer->newline();
}

}  // namespace fr
