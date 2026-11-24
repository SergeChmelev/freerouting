// Port of app.freerouting.board.facade.BasicBoard
#include "board/facade/BasicBoard.h"

#include <cstdlib>
#include <string>
#include "geometry/planar/GeomCopy.h"

#include <unordered_set>

#include "autoroute/maze/AutorouteControl.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BoardConnectivityQueries.h"
#include "board/facade/BoardItemRepository.h"
#include "board/facade/BoardSnapshotManager.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/ComponentObstacleArea.h"
#include "board/model/items/ComponentOutline.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/Item.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/items/ViaObstacleArea.h"
#include "board/model/items/WireObstacleArea.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/BoardObservers.h"
#include "board/state/ChangedArea.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Padstack.h"
#include "datastructures/ShapeTree.h"
#include "core/scoring/BoardStatisticsBoundsCalculator.h"
#include "datastructures/UndoableObjects.h"
#include "drc/NetRoutingLedger.h"
#include "geometry/planar/Area.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/Net.h"
#include "rules/Nets.h"

namespace fr {

namespace {
// Diagnostics: board copies made / freed (BasicBoard::copiesMade / copiesDiscarded).
int64_t gCopiesMade = 0;
int64_t gCopiesDiscarded = 0;
}  // namespace

BasicBoard::BasicBoard(IntBox* boundingBox, LayerStructure* layerStructure,
                       jrt::Array<PolylineShape*>* outlineShapes, int32_t outlineClClassNo,
                       BoardRules* rules, Communication* communication) {
  // Java field initializers of transient fields.
  normalizeSuppressedNetNos = new (jrt::kOp) jrt::HashSet<int32_t>();
  updateBox = IntBox::EMPTY();
  this->layerStructure = layerStructure;
  this->rules = rules;
  library = new (jrt::kBoard) BoardLibrary();
  itemList = new (jrt::kBoard) UndoableObjects();
  watchItemList();
  components = new (jrt::kBoard) Components();
  this->communication = communication;
  this->boundingBox = boundingBox;
  searchTreeManager = new (jrt::kBoard) SearchTreeManager(this);
  rules->nets->setBoard(this);
  insertOutline(outlineShapes, outlineClClassNo);
}

BasicBoard::BasicBoard(BasicBoard_CopyTag tag, BasicBoard* source) {
  (void)tag;
  ++gCopiesMade;
  // PORT NOTE: replaces Java deserialization (see RoutingBoardUndoFacade.h). Non-transient state:
  // mutable parts deep copied, immutable parts (library, layer structure, bounding box) shared.
  itemList = nullptr;  // set by copyItemListFrom
  components = source->components->deepCopy();
  rules = source->rules->deepCopy();
  library = source->library;
  layerStructure = source->layerStructure;
  communication = source->communication->copyForBoard();
  boundingBox = source->boundingBox;
  preExistingClearanceViolationsCount = source->preExistingClearanceViolationsCount;
  maxTraceHalfWidth = source->maxTraceHalfWidth;
  minTraceHalfWidth = source->minTraceHalfWidth;
  // Transient fields get the Java default values (not their initializers).
  searchTreeManager = nullptr;
  routingLedger_ = nullptr;
  normalContactCache = nullptr;
  normalContactCacheVersion = 0;
  normalizeSuppressedNetNos = nullptr;
  revision = 0;
  changeRevisions = nullptr;
  changeBoxes = nullptr;
  changeFirstLayers = nullptr;
  changeLastLayers = nullptr;
  pendingBox = nullptr;
  pendingFirstLayer = 0;
  pendingLastLayer = 0;
  pendingUnknown = false;
  journalEnabled = false;
  netAssignmentChangeCount = 0;
  itemRepository = nullptr;
  connectivityQueries = nullptr;
  snapshotManager = nullptr;
  unfixableClearanceViolationsCount = 0;
  ruleBlockedPins = nullptr;
  tolerancePins = nullptr;
  clearanceBaseline = nullptr;
  updateBox = nullptr;
  // The nets of the copied rules belong to this board.
  if (rules != nullptr && rules->nets != nullptr) {
    rules->nets->setBoard(this);
  }
}

BasicBoard* BasicBoard::deserialize(BasicBoard* snapshot) {
  return BoardSnapshotManager::deserialize(snapshot);
}

jrt::Set<Item*>* BasicBoard::cachedNormalContacts(Item* item) {
  int64_t version = itemList->getContentVersion();
  if (normalContactCache == nullptr || normalContactCacheVersion != version) {
    // The cache owns its contact sets: the stale ones go with it (the item walks that read them
    // do not change the item list, so none is being read now).
    if (normalContactCache != nullptr) {
      normalContactCache->forEach([](Item*, jrt::Set<Item*>* contacts) { jrt::dispose(contacts); });
      jrt::dispose(normalContactCache);
    }
    normalContactCache = new (jrt::kBoard) jrt::IdentityHashMap<Item*, jrt::Set<Item*>*>();
    normalContactCacheVersion = version;
  }
  jrt::Set<Item*>* contacts = normalContactCache->get(item);
  if (contacts == nullptr) {
    // PORT NOTE (stage E): the cached set outlives the current operation region: allocate it in
    // the item's domain (heap on the main board, the candidate region on a worker board).
    jrt::DomainScope domain(item);
    contacts = item->getNormalContacts();
    if (contacts != nullptr) {
      normalContactCache->put(item, contacts);
    }
  }
  return contacts;
}

NetRoutingLedger* BasicBoard::routingLedger() {
  if (routingLedger_ == nullptr) {
    routingLedger_ = new (jrt::kBoard) NetRoutingLedger(this);
  }
  return routingLedger_;
}

void BasicBoard::incrementRevision() {
  revision++;
  if (!journalEnabled) {
    return;
  }
  if (pendingUnknown || pendingBox == nullptr) {
    journalChange(nullptr, 0, -1);
  } else {
    journalChange(pendingBox, pendingFirstLayer, pendingLastLayer);
  }
  pendingBox = nullptr;
  pendingFirstLayer = jrt::Integer::MAX_VALUE;
  pendingLastLayer = jrt::Integer::MIN_VALUE;
  pendingUnknown = false;
}

void BasicBoard::incrementRevision(Item* item) {
  noteChange(item);
  incrementRevision();
}

void BasicBoard::noteChange(Item* item) {
  if (!journalEnabled) {
    return;
  }
  IntBox* box = item->boundingBox();
  if (box == nullptr) {
    pendingUnknown = true;
    return;
  }
  noteChange(box, item->firstLayer(), item->lastLayer());
}

IntBox* BasicBoard::keepBox(IntBox* box) {
  return static_cast<IntBox*>(geomcopy::keep(static_cast<TileShape*>(box), this));
}

void BasicBoard::noteChange(IntBox* box, int32_t firstLayer, int32_t lastLayer) {
  if (!journalEnabled) {
    return;
  }
  // PORT NOTE (stage E): board-owned geometry: in the board's domain, copied out of regions.
  jrt::DomainScope domain(this);
  pendingBox = pendingBox == nullptr ? keepBox(box) : pendingBox->union_(box);
  pendingFirstLayer = jrt::Math::min(pendingFirstLayer, firstLayer);
  pendingLastLayer = jrt::Math::max(pendingLastLayer, lastLayer);
}

void BasicBoard::noteUnknownChange() {
  if (!journalEnabled) {
    return;
  }
  pendingUnknown = true;
}

void BasicBoard::journalChange(IntBox* box, int32_t firstLayer, int32_t lastLayer) {
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): the journal is board-owned
  if (changeRevisions == nullptr) {
    changeRevisions = new (jrt::kBoard) jrt::Array<int32_t>(CHANGE_JOURNAL_SIZE);
    for (int32_t i = 0; i < changeRevisions->length; i++) {
      (*changeRevisions)[i] = jrt::Integer::MIN_VALUE;
    }
    changeBoxes = new (jrt::kGeom) jrt::Array<IntBox*>(CHANGE_JOURNAL_SIZE);
    changeFirstLayers = new (jrt::kBoard) jrt::Array<int32_t>(CHANGE_JOURNAL_SIZE);
    changeLastLayers = new (jrt::kBoard) jrt::Array<int32_t>(CHANGE_JOURNAL_SIZE);
  }
  int32_t slot = jrt::Math::floorMod(revision, CHANGE_JOURNAL_SIZE);
  (*changeRevisions)[slot] = revision;
  (*changeBoxes)[slot] = keepBox(box);
  (*changeFirstLayers)[slot] = firstLayer;
  (*changeLastLayers)[slot] = lastLayer;
}

bool BasicBoard::changedSince(int32_t sinceRevision, int32_t layer, IntBox* box) {
  if (sinceRevision == revision) {
    return false;
  }
  if (!journalEnabled) {
    // the changes so far are not known: from now on they are
    journalEnabled = true;
    return true;
  }
  if (changeRevisions == nullptr || sinceRevision > revision ||
      revision - sinceRevision > CHANGE_JOURNAL_SIZE) {
    return true;
  }
  for (int32_t r = sinceRevision + 1; r <= revision; r++) {
    int32_t slot = jrt::Math::floorMod(r, CHANGE_JOURNAL_SIZE);
    if ((*changeRevisions)[slot] != r || (*changeBoxes)[slot] == nullptr) {
      return true;
    }
    if (layer >= (*changeFirstLayers)[slot] && layer <= (*changeLastLayers)[slot] &&
        (*changeBoxes)[slot]->intersects(box)) {
      return true;
    }
  }
  return false;
}

void BasicBoard::noteNetAssignmentChanged() {
  netAssignmentChangeCount++;
  itemList->noteContentChange();
  if (routingLedger_ != nullptr) {
    routingLedger_->invalidate();
  }
}

BasicBoard* BasicBoard::serialize(bool basicProfile) {
  return getSnapshotManager()->serialize(basicProfile);
}

BasicBoard* BasicBoard::clone() {
  return BoardSnapshotManager::deserialize(getSnapshotManager()->serialize(false));
}

std::string BasicBoard::getHash() { return getSnapshotManager()->getHash(); }

int32_t BasicBoard::diffTraces(BasicBoard* compareTo) {
  return getSnapshotManager()->diffTraces(compareTo);
}

PolylineTrace* BasicBoard::insertTraceWithoutCleaning(Polyline* polyline, int32_t layer,
                                                      int32_t halfWidth,
                                                      jrt::Array<int32_t>* netNumbers,
                                                      int32_t clearanceClass,
                                                      FixedState fixedState) {
  if (polyline->cornerCount() < 2) {
    return nullptr;
  }
  PolylineTrace* newTrace = new (jrt::kBoardData) PolylineTrace(polyline, layer, halfWidth, netNumbers,
                                              clearanceClass, 0, 0, fixedState, this);
  if (newTrace->firstCorner()->equals(newTrace->lastCorner())) {
    if (static_cast<int32_t>(fixedState) < static_cast<int32_t>(FixedState::USER_FIXED)) {
      return nullptr;
    }
  }
  insertItem(newTrace);
  if (newTrace->netsNormal()) {
    maxTraceHalfWidth = jrt::Math::max(maxTraceHalfWidth, halfWidth);
    minTraceHalfWidth = jrt::Math::min(minTraceHalfWidth, halfWidth);
  }
  return newTrace;
}

void BasicBoard::insertTrace(Polyline* polyline, int32_t layer, int32_t halfWidth,
                             jrt::Array<int32_t>* netNumbers, int32_t clearanceClass,
                             FixedState fixedState) {
  PolylineTrace* newTrace =
      insertTraceWithoutCleaning(polyline, layer, halfWidth, netNumbers, clearanceClass, fixedState);
  if (newTrace == nullptr) {
    return;
  }
  IntOctagon* clipShape = nullptr;
  if (auto* board = jrt::fast_cast<RoutingBoard>(this)) {
    ChangedArea* changedArea = board->changedArea;
    if (changedArea != nullptr) {
      clipShape = changedArea->getArea(layer);
    }
  }

  try {
    newTrace->normalize(clipShape);
  } catch (const jrt::Exception& e) {
    // Normalization can fail for geometrically degenerate trace segments. The segment is skipped
    // and the connection may remain unrouted. This is non-critical.
    FRLogger::warn(
        "A trace segment could not be normalized and was skipped. "
        "Affected net may have an unrouted connection. Cause: " +
        e.getMessage());
    // PORT NOTE: Java prints the exception's toString(); the C++ exception has no class name.
    FRLogger::debug("BasicBoard.insert_trace_segment: normalization exception detail: " +
                    e.getMessage());
  }
}

void BasicBoard::insertTrace(jrt::Array<Point*>* points, int32_t layer, int32_t halfWidth,
                             jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                             FixedState fixedState) {
  for (int32_t i = 0; i < points->length; i++) {
    if (!this->boundingBox->contains((*points)[i])) {
      FRLogger::warn("LayeredBoard.insert_trace: input point out of range");
    }
  }
  Polyline* poly = new (jrt::kGeom) Polyline(points);
  insertTrace(poly, layer, halfWidth, netNumbers, clearanceClassIndex, fixedState);
}

Via* BasicBoard::insertVia(Padstack* padstack, Point* center, jrt::Array<int32_t>* netNumbers,
                           int32_t clearanceClassIndex, FixedState fixedState,
                           bool attachAllowed) {
  Via* newVia = new (jrt::kBoardData) Via(padstack, center, netNumbers, clearanceClassIndex, 0, 0, fixedState,
                        attachAllowed, this);
  insertItem(newVia);
  int32_t fromLayer = padstack->fromLayer();
  int32_t toLayer = padstack->toLayer();
  for (int32_t i = fromLayer; i <= toLayer; i++) {
    for (int32_t currentNetNumber : *netNumbers) {
      splitTraces(center, i, currentNetNumber);
    }
  }
  return newVia;
}

Via* BasicBoard::insertEscapeVia(Padstack* padstack, Point* center,
                                 jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex,
                                 FixedState fixedState, int32_t smdLayer) {
  Via* newVia = new (jrt::kBoardData) Via(padstack, center, netNumbers, clearanceClassIndex, 0, 0, fixedState, true,
                        this);
  newVia->isEscapeVia = true;
  newVia->escapeViaSmdLayer = smdLayer;
  insertItem(newVia);
  int32_t fromLayer = padstack->fromLayer();
  int32_t toLayer = padstack->toLayer();
  for (int32_t i = fromLayer; i <= toLayer; i++) {
    for (int32_t currentNetNumber : *netNumbers) {
      splitTraces(center, i, currentNetNumber);
    }
  }
  return newVia;
}

Pin* BasicBoard::insertPin(int32_t componentId, int32_t pinIndex, jrt::Array<int32_t>* netNumbers,
                           int32_t clearanceClassIndex, FixedState fixedState) {
  Pin* newPin =
      new (jrt::kBoardData) Pin(componentId, pinIndex, netNumbers, clearanceClassIndex, 0, fixedState, this);
  insertItem(newPin);
  return newPin;
}

ObstacleArea* BasicBoard::insertObstacle(Area* area, int32_t layer, int32_t clearanceClassIndex,
                                         FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_obstacle: area is null");
    return nullptr;
  }
  ObstacleArea* obs = new (jrt::kBoardData) ObstacleArea(area, layer, Vector::ZERO(), 0, false, clearanceClassIndex,
                                       0, 0, std::nullopt, fixedState, this);
  insertItem(obs);
  return obs;
}

ObstacleArea* BasicBoard::insertObstacle(Area* area, int32_t layer,
                                         jrt::Array<int32_t>* netNumbers,
                                         int32_t clearanceClassIndex, int32_t componentId,
                                         const std::optional<std::string>& name,
                                         FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_obstacle: area is null");
    return nullptr;
  }
  ObstacleArea* obs = new (jrt::kBoardData) ObstacleArea(area, layer, Vector::ZERO(), 0, false, netNumbers,
                                       clearanceClassIndex, 0, componentId, name, fixedState, this);
  insertItem(obs);
  return obs;
}

ObstacleArea* BasicBoard::insertObstacle(Area* area, int32_t layer, Vector* translation,
                                         double rotationInDegree, bool sideChanged,
                                         int32_t clearanceClassIndex, int32_t componentId,
                                         const std::optional<std::string>& name,
                                         FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_obstacle: area is null");
    return nullptr;
  }
  ObstacleArea* obs = new (jrt::kBoardData) ObstacleArea(area, layer, translation, rotationInDegree, sideChanged,
                                       clearanceClassIndex, 0, componentId, name, fixedState, this);
  insertItem(obs);
  return obs;
}

ViaObstacleArea* BasicBoard::insertViaObstacle(Area* area, int32_t layer,
                                               int32_t clearanceClassIndex,
                                               FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_via_obstacle: area is null");
    return nullptr;
  }
  ViaObstacleArea* obs = new (jrt::kBoardData) ViaObstacleArea(area, layer, Vector::ZERO(), 0, false,
                                             clearanceClassIndex, 0, 0, std::nullopt, fixedState,
                                             this);
  insertItem(obs);
  return obs;
}

ViaObstacleArea* BasicBoard::insertViaObstacle(Area* area, int32_t layer, Vector* translation,
                                               double rotationInDegree, bool sideChanged,
                                               int32_t clearanceClassIndex, int32_t componentId,
                                               const std::optional<std::string>& name,
                                               FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_via_obstacle: area is null");
    return nullptr;
  }
  ViaObstacleArea* obs =
      new (jrt::kBoardData) ViaObstacleArea(area, layer, translation, rotationInDegree, sideChanged,
                          clearanceClassIndex, 0, componentId, name, fixedState, this);
  insertItem(obs);
  return obs;
}

WireObstacleArea* BasicBoard::insertWireObstacle(Area* area, int32_t layer,
                                                 int32_t clearanceClassIndex,
                                                 FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_wire_obstacle: area is null");
    return nullptr;
  }
  WireObstacleArea* obs = new (jrt::kBoardData) WireObstacleArea(area, layer, Vector::ZERO(), 0, false,
                                               clearanceClassIndex, 0, 0, std::nullopt,
                                               fixedState, this);
  insertItem(obs);
  return obs;
}

WireObstacleArea* BasicBoard::insertWireObstacle(Area* area, int32_t layer, Vector* translation,
                                                 double rotationInDegree, bool sideChanged,
                                                 int32_t clearanceClassIndex, int32_t componentId,
                                                 const std::optional<std::string>& name,
                                                 FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_wire_obstacle: area is null");
    return nullptr;
  }
  WireObstacleArea* obs =
      new (jrt::kBoardData) WireObstacleArea(area, layer, translation, rotationInDegree, sideChanged,
                           clearanceClassIndex, 0, componentId, name, fixedState, this);
  insertItem(obs);
  return obs;
}

ComponentObstacleArea* BasicBoard::insertComponentObstacle(Area* area, int32_t layer,
                                                           int32_t clearanceClassIndex,
                                                           FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_component_obstacle: area is null");
    return nullptr;
  }
  ComponentObstacleArea* obs = new (jrt::kBoardData) ComponentObstacleArea(area, layer, Vector::ZERO(), 0, false,
                                                         clearanceClassIndex, 0, 0, std::nullopt,
                                                         fixedState, this);
  insertItem(obs);
  return obs;
}

ComponentObstacleArea* BasicBoard::insertComponentObstacle(
    Area* area, int32_t layer, Vector* translation, double rotationInDegree, bool sideChanged,
    int32_t clearanceClassIndex, int32_t componentId, const std::optional<std::string>& name,
    FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_component_obstacle: area is null");
    return nullptr;
  }
  ComponentObstacleArea* obs =
      new (jrt::kBoardData) ComponentObstacleArea(area, layer, translation, rotationInDegree, sideChanged,
                                clearanceClassIndex, 0, componentId, name, fixedState, this);
  insertItem(obs);
  return obs;
}

ComponentOutline* BasicBoard::insertComponentOutline(Area* area, bool isFront, Vector* translation,
                                                     double rotationInDegree, int32_t componentId,
                                                     bool isCourtyard, bool isFabrication,
                                                     bool isClosed, FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_component_outline: area is null");
    return nullptr;
  }
  if (!area->isBounded()) {
    FRLogger::warn("BasicBoard.insert_component_outline: area is not bounded");
    return nullptr;
  }
  ComponentOutline* outline =
      new (jrt::kBoardData) ComponentOutline(area, isFront, translation, rotationInDegree, 0, componentId,
                           isCourtyard, isFabrication, isClosed, fixedState, this);
  insertItem(outline);
  return outline;
}

ConductionArea* BasicBoard::insertConductionArea(Area* area, int32_t layer,
                                                 jrt::Array<int32_t>* netNumbers,
                                                 int32_t clearanceClassIndex, bool isObstacle,
                                                 FixedState fixedState) {
  if (area == nullptr) {
    FRLogger::warn("BasicBoard.insert_conduction_area: area is null");
    return nullptr;
  }
  ConductionArea* c =
      new (jrt::kBoardData) ConductionArea(area, layer, Vector::ZERO(), 0, false, netNumbers, clearanceClassIndex, 0,
                         0, std::nullopt, isObstacle, fixedState, this);
  insertItem(c);
  return c;
}

BoardOutline* BasicBoard::insertOutline(jrt::Array<PolylineShape*>* outlineShapes,
                                        int32_t clearanceClassIndex) {
  BoardOutline* result = new (jrt::kBoardData) BoardOutline(outlineShapes, clearanceClassIndex, 0, this);
  insertItem(result);
  return result;
}

void BasicBoard::expandBoundingBoxToIncludeAllItems() {
  IntBox* bounds = this->boundingBox;
  bool changed = false;
  jrt::DisposeOnExit loopSource1(getItems());
  for (Item* item : *loopSource1) {
    IntBox* itemBox = item->boundingBox();
    if (itemBox != nullptr && !itemBox->isEmpty() && !bounds->contains(itemBox)) {
      bounds = bounds->union_(itemBox);
      changed = true;
    }
  }
  if (changed) {
    this->boundingBox = bounds->offset(1000);
    BoardOutline* outline = getOutline();
    if (outline != nullptr) {
      outline->invalidateEdgePinNets();
    }
  }
}

BoardOutline* BasicBoard::getOutline() { return getItemRepository()->getOutline(); }

void BasicBoard::invalidateEdgePinNetCache() {
  BoardOutline* outline = getOutline();
  if (outline != nullptr) {
    outline->invalidateEdgePinNets();
    outline->updateSlicesToTraceWidths();
  }
}

void BasicBoard::removeItem(Item* item) {
  getItemRepository()->removeItem(item);
  if (jrt::fast_cast<Pin>(item) != nullptr || jrt::fast_cast<BoardOutline>(item) != nullptr) {
    invalidateEdgePinNetCache();
  }
}

void BasicBoard::detachItemForMove(Item* item) {
  getItemRepository()->detachItemForMove(item);
  if (jrt::fast_cast<Pin>(item) != nullptr || jrt::fast_cast<BoardOutline>(item) != nullptr) {
    invalidateEdgePinNetCache();
  }
}

Item* BasicBoard::getItem(int32_t id) { return getItemRepository()->getItem(id); }

jrt::Collection<Item*>* BasicBoard::getItems() { return getItemRepository()->getItems(); }

jrt::Collection<Item*>* BasicBoard::getConnectableItems(int32_t netNumber) {
  return getConnectivityQueries()->getConnectableItems(netNumber);
}

int32_t BasicBoard::connectableItemCount(int32_t netNumber) {
  return getConnectivityQueries()->connectableItemCount(netNumber);
}

jrt::Collection<Item*>* BasicBoard::getComponentItems(int32_t componentId) {
  return getConnectivityQueries()->getComponentItems(componentId);
}

jrt::Collection<Pin*>* BasicBoard::getComponentPins(int32_t componentId) {
  return getConnectivityQueries()->getComponentPins(componentId);
}

Pin* BasicBoard::getPin(int32_t componentId, int32_t pinIndex) {
  return getConnectivityQueries()->getPin(componentId, pinIndex);
}

bool BasicBoard::removeItems(jrt::Collection<Item*>* itemList) {
  bool result = true;
  for (Item* currentItem : *itemList) {
    if (currentItem->isDeletionForbidden() || currentItem->isUserFixed()) {
      // We are not allowed to delete this item
      result = false;
    } else {
      removeItem(currentItem);
    }
  }
  return result;
}

jrt::Collection<ConductionArea*>* BasicBoard::getConductionAreas() {
  return getItemRepository()->getConductionAreas();
}

jrt::Set<Item*>* BasicBoard::powerLayerPlanes(jrt::Collection<Item*>* items) {
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  for (Item* item : *items) {
    if (auto* area = jrt::fast_cast<ConductionArea>(item)) {
      if (area->getComponentId() <= 0 &&
          !(*layerStructure->layers)[area->getLayer()]->isSignal) {
        result->add(area);
      }
    }
  }
  return result;
}

jrt::Collection<Pin*>* BasicBoard::getPins() { return getItemRepository()->getPins(); }

int32_t BasicBoard::getPinCount() { return getItemRepository()->getPinCount(); }

jrt::Collection<Pin*>* BasicBoard::getSmdPins() { return getItemRepository()->getSmdPins(); }

jrt::Collection<Via*>* BasicBoard::getVias() { return getItemRepository()->getVias(); }

int32_t BasicBoard::getViaCount() { return getItemRepository()->getViaCount(); }

jrt::Collection<Trace*>* BasicBoard::getTraces() { return getItemRepository()->getTraces(); }

double BasicBoard::cumulativeTraceLength() { return getItemRepository()->cumulativeTraceLength(); }

bool BasicBoard::combineTraces(int32_t netNumber) {
  bool result = false;
  bool somethingChanged = true;
  while (somethingChanged) {
    somethingChanged = false;
    auto itScope = itemList->readScope();
    jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
    for (;;) {
      Item* currentItem = jrt::fast_cast<Item>(itemList->readObject(it));
      if (currentItem == nullptr) {
        break;
      }
      if (netNumber < 0 || currentItem->containsNet(netNumber)) {
        if (auto* trace = jrt::fast_cast<Trace>(currentItem)) {
          if (currentItem->isOnTheBoard()) {
            if (trace->combine()) {
              somethingChanged = true;
              result = true;
              break;
            }
          }
        }
      }
    }
  }
  return result;
}

namespace {

std::string netNameOf(BoardRules* rules, int32_t netNumber) {
  return rules != nullptr && rules->nets != nullptr && rules->nets->get(netNumber) != nullptr
             ? rules->nets->get(netNumber)->name
             : jrt::str(netNumber);
}

}  // namespace

bool BasicBoard::normalizeTraces(int32_t netNumber) {
  if (normalizeSuppressedNetNos == nullptr) {
    normalizeSuppressedNetNos = new (jrt::kBoard) jrt::HashSet<int32_t>();
  }
  if (normalizeSuppressedNetNos->contains(netNumber)) {
    std::string netName = netNameOf(rules, netNumber);
    FRLogger::debug("BasicBoard.normalizeTraces: skipping net '" + netName +
                    "' because normalization already hit the oscillation cap on this board "
                    "candidate.");
    return false;
  }
  bool result = false;
  bool somethingChanged = true;
  int32_t iterationCount = 0;
  while (somethingChanged) {
    if (++iterationCount > MAX_NORMALIZE_ITERATIONS) {
      // Safety valve: after this many passes the outer loop has not converged.
      std::string netName = netNameOf(rules, netNumber);
      FRLogger::warn(
          "BasicBoard.normalizeTraces: reached " + jrt::str(MAX_NORMALIZE_ITERATIONS) +
          " iterations for net '" + netName + "' — stopping to prevent hang." +
          " The board geometry for this net may be oscillating (split/combine cycle);" +
          " traces are kept as-is and further normalization for this net is suppressed" +
          " on this board candidate.");
      normalizeSuppressedNetNos->add(netNumber);
      break;
    }
    somethingChanged = false;

    // Collect traces for this net on this iteration.
    jrt::List<PolylineTrace*>* netTraces = new (jrt::kOp) jrt::ArrayList<PolylineTrace*>();
    auto itScope = itemList->readScope();
    jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
    for (;;) {
      Item* currentItem;
      try {
        currentItem = jrt::fast_cast<Item>(itemList->readObject(it));
      } catch (const jrt::ConcurrentModificationException&) {
        somethingChanged = true;
        break;
      }
      if (currentItem == nullptr) {
        break;
      }
      if (currentItem->containsNet(netNumber)) {
        if (auto* currentTrace = jrt::fast_cast<PolylineTrace>(currentItem)) {
          if (currentItem->isOnTheBoard()) {
            netTraces->add(currentTrace);
          }
        }
      }
    }

    if (somethingChanged) {
      continue;
    }

    for (PolylineTrace* currentTrace : *netTraces) {
      if (currentTrace->isOnTheBoard()) {
        if (currentTrace->normalize(nullptr)) {
          somethingChanged = true;
          result = true;
        } else if (!currentTrace->isUserFixed() && this->removeIfCycle(currentTrace)) {
          somethingChanged = true;
          result = true;
        }
      }
    }
  }
  return result;
}

bool BasicBoard::normalizeAllTraces() {
  bool result = false;
  // Group all PolylineTrace items by net
  auto* tracesByNet = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<PolylineTrace*>*>();
  auto itScope = itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    Item* currentItem;
    try {
      currentItem = jrt::fast_cast<Item>(itemList->readObject(it));
    } catch (const jrt::ConcurrentModificationException&) {
      // Retry collection if modified
      tracesByNet->clear();
      itScope.restart();
      continue;
    }
    if (currentItem == nullptr) {
      break;
    }
    if (auto* currentTrace = jrt::fast_cast<PolylineTrace>(currentItem)) {
      if (currentItem->isOnTheBoard()) {
        for (int32_t netNumber : *currentTrace->netNumbers) {
          tracesByNet
              ->computeIfAbsent(netNumber,
                                [](int32_t) -> jrt::List<PolylineTrace*>* {
                                  return new (jrt::kOp) jrt::ArrayList<PolylineTrace*>();
                                })
              ->add(currentTrace);
        }
      }
    }
  }

  for (auto* entry : *tracesByNet->entrySet()) {
    int32_t netNumber = entry->getKey();
    jrt::List<PolylineTrace*>* netTraces = entry->getValue();
    bool somethingChanged = true;
    int32_t iterationCount = 0;
    while (somethingChanged) {
      if (++iterationCount > MAX_NORMALIZE_ITERATIONS) {
        std::string netName = netNameOf(rules, netNumber);
        FRLogger::warn("BasicBoard.normalize_all_traces: reached " +
                       jrt::str(MAX_NORMALIZE_ITERATIONS) + " iterations for net '" + netName +
                       "' — stopping to prevent hang.");
        break;
      }
      somethingChanged = false;

      for (PolylineTrace* currentTrace : *netTraces) {
        if (currentTrace->isOnTheBoard()) {
          if (currentTrace->normalize(nullptr)) {
            somethingChanged = true;
            result = true;
          } else if (!currentTrace->isUserFixed() && this->removeIfCycle(currentTrace)) {
            somethingChanged = true;
            result = true;
          }
        }
      }

      // If something changed, collect the traces for this net again
      if (somethingChanged) {
        netTraces->clear();
        auto it2Scope = itemList->readScope();
        jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it2 = &it2Scope;
        for (;;) {
          Item* currentItem;
          try {
            currentItem = jrt::fast_cast<Item>(itemList->readObject(it2));
          } catch (const jrt::ConcurrentModificationException&) {
            netTraces->clear();
            it2Scope.restart();
            continue;
          }
          if (currentItem == nullptr) {
            break;
          }
          if (currentItem->containsNet(netNumber)) {
            if (auto* currentTrace = jrt::fast_cast<PolylineTrace>(currentItem)) {
              if (currentItem->isOnTheBoard()) {
                netTraces->add(currentTrace);
              }
            }
          }
        }
      }
    }
  }
  return result;
}

bool BasicBoard::splitTraces(Point* location, int32_t layer, int32_t netNumber) {
  ItemSelectionFilter* filter =
      new (jrt::kOp) ItemSelectionFilter(ItemSelectionFilter_SelectableChoices::TRACES);
  jrt::Collection<Item*>* pickedItems = this->pickItems(location, layer, filter);
  IntOctagon* locationShape = TileShape::getInstance(location)->boundingOctagon();
  bool traceSplit = false;
  for (Item* currentItem : *pickedItems) {
    Trace* currentTrace = jrt::fast_cast<Trace>(currentItem);
    if (currentTrace->containsNet(netNumber)) {
      jrt::Collection<PolylineTrace*>* splitPieces = currentTrace->split(locationShape);
      if (splitPieces->size() != 1) {
        traceSplit = true;
      }
    }
  }
  return traceSplit;
}

jrt::Collection<jrt::Collection<Item*>*>* BasicBoard::getConnectedSets(int32_t netNumber) {
  return getConnectivityQueries()->getConnectedSets(netNumber);
}

jrt::Set<SearchTreeObject*>* BasicBoard::overlappingObjects(ConvexShape* shape, int32_t layer) {
  return this->searchTreeManager->getDefaultTree()->overlappingObjects(shape, layer);
}

jrt::Set<Item*>* BasicBoard::overlappingItemsWithClearance(ConvexShape* shape, int32_t layer,
                                                           jrt::Array<int32_t>* ignoreNetNos,
                                                           int32_t clearanceClass) {
  ShapeSearchTree* defaultTree = this->searchTreeManager->getDefaultTree();
  return defaultTree->overlappingItemsWithClearance(shape, layer, ignoreNetNos, clearanceClass);
}

jrt::Set<Item*>* BasicBoard::overlappingItems(Area* area, int32_t layer) {
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  jrt::Array<TileShape*>* tileShapes = area->splitToConvex();
  for (int32_t i = 0; i < tileShapes->length; i++) {
    jrt::DisposeOnExit<jrt::Set<SearchTreeObject*>> currentOverlapsOwner(overlappingObjects((*tileShapes)[i], layer));
    jrt::Set<SearchTreeObject*>* currentOverlaps = currentOverlapsOwner.get();
    for (SearchTreeObject* currentOverlap : *currentOverlaps) {
      if (auto* item = jrt::fast_cast<Item>(currentOverlap)) {
        result->add(item);
      }
    }
  }
  return result;
}

bool BasicBoard::checkShape(Area* shape, int32_t layer, jrt::Array<int32_t>* netNumbers,
                            int32_t clearanceClassIndex) {
  jrt::Array<TileShape*>* tiles = shape->splitToConvex();
  ShapeSearchTree* defaultTree = this->searchTreeManager->getDefaultTree();
  for (int32_t i = 0; i < tiles->length; i++) {
    TileShape* currentShape = (*tiles)[i];
    if (!currentShape->isContainedIn(boundingBox)) {
      return false;
    }
    // Area input is physical geometry, unlike Item.getTileShape(), which is tree-prepared.
    int32_t compensation = defaultTree->clearanceCompensationValue(clearanceClassIndex, layer);
    TileShape* queryShape =
        compensation == 0 ? currentShape
                          : jrt::fast_cast<TileShape>(currentShape->enlarge(compensation));
    jrt::Set<SearchTreeObject*>* obstacles = new (jrt::kOp) jrt::TreeSet<SearchTreeObject*>();
    defaultTree->overlappingObjectsWithClearance(queryShape, layer, netNumbers,
                                                 clearanceClassIndex, obstacles);
    for (SearchTreeObject* currentObject : *obstacles) {
      bool isObstacle = true;
      for (int32_t j = 0; j < netNumbers->length; j++) {
        if (!currentObject->isObstacle((*netNumbers)[j])) {
          isObstacle = false;
        }
      }
      if (isObstacle) {
        return false;
      }
    }
  }
  return true;
}

bool BasicBoard::checkTraceShape(TileShape* shape, int32_t layer, jrt::Array<int32_t>* netNumbers,
                                 int32_t clearanceClassIndex, jrt::Set<Pin*>* contactPins) {
  if (!shape->isContainedIn(boundingBox)) {
    return false;
  }
  ShapeSearchTree* defaultTree = this->searchTreeManager->getDefaultTree();
  jrt::Collection<ShapeTree_TreeEntry*>* treeEntries = new (jrt::kOp) jrt::LinkedList<ShapeTree_TreeEntry*>();
  jrt::DisposeAllOnExit<ShapeTree_TreeEntry> treeEntriesOwner(treeEntries);
  jrt::DisposeOnExit<jrt::Array<int32_t>> ignoreNetNosOwner(new (jrt::kOp) jrt::Array<int32_t>(0));
  jrt::Array<int32_t>* ignoreNetNos = ignoreNetNosOwner.get();
  defaultTree->overlappingTreeEntriesWithClearance(shape, layer, ignoreNetNos, clearanceClassIndex,
                                                   treeEntries);
  for (ShapeTree_TreeEntry* currentTreeEntry : *treeEntries) {
    auto* currentItem = jrt::fast_cast<Item>(currentTreeEntry->object);
    if (currentItem == nullptr) {
      continue;
    }
    if (contactPins != nullptr) {
      auto* currentPin = jrt::fast_cast<Pin>(currentItem);
      if (currentPin != nullptr && contactPins->contains(currentPin)) {
        continue;
      }
      if (currentPin != nullptr) {
        // The contact pins of the trace should be contained in ignoreItems.
        // Other pins are handled as obstacles to avoid acid traps.
        return false;
      }
    }
    bool isObstacle = true;
    if (auto* outline = jrt::fast_cast<BoardOutline>(currentItem)) {
      isObstacle = !outline->exemptsTrace(currentTreeEntry->shapeIndexInObject, netNumbers);
    } else {
      for (int32_t i = 0; i < netNumbers->length; i++) {
        if (!currentItem->isTraceObstacle((*netNumbers)[i],
                                          currentTreeEntry->shapeIndexInObject)) {
          isObstacle = false;
        }
      }
    }
    if (isObstacle && (jrt::fast_cast<PolylineTrace>(currentItem) != nullptr) &&
        contactPins != nullptr) {
      // check for traces of foreign nets at tie pins, which will be ignored inside the pin shape
      TileShape* intersection = nullptr;
      for (Pin* currentContactPin : *contactPins) {
        if (currentContactPin->netCount() <= 1 || !currentContactPin->sharesNet(currentItem)) {
          continue;
        }
        if (intersection == nullptr) {
          TileShape* obstacleTraceShape =
              currentItem->getTileShape(currentTreeEntry->shapeIndexInObject);
          intersection = shape->intersection(obstacleTraceShape);
        }
        TileShape* pinShape = currentContactPin->getTileShapeOnLayer(layer);
        if (pinShape->containsApprox(intersection)) {
          isObstacle = false;
          break;
        }
      }
    }
    if (isObstacle) {
      return false;
    }
  }
  return true;
}

bool BasicBoard::checkPolylineTrace(Polyline* polyline, int32_t layer, int32_t penHalfWidth,
                                    jrt::Array<int32_t>* netNumbers,
                                    int32_t clearanceClassIndex) {
  Trace* tmpTrace = new (jrt::kOp) PolylineTrace(polyline, layer, penHalfWidth, netNumbers,
                                      clearanceClassIndex, 0, 0, FixedState::UNFIXED, this);
  jrt::Set<Pin*>* contactPins = tmpTrace->touchingPinsAtEndCorners();
  for (int32_t i = 0; i < tmpTrace->tileShapeCount(); i++) {
    if (!this->checkTraceShape(tmpTrace->getTileShape(i), layer, netNumbers, clearanceClassIndex,
                               contactPins)) {
      return false;
    }
  }
  return true;
}

int32_t BasicBoard::getLayerCount() { return layerStructure->layers->length; }

jrt::Set<Item*>* BasicBoard::pickItems(Point* location, int32_t layer,
                                       ItemSelectionFilter* filter) {
  TileShape* pointShape = TileShape::getInstance(location);
  jrt::DisposeOnExit<jrt::Collection<SearchTreeObject*>> overlapsOwner(overlappingObjects(pointShape, layer));
  jrt::Collection<SearchTreeObject*>* overlaps = overlapsOwner.get();
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  for (SearchTreeObject* currentObject : *overlaps) {
    if (auto* item = jrt::fast_cast<Item>(currentObject)) {
      result->add(item);
    }
  }
  if (filter != nullptr) {
    result = filter->filter(result);
  }
  return result;
}

bool BasicBoard::contains(Point* point) { return point->isContainedIn(boundingBox); }

int32_t BasicBoard::clearanceValue(int32_t class1, int32_t class2, int32_t layer) {
  if (rules == nullptr || rules->clearanceMatrix == nullptr) {
    return 0;
  }
  return rules->clearanceMatrix->getValue(class1, class2, layer, true);
}

IntBox* BasicBoard::getBoundingBox(jrt::Collection<Item*>* itemList) {
  IntBox* result = IntBox::EMPTY();
  for (Item* currentItem : *itemList) {
    result = result->union_(currentItem->boundingBox());
  }
  return result;
}

void BasicBoard::resetGraphicsUpdateBox() { updateBox = IntBox::EMPTY(); }

void BasicBoard::joinGraphicsUpdateBox(IntBox* box) {
  if (updateBox == nullptr) {
    resetGraphicsUpdateBox();
  }
  jrt::DomainScope domain(this);  // PORT NOTE (stage E): board-owned geometry
  updateBox = updateBox->union_(box);
}

void BasicBoard::startNotifyObservers() {
  if ((communication != nullptr) && (this->communication->observers != nullptr)) {
    communication->observers->activate();
  }
}

void BasicBoard::endNotifyObservers() {
  if ((communication != nullptr) && (this->communication->observers != nullptr)) {
    communication->observers->deactivate();
  }
}

bool BasicBoard::observersActive() {
  bool result;
  if ((communication != nullptr) && (this->communication->observers != nullptr)) {
    result = communication->observers->isActive();
  } else {
    result = false;
  }
  return result;
}

Connectable* BasicBoard::makeConductive(ObstacleArea* area, int32_t netNumber) {
  Item* newItem;
  Area* currentArea = area->getRelativeArea();
  int32_t layer = area->getLayer();
  FixedState fixedState = area->getFixedState();
  Vector* translation = area->getTranslation();
  double rotation = area->getRotationInDegree();
  bool sideChanged = area->getSideChanged();
  jrt::Array<int32_t>* netNumbers = new (jrt::kOp) jrt::Array<int32_t>(1);
  (*netNumbers)[0] = netNumber;
  newItem = new (jrt::kBoardData) ConductionArea(currentArea, layer, translation, rotation, sideChanged, netNumbers,
                               area->clearanceClassIndex(), 0, area->getComponentId(), area->name,
                               true, fixedState, this);
  removeItem(area);
  insertItem(newItem);
  return jrt::fast_cast<Connectable>(newItem);
}

void BasicBoard::insertItem(Item* item) {
  getItemRepository()->insertItem(item);
  if (auto* pin = jrt::fast_cast<Pin>(item)) {
    BoardOutline* outline = getOutline();
    if (outline != nullptr) {
      outline->invalidateEdgePinNets();
      if (outline->isEdgePin(pin)) {
        outline->updateSlicesToTraceWidths();
      }
    }
  } else if (jrt::fast_cast<BoardOutline>(item) != nullptr) {
    BoardOutline* outline = getOutline();
    if (outline != nullptr) {
      outline->invalidateEdgePinNets();
    }
  }
}

bool BasicBoard::undo(jrt::Set<int32_t>* changedNets) {
  this->components->undo(this->communication->observers);
  jrt::Collection<UndoableObjects_Storable*>* cancelledObjects =
      new (jrt::kOp) jrt::LinkedList<UndoableObjects_Storable*>();
  jrt::Collection<UndoableObjects_Storable*>* restoredObjects =
      new (jrt::kOp) jrt::LinkedList<UndoableObjects_Storable*>();
  bool result = itemList->undo(cancelledObjects, restoredObjects);
  applyUndoRedoSideEffects(cancelledObjects, restoredObjects, changedNets);
  return result;
}

bool BasicBoard::redo(jrt::Set<int32_t>* changedNets) {
  this->components->redo(this->communication->observers);
  jrt::Collection<UndoableObjects_Storable*>* cancelledObjects =
      new (jrt::kOp) jrt::LinkedList<UndoableObjects_Storable*>();
  jrt::Collection<UndoableObjects_Storable*>* restoredObjects =
      new (jrt::kOp) jrt::LinkedList<UndoableObjects_Storable*>();
  bool result = itemList->redo(cancelledObjects, restoredObjects);
  applyUndoRedoSideEffects(cancelledObjects, restoredObjects, changedNets);
  return result;
}

void BasicBoard::applyUndoRedoSideEffects(
    jrt::Collection<UndoableObjects_Storable*>* cancelledObjects,
    jrt::Collection<UndoableObjects_Storable*>* restoredObjects, jrt::Set<int32_t>* changedNets) {
  jrt::Iterator<UndoableObjects_Storable*>* it = cancelledObjects->iterator();
  while (it->hasNext()) {
    Item* currentItem = jrt::fast_cast<Item>(it->next());
    searchTreeManager->remove(currentItem);
    routingLedger()->noteRemoved(currentItem);
    if ((communication != nullptr) && (communication->observers != nullptr)) {
      communication->observers->notifyDeleted(currentItem);
    }
    if (changedNets != nullptr) {
      for (int32_t i = 0; i < currentItem->netCount(); i++) {
        changedNets->add(currentItem->getNetNumber(i));
      }
    }
  }
  it = restoredObjects->iterator();
  while (it->hasNext()) {
    Item* currentItem = jrt::fast_cast<Item>(it->next());
    currentItem->board = this;
    searchTreeManager->insert(currentItem);
    routingLedger()->noteInserted(currentItem);
    currentItem->clearAutorouteInfo();
    if ((communication != nullptr) && (communication->observers != nullptr)) {
      communication->observers->notifyNew(currentItem);
    }
    if (changedNets != nullptr) {
      for (int32_t i = 0; i < currentItem->netCount(); i++) {
        changedNets->add(currentItem->getNetNumber(i));
      }
    }
  }
  invalidateEdgePinNetCache();
}

void BasicBoard::generateSnapshot() { getSnapshotManager()->generateSnapshot(); }

bool BasicBoard::popSnapshot() { return getSnapshotManager()->popSnapshot(); }

Trace* BasicBoard::getTraceTail(Point* location, int32_t layer, jrt::Array<int32_t>* netNumbers) {
  TileShape* pointShape = TileShape::getInstance(location);
  jrt::DisposeOnExit<jrt::Collection<SearchTreeObject*>> foundItemsOwner(overlappingObjects(pointShape, layer));
  jrt::Collection<SearchTreeObject*>* foundItems = foundItemsOwner.get();
  for (SearchTreeObject* currentObject : *foundItems) {
    if (auto* currentTrace = jrt::fast_cast<Trace>(currentObject)) {
      if (!currentTrace->netsEqual(netNumbers) || currentTrace->hasInteriorContacts()) {
        continue;
      }
      if (currentTrace->firstCorner()->equals(location)) {
        jrt::DisposeOnExit<jrt::Collection<Item*>> contactsOwner(currentTrace->getStartContacts());
        jrt::Collection<Item*>* contacts = contactsOwner.get();
        if (contacts->isEmpty()) {
          return currentTrace;
        }
      }
      if (currentTrace->lastCorner()->equals(location)) {
        jrt::DisposeOnExit<jrt::Collection<Item*>> contactsOwner(currentTrace->getEndContacts());
        jrt::Collection<Item*>* contacts = contactsOwner.get();
        if (contacts->isEmpty()) {
          return currentTrace;
        }
      }
    }
  }
  return nullptr;
}

bool BasicBoard::removeIfCycle(Trace* trace) {
  if (!trace->isOnTheBoard() || trace->isDeletionForbidden()) {
    return false;
  }
  if (!trace->isCycle()) {
    return false;
  }
  jrt::Set<Item*>* connectionItems = trace->getConnectionItems();
  if (!connectionItems->contains(trace)) {
    return false;
  }
  keepJunctionVias(connectionItems);
  // Remove tails at the endpoints after removing the cycle, if there was no tail before.
  int32_t currentLayer = trace->getLayer();
  jrt::Array<int32_t>* currentNetNumbers = trace->netNumbers;
  jrt::Array<Point*>* endCorners = new (jrt::kGeom) jrt::Array<Point*>(2);
  (*endCorners)[0] = trace->firstCorner();
  (*endCorners)[1] = trace->lastCorner();
  jrt::Array<bool>* tailAtEndpointBefore = new (jrt::kOp) jrt::Array<bool>(2);
  for (int32_t i = 0; i < 2; i++) {
    Trace* tail = getTraceTail((*endCorners)[i], currentLayer, currentNetNumbers);
    (*tailAtEndpointBefore)[i] = tail != nullptr;
  }
  this->removeItems(connectionItems);
  for (int32_t i = 0; i < 2; i++) {
    if (!(*tailAtEndpointBefore)[i]) {
      Trace* tail = getTraceTail((*endCorners)[i], currentLayer, currentNetNumbers);
      if (tail != nullptr) {
        jrt::Set<Item*>* tailItems = tail->getConnectionItems();
        keepJunctionVias(tailItems);
        removeItems(tailItems);
      }
    }
  }
  return true;
}

void BasicBoard::keepJunctionVias(jrt::Set<Item*>* connectionItems) {
  // Java: Set.copyOf(connectionItems) (an immutable copy, only used for contains).
  jrt::Set<Item*>* walked = new (jrt::kOp) jrt::HashSet<Item*>(connectionItems);
  connectionItems->removeIf([walked](Item* item) {
    auto* via = jrt::fast_cast<Via>(item);
    return via != nullptr && touchesItemOutside(via, walked);
  });
}

bool BasicBoard::touchesItemOutside(Via* via, jrt::Set<Item*>* items) {
  jrt::DisposeOnExit loopSource1(via->getNormalContacts());
  for (Item* contact : *loopSource1) {
    if (!items->contains(contact)) {
      return true;
    }
  }
  return false;
}

BoardItemRepository* BasicBoard::getItemRepository() {
  if (itemRepository == nullptr) {
    itemRepository = new (jrt::kBoard) BoardItemRepository(this);
  }
  return itemRepository;
}

BoardConnectivityQueries* BasicBoard::getConnectivityQueries() {
  if (connectivityQueries == nullptr) {
    connectivityQueries = new (jrt::kBoard) BoardConnectivityQueries(this);
  }
  return connectivityQueries;
}

BoardSnapshotManager* BasicBoard::getSnapshotManager() {
  if (snapshotManager == nullptr) {
    snapshotManager = new (jrt::kBoard) BoardSnapshotManager(this);
  }
  return snapshotManager;
}

void BasicBoard::afterDeserialize() {
  // Java readObject after defaultReadObject().
  if (this->rules != nullptr && this->rules->getMinHoleToHole() <= 0 &&
      this->communication != nullptr) {
    this->rules->setMinHoleToHole(jrt::l2i(jrt::Math::round(
        250.0 * jrt::Math::max(1, this->communication->resolution) /
        Unit_scale(1.0, this->communication->unit, Unit::UM))));
  }
  // insert the items on the board into the search trees
  searchTreeManager = new (jrt::kBoard) SearchTreeManager(this);
  normalizeSuppressedNetNos = new (jrt::kBoard) jrt::HashSet<int32_t>();
  itemRepository = nullptr;
  connectivityQueries = nullptr;
  snapshotManager = nullptr;
  jrt::DisposeOnExit loopSource2(this->getItems());
  for (Item* currentItem : *loopSource2) {
    currentItem->board = this;
    searchTreeManager->insert(currentItem);
  }
}

void BasicBoard::copyItemListFrom(BasicBoard* source,
                                  jrt::IdentityHashMap<Item*, Item*>* itemMap) {
  BasicBoard* self = this;
  auto* memo = new (jrt::kOp) jrt::IdentityHashMap<UndoableObjects_Storable*, UndoableObjects_Storable*>();
  auto mapObject = [self, memo, itemMap](UndoableObjects_Storable* original)
      -> UndoableObjects_Storable* {
    if (original == nullptr) {
      return nullptr;
    }
    UndoableObjects_Storable* known = memo->get(original);
    if (known != nullptr) {
      return known;
    }
    auto* originalItem = jrt::fast_cast<Item>(original);
    if (originalItem == nullptr) {
      // Not an item (does not occur on a board): plain clone like Java serialization would copy it.
      auto* copied = jrt::fast_cast<UndoableObjects_Storable>(original->clone());
      memo->put(original, copied);
      return copied;
    }
    // Item.clone(): copy(getId()) with onTheBoard copied.
    auto* copiedItem = jrt::fast_cast<Item>(originalItem->clone());
    // PORT NOTE: non-transient fields Java serialization keeps but copy(id) does not carry.
    // (Pin.changedTo is not remapped: it only changes by a GUI pin swap, out of scope.)
    copiedItem->smallestClearance = originalItem->smallestClearance;
    if (auto* originalArea = jrt::fast_cast<ConductionArea>(originalItem)) {
      auto* copiedArea = jrt::fast_cast<ConductionArea>(copiedItem);
      if (copiedArea != nullptr && copiedArea->getIsFilled() != originalArea->getIsFilled()) {
        copiedArea->setIsFilled(originalArea->getIsFilled());
      }
    }
    if (auto* originalOutline = jrt::fast_cast<BoardOutline>(originalItem)) {
      auto* copiedOutline = jrt::fast_cast<BoardOutline>(copiedItem);
      if (copiedOutline != nullptr) {
        copiedOutline->edgePinNetsCrossOutline = originalOutline->edgePinNetsCrossOutline;
        copiedItem->board = self;  // searchTreeManager is still null: no tree reinsertion
        copiedOutline->generateKeepoutOutside(originalOutline->keepoutOutsideOutlineGenerated());
      }
    }
    copiedItem->board = self;
    memo->put(original, copiedItem);
    if (itemMap != nullptr) {
      itemMap->put(originalItem, copiedItem);
    }
    return copiedItem;
  };
  this->itemList = source->itemList->copyMapped(mapObject);
  watchItemList();
  // Second pass, now that this->itemList exists (setClearanceClassIndex notes a content change on
  // the board's item list): restore what copy(id) of some classes drops.
  for (auto* entry : *memo->entrySet()) {
    auto* originalItem = jrt::fast_cast<Item>(entry->getKey());
    auto* copiedItem = jrt::fast_cast<Item>(entry->getValue());
    if (originalItem == nullptr || copiedItem == nullptr) {
      continue;
    }
    if (jrt::fast_cast<ComponentObstacleArea>(originalItem) != nullptr ||
        jrt::fast_cast<ComponentOutline>(originalItem) != nullptr) {
      // copy drops the net numbers (and ComponentOutline also the clearance class).
      copiedItem->netNumbers =
          originalItem->netNumbers != nullptr ? originalItem->netNumbers->clone() : nullptr;
      if (copiedItem->clearanceClassIndex() != originalItem->clearanceClassIndex()) {
        copiedItem->setClearanceClassIndex(originalItem->clearanceClassIndex());
      }
    }
  }
  // PORT NOTE: not restorable from this bucket (private fields without setters in the item
  // headers): Pin.changedTo (Java points to the copy of the swapped pin; only a GUI pin swap
  // changes it), DrillItem.precalculatedMinWidth/FirstLayer/LastLayer (recomputed from the shared
  // padstack on use; a stale precalculatedMinWidth of the source is not carried over) and the
  // cached DrillItem center of a Pin (recomputed lazily from the deep-copied component).
  // Via.padstack stays the source's object, which is correct because the library is shared.
  jrt::disposeMapAndEntries(memo);  // stage E: copy temporary (keys are the source's items)
}

int64_t BasicBoard::copiesMade() { return gCopiesMade; }

namespace {
int64_t gGraveyardFlushes = 0, gGraveyardSkips = 0, gDeadItemsFreed = 0, gDeadShapesFreed = 0;
int64_t gDeadItemsNoted = 0;
}  // namespace

std::string BasicBoard::graveyardStats() {
  return "graveyard: " + jrt::str(gGraveyardFlushes) + " flushes, " + jrt::str(gGraveyardSkips) +
         " skipped (live engine), items noted " + jrt::str(gDeadItemsNoted) + " / freed " +
         jrt::str(gDeadItemsFreed) + ", tree shapes freed " + jrt::str(gDeadShapesFreed);
}
int64_t BasicBoard::copiesDiscarded() { return gCopiesDiscarded; }

bool BasicBoard::discardEnabled(const char* site) {
  // Diagnostics: FR_NO_BOARD_DISCARD=all, or a list of drop sites (optimizer, candidate, replay,
  // history, autoroute, fallback, other) keeps those boards (they leak as before stage E7).
  static const std::string off = [] {
    const char* e = std::getenv("FR_NO_BOARD_DISCARD");
    return std::string(e != nullptr ? e : "");
  }();
  return off.empty() || (off != "all" && off.find(site) == std::string::npos);
}

void BasicBoard::discard(BasicBoard* board, const char* site) {
  if (board == nullptr || !discardEnabled(site)) {
    return;
  }
  ++gCopiesDiscarded;
  board->releaseOwnedData();
  jrt::dispose(board);
}

void BasicBoard::watchItemList() {
  itemList->setDeadObjectListener([this](UndoableObjects_Storable* object) { noteDeadObject(object); });
}

void BasicBoard::noteDeadObject(UndoableObjects_Storable* object) {
  if (auto* item = jrt::fast_cast<Item>(object)) {
    deadItems_.push_back(item);
    ++gDeadItemsNoted;
  }
}

void BasicBoard::retainTreeShapes(jrt::Array<TileShape*>* shapes) {
  for (TileShape* s : *shapes) {
    if (s != nullptr) ++treeShapeRefs_[s];
  }
}

void BasicBoard::releaseTreeShapes(jrt::Array<TileShape*>* shapes) {
  for (TileShape* s : *shapes) {
    if (s == nullptr) continue;
    auto it = treeShapeRefs_.find(s);
    if (it == treeShapeRefs_.end()) continue;
    if (--it->second == 0) {
      treeShapeRefs_.erase(it);
      deadTreeShapes_.push_back(s);
    }
  }
  deadShapeArrays_.push_back(shapes);
}

void BasicBoard::retainLeafShape(RegularTileShape* shape) {
  auto it = treeShapeRefs_.find(static_cast<TileShape*>(shape));
  if (it != treeShapeRefs_.end()) ++it->second;
}

void BasicBoard::releaseLeafShape(RegularTileShape* shape) {
  auto* s = static_cast<TileShape*>(shape);
  auto it = treeShapeRefs_.find(s);
  if (it == treeShapeRefs_.end()) return;
  if (--it->second == 0) {
    treeShapeRefs_.erase(it);
    deadTreeShapes_.push_back(s);
  }
}

void BasicBoard::releaseGraveyard() {
  if (graveyardSafe()) {
    ++gGraveyardFlushes;
    releaseGraveyardNow();
  } else {
    ++gGraveyardSkips;
  }
}

void BasicBoard::releaseGraveyardNow() {
  // Items first: leaving the trees releases their tree shapes into the graveyard too.
  std::vector<Item*> items;
  items.swap(deadItems_);
  std::unordered_set<Item*> seenItems;
  for (Item* item : items) {
    if (!seenItems.insert(item).second) continue;
    item->clearSearchTreeEntries();
    jrt::dispose(item->netNumbers);  // every item (and undo version) has its own array
    jrt::dispose(item);
    ++gDeadItemsFreed;
  }
  std::unordered_set<TileShape*> seenShapes;
  for (TileShape* s : deadTreeShapes_) {
    // a shape that an array took again since it was dropped is still in use
    if (treeShapeRefs_.count(s) == 0 && seenShapes.insert(s).second) {
      jrt::dispose(s);
      ++gDeadShapesFreed;
    }
  }
  deadTreeShapes_.clear();
  std::unordered_set<jrt::Array<TileShape*>*> seenArrays;
  for (auto* a : deadShapeArrays_) {
    if (seenArrays.insert(a).second) jrt::dispose(a);
  }
  deadShapeArrays_.clear();
}

void BasicBoard::releaseOwnedData() {
  releaseGraveyardNow();  // the board dies: nothing outside it uses its removed items any more
  BoardStatisticsBoundsCalculator::forget(this);
  // The copy's rules are not freed yet: drop their back link to this board.
  if (rules != nullptr && rules->nets != nullptr && rules->nets->getBoard() == this) {
    rules->nets->setBoard(nullptr);
  }
  // Trees first: their leaves are referenced by the items' leaf arrays, never read again.
  if (searchTreeManager != nullptr) {
    searchTreeManager->disposeTrees();
    jrt::dispose(searchTreeManager);
    searchTreeManager = nullptr;
  }
  jrt::dispose(itemRepository);  // with its id map
  itemRepository = nullptr;
  if (normalContactCache != nullptr) {
    normalContactCache->forEach([](Item*, jrt::Set<Item*>* contacts) { jrt::dispose(contacts); });
    jrt::disposeMapAndEntries(normalContactCache);
    normalContactCache = nullptr;
  }
  if (itemList != nullptr) {
    // Every item of the list is this board's own (copies clone them; undo versions are clones
    // too). Their geometry, net-number arrays and padstacks may be shared and are kept.
    itemList->disposeAll([](UndoableObjects_Storable* object) {
      if (auto* item = jrt::fast_cast<Item>(object)) {
        item->clearSearchTreeEntries();
        jrt::dispose(item->netNumbers);  // every item (and undo version) has its own array
      }
      jrt::dispose(object);
    });
    jrt::dispose(itemList);
    itemList = nullptr;
  }
  releaseGraveyardNow();  // tree shapes and arrays the items' search-tree info released above
  if (components != nullptr) {
    components->disposeAll();
    jrt::dispose(components);
    components = nullptr;
  }
}

BasicBoard* BasicBoard::copyAsDeserialized(jrt::IdentityHashMap<Item*, Item*>* itemMap) {
  BasicBoard* copy = new (jrt::kBoard) BasicBoard(BasicBoard_CopyTag{}, this);
  copy->copyItemListFrom(this, itemMap);
  copy->afterDeserialize();
  return copy;
}

void BasicBoard::deleteAllTracksAndVias() {
  auto itScope = itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    UndoableObjects_Storable* currentItem = itemList->readObject(it);
    if (currentItem == nullptr) {
      break;
    }
    if (jrt::fast_cast<Trace>(currentItem) != nullptr) {
      // delete the trace
      itemList->delete_(currentItem);
    }
    if (jrt::fast_cast<Via>(currentItem) != nullptr) {
      // delete the via
      itemList->delete_(currentItem);
    }
  }
}

void BasicBoard::unfillConductionAreas() {
  this->rules->setIgnoreConduction(true);
  auto itScope = itemList->readScope();
  jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
  for (;;) {
    UndoableObjects_Storable* currentItem = itemList->readObject(it);
    if (currentItem == nullptr) {
      break;
    }
    if (auto* currentConductionArea = jrt::fast_cast<ConductionArea>(currentItem)) {
      currentConductionArea->setIsFilled(false);
      currentConductionArea->setIsObstacle(false);
    }
  }
  this->searchTreeManager->reinsertTreeItems();
}

void BasicBoard::areThereItemsOnInactiveLayer(AutorouteControl* ctrl) {
  if (this->getLayerCount() > 2) {
    bool hasSomethingOnInactiveLayer = false;
    auto itScope = this->itemList->readScope();
    jrt::Iterator<UndoableObjects_UndoableObjectNode*>* it = &itScope;
    for (;;) {
      UndoableObjects_Storable* currentObject = this->itemList->readObject(it);
      if (currentObject == nullptr) {
        break;
      }
      if (auto* currentItem = jrt::fast_cast<PolylineTrace>(currentObject)) {
        // This is a connectable item, like PolylineTrace or Pin
        if (!(*ctrl->layerActive)[currentItem->getLayer()]) {
          hasSomethingOnInactiveLayer = true;
          FRLogger::warn("There is an item on an inactive layer.");
          break;
        }
      }
    }
    (void)hasSomethingOnInactiveLayer;
  }
}

int32_t BasicBoard::getNon45DegreeTraceCount() {
  int32_t count = 0;
  jrt::DisposeOnExit loopSource3(getItems());
  for (Item* currentObject : *loopSource3) {
    if (auto* currentTrace = jrt::fast_cast<PolylineTrace>(currentObject)) {
      if (!currentTrace->polyline()->isMultipleOf45Degree()) {
        ++count;
      }
    }
  }
  return count;
}

}  // namespace fr
