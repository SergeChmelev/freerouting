// Port of app.freerouting.rules.ViaInfo
#include "rules/ViaInfo.h"

#include "core/library/Padstack.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "util/TextManager.h"

namespace fr {

void ViaInfo::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager("ViaInfo", locale);

  printer->appendBold(tm->getText("via") + " ");
  printer->appendBold(this->name);
  printer->appendBold(": ");
  printer->append(tm->getText("padstack") + " ");
  printer->append(this->padstack->name, tm->getText("padstack_info"), this->padstack);
  printer->append(", " + tm->getText("clearanceClass") + " ");
  std::string currentName = boardRules->clearanceMatrix->getName(this->clearanceClassIndex);
  printer->append(currentName, tm->getText("clearance_class_2"),
                  boardRules->clearanceMatrix->getRow(this->clearanceClassIndex));
  printer->append(", " + tm->getText("attach_smd") + " ");
  if (attachSmdAllowed_) {
    printer->append(" " + tm->getText("on"));
  } else {
    printer->append(" " + tm->getText("off"));
  }
  printer->newline();
}

}  // namespace fr
