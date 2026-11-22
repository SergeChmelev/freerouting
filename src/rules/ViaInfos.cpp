// Port of app.freerouting.rules.ViaInfos
#include "rules/ViaInfos.h"

#include "rules/ViaInfo.h"
#include "util/TextManager.h"

namespace fr {

ViaInfos::ViaInfos() : list(new jrt::LinkedList<ViaInfo*>()) {}

bool ViaInfos::add(ViaInfo* viaInfo) {
  if (nameExists(viaInfo->getName())) {
    return false;
  }
  this->list->add(viaInfo);
  return true;
}

int32_t ViaInfos::count() { return this->list->size(); }

ViaInfo* ViaInfos::get(int32_t index) {
  // Java: assert index >= 0 && index < this.list.size();
  return this->list->get(index);
}

ViaInfo* ViaInfos::get(const std::string& name) {
  for (ViaInfo* currentVia : *this->list) {
    if (currentVia->getName() == name) {
      return currentVia;
    }
  }
  return nullptr;
}

bool ViaInfos::nameExists(const std::string& name) {
  for (ViaInfo* currentVia : *this->list) {
    if (currentVia->getName() == name) {
      return true;
    }
  }
  return false;
}

bool ViaInfos::remove(ViaInfo* viaInfo) { return this->list->remove(viaInfo); }

void ViaInfos::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager("ViaInfos", locale);

  printer->appendBold(tm->getText("vias") + ": ");
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
