// Port of app.freerouting.rules.BoardRules
#include "rules/BoardRules.h"

#include <unordered_map>
#include <unordered_set>

#include "board/model/items/Item.h"
#include "board/model/structure/AngleRestriction.h"
#include "board/model/structure/LayerStructure.h"
#include "core/library/Padstack.h"
#include "geometry/planar/ConvexShape.h"
#include "logger/FRLogger.h"
#include "rules/ClearanceMatrix.h"
#include "rules/DefaultItemClearanceClasses.h"
#include "rules/DifferentialPair.h"
#include "rules/DifferentialPairs.h"
#include "rules/MeanderPatternType.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/NetClasses.h"
#include "rules/NetMeanderConstraint.h"
#include "rules/Nets.h"
#include "rules/PinClearanceSource.h"
#include "rules/ViaInfo.h"
#include "rules/ViaInfos.h"
#include "rules/ViaRule.h"

namespace fr {

namespace {
// PORT NOTE: identity map for deepCopy. std::unordered_map instead of jrt::IdentityHashMap so that
// copying does not assign identity hash codes to the source objects.
template <class K, class V>
class IdMap {
 public:
  V get(K k) {
    auto it = m_.find(k);
    return it == m_.end() ? nullptr : it->second;
  }
  void put(K k, V v) { m_[k] = v; }

 private:
  std::unordered_map<K, V> m_;
};
}  // namespace

BoardRules::BoardRules(LayerStructure* layerStructure, ClearanceMatrix* clearanceMatrix) {
  // Java field initializers (run before the constructor body)
  this->viaInfos = new (jrt::kBoard) ViaInfos();
  this->viaRules = new (jrt::kBoard) jrt::ArrayList<ViaRule*>();
  this->netClasses = new (jrt::kBoard) NetClasses();
  this->differentialPairs = new (jrt::kBoard) DifferentialPairs();
  this->defaultMeanderConstraints = new (jrt::kBoard) jrt::TreeMap<MeanderPatternType, NetMeanderConstraint*>();
  this->explicitMeanderConstraints = new (jrt::kBoard) jrt::HashSet<MeanderPatternType>();

  this->layerStructure = layerStructure;
  this->clearanceMatrix = clearanceMatrix;
  nets = new (jrt::kBoard) Nets();
  this->traceAngleRestriction = AngleRestriction::FORTYFIVE_DEGREE;

  this->minTraceHalfWidth = 100000;
  this->maxTraceHalfWidth = 100;
  this->holeClearance = 0;
  initDefaultMeanderConstraints(1000.0);
}

void BoardRules::initDefaultMeanderConstraints(double mmToBoardScale) {
  for (MeanderPatternType type : *MeanderPatternType_values()) {
    if (!this->explicitMeanderConstraints->contains(type)) {
      this->defaultMeanderConstraints->put(
          type, NetMeanderConstraint::createDefaultPreset(type, mmToBoardScale));
    }
  }
}

bool BoardRules::hasExplicitMeanderConstraint(MeanderPatternType type) {
  return this->explicitMeanderConstraints->contains(type);
}

NetMeanderConstraint* BoardRules::getDefaultMeanderConstraint(
    std::optional<MeanderPatternType> type) {
  MeanderPatternType key = type.has_value() ? *type : MeanderPatternType::SINGLE_TRACK;
  NetMeanderConstraint* constraint = this->defaultMeanderConstraints->get(key);
  if (constraint != nullptr) {
    return constraint;
  }
  return NetMeanderConstraint::createDefaultPreset(key, 1000.0);
}

void BoardRules::setDefaultMeanderConstraint(MeanderPatternType type,
                                             NetMeanderConstraint* constraint) {
  if (constraint != nullptr) {
    this->defaultMeanderConstraints->put(type, constraint);
    this->explicitMeanderConstraints->add(type);
  }
}

NetMeanderConstraint* BoardRules::getMeanderConstraint(Net* net, MeanderPatternType fallbackType) {
  if (net != nullptr) {
    if (net->hasExplicitMeanderConstraint()) {
      return net->getExplicitMeanderConstraint();
    }
    if (net->getNetClass() != nullptr && net->getNetClass()->hasMeanderConstraint()) {
      return net->getNetClass()->getMeanderConstraint();
    }
  }
  return getDefaultMeanderConstraint(fallbackType);
}

NetMeanderConstraint* BoardRules::getMeanderConstraint(Net* net) {
  MeanderPatternType defaultType = (net != nullptr && this->differentialPairs != nullptr &&
                                    this->differentialPairs->findForNet(net) != nullptr)
                                       ? MeanderPatternType::DIFF_PAIR
                                       : MeanderPatternType::SINGLE_TRACK;
  return getMeanderConstraint(net, defaultType);
}

int32_t BoardRules::getTraceHalfWidth(int32_t netNumber, int32_t layer) {
  Net* currentNet = nets->get(netNumber);
  return jrt::Math::max(currentNet->getNetClass()->getTraceHalfWidth(layer),
                        minAllowedTraceHalfWidth);
}

bool BoardRules::traceWidthsAreLayerDependent(int32_t netNumber) {
  int32_t compareWidth = getTraceHalfWidth(netNumber, 0);
  for (int32_t i = 1; i < this->layerStructure->layers->length; i++) {
    if (getTraceHalfWidth(netNumber, i) != compareWidth) {
      return true;
    }
  }
  return false;
}

int32_t BoardRules::getMinNetClassTraceHalfWidth() {
  int32_t result = minTraceHalfWidth;
  if (netClasses == nullptr) {
    return result;
  }
  for (int32_t i = 0; i < netClasses->count(); i++) {
    NetClass* netClass = netClasses->get(i);
    for (int32_t layer = 0; layer < netClass->layerCount(); layer++) {
      int32_t halfWidth = netClass->getTraceHalfWidth(layer);
      if (halfWidth > 0 && netClass->isActiveRoutingLayer(layer)) {
        result = jrt::Math::min(result, halfWidth);
      }
    }
  }
  return result;
}

void BoardRules::setDefaultTraceHalfWidth(int32_t layer, int32_t value) {
  this->getDefaultNetClass()->setTraceHalfWidth(layer, value);
  minTraceHalfWidth = jrt::Math::min(minTraceHalfWidth, value);
  maxTraceHalfWidth = jrt::Math::max(maxTraceHalfWidth, value);
}

int32_t BoardRules::getDefaultTraceHalfWidth(int32_t layer) {
  return this->getDefaultNetClass()->getTraceHalfWidth(layer);
}

void BoardRules::setDefaultTraceHalfWidths(int32_t value) {
  if (value <= 0) {
    FRLogger::warn("BoardRules.set_trace_half_widths: value out of range");
    return;
  }
  this->getDefaultNetClass()->setTraceHalfWidth(value);
  minTraceHalfWidth = jrt::Math::min(minTraceHalfWidth, value);
  maxTraceHalfWidth = jrt::Math::max(maxTraceHalfWidth, value);
}

NetClass* BoardRules::getDefaultNetClass() {
  if (this->netClasses->count() <= 0) {
    // net rules not yet initialized
    this->createDefaultNetClass();
  }
  return this->netClasses->get(0);
}

NetClass* BoardRules::getNewNetClass() {
  NetClass* result = this->netClasses->append(this->layerStructure, this->clearanceMatrix);
  result->setTraceClearanceClass(this->getDefaultNetClass()->getTraceClearanceClass());
  result->setViaRule(this->getDefaultViaRule());
  result->setTraceHalfWidth(this->getDefaultNetClass()->getTraceHalfWidth(0));
  return result;
}

NetClass* BoardRules::getNewNetClass(const std::string& name) {
  NetClass* result =
      this->netClasses->append(name, this->layerStructure, this->clearanceMatrix, false);
  result->setTraceClearanceClass(this->getDefaultNetClass()->getTraceClearanceClass());
  result->setViaRule(this->getDefaultViaRule());
  result->setTraceHalfWidth(this->getDefaultNetClass()->getTraceHalfWidth(0));
  return result;
}

void BoardRules::createDefaultViaRule(NetClass* netClass, const std::string& name) {
  // Add the rule  containing all vias.
  ViaRule* defaultRule = new (jrt::kBoard) ViaRule(name);
  int32_t defaultViaClClass =
      netClass->defaultItemClearanceClasses->get(DefaultItemClearanceClasses_ItemClass::VIA);
  for (int32_t i = 0; i < this->viaInfos->count(); i++) {
    ViaInfo* currentViaInfo = this->viaInfos->get(i);
    if (currentViaInfo->getClearanceClassIndex() == defaultViaClClass) {
      Padstack* currentPadstack = currentViaInfo->getPadstack();
      int32_t currentFromLayer = currentPadstack->fromLayer();
      int32_t currentToLayer = currentPadstack->toLayer();
      ViaInfo* existingVia = defaultRule->getLayerRange(currentFromLayer, currentToLayer);
      if (existingVia != nullptr) {
        ConvexShape* newShape = currentPadstack->getShape(currentFromLayer);
        ConvexShape* existingShape = existingVia->getPadstack()->getShape(currentFromLayer);
        if (newShape->maxWidth() < existingShape->maxWidth()) {
          // The via with the smallest pad shape is preferred
          defaultRule->removeVia(existingVia);
          defaultRule->appendVia(currentViaInfo);
        }
      } else {
        defaultRule->appendVia(currentViaInfo);
      }
    }
  }
  this->viaRules->add(defaultRule);
  netClass->setViaRule(defaultRule);
}

void BoardRules::createDefaultNetClass() {
  // add the default net rule
  NetClass* defaultNetClass =
      this->netClasses->append("default", this->layerStructure, this->clearanceMatrix, false);
  int32_t defaultTraceHalfWidth = 1500;
  defaultNetClass->setTraceHalfWidth(defaultTraceHalfWidth);
  defaultNetClass->setTraceClearanceClass(1);
}

NetClass* BoardRules::appendNetClass() {
  NetClass* newClass = this->netClasses->append(this->layerStructure, this->clearanceMatrix);
  NetClass* defaultClass = this->netClasses->get(0);
  newClass->setViaRule(defaultClass->getViaRule());
  newClass->setTraceHalfWidth(defaultClass->getTraceHalfWidth(0));
  newClass->setTraceClearanceClass(defaultClass->getTraceClearanceClass());
  return newClass;
}

NetClass* BoardRules::appendNetClass(const std::string& name) {
  NetClass* foundClass = this->netClasses->get(name);
  if (foundClass != nullptr) {
    return foundClass;
  }
  NetClass* newClass =
      this->netClasses->append(name, this->layerStructure, this->clearanceMatrix, false);
  NetClass* defaultClass = this->netClasses->get(0);
  newClass->defaultItemClearanceClasses =
      new (jrt::kBoard) DefaultItemClearanceClasses(defaultClass->defaultItemClearanceClasses);
  newClass->setViaRule(defaultClass->getViaRule());
  newClass->setTraceHalfWidth(defaultClass->getTraceHalfWidth(0));
  newClass->setTraceClearanceClass(defaultClass->getTraceClearanceClass());
  return newClass;
}

ViaRule* BoardRules::getDefaultViaRule() {
  if (this->viaRules->isEmpty()) {
    return nullptr;
  }
  return this->viaRules->getFirst();
}

ViaRule* BoardRules::getViaRule(const std::string& name) {
  for (ViaRule* currentRule : *viaRules) {
    if (currentRule->name == name) {
      return currentRule;
    }
  }
  return nullptr;
}

void BoardRules::changeClearanceClassIndex(int32_t fromIndex, int32_t toIndex,
                                           jrt::Collection<Item*>* boardItems) {
  for (Item* currentItem : *boardItems) {
    if (currentItem->clearanceClassIndex() == fromIndex) {
      currentItem->setClearanceClassIndex(toIndex);
    }
  }

  for (int32_t i = 0; i < this->netClasses->count(); i++) {
    NetClass* currentNetClass = this->netClasses->get(i);
    if (currentNetClass->getTraceClearanceClass() == fromIndex) {
      currentNetClass->setTraceClearanceClass(toIndex);
    }
    for (DefaultItemClearanceClasses_ItemClass currentItemClass :
         *DefaultItemClearanceClasses_ItemClass_values()) {
      if (currentNetClass->defaultItemClearanceClasses->get(currentItemClass) == fromIndex) {
        currentNetClass->defaultItemClearanceClasses->set(currentItemClass, toIndex);
      }
    }
  }

  for (int32_t i = 0; i < this->viaInfos->count(); i++) {
    ViaInfo* currentVia = this->viaInfos->get(i);
    if (currentVia->getClearanceClassIndex() == fromIndex) {
      currentVia->setClearanceClassIndex(toIndex);
    }
  }
}

bool BoardRules::removeClearanceClass(int32_t index, jrt::Collection<Item*>* boardItems) {
  for (Item* currentItem : *boardItems) {
    if (currentItem->clearanceClassIndex() == index) {
      return false;
    }
  }
  for (int32_t i = 0; i < this->netClasses->count(); i++) {
    NetClass* currentNetClass = this->netClasses->get(i);
    if (currentNetClass->getTraceClearanceClass() == index) {
      return false;
    }
    for (DefaultItemClearanceClasses_ItemClass currentItemClass :
         *DefaultItemClearanceClasses_ItemClass_values()) {
      if (currentNetClass->defaultItemClearanceClasses->get(currentItemClass) == index) {
        return false;
      }
    }
  }

  for (int32_t i = 0; i < this->viaInfos->count(); i++) {
    ViaInfo* currentVia = this->viaInfos->get(i);
    if (currentVia->getClearanceClassIndex() == index) {
      return false;
    }
  }

  for (Item* currentItem : *boardItems) {
    if (currentItem->clearanceClassIndex() > index) {
      currentItem->setClearanceClassIndex(currentItem->clearanceClassIndex() - 1);
    }
  }

  for (int32_t i = 0; i < this->netClasses->count(); i++) {
    NetClass* currentNetClass = this->netClasses->get(i);
    if (currentNetClass->getTraceClearanceClass() > index) {
      currentNetClass->setTraceClearanceClass(currentNetClass->getTraceClearanceClass() - 1);
    }
    for (DefaultItemClearanceClasses_ItemClass currentItemClass :
         *DefaultItemClearanceClasses_ItemClass_values()) {
      int32_t currentClassNo = currentNetClass->defaultItemClearanceClasses->get(currentItemClass);
      if (currentClassNo > index) {
        currentNetClass->defaultItemClearanceClasses->set(currentItemClass, currentClassNo - 1);
      }
    }
  }

  for (int32_t i = 0; i < this->viaInfos->count(); i++) {
    ViaInfo* currentVia = this->viaInfos->get(i);
    if (currentVia->getClearanceClassIndex() > index) {
      currentVia->setClearanceClassIndex(currentVia->getClearanceClassIndex() - 1);
    }
  }
  this->clearanceMatrix->removeClass(index);
  return true;
}

double BoardRules::getDefaultViaDiameter() {
  ViaRule* defaultViaRule = this->getDefaultViaRule();
  if (defaultViaRule == nullptr) {
    return 0;
  }
  if (defaultViaRule->viaCount() <= 0) {
    return 0;
  }
  Padstack* viaPadstack = defaultViaRule->getVia(0)->getPadstack();
  ConvexShape* currentShape = viaPadstack->getShape(viaPadstack->fromLayer());
  double result = currentShape->maxWidth();
  currentShape = viaPadstack->getShape(viaPadstack->toLayer());
  return jrt::Math::max(result, currentShape->maxWidth());
}

// -------------------------------------------------------------------------------------------------
// PORT NOTE: deepCopy replaces the Java serialization round trip of BoardRules (part of the board
// copy, see RoutingBoardUndoFacade.h). Every mutable object reachable from the rules is copied
// exactly once (identity maps), so identity relations are preserved in the copy: a NetClass whose
// ViaRule is also in viaRules refers to the same copied ViaRule, ViaRules and ViaInfos share the
// copied ViaInfos, nets and differential pairs share the copied nets, ViaInfo::boardRules points to
// the copy. Shared with the source (immutable or board-owned, see RoutingBoardUndoFacade.h): the
// LayerStructure, Padstacks, NetLengthConstraint / NetMeanderConstraint records and the
// ViaRule::EMPTY() sentinel (Java would copy these). Nets::board keeps pointing to the source
// board: the caller re-points it with copy->nets->setBoard(copyBoard). Transient state is reset as
// Java readObject leaves it: clearanceToleranceUm = 1.0, pinClearanceSources = null; exactCopper
// null -> true.
// -------------------------------------------------------------------------------------------------
void BoardRules::disposeCopy(BoardRules* r) {
  // Stage E: frees what deepCopy() made, nothing it shares with the source (layer structure,
  // padstacks, constraint records, ViaRule::EMPTY(), foreign matrices / net lists / rules).
  if (r == nullptr) return;
  std::unordered_set<const void*> done;
  auto once = [&done](const void* p) { return p != nullptr && done.insert(p).second; };
  auto disposeViaInfo = [&](ViaInfo* v) {
    // seen first (it may be freed already), then owned (a foreign rules' via info is kept)
    if (v == nullptr || !once(v)) return;
    if (v->boardRules == r) jrt::dispose(v);
  };
  if (r->viaInfos != nullptr) {
    for (ViaInfo* v : *r->viaInfos->list) disposeViaInfo(v);
    jrt::dispose(r->viaInfos->list);
    jrt::dispose(r->viaInfos);
  }
  auto disposeViaRule = [&](ViaRule* vr) {
    if (vr == nullptr || vr == ViaRule::EMPTY() || !once(vr)) return;
    for (ViaInfo* v : *vr->list) disposeViaInfo(v);
    jrt::dispose(vr->list);
    jrt::dispose(vr);
  };
  if (r->viaRules != nullptr) {
    for (ViaRule* vr : *r->viaRules) disposeViaRule(vr);
    jrt::dispose(r->viaRules);
  }
  if (r->netClasses != nullptr) {
    for (NetClass* nc : *r->netClasses->classArr) {
      if (!once(nc)) continue;
      disposeViaRule(nc->viaRule);
      jrt::dispose(nc->traceHalfWidthArr);
      jrt::dispose(nc->activeRoutingLayerArr);
      if (once(nc->defaultItemClearanceClasses)) jrt::dispose(nc->defaultItemClearanceClasses);
      jrt::dispose(nc);
    }
    jrt::dispose(r->netClasses->classArr);
    jrt::dispose(r->netClasses);
  }
  if (r->nets != nullptr) {
    for (Net* n : *r->nets->nets) {
      if (n != nullptr && n->netList == r->nets && once(n)) jrt::dispose(n);
    }
    jrt::dispose(r->nets->nets);
    jrt::dispose(r->nets);
  }
  if (r->differentialPairs != nullptr) {
    for (auto* e : r->differentialPairs->pairsByName->entryVector()) {
      if (once(e->getValue())) jrt::dispose(e->getValue());
    }
    for (auto* e : r->differentialPairs->pairsByNetNumber->entryVector()) {
      if (once(e->getValue())) jrt::dispose(e->getValue());
    }
    jrt::disposeMapAndEntries(r->differentialPairs->pairsByName);
    jrt::disposeMapAndEntries(r->differentialPairs->pairsByNetNumber);
    jrt::dispose(r->differentialPairs);
  }
  jrt::dispose(r->defaultMeanderConstraints);  // its entries with it; the values are shared
  jrt::dispose(r->explicitMeanderConstraints);
  ClearanceMatrix::disposeCopy(r->clearanceMatrix);
  jrt::dispose(r);
}

BoardRules* BoardRules::deepCopy() {
  BoardRules* copy = new (jrt::kBoard) BoardRules();
  copy->layerStructure = this->layerStructure;
  copy->clearanceMatrix =
      this->clearanceMatrix != nullptr ? this->clearanceMatrix->deepCopy() : nullptr;
  ClearanceMatrix* srcMatrix = this->clearanceMatrix;
  ClearanceMatrix* dstMatrix = copy->clearanceMatrix;
  auto mapMatrix = [srcMatrix, dstMatrix](ClearanceMatrix* m) -> ClearanceMatrix* {
    // A net class always refers to the matrix of its rules; a foreign matrix stays shared.
    return m == srcMatrix ? dstMatrix : m;
  };

  // --- via infos ---
  IdMap<ViaInfo*, ViaInfo*> viaInfoMapStore;  // local (stage E)
  auto* viaInfoMap = &viaInfoMapStore;
  BoardRules* self = this;
  auto mapViaInfo = [viaInfoMap, self, copy](ViaInfo* v) -> ViaInfo* {
    if (v == nullptr) {
      return nullptr;
    }
    ViaInfo* result = viaInfoMap->get(v);
    if (result == nullptr) {
      BoardRules* rules = v->boardRules == self ? copy : v->boardRules;
      result = new (jrt::kBoard) ViaInfo(v->getName(), v->getPadstack(), v->getClearanceClassIndex(),
                           v->attachSmdAllowed(), rules);
      viaInfoMap->put(v, result);
    }
    return result;
  };
  copy->viaInfos = new (jrt::kBoard) ViaInfos();
  for (ViaInfo* v : *this->viaInfos->list) {
    copy->viaInfos->list->add(mapViaInfo(v));
  }

  // --- via rules ---
  IdMap<ViaRule*, ViaRule*> viaRuleMapStore;  // local (stage E)
  auto* viaRuleMap = &viaRuleMapStore;
  auto mapViaRule = [viaRuleMap, mapViaInfo](ViaRule* r) -> ViaRule* {
    if (r == nullptr) {
      return nullptr;
    }
    if (r == ViaRule::EMPTY()) {
      return r;  // immutable sentinel, shared
    }
    ViaRule* result = viaRuleMap->get(r);
    if (result == nullptr) {
      result = new (jrt::kBoard) ViaRule(r->name);
      viaRuleMap->put(r, result);
      for (ViaInfo* v : *r->list) {
        result->list->add(mapViaInfo(v));
      }
    }
    return result;
  };
  copy->viaRules = new (jrt::kOp) jrt::ArrayList<ViaRule*>();
  for (ViaRule* r : *this->viaRules) {
    copy->viaRules->add(mapViaRule(r));
  }

  // --- net classes ---
  IdMap<DefaultItemClearanceClasses*, DefaultItemClearanceClasses*> itemClassesMapStore;  // local (stage E)
  auto* itemClassesMap = &itemClassesMapStore;
  IdMap<NetClass*, NetClass*> netClassMapStore;  // local (stage E)
  auto* netClassMap = &netClassMapStore;
  auto mapNetClass = [netClassMap, itemClassesMap, mapViaRule,
                      mapMatrix](NetClass* nc) -> NetClass* {
    if (nc == nullptr) {
      return nullptr;
    }
    NetClass* result = netClassMap->get(nc);
    if (result == nullptr) {
      result = new (jrt::kBoard) NetClass(nc->name, nc->boardLayerStructure, mapMatrix(nc->clearanceMatrix),
                            nc->isIgnoredByAutorouter);
      netClassMap->put(nc, result);
      // Stage E: the constructor's arrays and default classes are replaced right away.
      jrt::dispose(result->traceHalfWidthArr);
      jrt::dispose(result->activeRoutingLayerArr);
      jrt::dispose(result->defaultItemClearanceClasses);
      result->defaultItemClearanceClasses = nullptr;
      result->traceHalfWidthArr = nc->traceHalfWidthArr->clone();
      result->activeRoutingLayerArr = nc->activeRoutingLayerArr->clone();
      DefaultItemClearanceClasses* srcClasses = nc->defaultItemClearanceClasses;
      if (srcClasses == nullptr) {
        result->defaultItemClearanceClasses = nullptr;
      } else {
        DefaultItemClearanceClasses* dstClasses = itemClassesMap->get(srcClasses);
        if (dstClasses == nullptr) {
          dstClasses = new (jrt::kBoard) DefaultItemClearanceClasses(srcClasses);
          itemClassesMap->put(srcClasses, dstClasses);
        }
        result->defaultItemClearanceClasses = dstClasses;
      }
      result->isIgnoredByAutorouter = nc->isIgnoredByAutorouter;
      result->isRoutedFirst = nc->isRoutedFirst;
      result->viaRule = mapViaRule(nc->viaRule);
      result->traceClearanceClass = nc->traceClearanceClass;
      result->shoveFixed = nc->shoveFixed;
      result->pullTight = nc->pullTight;
      result->ignoreCyclesWithAreas = nc->ignoreCyclesWithAreas;
      result->minimumTraceLength = nc->minimumTraceLength;
      result->maximumTraceLength = nc->maximumTraceLength;
      result->meanderConstraint = nc->meanderConstraint;  // immutable record, shared
    }
    return result;
  };
  copy->netClasses = new (jrt::kBoard) NetClasses();
  for (NetClass* nc : *this->netClasses->classArr) {
    copy->netClasses->classArr->add(mapNetClass(nc));
  }

  // --- nets ---
  copy->nets = new (jrt::kBoard) Nets();
  copy->nets->board = this->nets->board;  // re-pointed by the caller
  Nets* srcNets = this->nets;
  Nets* dstNets = copy->nets;
  IdMap<Net*, Net*> netMapStore;  // local (stage E)
  auto* netMap = &netMapStore;
  auto mapNet = [netMap, srcNets, dstNets, mapNetClass](Net* n) -> Net* {
    if (n == nullptr) {
      return nullptr;
    }
    Net* result = netMap->get(n);
    if (result == nullptr) {
      // A net always belongs to the net list of its rules.
      result = new (jrt::kBoard) Net(n, n->netList == srcNets ? dstNets : n->netList);
      netMap->put(n, result);
      result->netClass = mapNetClass(n->netClass);
    }
    return result;
  };
  for (Net* n : *this->nets->nets) {
    copy->nets->nets->add(mapNet(n));
  }

  // --- differential pairs ---
  IdMap<DifferentialPair*, DifferentialPair*> pairMapStore;  // local (stage E)
  auto* pairMap = &pairMapStore;
  auto mapPair = [pairMap, mapNet](DifferentialPair* p) -> DifferentialPair* {
    if (p == nullptr) {
      return nullptr;
    }
    DifferentialPair* result = pairMap->get(p);
    if (result == nullptr) {
      result = new (jrt::kBoard) DifferentialPair(mapNet(p->positiveNet()), mapNet(p->negativeNet()),
                                    p->baseName(), p->coupledSpacing(), p->maxSkew());
      pairMap->put(p, result);
    }
    return result;
  };
  if (this->differentialPairs == nullptr) {
    copy->differentialPairs = nullptr;
  } else {
    copy->differentialPairs = new (jrt::kBoard) DifferentialPairs();
    for (auto* e : *this->differentialPairs->pairsByName->entrySet()) {
      copy->differentialPairs->pairsByName->put(e->getKey(), mapPair(e->getValue()));
    }
    for (auto* e : *this->differentialPairs->pairsByNetNumber->entrySet()) {
      copy->differentialPairs->pairsByNetNumber->put(e->getKey(), mapPair(e->getValue()));
    }
  }

  // --- scalar settings ---
  copy->traceAngleRestriction = this->traceAngleRestriction;
  copy->ignoreConduction = this->ignoreConduction;
  copy->minTraceHalfWidth = this->minTraceHalfWidth;
  copy->maxTraceHalfWidth = this->maxTraceHalfWidth;
  copy->pinEdgeToTurnDist = this->pinEdgeToTurnDist;
  copy->useSlowAutorouteAlgorithm = this->useSlowAutorouteAlgorithm;
  copy->holeClearance = this->holeClearance;
  copy->minHoleToHole = this->minHoleToHole;
  copy->exactCopper = this->exactCopper;
  copy->viaInPad = this->viaInPad;
  copy->viaTouchingPad = this->viaTouchingPad;
  copy->minAllowedTraceHalfWidth = this->minAllowedTraceHalfWidth;

  // --- meander constraints (values are immutable records, shared) ---
  copy->defaultMeanderConstraints = new (jrt::kOp) jrt::TreeMap<MeanderPatternType, NetMeanderConstraint*>();
  for (auto* e : *this->defaultMeanderConstraints->entrySet()) {
    copy->defaultMeanderConstraints->put(e->getKey(), e->getValue());
  }
  copy->explicitMeanderConstraints = new (jrt::kOp) jrt::HashSet<MeanderPatternType>();
  for (MeanderPatternType t : *this->explicitMeanderConstraints) {
    copy->explicitMeanderConstraints->add(t);
  }

  // --- Java readObject: transient fields and fixups ---
  copy->clearanceToleranceUm = 1.0;
  copy->pinClearanceSources = nullptr;
  if (!copy->exactCopper.has_value()) {
    copy->exactCopper = true;
  }
  return copy;
}

}  // namespace fr
