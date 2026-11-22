// Port of app.freerouting.rules.NetClass
#include "rules/NetClass.h"

#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "logger/FRLogger.h"
#include "rules/ClearanceMatrix.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/NetLengthConstraint.h"
#include "rules/NetMeanderConstraint.h"
#include "rules/ViaRule.h"
#include "util/TextManager.h"

namespace fr {

NetClass::NetClass(const std::string& name, LayerStructure* layerStructure,
                   ClearanceMatrix* clearanceMatrix, bool ignoredByAutorouter) {
  // Java field initializers
  this->defaultItemClearanceClasses = new DefaultItemClearanceClasses();
  this->meanderConstraint = NetMeanderConstraint::UNCONSTRAINED();

  this->name = name;
  this->boardLayerStructure = layerStructure;
  this->clearanceMatrix = clearanceMatrix;
  this->traceHalfWidthArr = new jrt::Array<int32_t>(layerStructure->layers->length);
  this->activeRoutingLayerArr = new jrt::Array<bool>(layerStructure->layers->length);
  for (int32_t i = 0; i < layerStructure->layers->length; i++) {
    (*this->activeRoutingLayerArr)[i] = (*layerStructure->layers)[i]->isSignal;
  }
  this->isIgnoredByAutorouter = ignoredByAutorouter;
}

void NetClass::setTraceHalfWidth(int32_t value) { jrt::Arrays::fill(traceHalfWidthArr, value); }

void NetClass::setTraceHalfWidth(int32_t layer, int32_t value) {
  (*traceHalfWidthArr)[layer] = value;
}

void NetClass::setTraceHalfWidthOnInner(int32_t value) {
  for (int32_t i = 1; i < traceHalfWidthArr->length - 1; i++) {
    (*traceHalfWidthArr)[i] = value;
  }
}

int32_t NetClass::getTraceHalfWidth(int32_t layer) {
  if (layer < 0 || layer >= traceHalfWidthArr->length) {
    FRLogger::warn(" NetClass.get_trace_half_width: layer out of range");
    return 0;
  }
  return (*traceHalfWidthArr)[layer];
}

void NetClass::copySettingsFrom(NetClass* other) {
  jrt::arraycopy(other->traceHalfWidthArr, 0, this->traceHalfWidthArr, 0,
                 jrt::Math::min(other->traceHalfWidthArr->length, this->traceHalfWidthArr->length));
  jrt::arraycopy(
      other->activeRoutingLayerArr, 0, this->activeRoutingLayerArr, 0,
      jrt::Math::min(other->activeRoutingLayerArr->length, this->activeRoutingLayerArr->length));
  this->defaultItemClearanceClasses =
      new DefaultItemClearanceClasses(other->defaultItemClearanceClasses);
  this->isIgnoredByAutorouter = other->isIgnoredByAutorouter;
  this->isRoutedFirst = other->isRoutedFirst;
  this->viaRule = other->viaRule;
  this->traceClearanceClass = other->traceClearanceClass;
  this->shoveFixed = other->shoveFixed;
  this->pullTight = other->pullTight;
  this->ignoreCyclesWithAreas = other->ignoreCyclesWithAreas;
  this->minimumTraceLength = other->minimumTraceLength;
  this->maximumTraceLength = other->maximumTraceLength;
  this->meanderConstraint = other->meanderConstraint;
}

NetLengthConstraint* NetClass::getLengthConstraint() {
  return new NetLengthConstraint(this->minimumTraceLength, this->maximumTraceLength);
}

void NetClass::setLengthConstraint(NetLengthConstraint* constraint) {
  if (constraint == nullptr) {
    this->minimumTraceLength = 0;
    this->maximumTraceLength = 0;
  } else {
    this->minimumTraceLength = constraint->minLength();
    this->maximumTraceLength = constraint->maxLength();
  }
}

NetMeanderConstraint* NetClass::getMeanderConstraint() {
  return this->meanderConstraint != nullptr ? this->meanderConstraint
                                            : NetMeanderConstraint::UNCONSTRAINED();
}

void NetClass::setMeanderConstraint(NetMeanderConstraint* constraint) {
  this->meanderConstraint =
      (constraint != nullptr) ? constraint : NetMeanderConstraint::UNCONSTRAINED();
}

bool NetClass::hasMeanderConstraint() {
  return this->meanderConstraint != nullptr && this->meanderConstraint->isConstrained();
}

bool NetClass::isActiveRoutingLayer(int32_t layerNumber) {
  if (layerNumber < 0 || layerNumber >= this->activeRoutingLayerArr->length) {
    return false;
  }
  return (*this->activeRoutingLayerArr)[layerNumber];
}

void NetClass::setActiveRoutingLayer(int32_t layerNumber, bool active) {
  if (layerNumber < 0 || layerNumber >= this->activeRoutingLayerArr->length) {
    return;
  }
  (*this->activeRoutingLayerArr)[layerNumber] = active;
}

void NetClass::setAllLayersActive(bool value) { jrt::Arrays::fill(this->activeRoutingLayerArr, value); }

void NetClass::setAllInnerLayersActive(bool value) {
  for (int32_t i = 1; i < traceHalfWidthArr->length - 1; i++) {
    (*activeRoutingLayerArr)[i] = value;
  }
}

void NetClass::printInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new TextManager("NetClass", locale);

  printer->appendBold(tm->getText("net_class_2") + " ");
  printer->appendBold(this->name);
  printer->appendBold(":");
  printer->append(" " + tm->getText("traceClearanceClass") + " ");
  std::string clearanceName = clearanceMatrix->getName(this->traceClearanceClass);
  printer->append(clearanceName, tm->getText("trace_clearance_class_2"),
                  clearanceMatrix->getRow(this->traceClearanceClass));
  if (this->shoveFixed) {
    printer->append(", " + tm->getText("shoveFixed"));
  }
  printer->append(", " + tm->getText("viaRule") + " ");
  printer->append(viaRule->name, tm->getText("via_rule_2"), viaRule);
  if (traceWidthIsLayerDependent()) {
    for (int32_t i = 0; i < traceHalfWidthArr->length; i++) {
      printer->newline();
      printer->indent();
      printer->append(tm->getText("traceWidth") + " ");
      printer->append(static_cast<double>(2 * (*traceHalfWidthArr)[i]));
      printer->append(" " + tm->getText("on_layer") + " ");
      printer->append((*this->boardLayerStructure->layers)[i]->name);
    }
  } else {
    printer->append(", " + tm->getText("traceWidth") + " ");
    printer->append(static_cast<double>(2 * (*traceHalfWidthArr)[0]));
  }
  printer->newline();
}

bool NetClass::traceWidthIsLayerDependent() {
  int32_t compareValue = (*traceHalfWidthArr)[0];
  for (int32_t i = 1; i < traceHalfWidthArr->length; i++) {
    if ((*this->boardLayerStructure->layers)[i]->isSignal) {
      if ((*traceHalfWidthArr)[i] != compareValue) {
        return true;
      }
    }
  }
  return false;
}

bool NetClass::traceWidthIsInnerLayerDependent() {
  if (traceHalfWidthArr->length <= 3) {
    return false;
  }
  int32_t firstInnerLayerNo = 1;
  while (firstInnerLayerNo < traceHalfWidthArr->length - 1 &&
         !(*this->boardLayerStructure->layers)[firstInnerLayerNo]->isSignal) {
    ++firstInnerLayerNo;
  }
  if (firstInnerLayerNo >= traceHalfWidthArr->length - 1) {
    return false;
  }
  int32_t compareWidth = (*traceHalfWidthArr)[firstInnerLayerNo];
  for (int32_t i = firstInnerLayerNo + 1; i < traceHalfWidthArr->length - 1; i++) {
    if ((*this->boardLayerStructure->layers)[i]->isSignal) {
      if ((*traceHalfWidthArr)[i] != compareWidth) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace fr
