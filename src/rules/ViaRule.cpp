// Port of app.freerouting.rules.ViaRule
#include "rules/ViaRule.h"

#include "core/library/Padstack.h"
#include "rules/ViaInfo.h"
#include "util/TextManager.h"

namespace fr {

ViaRule* ViaRule::EMPTY() {
  static ViaRule* v = new (jrt::kBoard) ViaRule("empty");
  return v;
}

ViaRule::ViaRule(const std::string& name) : name(name), list(new (jrt::kBoard) jrt::LinkedList<ViaInfo*>()) {}

void ViaRule::appendVia(ViaInfo* via) { list->add(via); }

bool ViaRule::removeVia(ViaInfo* via) { return list->remove(via); }

int32_t ViaRule::viaCount() { return list->size(); }

ViaInfo* ViaRule::getVia(int32_t index) {
  // Java: assert index >= 0 && index < list.size();
  return list->get(index);
}

bool ViaRule::contains(ViaInfo* viaInfo) {
  for (ViaInfo* currentInfo : *this->list) {
    if (viaInfo == currentInfo) {
      return true;
    }
  }
  return false;
}

bool ViaRule::containsPadstack(Padstack* padstack) {
  for (ViaInfo* currentInfo : *this->list) {
    if (currentInfo->getPadstack() == padstack) {
      return true;
    }
  }
  return false;
}

ViaInfo* ViaRule::getLayerRange(int32_t fromLayer, int32_t toLayer) {
  for (ViaInfo* currentInfo : *this->list) {
    if (currentInfo->getPadstack()->fromLayer() == fromLayer &&
        currentInfo->getPadstack()->toLayer() == toLayer) {
      return currentInfo;
    }
  }
  return nullptr;
}

bool ViaRule::swap(ViaInfo* first, ViaInfo* second) {
  int32_t index1 = this->list->indexOf(first);
  int32_t index2 = this->list->indexOf(second);
  if (index1 < 0 || index2 < 0) {
    return false;
  }
  if (index1 == index2) {
    return true;
  }
  this->list->set(index1, second);
  this->list->set(index2, first);
  return true;
}

void ViaRule::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager("ViaRule", locale);

  printer->appendBold(tm->getText("via_rule_2") + " ");
  printer->appendBold(this->name);
  printer->appendBold(":");
  int32_t counter = 0;
  bool firstTime = true;
  const int32_t maxViasPerRow = 5;
  for (ViaInfo* currentVia : *this->list) {
    if (firstTime) {
      firstTime = false;
    } else {
      printer->append(", ");
    }
    if (counter == 0) {
      printer->newline();
      printer->indent();
    }
    printer->append(currentVia->getName(), tm->getText("viaInfo"), currentVia);
    counter = (counter + 1) % maxViasPerRow;
  }
}

}  // namespace fr
