// Port of app.freerouting.rules.Net
#include "rules/Net.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "datastructures/UndoableObjects.h"
#include "rules/BoardRules.h"
#include "rules/NetClass.h"
#include "rules/NetLengthConstraint.h"
#include "rules/NetMeanderConstraint.h"
#include "rules/Nets.h"
#include "util/TextManager.h"

namespace fr {

Net::Net(const std::string& name, int32_t subnetNumber, int32_t number, Nets* netList,
         bool containsPlane)
    : name(name), subnetNumber(subnetNumber), netNumber(number) {
  this->containsPlane_ = containsPlane;
  this->netList = netList;
  this->netClass = netList->getBoard()->rules->getDefaultNetClass();
}

Net::Net(Net* source, Nets* netList)
    : name(source->name), subnetNumber(source->subnetNumber), netNumber(source->netNumber) {
  this->containsPlane_ = source->containsPlane_;
  this->netList = netList;
  this->netClass = nullptr;  // set by BoardRules::deepCopy
  this->lengthConstraint = source->lengthConstraint;
  this->meanderConstraint = source->meanderConstraint;
}

int32_t Net::compareTo(Net* other) {
  if (other == nullptr) {
    throw jrt::NullPointerException("other net cannot be null");
  }
  int32_t res = jrt::compareStringsIgnoreCase(this->name, other->name);
  if (res != 0) {
    return res;
  }
  res = jrt::compareStrings(this->name, other->name);
  if (res != 0) {
    return res;
  }
  res = jrt::Integer::compare(this->subnetNumber, other->subnetNumber);
  if (res != 0) {
    return res;
  }
  return jrt::Integer::compare(this->netNumber, other->netNumber);
}

bool Net::isRoutedFirst() { return this->netClass != nullptr && this->netClass->isRoutedFirst; }

double Net::getMinimumTraceLength() {
  if (this->lengthConstraint != nullptr && this->lengthConstraint->hasMin()) {
    return this->lengthConstraint->minLength();
  }
  return this->netClass != nullptr ? this->netClass->getMinimumTraceLength() : 0.0;
}

void Net::setMinimumTraceLength(double value) {
  double currentMax = (this->lengthConstraint != nullptr) ? this->lengthConstraint->maxLength() : 0.0;
  this->lengthConstraint = new (jrt::kBoard) NetLengthConstraint(value, currentMax);
}

double Net::getMaximumTraceLength() {
  if (this->lengthConstraint != nullptr && this->lengthConstraint->hasMax()) {
    return this->lengthConstraint->maxLength();
  }
  return this->netClass != nullptr ? this->netClass->getMaximumTraceLength() : 0.0;
}

void Net::setMaximumTraceLength(double value) {
  double currentMin = (this->lengthConstraint != nullptr) ? this->lengthConstraint->minLength() : 0.0;
  this->lengthConstraint = new (jrt::kBoard) NetLengthConstraint(currentMin, value);
}

NetLengthConstraint* Net::getLengthConstraint() {
  return new (jrt::kBoard) NetLengthConstraint(getMinimumTraceLength(), getMaximumTraceLength());
}

bool Net::hasExplicitLengthConstraint() {
  return this->lengthConstraint != nullptr && this->lengthConstraint->isConstrained();
}

NetMeanderConstraint* Net::getMeanderConstraint() {
  if (this->meanderConstraint != nullptr && this->meanderConstraint->isConstrained()) {
    return this->meanderConstraint;
  }
  if (this->netClass != nullptr && this->netClass->hasMeanderConstraint()) {
    return this->netClass->getMeanderConstraint();
  }
  if (this->netList != nullptr && this->netList->getBoard() != nullptr &&
      this->netList->getBoard()->rules != nullptr) {
    return this->netList->getBoard()->rules->getMeanderConstraint(this);
  }
  return NetMeanderConstraint::UNCONSTRAINED();
}

bool Net::hasExplicitMeanderConstraint() {
  return this->meanderConstraint != nullptr && this->meanderConstraint->isConstrained();
}

jrt::Collection<Item*>* Net::getTerminalItems() {
  jrt::Collection<Item*>* result = new (jrt::kOp) jrt::LinkedList<Item*>();
  BasicBoard* board = this->netList->getBoard();
  auto* it = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    if (jrt::fast_cast<Connectable>(currentItem) != nullptr) {
      if (currentItem->containsNet(this->netNumber) && !currentItem->isRoutable()) {
        result->add(currentItem);
      }
    }
  }
  return result;
}

jrt::Collection<Pin*>* Net::getPins() {
  jrt::Collection<Pin*>* result = new (jrt::kOp) jrt::LinkedList<Pin*>();
  BasicBoard* board = this->netList->getBoard();
  auto* it = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    if (auto* pin = jrt::fast_cast<Pin>(currentItem)) {
      if (currentItem->containsNet(this->netNumber)) {
        result->add(pin);
      }
    }
  }
  return result;
}

jrt::Collection<Item*>* Net::getItems() {
  jrt::Collection<Item*>* result = new (jrt::kOp) jrt::LinkedList<Item*>();
  BasicBoard* board = this->netList->getBoard();
  auto* it = board->itemList->startReadObject();
  for (;;) {
    Item* currentItem = jrt::fast_cast<Item>(board->itemList->readObject(it));
    if (currentItem == nullptr) {
      break;
    }
    if (currentItem->containsNet(this->netNumber)) {
      result->add(currentItem);
    }
  }
  return result;
}

double Net::getTraceLength() {
  double cumulativeTraceLength = 0;
  jrt::Collection<Item*>* netItems = netList->getBoard()->getConnectableItems(this->netNumber);
  for (Item* currentItem : *netItems) {
    if (auto* trace = jrt::fast_cast<Trace>(currentItem)) {
      cumulativeTraceLength += trace->getLength();
    }
  }
  return cumulativeTraceLength;
}

int32_t Net::getViaCount() {
  int32_t result = 0;
  jrt::Collection<Item*>* netItems = netList->getBoard()->getConnectableItems(this->netNumber);
  for (Item* currentItem : *netItems) {
    if (jrt::fast_cast<Via>(currentItem) != nullptr) {
      ++result;
    }
  }
  return result;
}

void Net::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  int32_t viaCount = this->getViaCount();
  double cumulativeTraceLength = this->getTraceLength();
  jrt::Collection<Item*>* terminalItems = this->getTerminalItems();
  auto* terminals = new (jrt::kOp) jrt::LinkedList<ItemInfoPrinter_Printable*>();
  for (Item* item : *terminalItems) {
    terminals->add(item);
  }
  int32_t terminalItemCount = terminals->size();

  TextManager* tm = new (jrt::kBoard) TextManager("Net", locale);

  printer->appendBold(tm->getText("net") + " ");
  printer->appendBold(this->name);
  printer->appendBold(": ");
  printer->append(tm->getText("class") + " ");
  printer->append(netClass->getName(), tm->getText("netClass"), netClass);
  printer->append(", ");
  printer->appendObjects(jrt::str(terminalItemCount), tm->getText("terminal_items_2"), terminals);
  printer->append(" " + tm->getText("terminalItems"));
  printer->append(", " + tm->getText("viaCount") + " ");
  printer->append(jrt::str(viaCount));
  printer->append(", " + tm->getText("traceLength") + " ");
  printer->append(cumulativeTraceLength);
  printer->newline();
}

}  // namespace fr
