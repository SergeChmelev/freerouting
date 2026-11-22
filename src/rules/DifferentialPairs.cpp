// Port of app.freerouting.rules.DifferentialPairs
#include "rules/DifferentialPairs.h"

#include "rules/DifferentialPair.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr {

DifferentialPairs::DifferentialPairs()
    : pairsByName(new jrt::LinkedHashMap<std::string, DifferentialPair*>()),
      pairsByNetNumber(new jrt::HashMap<int32_t, DifferentialPair*>()) {}

void DifferentialPairs::add(DifferentialPair* pair) {
  if (pair == nullptr) {
    throw jrt::NullPointerException("pair cannot be null");
  }

  std::string key = pair->baseName();
  DifferentialPair* existingByName = this->pairsByName->get(key);
  if (existingByName != nullptr && !hasSameIdentity(existingByName, pair)) {
    throw jrt::IllegalArgumentException(
        "Differential pair " + pair->baseName() + " is already registered with different nets (" +
        existingByName->positiveNet()->name + ", " + existingByName->negativeNet()->name + ")");
  }

  checkNetAvailability(pair->positiveNet(), pair);
  checkNetAvailability(pair->negativeNet(), pair);

  this->pairsByName->put(key, pair);
  indexSubnets(pair->positiveNet(), pair);
  indexSubnets(pair->negativeNet(), pair);
}

bool DifferentialPairs::hasSameIdentity(DifferentialPair* a, DifferentialPair* b) {
  return a->baseName() == b->baseName() && a->positiveNet()->equals(b->positiveNet()) &&
         a->negativeNet()->equals(b->negativeNet());
}

void DifferentialPairs::checkNetAvailability(Net* net, DifferentialPair* pair) {
  if (net == nullptr) {
    return;
  }
  DifferentialPair* existing = pairsByNetNumber->get(net->netNumber);
  if (existing != nullptr && !hasSameIdentity(existing, pair)) {
    throw jrt::IllegalArgumentException("Net " + net->name + " is already registered in pair " +
                                        existing->baseName());
  }
  if (net->netList != nullptr) {
    for (Net* subnet : *net->netList->get(net->name)) {
      DifferentialPair* existingSub = pairsByNetNumber->get(subnet->netNumber);
      if (existingSub != nullptr && !hasSameIdentity(existingSub, pair)) {
        throw jrt::IllegalArgumentException("Net " + subnet->name +
                                            " is already registered in pair " +
                                            existingSub->baseName());
      }
    }
  }
}

void DifferentialPairs::indexSubnets(Net* net, DifferentialPair* pair) {
  if (net == nullptr) {
    return;
  }
  this->pairsByNetNumber->put(net->netNumber, pair);
  if (net->netList != nullptr) {
    for (Net* subnet : *net->netList->get(net->name)) {
      this->pairsByNetNumber->put(subnet->netNumber, pair);
    }
  }
}

void DifferentialPairs::removeSubnets(Net* net) {
  if (net == nullptr) {
    return;
  }
  this->pairsByNetNumber->remove(net->netNumber);
  if (net->netList != nullptr) {
    for (Net* subnet : *net->netList->get(net->name)) {
      this->pairsByNetNumber->remove(subnet->netNumber);
    }
  }
}

DifferentialPair* DifferentialPairs::get(const std::string& baseName) {
  DifferentialPair* exact = this->pairsByName->get(baseName);
  if (exact != nullptr) {
    return exact;
  }
  // Case-insensitive fallback for loose lookups
  for (auto* entry : *this->pairsByName->entrySet()) {
    if (jrt::equalsIgnoreCase(entry->getKey(), baseName)) {
      return entry->getValue();
    }
  }
  return nullptr;
}

DifferentialPair* DifferentialPairs::findForNet(Net* net) {
  if (net == nullptr) {
    return nullptr;
  }
  return findForNet(net->netNumber);
}

DifferentialPair* DifferentialPairs::findForNet(int32_t netNumber) {
  return this->pairsByNetNumber->get(netNumber);
}

bool DifferentialPairs::remove(DifferentialPair* pair) {
  if (pair == nullptr) {
    return false;
  }
  std::string key = pair->baseName();
  DifferentialPair* existing = this->pairsByName->get(key);
  if (existing != nullptr && hasSameIdentity(existing, pair)) {
    this->pairsByName->remove(key);
    removeSubnets(existing->positiveNet());
    removeSubnets(existing->negativeNet());
    return true;
  }
  return false;
}

jrt::Collection<DifferentialPair*>* DifferentialPairs::all() {
  return new jrt::ArrayList<DifferentialPair*>(this->pairsByName->values());
}

int32_t DifferentialPairs::count() { return this->pairsByName->size(); }

bool DifferentialPairs::isEmpty() { return this->pairsByName->isEmpty(); }

void DifferentialPairs::clear() {
  this->pairsByName->clear();
  this->pairsByNetNumber->clear();
}

}  // namespace fr
