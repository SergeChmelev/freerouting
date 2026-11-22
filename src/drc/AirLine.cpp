// Port of app.freerouting.drc.AirLine
#include "drc/AirLine.h"

#include "board/model/items/Item.h"
#include "rules/Net.h"

namespace fr {

int32_t AirLine::compareTo(AirLine* other) {
  return jrt::compareStrings(this->net->name, other->net->name);
}

std::string AirLine::toString() {
  return this->net->name + ": " + jrt::str(fromItem) + " - " + jrt::str(toItem);
}

}  // namespace fr
