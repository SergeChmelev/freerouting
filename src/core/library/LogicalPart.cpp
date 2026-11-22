// Port of app.freerouting.core.library.LogicalPart
#include "core/library/LogicalPart.h"

#include "logger/FRLogger.h"
#include "util/TextManager.h"

namespace fr {

LogicalPart_PartPin* LogicalPart::getPin(int32_t pinIndex) {
  if (pinIndex < 0 || pinIndex >= partPinArr->length) {
    FRLogger::warn("LogicalPart.getPin: pinIndex out of range");
    return nullptr;
  }
  return (*partPinArr)[pinIndex];
}

void LogicalPart::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager("app.freerouting.core.library.LogicalPart", locale);

  printer->appendBold(tm->getText("logical_part_2") + " ");
  printer->appendBold(this->name);
  for (int32_t i = 0; i < this->partPinArr->length; i++) {
    LogicalPart_PartPin* currentPin = (*this->partPinArr)[i];
    printer->newline();
    printer->indent();
    printer->append(tm->getText("pin") + " ");
    printer->append(currentPin->pinName);
    printer->append(", " + tm->getText("gate") + " ");
    printer->append(currentPin->gateName);
    printer->append(", " + tm->getText("swap_code") + " ");
    int32_t gateSwapCode = currentPin->gateSwapCode;
    printer->append(jrt::str(gateSwapCode));
    printer->append(", " + tm->getText("gate_pin") + " ");
    printer->append(currentPin->gatePinName);
    printer->append(", " + tm->getText("swap_code") + " ");
    int32_t pinSwapCode = currentPin->gatePinSwapCode;
    printer->append(jrt::str(pinSwapCode));
  }
  printer->newline();
  printer->newline();
}

}  // namespace fr
