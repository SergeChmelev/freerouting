#include "io/specctra/parser/NetList.h"

#include "io/specctra/parser/Net.h"

namespace fr::dsn {

bool NetList::contains(Net_Id* netId) { return nets->containsKey(netId); }

Net* NetList::addNet(Net_Id* netId) {
  Net* result;
  if (nets->containsKey(netId)) {
    result = nullptr;
  } else {
    result = new Net(netId);
    nets->put(netId, result);
  }
  return result;
}

Net* NetList::getNet(Net_Id* netId) { return nets->get(netId); }

jrt::Collection<Net*>* NetList::getNets(const std::string& componentName,
                                        const std::string& pinName) {
  jrt::Collection<Net*>* result = new jrt::LinkedList<Net*>();
  auto* searchPin = new Net_Pin(componentName, pinName);
  jrt::Collection<Net*>* netList = nets->values();
  for (Net* currentNet : *netList) {
    jrt::Set<Net_Pin*>* netPins = currentNet->getPins();
    if (netPins != nullptr && netPins->contains(searchPin)) {
      result->add(currentNet);
    }
  }
  return result;
}

}  // namespace fr::dsn
