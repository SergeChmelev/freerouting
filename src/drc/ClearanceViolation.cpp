// Port of app.freerouting.drc.ClearanceViolation
#include "drc/ClearanceViolation.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/ComponentOutline.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "util/TextManager.h"

namespace fr {

bool ClearanceViolation::isUnfixable() {
  return (this->firstItem == nullptr || !this->firstItem->isRoutable()) &&
         (this->secondItem == nullptr || !this->secondItem->isRoutable());
}

ClearanceViolation_Category ClearanceViolation::getCategory() {
  if (!isUnfixable()) {
    return ClearanceViolation_Category::POTENTIALLY_FIXABLE;
  }
  bool firstIsPin = jrt::fast_cast<Pin>(this->firstItem) != nullptr;
  bool secondIsPin = jrt::fast_cast<Pin>(this->secondItem) != nullptr;
  if (firstIsPin && secondIsPin) {
    return ClearanceViolation_Category::PIN_TO_PIN;
  }
  bool firstIsOutlineOrKeepout = isOutlineOrKeepout(this->firstItem);
  bool secondIsOutlineOrKeepout = isOutlineOrKeepout(this->secondItem);
  if ((firstIsPin && secondIsOutlineOrKeepout) || (secondIsPin && firstIsOutlineOrKeepout)) {
    return ClearanceViolation_Category::PIN_TO_OUTLINE_OR_KEEPOUT;
  }
  bool firstIsRoute = jrt::fast_cast<Trace>(this->firstItem) != nullptr ||
                      jrt::fast_cast<Via>(this->firstItem) != nullptr;
  bool secondIsRoute = jrt::fast_cast<Trace>(this->secondItem) != nullptr ||
                       jrt::fast_cast<Via>(this->secondItem) != nullptr;
  if (firstIsRoute || secondIsRoute) {
    return ClearanceViolation_Category::FIXED_ROUTE;
  }
  return ClearanceViolation_Category::OTHER_UNFIXABLE;
}

bool ClearanceViolation::isOutlineOrKeepout(Item* item) {
  return jrt::fast_cast<BoardOutline>(item) != nullptr ||
         jrt::fast_cast<ComponentOutline>(item) != nullptr ||
         jrt::fast_cast<ObstacleArea>(item) != nullptr;
}

jrt::List<ClearanceViolation*>* ClearanceViolation::aggregateSortedBySeverity(
    jrt::Collection<Item*>* items) {
  auto* violations = new (jrt::kOp) jrt::ArrayList<ClearanceViolation*>();
  for (Item* item : *items) {
    violations->addAll(item->clearanceViolations());
  }
  violations->sort([](ClearanceViolation* o1, ClearanceViolation* o2) {
    return -jrt::Double::compare(o1->expectedClearance - o1->actualClearance,
                                 o2->expectedClearance - o2->actualClearance);
  });
  return violations;
}

double ClearanceViolation::smallestClearance(jrt::Collection<Item*>* items) {
  double smallest = jrt::Double::MAX_VALUE;
  for (Item* item : *items) {
    if ((item->smallestClearance >= 0) && (item->smallestClearance < smallest)) {
      smallest = item->smallestClearance;
    }
  }
  return smallest;
}

void ClearanceViolation::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager("ClearanceViolation", locale);

  printer->appendBold(tm->getText("clearance_violation_2"));
  printer->append(" " + tm->getText("at") + " ");
  printer->append(shape->centreOfGravity());
  printer->append(", " + tm->getText("width") + " ");
  printer->append(2 * this->shape->smallestRadius());
  printer->append(", " + tm->getText("layer") + " ");
  printer->append((*firstItem->board->layerStructure->layers)[this->layer]->name);
  printer->append(", " + tm->getText("between"));
  printer->newline();
  printer->indent();
  firstItem->printInfo(printer, locale);
  printer->indent();
  secondItem->printInfo(printer, locale);
  printer->newline();
  printer->indent();
  std::string clearanceViolationInfoExpectedClearance =
      tm->getText("clearanceViolationInfoExpectedClearance",
                  {jrt::format("%.4f", this->expectedClearance / 10000.0),
                   jrt::format("%.4f", this->actualClearance / 10000.0)});
  printer->append(clearanceViolationInfoExpectedClearance);
}

}  // namespace fr
