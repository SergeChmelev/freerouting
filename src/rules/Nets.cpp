// Port of app.freerouting.rules.Nets
#include "rules/Nets.h"

#include "logger/FRLogger.h"
#include "rules/Net.h"
#include "util/TextManager.h"

namespace fr {

Nets::Nets() { nets = new (jrt::kBoard) jrt::ArrayList<Net*>(); }

int32_t Nets::maxNetNumber() { return nets->size(); }

Net* Nets::get(const std::string& name, int32_t subnetNumber) {
  for (Net* currentNet : *nets) {
    if (currentNet != nullptr && currentNet->subnetNumber == subnetNumber &&
        name == currentNet->name) {
      return currentNet;
    }
  }
  return nullptr;
}

jrt::Collection<Net*>* Nets::get(const std::string& name) {
  jrt::Collection<Net*>* result = new (jrt::kOp) jrt::LinkedList<Net*>();
  for (Net* currentNet : *nets) {
    if (currentNet != nullptr && name == currentNet->name) {
      result->add(currentNet);
    }
  }
  return result;
}

Net* Nets::get(int32_t netNumber) {
  if (netNumber < 1 || netNumber > nets->size()) {
    return nullptr;
  }
  Net* result = nets->get(netNumber - 1);
  if (result != nullptr && result->netNumber != netNumber) {
    FRLogger::warn("Nets.get: inconsistent netNumber");
  }
  return result;
}

Net* Nets::newNet(const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager("NetClasses", locale);

  std::string netName = tm->getText("net#") + jrt::str(nets->size() + 1);
  return add(netName, 1, false);
}

Net* Nets::add(const std::string& name, int32_t subnetNumber, bool containsPlane) {
  int32_t newNetNo = nets->size() + 1;
  if (newNetNo >= max_legal_net_number) {
    FRLogger::warn("Nets.add_net: maxNetNo out of range");
  }
  Net* newNet = new (jrt::kBoard) Net(name, subnetNumber, newNetNo, this, containsPlane);
  nets->add(newNet);
  return newNet;
}

}  // namespace fr
