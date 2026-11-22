#include "autoroute/pipeline/ExitStubs.h"

#include <algorithm>
#include <vector>

#include "board/facade/BasicBoard.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "core/library/Padstack.h"
#include "drc/NetIncompletes.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "rules/PinClearanceSource.h"
#include "settings/AutorouterSettings.h"
#include "settings/RouterSettings.h"

namespace fr {

static_assert(ExitStubs::TIGHT_SLACK == 2 * ClearanceMatrix::clearance_safety_margin,
              "TIGHT_SLACK must equal 2 * ClearanceMatrix.clearance_safety_margin");

// ---------------------------------------------------------------------------------------------
// Records
// ---------------------------------------------------------------------------------------------

bool ExitStubs_Mirror::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<ExitStubs_Mirror>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(bend_, other->bend_) && jrt::equalsOf(end_, other->end_);
}

int32_t ExitStubs_Mirror::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(bend_));
  h = jrt::hashCombine(h, jrt::hashOf(end_));
  return h;
}

std::string ExitStubs_Mirror::toString() {
  return "Mirror[bend=" + jrt::str(bend_) + ", end=" + jrt::str(end_) + "]";
}

jrt::Array<Point*>* ExitStubs_Stub::corners(IntPoint* centre) {
  return bend_ != nullptr ? jrt::Array<Point*>::of({centre, bend_, end_}) : jrt::Array<Point*>::of({centre, end_});
}

bool ExitStubs_Stub::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<ExitStubs_Stub>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(pin_, other->pin_) && jrt::equalsOf(end_, other->end_) && halfWidth_ == other->halfWidth_ &&
         inward_ == other->inward_ && jrt::equalsOf(slack_, other->slack_) && jrt::equalsOf(bend_, other->bend_) &&
         jrt::equalsOf(mirror_, other->mirror_);
}

int32_t ExitStubs_Stub::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(pin_));
  h = jrt::hashCombine(h, jrt::hashOf(end_));
  h = jrt::hashCombine(h, jrt::hashOf(halfWidth_));
  h = jrt::hashCombine(h, jrt::hashOf(inward_));
  h = jrt::hashCombine(h, jrt::hashOf(slack_));
  h = jrt::hashCombine(h, jrt::hashOf(bend_));
  h = jrt::hashCombine(h, jrt::hashOf(mirror_));
  return h;
}

std::string ExitStubs_Stub::toString() {
  return "Stub[pin=" + jrt::str(static_cast<jrt::Object*>(pin_)) + ", end=" + jrt::str(end_) +
         ", halfWidth=" + jrt::str(halfWidth_) + ", inward=" + jrt::str(inward_) + ", slack=" + jrt::str(slack_) +
         ", bend=" + jrt::str(bend_) + ", mirror=" + jrt::str(mirror_) + "]";
}

bool ExitStubs_StubPlacement::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<ExitStubs_StubPlacement>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(stub_, other->stub_) && jrt::equalsOf(traces_, other->traces_);
}

int32_t ExitStubs_StubPlacement::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(stub_));
  h = jrt::hashCombine(h, jrt::hashOf(traces_));
  return h;
}

std::string ExitStubs_StubPlacement::toString() {
  return "StubPlacement[stub=" + jrt::str(stub_) + ", traces=" + jrt::str(traces_) + "]";
}

bool ExitStubs_RowEntry::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<ExitStubs_RowEntry>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(stub_, other->stub_) && jrt::equalsOf(centre_, other->centre_) && lateral_ == other->lateral_;
}

int32_t ExitStubs_RowEntry::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(stub_));
  h = jrt::hashCombine(h, jrt::hashOf(centre_));
  h = jrt::hashCombine(h, jrt::hashOf(lateral_));
  return h;
}

std::string ExitStubs_RowEntry::toString() {
  return "RowEntry[stub=" + jrt::str(stub_) + ", centre=" + jrt::str(centre_) + ", lateral=" + jrt::str(lateral_) +
         "]";
}

bool ExitStubs_DiagEntry::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<ExitStubs_DiagEntry>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(stub_, other->stub_) && jrt::equalsOf(centre_, other->centre_) &&
         jrt::equalsOf(lateral_, other->lateral_);
}

int32_t ExitStubs_DiagEntry::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(stub_));
  h = jrt::hashCombine(h, jrt::hashOf(centre_));
  h = jrt::hashCombine(h, jrt::hashOf(lateral_));
  return h;
}

std::string ExitStubs_DiagEntry::toString() {
  return "DiagEntry[stub=" + jrt::str(stub_) + ", centre=" + jrt::str(centre_) + ", lateral=" + jrt::str(lateral_) +
         "]";
}

ExitStubs_DiagRow::ExitStubs_DiagRow(int32_t dx, int32_t dy, double line)
    : dx(dx), dy(dy), line(line), row(new jrt::ArrayList<ExitStubs_DiagEntry*>()) {}

bool ExitStubs_JointEntry::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<ExitStubs_JointEntry>(o);
  if (other == nullptr) return false;
  return jrt::equalsOf(trace_, other->trace_) && jrt::equalsOf(points_, other->points_);
}

int32_t ExitStubs_JointEntry::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(trace_));
  h = jrt::hashCombine(h, jrt::hashOf(points_));
  return h;
}

std::string ExitStubs_JointEntry::toString() {
  return "JointEntry[trace=" + jrt::str(static_cast<jrt::Object*>(trace_)) + ", points=" + jrt::str(points_) + "]";
}

// ---------------------------------------------------------------------------------------------
// ExitStubs
// ---------------------------------------------------------------------------------------------

std::regex& ExitStubs::KICAD_PAD_SIZE() {
  static std::regex* v = new std::regex("Pad_([0-9]+(?:\\.[0-9]+)?)x([0-9]+(?:\\.[0-9]+)?)_");
  return *v;
}

std::regex& ExitStubs::TOLERANCE_COPY() {
  static std::regex* v = new std::regex("^(.*)#tolerance[0-9]+$");
  return *v;
}

namespace {
/** board.getItems().stream().map(Item::getId).collect(Collectors.toSet()) */
jrt::Set<int32_t>* itemIdSet(BasicBoard* board) {
  jrt::Set<int32_t>* result = new jrt::HashSet<int32_t>();
  for (Item* item : *board->getItems()) {
    result->add(item->getId());
  }
  return result;
}
}  // namespace

std::optional<std::string> ExitStubs::apply(RouterSettings* settings, RoutingBoard* board,
                                            jrt::List<int32_t>* itemIds) {
  if (settings->autorouter == nullptr || !(settings->autorouter->exitStubs == true)) {
    return std::nullopt;
  }
  jrt::Set<int32_t>* before = itemIdSet(board);
  jrt::List<ExitStubs_Stub*>* stubs = plan(board, settings);
  int32_t allowance = copperAllowance(board, settings);
  int32_t placed = 0;
  int32_t inward = 0;
  int32_t fanned = 0;
  jrt::Set<int32_t>* pins = new jrt::HashSet<int32_t>();
  jrt::Map<std::string, int32_t>* parts = new jrt::TreeMap<std::string, int32_t>();
  jrt::List<ExitStubs_StubPlacement*>* inserted = new jrt::ArrayList<ExitStubs_StubPlacement*>();

  for (ExitStubs_Stub* planned : *stubs) {
    ExitStubs_Stub* stub = planned;
    int32_t net = stub->pin_->getNetNumber(0);
    int32_t layer = stub->pin_->firstLayer();
    int32_t traceClass = board->rules->nets->get(net)->getNetClass()->getTraceClearanceClass();
    IntPoint* centre = ExitStubs::centre(stub->pin_);
    if (pathSlack(board, stub->corners(centre), layer, stub->halfWidth_, net, traceClass, allowance) < 0) {
      if (stub->mirrored() == nullptr ||
          pathSlack(board, stub->mirrored()->corners(centre), layer, stub->mirrored()->halfWidth_, net, traceClass,
                    allowance) < 0) {
        continue;
      }
      stub = stub->mirrored();
    }
    jrt::Set<int32_t>* beforeStub = itemIdSet(board);
    board->insertTrace(new Polyline(stub->corners(centre)), layer, stub->halfWidth_, jrt::Array<int32_t>::of({net}),
                       traceClass, PLACED_STATE);
    jrt::List<Item*>* traces = new jrt::ArrayList<Item*>();
    for (Item* i : *board->getItems()) {
      if (jrt::fast_cast<Trace>(i) != nullptr && !beforeStub->contains(i->getId())) {
        traces->add(i);
      }
    }
    inserted->add(new ExitStubs_StubPlacement(stub, traces));
  }

  int32_t unreachable = 0;
  for (bool removed = true; removed;) {
    removed = false;
    for (ExitStubs_StubPlacement* entry : *new jrt::ArrayList<ExitStubs_StubPlacement*>(inserted)) {
      if (entry->stub_->inward_ || endReachable(board, entry->stub_)) {
        continue;
      }
      for (Item* trace : *entry->traces_) {
        if (trace->isOnTheBoard()) {
          trace->setFixedState(FixedState::UNFIXED);
          board->removeItem(trace);
        }
      }
      inserted->remove(entry);
      unreachable++;
      removed = true;
    }
  }

  for (ExitStubs_StubPlacement* entry : *inserted) {
    placed++;
    pins->add(entry->stub_->pin_->getId());
    if (entry->stub_->inward_) {
      inward++;
    }
    if (entry->stub_->bend_ != nullptr) {
      fanned++;
    }
    std::string part = board->components->get(entry->stub_->pin_->getComponentId()) != nullptr
                           ? board->components->get(entry->stub_->pin_->getComponentId())->name
                           : jrt::Integer::toString(entry->stub_->pin_->getComponentId());
    parts->put(part, parts->getOrDefault(part, 0) + 1);
  }

  std::vector<int32_t> newIdVec;
  for (Item* i : *board->getItems()) {
    if (jrt::fast_cast<Trace>(i) != nullptr && i->getFixedState() == PLACED_STATE && !before->contains(i->getId())) {
      newIdVec.push_back(i->getId());
    }
  }
  std::sort(newIdVec.begin(), newIdVec.end());
  for (int32_t id : newIdVec) {
    itemIds->add(id);
  }

  if (placed == 0) {
    return unreachable > 0 ? std::optional<std::string>("Exit stubs: none placed; the ends of all " +
                                                        jrt::str(unreachable) +
                                                        " planned were out of the router's reach.")
                           : std::nullopt;
  }
  std::string sb;
  sb += "Exit stubs: ";
  sb += jrt::str(pins->size());
  sb += " fine-pitch pins whose way out meets the clearances exactly get ";
  sb += jrt::str(placed);
  sb += " stubs (";
  sb += jrt::str(inward);
  sb += " inward to a pad of their net, ";
  sb += jrt::str(fanned);
  sb += " bent in a fan; ";
  int32_t partIdx = 0;
  for (auto* p : *parts->entrySet()) {
    if (partIdx > 0) sb += ", ";
    if (partIdx == 6) {
      sb += "...";
      break;
    }
    sb += p->getKey();
    sb += " ";
    sb += jrt::str(p->getValue());
    partIdx++;
  }
  if (unreachable > 0) {
    sb += "; ";
    sb += jrt::str(unreachable);
    sb += " left out, their ends out of the router's reach";
  }
  sb += "), router.autorouter.exit_stubs=true.";
  return sb;
}

bool ExitStubs::endReachable(RoutingBoard* board, ExitStubs_Stub* stub) {
  int32_t net = stub->pin_->getNetNumber(0);
  int32_t layer = stub->pin_->firstLayer();
  int32_t traceClass = board->rules->nets->get(net)->getNetClass()->getTraceClearanceClass();
  IntPoint* from = stub->bend_ != nullptr ? stub->bend_ : centre(stub->pin_);
  int32_t dx = jrt::Integer::signum(stub->end_->x - from->x);
  int32_t dy = jrt::Integer::signum(stub->end_->y - from->y);
  int32_t length = endProbeLength(board, stub->end_, layer, stub->halfWidth_, net, traceClass);

  int32_t dirs[3][2] = {{dx, dy},
                        {jrt::Integer::signum(dx - dy), jrt::Integer::signum(dx + dy)},
                        {jrt::Integer::signum(dx + dy), jrt::Integer::signum(dy - dx)}};
  for (auto& d : dirs) {
    Polyline* path = new Polyline(jrt::Array<Point*>::of(
        {stub->end_, new IntPoint(stub->end_->x + d[0] * length, stub->end_->y + d[1] * length)}));
    if (board->checkPolylineTrace(path, layer, stub->halfWidth_, jrt::Array<int32_t>::of({net}), traceClass)) {
      return true;
    }
  }
  return false;
}

int32_t ExitStubs::endProbeLength(BasicBoard* board, IntPoint* end, int32_t layer, int32_t half, int32_t net,
                                  int32_t traceClass) {
  ClearanceMatrix* matrix = board->rules->clearanceMatrix;
  int32_t margin = ClearanceMatrix::clearance_safety_margin;
  int32_t length = half + matrix->getValue(traceClass, traceClass, layer, false) + margin;
  int32_t widest = half + matrix->maxValue(traceClass, layer) + margin;
  if (widest <= length) {
    return length;
  }
  int32_t reach = length;
  int32_t search = widest + reach;
  IntBox* area = new IntBox(end->x - search, end->y - search, end->x + search, end->y + search);
  FloatPoint* at = end->toFloat();
  for (SearchTreeObject* entry : *board->overlappingObjects(area, layer)) {
    auto* item = jrt::fast_cast<Item>(entry);
    if (item == nullptr || !item->isTraceObstacle(net)) {
      continue;
    }
    int32_t needed = half + matrix->getValue(item->clearanceClassIndex(), traceClass, layer, false) + margin;
    if (needed <= length) {
      continue;
    }
    for (int32_t i = 0; i < item->tileShapeCount(); i++) {
      if (item->shapeLayer(i) == layer && item->isTraceObstacle(net, i) && item->getTileShape(i) != nullptr &&
          !item->getTileShape(i)->isEmpty() && segmentDistance(at, at, item->getTileShape(i)) <= needed + reach) {
        length = needed;
        break;
      }
    }
  }
  return length;
}

jrt::List<ExitStubs_Stub*>* ExitStubs::plan(BasicBoard* board, RouterSettings* settings) {
  jrt::Map<int32_t, int32_t>* pinsOfNet = new jrt::HashMap<int32_t, int32_t>();
  for (Pin* pin : *board->getPins()) {
    for (int32_t i = 0; i < pin->netCount(); i++) {
      int32_t n = pin->getNetNumber(i);
      pinsOfNet->put(n, pinsOfNet->getOrDefault(n, 0) + 1);
    }
  }
  int32_t allowance = copperAllowance(board, settings);
  jrt::List<ExitStubs_Stub*>* result = new jrt::ArrayList<ExitStubs_Stub*>();
  for (int32_t id = 1; id <= board->components->count(); id++) {
    jrt::List<Pin*>* pins = new jrt::ArrayList<Pin*>(board->getComponentPins(id));
    if (pins->size() < MIN_PINS) {
      continue;
    }
    // PORT NOTE: DoubleStream.average() sums with Kahan compensation; the summands are int
    // coordinates, whose double sum is exact, so a plain sum gives the same value.
    double sumX = 0.0;
    double sumY = 0.0;
    for (Pin* p : *pins) {
      sumX += centre(p)->x;
    }
    for (Pin* p : *pins) {
      sumY += centre(p)->y;
    }
    double cx = pins->isEmpty() ? 0.0 : sumX / pins->size();
    double cy = pins->isEmpty() ? 0.0 : sumY / pins->size();
    jrt::List<ExitStubs_Stub*>* part = new jrt::ArrayList<ExitStubs_Stub*>();
    jrt::List<Pin*>* sortedPins = new jrt::ArrayList<Pin*>(pins);
    sortedPins->sort([](Pin* a, Pin* b) { return jrt::Integer::compare(a->getId(), b->getId()); });
    for (Pin* pin : *sortedPins) {
      part->addAll(candidates(board, settings, pin, pins, pinsOfNet, cx, cy, allowance));
    }
    result->addAll(fans(board, part, allowance));
  }
  return result;
}

jrt::List<ExitStubs_Stub*>* ExitStubs::plan(BasicBoard* board) { return plan(board, nullptr); }

jrt::List<ExitStubs_Stub*>* ExitStubs::fans(BasicBoard* board, jrt::List<ExitStubs_Stub*>* stubs,
                                            int32_t allowance) {
  ClearanceMatrix* matrix = board->rules->clearanceMatrix;
  int32_t margin = ClearanceMatrix::clearance_safety_margin;
  jrt::Map<ExitStubs_RowKey*, jrt::List<ExitStubs_RowEntry*>*>* rows =
      new jrt::HashMap<ExitStubs_RowKey*, jrt::List<ExitStubs_RowEntry*>*>();
  jrt::Set<int32_t>* inwardPins = new jrt::HashSet<int32_t>();
  for (ExitStubs_Stub* s : *stubs) {
    if (s->inward_) {
      inwardPins->add(s->pin_->getId());
    }
  }
  jrt::List<ExitStubs_Stub*>* result = new jrt::ArrayList<ExitStubs_Stub*>();
  jrt::List<ExitStubs_Stub*>* diagonal = new jrt::ArrayList<ExitStubs_Stub*>();

  for (ExitStubs_Stub* stub : *stubs) {
    if (stub->inward_ || stub->bend_ != nullptr) {
      result->add(stub);
      continue;
    }
    IntPoint* centre = ExitStubs::centre(stub->pin_);
    int32_t dx = jrt::Integer::signum(stub->end_->x - centre->x);
    int32_t dy = jrt::Integer::signum(stub->end_->y - centre->y);
    if (dx != 0 && dy != 0) {
      diagonal->add(stub);
      continue;
    }
    IntBox* box = stub->pin_->getTileShape(0)->boundingBox();
    int32_t line = dx != 0 ? (dx > 0 ? box->ur->x : box->ll->x) : (dy > 0 ? box->ur->y : box->ll->y);
    ExitStubs_RowKey* key = new ExitStubs_RowKey(dx, dy, line);
    rows->computeIfAbsent(key,
                          [](ExitStubs_RowKey* const&) -> jrt::List<ExitStubs_RowEntry*>* {
                            return new jrt::ArrayList<ExitStubs_RowEntry*>();
                          })
        ->add(new ExitStubs_RowEntry(stub, centre, dx != 0 ? centre->y : centre->x));
  }

  for (auto* entry : *rows->entrySet()) {
    jrt::List<ExitStubs_RowEntry*>* all = entry->getValue();
    all->sort([](ExitStubs_RowEntry* a, ExitStubs_RowEntry* b) { return jrt::Integer::compare(a->lateral_, b->lateral_); });
    int32_t pitch = 0;
    if (all->size() > 1) {
      pitch = jrt::Integer::MAX_VALUE;
      for (int32_t i = 1; i < all->size(); i++) {
        pitch = jrt::Math::min(pitch, all->get(i)->lateral_ - all->get(i - 1)->lateral_);
      }
    }
    jrt::List<jrt::List<ExitStubs_RowEntry*>*>* runs = new jrt::ArrayList<jrt::List<ExitStubs_RowEntry*>*>();
    for (ExitStubs_RowEntry* re : *all) {
      if (runs->isEmpty() ||
          re->lateral_ - runs->get(runs->size() - 1)->get(runs->get(runs->size() - 1)->size() - 1)->lateral_ >
              pitch * 3 / 2) {
        runs->add(new jrt::ArrayList<ExitStubs_RowEntry*>());
      }
      runs->get(runs->size() - 1)->add(re);
    }
    ExitStubs_RowKey* key = entry->getKey();
    for (jrt::List<ExitStubs_RowEntry*>* run : *runs) {
      std::function<jrt::List<ExitStubs_Stub*>*()> fanned = [=]() {
        return fanOut(board, run, key->dx(), key->dy(), key->line(), allowance, matrix, margin, true);
      };
      std::function<jrt::List<ExitStubs_Stub*>*()> staircaseFn = [=]() {
        return staircase(board, run, key->dx(), key->dy(), key->line(), allowance, matrix, margin);
      };
      result->addAll(reachable(board,
                               fanOut(board, run, key->dx(), key->dy(), key->line(), allowance, matrix, margin, false),
                               fanned, matrix, staircaseFn, run->size(), inwardPins));
    }
  }
  result->addAll(diagonalFans(board, diagonal, allowance, matrix, margin));
  return result;
}

jrt::List<ExitStubs_Stub*>* ExitStubs::diagonalFans(BasicBoard* board, jrt::List<ExitStubs_Stub*>* stubs,
                                                    int32_t allowance, ClearanceMatrix* matrix, int32_t margin) {
  jrt::List<ExitStubs_Stub*>* result = new jrt::ArrayList<ExitStubs_Stub*>();
  jrt::List<ExitStubs_DiagRow*>* rows = new jrt::ArrayList<ExitStubs_DiagRow*>();

  for (ExitStubs_Stub* stub : *stubs) {
    IntPoint* centre = ExitStubs::centre(stub->pin_);
    int32_t dx = jrt::Integer::signum(stub->end_->x - centre->x);
    int32_t dy = jrt::Integer::signum(stub->end_->y - centre->y);
    jrt::Array<FloatPoint*>* corners = stub->pin_->getTileShape(0)->cornerApproxArr();
    double line = -jrt::Double::MAX_VALUE;
    for (FloatPoint* c : *corners) {
      double val = (c->x * dx + c->y * dy) / jrt::Math::sqrt(2);
      if (val > line) line = val;
    }
    int32_t lateralInt = centre->x * -dy + centre->y * dx;
    double lateral = lateralInt / jrt::Math::sqrt(2);
    ExitStubs_DiagRow* matched = nullptr;
    for (ExitStubs_DiagRow* r : *rows) {
      if (r->dx == dx && r->dy == dy && jrt::Math::abs(r->line - line) <= 4) {
        matched = r;
        break;
      }
    }
    if (matched == nullptr) {
      matched = new ExitStubs_DiagRow(dx, dy, line);
      rows->add(matched);
    }
    matched->row->add(new ExitStubs_DiagEntry(stub, centre, lateral));
  }

  for (ExitStubs_DiagRow* r : *rows) {
    jrt::List<ExitStubs_DiagEntry*>* all = r->row;
    all->sort(
        [](ExitStubs_DiagEntry* a, ExitStubs_DiagEntry* b) { return jrt::Double::compare(a->lateral_, b->lateral_); });
    double pitch = 0;
    if (all->size() > 1) {
      pitch = jrt::Double::MAX_VALUE;
      for (int32_t i = 1; i < all->size(); i++) {
        pitch = jrt::Math::min(pitch, all->get(i)->lateral_ - all->get(i - 1)->lateral_);
      }
    }
    jrt::List<jrt::List<ExitStubs_DiagEntry*>*>* runs = new jrt::ArrayList<jrt::List<ExitStubs_DiagEntry*>*>();
    for (ExitStubs_DiagEntry* entry : *all) {
      if (runs->isEmpty() ||
          entry->lateral_ - runs->get(runs->size() - 1)->get(runs->get(runs->size() - 1)->size() - 1)->lateral_ >
              pitch * 1.5) {
        runs->add(new jrt::ArrayList<ExitStubs_DiagEntry*>());
      }
      runs->get(runs->size() - 1)->add(entry);
    }
    for (jrt::List<ExitStubs_DiagEntry*>* run : *runs) {
      std::function<jrt::List<ExitStubs_Stub*>*()> fanned = [=]() {
        return diagonalFanOut(board, run, r->dx, r->dy, allowance, matrix, margin, true);
      };
      result->addAll(reachable(board, diagonalFanOut(board, run, r->dx, r->dy, allowance, matrix, margin, false),
                               fanned, matrix, nullptr, 0, nullptr));
    }
  }
  return result;
}

jrt::List<ExitStubs_Stub*>* ExitStubs::diagonalFanOut(BasicBoard* board, jrt::List<ExitStubs_DiagEntry*>* run,
                                                      int32_t dx, int32_t dy, int32_t allowance,
                                                      ClearanceMatrix* matrix, int32_t margin, bool force) {
  jrt::List<ExitStubs_Stub*>* straight = new jrt::ArrayList<ExitStubs_Stub*>();
  for (ExitStubs_DiagEntry* e : *run) {
    straight->add(e->stub_);
  }
  if (run->size() < 2) {
    return straight;
  }
  int32_t layer = run->get(0)->stub_->pin_->firstLayer();
  int32_t half = 0;
  {
    // run.stream().mapToInt(e -> e.stub.halfWidth).max().orElse(0)
    bool any = false;
    for (ExitStubs_DiagEntry* e : *run) {
      if (!any || e->stub_->halfWidth_ > half) {
        half = e->stub_->halfWidth_;
        any = true;
      }
    }
  }
  double pitch = jrt::Double::MAX_VALUE;
  int32_t between = 0;
  for (int32_t i = 1; i < run->size(); i++) {
    pitch = jrt::Math::min(pitch, run->get(i)->lateral_ - run->get(i - 1)->lateral_);
    between = jrt::Math::max(between, matrix->getValue(traceClass(board, run->get(i - 1)->stub_->pin_),
                                                       traceClass(board, run->get(i)->stub_->pin_), layer, true));
  }
  if (!force && pitch - 2 * half >= between + TIGHT_SLACK) {
    return straight;
  }
  double root2 = jrt::Math::sqrt(2);
  ExitStubs_DiagonalResult* diag = diagonal(run->get(0)->stub_->pin_->getTileShape(0));
  if (diag == nullptr) {
    return straight;
  }
  int32_t spread = 2 * half + between + TIGHT_SLACK;
  double rise = jrt::Math::max(static_cast<double>(margin), jrt::Math::ceil(spread * root2 - pitch));
  int32_t tipSteps = jrt::d2i(jrt::Math::ceil(diag->halfLength() / root2));
  IntPoint* c0 = run->get(0)->centre_;
  int32_t netOf0 = run->get(0)->stub_->pin_->getNetNumber(0);
  auto* requiredHolder = new jrt::Array<int32_t>(1);
  slack(board, c0, new IntPoint(c0->x + dx * tipSteps, c0->y + dy * tipSteps), layer, half, netOf0,
        traceClass(board, run->get(0)->stub_->pin_), allowance, requiredHolder);
  double first = jrt::Math::max(static_cast<double>(margin),
                                jrt::Math::ceil((half + (*requiredHolder)[0]) * root2) - (pitch - diag->halfWidth())) +
                 margin;
  double reach = jrt::Math::ceil(half + between + margin * root2);

  int32_t n = run->size();
  jrt::List<ExitStubs_Stub*>* result = new jrt::ArrayList<ExitStubs_Stub*>();
  for (int32_t i = 0; i < n; i++) {
    ExitStubs_DiagEntry* de = run->get(i);
    ExitStubs_Stub* stub = de->stub_;
    IntPoint* centre = de->centre_;
    int32_t net = stub->pin_->getNetNumber(0);
    int32_t traceClass = ExitStubs::traceClass(board, stub->pin_);
    int32_t side = (2 * i + 1 < n) ? -1 : (2 * i + 1 > n) ? 1 : 0;
    int32_t level = side < 0 ? i : (side > 0 ? n - 1 - i : (n - 1) / 2);
    int32_t bendSteps = tipSteps + jrt::d2i(jrt::Math::ceil((first + level * rise) / root2));
    IntPoint* bendAt = new IntPoint(centre->x + dx * bendSteps, centre->y + dy * bendSteps);
    ExitStubs_Stub* fan;
    if (side == 0) {
      int32_t endSteps = bendSteps + jrt::d2i(jrt::Math::ceil(2 * reach / root2));
      fan = new ExitStubs_Stub(stub->pin_, new IntPoint(centre->x + dx * endSteps, centre->y + dy * endSteps),
                               stub->halfWidth_, false, stub->slack_, nullptr, nullptr);
    } else {
      int32_t a[2] = {dx, 0};
      int32_t b[2] = {0, dy};
      int32_t* turn = jrt::Integer::signum(a[0] * -dy + a[1] * dx) == side ? a : b;
      fan = new ExitStubs_Stub(stub->pin_,
                               new IntPoint(bendAt->x + turn[0] * jrt::d2i(reach), bendAt->y + turn[1] * jrt::d2i(reach)),
                               stub->halfWidth_, false, stub->slack_, bendAt, nullptr);
    }
    if (pathSlack(board, fan->corners(centre), layer, fan->halfWidth_, net, traceClass, allowance) >= 0) {
      result->add(fan);
    }
  }
  return result;
}

jrt::List<ExitStubs_Stub*>* ExitStubs::reachable(BasicBoard* board, jrt::List<ExitStubs_Stub*>* planned,
                                                 std::function<jrt::List<ExitStubs_Stub*>*()> fanned,
                                                 ClearanceMatrix* matrix,
                                                 std::function<jrt::List<ExitStubs_Stub*>*()> staircase,
                                                 int32_t runCount, jrt::Set<int32_t>* inwardPins) {
  for (ExitStubs_Stub* s : *planned) {
    if (s->inward_) {
      return planned;
    }
  }
  jrt::List<ExitStubs_Stub*>* best = planned;
  int32_t reached = 0;
  for (ExitStubs_Stub* s : *planned) {
    if (reachableAmong(board, s, planned, matrix)) {
      reached++;
    }
  }
  bool allStraight = true;
  for (ExitStubs_Stub* s : *planned) {
    if (!(s->bend_ == nullptr)) {
      allStraight = false;
      break;
    }
  }
  if (planned->size() >= 2 && allStraight && reached < planned->size()) {
    jrt::List<ExitStubs_Stub*>* fan = fanned();
    int32_t fanReached = 0;
    for (ExitStubs_Stub* s : *fan) {
      if (reachableAmong(board, s, fan, matrix)) {
        fanReached++;
      }
    }
    if (fanReached > reached) {
      best = fan;
      reached = fanReached;
    }
  }
  if (staircase != nullptr && runCount >= 2 && reached < runCount) {
    jrt::List<ExitStubs_Stub*>* stairs = staircase();
    int32_t stairsScore = 0;
    for (ExitStubs_Stub* s : *stairs) {
      if ((inwardPins == nullptr || !inwardPins->contains(s->pin_->getId())) &&
          reachableAmong(board, s, stairs, matrix)) {
        stairsScore++;
      }
    }
    jrt::List<ExitStubs_Stub*>* currentBest = best;
    int32_t bestScore = 0;
    for (ExitStubs_Stub* s : *currentBest) {
      if ((inwardPins == nullptr || !inwardPins->contains(s->pin_->getId())) &&
          reachableAmong(board, s, currentBest, matrix)) {
        bestScore++;
      }
    }
    if (stairsScore > bestScore) {
      best = stairs;
    }
  }
  return best;
}

jrt::List<ExitStubs_Stub*>* ExitStubs::staircase(BasicBoard* board, jrt::List<ExitStubs_RowEntry*>* run, int32_t dx,
                                                 int32_t dy, int32_t line, int32_t allowance, ClearanceMatrix* matrix,
                                                 int32_t margin) {
  jrt::List<ExitStubs_Stub*>* result = new jrt::ArrayList<ExitStubs_Stub*>();
  int32_t n = run->size();
  if (n < 2) {
    jrt::List<ExitStubs_Stub*>* straight = new jrt::ArrayList<ExitStubs_Stub*>();
    for (ExitStubs_RowEntry* e : *run) {
      straight->add(e->stub_);
    }
    return straight;
  }
  int32_t layer = run->get(0)->stub_->pin_->firstLayer();
  int32_t half = 0;
  {
    bool any = false;
    for (ExitStubs_RowEntry* e : *run) {
      if (!any || e->stub_->halfWidth_ > half) {
        half = e->stub_->halfWidth_;
        any = true;
      }
    }
  }
  int32_t pitch = jrt::Integer::MAX_VALUE;
  int32_t between = 0;
  for (int32_t i = 1; i < n; i++) {
    pitch = jrt::Math::min(pitch, run->get(i)->lateral_ - run->get(i - 1)->lateral_);
    between = jrt::Math::max(between, matrix->getValue(traceClass(board, run->get(i - 1)->stub_->pin_),
                                                       traceClass(board, run->get(i)->stub_->pin_), layer, true));
  }
  int32_t netOf0 = run->get(0)->stub_->pin_->getNetNumber(0);
  auto* requiredHolder = new jrt::Array<int32_t>(1);
  slack(board, run->get(0)->centre_,
        new IntPoint(dx != 0 ? line : run->get(0)->centre_->x, dy != 0 ? line : run->get(0)->centre_->y), layer, half,
        netOf0, traceClass(board, run->get(0)->stub_->pin_), allowance, requiredHolder);
  int32_t clearTips = half + (*requiredHolder)[0] + margin;
  int32_t rise = 2 * half + between + TIGHT_SLACK;
  const int32_t finalPitch = pitch;
  const int32_t finalBetween = between;

  std::function<ExitStubs_Stub*(int32_t, int32_t)> build = [=](int32_t i, int32_t baseAt) -> ExitStubs_Stub* {
    ExitStubs_RowEntry* re = run->get(i);
    ExitStubs_Stub* stub = re->stub_;
    IntPoint* centre = re->centre_;
    int32_t net = stub->pin_->getNetNumber(0);
    int32_t traceClass = ExitStubs::traceClass(board, stub->pin_);
    int32_t side = (2 * i + 1 < n) ? -1 : (2 * i + 1 > n) ? 1 : 0;
    int32_t level = side < 0 ? i : (side > 0 ? n - 1 - i : n / 2);
    int32_t lx = dx != 0 ? 0 : side;
    int32_t ly = dx != 0 ? side : 0;
    int32_t at = margin + jrt::Math::max(baseAt + level * rise, level == 0 ? 0 : clearTips);
    IntPoint* bendAt = dx != 0 ? new IntPoint(line + dx * at, centre->y) : new IntPoint(centre->x, line + dy * at);
    if (side == 0) {
      ExitStubs_Stub* up = new ExitStubs_Stub(stub->pin_, new IntPoint(bendAt->x + dx * rise, bendAt->y + dy * rise),
                                              stub->halfWidth_, false, stub->slack_, nullptr, nullptr);
      return pathSlack(board, up->corners(centre), layer, up->halfWidth_, net, traceClass, allowance) >= 0 ? up
                                                                                                           : nullptr;
    }
    int32_t length = half + finalBetween + margin;
    int32_t onward = half + matrix->maxValue(traceClass, layer) + margin;
    for (int32_t k = 0; k <= 8; k++, length += finalPitch) {
      IntPoint* end = new IntPoint(bendAt->x + lx * length, bendAt->y + ly * length);
      ExitStubs_Stub* turned = new ExitStubs_Stub(stub->pin_, end, stub->halfWidth_, false, stub->slack_, bendAt, nullptr);
      if (pathSlack(board, turned->corners(centre), layer, turned->halfWidth_, net, traceClass, allowance) < 0) {
        return nullptr;
      }
      Polyline* past =
          new Polyline(jrt::Array<Point*>::of({end, new IntPoint(end->x + lx * onward, end->y + ly * onward)}));
      if (board->checkPolylineTrace(past, layer, turned->halfWidth_, jrt::Array<int32_t>::of({net}), traceClass)) {
        return turned;
      }
    }
    return nullptr;
  };

  int32_t lowerCount = n / 2;
  int32_t upperFrom = (n + 1) / 2;

  jrt::List<int32_t>* lowerHalf = new jrt::ArrayList<int32_t>();
  for (int32_t i = 0; i < lowerCount; i++) lowerHalf->add(i);

  jrt::List<int32_t>* upperHalf = new jrt::ArrayList<int32_t>();
  for (int32_t i = n - 1; i >= upperFrom; i--) upperHalf->add(i);

  jrt::List<jrt::List<int32_t>*>* halves = jrt::ArrayList<jrt::List<int32_t>*>::of({lowerHalf, upperHalf});
  for (jrt::List<int32_t>* halfIndices : *halves) {
    if (halfIndices->isEmpty()) {
      continue;
    }
    int32_t baseAt = build(halfIndices->get(0), 0) == nullptr ? clearTips : 0;
    for (int32_t i : *halfIndices) {
      ExitStubs_Stub* stub = build(i, baseAt);
      if (stub != nullptr) {
        result->add(stub);
      }
    }
  }
  if (n % 2 == 1) {
    ExitStubs_Stub* middle = build(n / 2, 0);
    if (middle != nullptr) {
      result->add(middle);
    }
  }
  return result;
}

bool ExitStubs::reachableAmong(BasicBoard* board, ExitStubs_Stub* stub, jrt::List<ExitStubs_Stub*>* run,
                               ClearanceMatrix* matrix) {
  int32_t net = stub->pin_->getNetNumber(0);
  int32_t layer = stub->pin_->firstLayer();
  int32_t traceClass = ExitStubs::traceClass(board, stub->pin_);
  IntPoint* from = stub->bend_ != nullptr ? stub->bend_ : centre(stub->pin_);
  int32_t dx = jrt::Integer::signum(stub->end_->x - from->x);
  int32_t dy = jrt::Integer::signum(stub->end_->y - from->y);
  int32_t length = endProbeLength(board, stub->end_, layer, stub->halfWidth_, net, traceClass);

  int32_t dirs[3][2] = {{dx, dy},
                        {jrt::Integer::signum(dx - dy), jrt::Integer::signum(dx + dy)},
                        {jrt::Integer::signum(dx + dy), jrt::Integer::signum(dy - dx)}};
  for (auto& d : dirs) {
    IntPoint* b = new IntPoint(stub->end_->x + d[0] * length, stub->end_->y + d[1] * length);
    if (!board->checkPolylineTrace(new Polyline(jrt::Array<Point*>::of({stub->end_, b})), layer, stub->halfWidth_,
                                   jrt::Array<int32_t>::of({net}), traceClass)) {
      continue;
    }
    bool clear = true;
    for (ExitStubs_Stub* other : *run) {
      if (other->pin_ == stub->pin_) {
        continue;
      }
      jrt::Array<Point*>* corners = other->corners(centre(other->pin_));
      int32_t required = stub->halfWidth_ + other->halfWidth_ +
                         matrix->getValue(ExitStubs::traceClass(board, other->pin_), traceClass, layer, true);
      for (int32_t i = 0; i + 1 < corners->length && clear; i++) {
        clear = segmentsDistance(stub->end_->toFloat(), b->toFloat(), (*corners)[i]->toFloat(),
                                 (*corners)[i + 1]->toFloat()) >= required;
      }
      if (!clear) {
        break;
      }
    }
    if (clear) {
      return true;
    }
  }
  return false;
}

double ExitStubs::segmentsDistance(FloatPoint* a, FloatPoint* b, FloatPoint* c, FloatPoint* d) {
  return cross(a, b, c, d) ? 0.0
                           : jrt::Math::min(jrt::Math::min(pointSegment(a, c, d), pointSegment(b, c, d)),
                                            jrt::Math::min(pointSegment(c, a, b), pointSegment(d, a, b)));
}

jrt::List<ExitStubs_Stub*>* ExitStubs::fanOut(BasicBoard* board, jrt::List<ExitStubs_RowEntry*>* run, int32_t dx,
                                              int32_t dy, int32_t line, int32_t allowance, ClearanceMatrix* matrix,
                                              int32_t margin, bool force) {
  jrt::List<ExitStubs_Stub*>* straight = new jrt::ArrayList<ExitStubs_Stub*>();
  for (ExitStubs_RowEntry* e : *run) {
    straight->add(e->stub_);
  }
  if (run->size() < 2) {
    return straight;
  }
  int32_t layer = run->get(0)->stub_->pin_->firstLayer();
  int32_t half = 0;
  {
    bool any = false;
    for (ExitStubs_RowEntry* e : *run) {
      if (!any || e->stub_->halfWidth_ > half) {
        half = e->stub_->halfWidth_;
        any = true;
      }
    }
  }
  int32_t pitch = jrt::Integer::MAX_VALUE;
  int32_t between = 0;
  for (int32_t i = 1; i < run->size(); i++) {
    pitch = jrt::Math::min(pitch, run->get(i)->lateral_ - run->get(i - 1)->lateral_);
    between = jrt::Math::max(between, matrix->getValue(traceClass(board, run->get(i - 1)->stub_->pin_),
                                                       traceClass(board, run->get(i)->stub_->pin_), layer, true));
  }
  if (!force && pitch - 2 * half >= between + TIGHT_SLACK) {
    return straight;
  }
  int32_t spread = 2 * half + between + TIGHT_SLACK;
  int32_t rise = jrt::Math::max(margin, jrt::d2i(jrt::Math::ceil(spread * jrt::Math::sqrt(2) - pitch)));
  IntBox* pad = run->get(0)->stub_->pin_->getTileShape(0)->boundingBox();
  int32_t padHalf = (dx != 0 ? pad->height() : pad->width()) / 2;
  int32_t netOf0 = run->get(0)->stub_->pin_->getNetNumber(0);
  auto* requiredHolder = new jrt::Array<int32_t>(1);
  slack(board, run->get(0)->centre_,
        new IntPoint(dx != 0 ? line : run->get(0)->centre_->x, dy != 0 ? line : run->get(0)->centre_->y), layer, half,
        netOf0, traceClass(board, run->get(0)->stub_->pin_), allowance, requiredHolder);
  int32_t first =
      jrt::Math::max(margin, jrt::d2i(jrt::Math::ceil((half + (*requiredHolder)[0]) * jrt::Math::sqrt(2))) -
                                 (pitch - padHalf)) +
      margin;
  int32_t reach = jrt::d2i(jrt::Math::ceil((half + between) / jrt::Math::sqrt(2))) + margin;
  int32_t n = run->size();
  jrt::List<ExitStubs_Stub*>* result = new jrt::ArrayList<ExitStubs_Stub*>();
  for (int32_t i = 0; i < n; i++) {
    ExitStubs_RowEntry* re = run->get(i);
    ExitStubs_Stub* stub = re->stub_;
    IntPoint* centre = re->centre_;
    int32_t net = stub->pin_->getNetNumber(0);
    int32_t traceClass = ExitStubs::traceClass(board, stub->pin_);
    int32_t side = (2 * i + 1 < n) ? -1 : (2 * i + 1 > n) ? 1 : 0;
    int32_t level = side < 0 ? i : (side > 0 ? n - 1 - i : (n - 1) / 2);
    int32_t lx = dx != 0 ? 0 : side;
    int32_t ly = dx != 0 ? side : 0;
    int32_t at = first + level * rise;
    IntPoint* bendAt = dx != 0 ? new IntPoint(line + dx * at, centre->y) : new IntPoint(centre->x, line + dy * at);
    ExitStubs_Stub* fan =
        side == 0 ? new ExitStubs_Stub(stub->pin_,
                                       new IntPoint(bendAt->x + dx * reach * 2, bendAt->y + dy * reach * 2),
                                       stub->halfWidth_, false, stub->slack_, nullptr, nullptr)
                  : new ExitStubs_Stub(stub->pin_,
                                       new IntPoint(bendAt->x + (dx + lx) * reach, bendAt->y + (dy + ly) * reach),
                                       stub->halfWidth_, false, stub->slack_, bendAt, nullptr);
    if (side != 0) {
      int32_t other = -side;
      int32_t otherLevel = other < 0 ? i : n - 1 - i;
      int32_t mx = dx != 0 ? 0 : other;
      int32_t my = dx != 0 ? other : 0;
      int32_t mat = first + otherLevel * rise;
      IntPoint* mirrorBend =
          dx != 0 ? new IntPoint(line + dx * mat, centre->y) : new IntPoint(centre->x, line + dy * mat);
      fan = new ExitStubs_Stub(
          fan->pin_, fan->end_, fan->halfWidth_, fan->inward_, fan->slack_, fan->bend_,
          new ExitStubs_Mirror(mirrorBend,
                               new IntPoint(mirrorBend->x + (dx + mx) * reach, mirrorBend->y + (dy + my) * reach)));
    }
    if (pathSlack(board, fan->corners(centre), layer, fan->halfWidth_, net, traceClass, allowance) >= 0) {
      result->add(fan);
    }
  }
  return result;
}

int32_t ExitStubs::traceClass(BasicBoard* board, Pin* pin) {
  return board->rules->nets->get(pin->getNetNumber(0))->getNetClass()->getTraceClearanceClass();
}

jrt::List<ExitStubs_Stub*>* ExitStubs::candidates(BasicBoard* board, RouterSettings* settings, Pin* pin,
                                                  jrt::List<Pin*>* partPins, jrt::Map<int32_t, int32_t>* pinsOfNet,
                                                  double cx, double cy, int32_t allowance) {
  jrt::List<ExitStubs_Stub*>* stubs = new jrt::ArrayList<ExitStubs_Stub*>();
  auto* inwardHolder = new jrt::Array<ExitStubs_Stub*>(1);
  ExitStubs_Stub* outward = ExitStubs::outward(board, settings, pin, partPins, pinsOfNet, cx, cy, allowance, inwardHolder);
  if (outward != nullptr) {
    stubs->add(outward);
  }
  if ((*inwardHolder)[0] != nullptr) {
    stubs->add((*inwardHolder)[0]);
  }
  return stubs;
}

ExitStubs_Stub* ExitStubs::outward(BasicBoard* board, RouterSettings* settings, Pin* pin, jrt::List<Pin*>* partPins,
                                   jrt::Map<int32_t, int32_t>* pinsOfNet, double cx, double cy, int32_t allowance,
                                   jrt::Array<ExitStubs_Stub*>* inwardHolder) {
  (*inwardHolder)[0] = nullptr;
  if (pin->netCount() != 1 || pin->firstLayer() != pin->lastLayer() || pin->tileShapeCount() != 1 ||
      pinsOfNet->getOrDefault(pin->getNetNumber(0), 0) < 2) {
    return nullptr;
  }
  int32_t net = pin->getNetNumber(0);
  int32_t layer = pin->firstLayer();
  if ((settings != nullptr && layer < settings->getLayerCount() && !settings->getLayerActive(layer)) ||
      board->rules->nets->get(net) == nullptr) {
    return nullptr;
  }
  NetClass* netClass = board->rules->nets->get(net)->getNetClass();
  if (netClass->isIgnoredByAutorouter || !netClass->isActiveRoutingLayer(layer) || pin->getTileShape(0) == nullptr) {
    return nullptr;
  }
  TileShape* shape = pin->getTileShape(0);
  IntBox* box = shape->boundingBox();
  int32_t w = box->width();
  int32_t h = box->height();
  IntPoint* centre = ExitStubs::centre(pin);
  int32_t classHalf = netClass->getTraceHalfWidth(layer);
  int32_t minHalf = board->rules->getMinAllowedTraceHalfWidth();
  int32_t half = minHalf > 0 ? minHalf : classHalf;
  int32_t traceClass = netClass->getTraceClearanceClass();
  if (w == h || jrt::Math::max(w, h) < 3 * jrt::Math::min(w, h) / 2) {
    return diagonalOutward(board, pin, shape, centre, half, classHalf, net, layer, traceClass, cx, cy, allowance);
  }
  bool alongX = w > h;
  int32_t halfLength = (alongX ? w : h) / 2;
  double offset = alongX ? centre->x - cx : centre->y - cy;
  if (jrt::Math::abs(offset) < halfLength) {
    return nullptr;
  }
  int32_t outwardSign = offset > 0 ? 1 : -1;
  int32_t ox = alongX ? outwardSign : 0;
  int32_t oy = alongX ? 0 : outwardSign;
  int32_t ix = -ox;
  int32_t iy = -oy;

  Pin* target = nullptr;
  int32_t targetDistance = jrt::Integer::MAX_VALUE;
  int64_t reach = jrt::d2l(INWARD_REACH_PAD_LENGTHS * 2 * halfLength);
  for (Pin* other : *partPins) {
    if (other == pin || !other->containsNet(net) || other->firstLayer() > layer || other->lastLayer() < layer ||
        Pin::arePiecesOfOnePad(pin, other) || padNumber(other) == padNumber(pin)) {
      continue;
    }
    for (int32_t i = 0; i < other->tileShapeCount(); i++) {
      if (other->shapeLayer(i) != layer || other->getTileShape(i) == nullptr) {
        continue;
      }
      IntBox* ob = other->getTileShape(i)->boundingBox();
      bool onLine = alongX ? ob->ll->y <= centre->y && centre->y <= ob->ur->y
                           : ob->ll->x <= centre->x && centre->x <= ob->ur->x;
      int32_t distance = alongX ? (ix > 0 ? ob->ll->x - centre->x : centre->x - ob->ur->x)
                                : (iy > 0 ? ob->ll->y - centre->y : centre->y - ob->ur->y);
      if (onLine && distance > halfLength && distance <= reach && distance < targetDistance) {
        target = other;
        targetDistance = distance;
      }
    }
  }
  if (target != nullptr) {
    int32_t length = targetDistance + half;
    IntPoint* end = new IntPoint(centre->x + ix * length, centre->y + iy * length);
    auto* largestHolder = new jrt::Array<int32_t>(1);
    double slack = ExitStubs::slack(board, centre, end, layer, half, net, traceClass, allowance, largestHolder);
    if (slack >= 0 && slack < TIGHT_SLACK && !routerPasses(board, centre, end, layer, half, net, traceClass)) {
      (*inwardHolder)[0] = new ExitStubs_Stub(
          pin, end, widest(board, centre, end, layer, half, classHalf, net, traceClass, allowance), true, slack);
    }
  }

  IntPoint* tip = new IntPoint(centre->x + ox * halfLength, centre->y + oy * halfLength);
  auto* largestHolder = new jrt::Array<int32_t>(1);
  double rowSlack = slack(board, centre, tip, layer, half, net, traceClass, allowance, largestHolder);
  if (rowSlack < 0 || rowSlack >= TIGHT_SLACK) {
    return nullptr;
  }
  int32_t run = (*largestHolder)[0] + half + ClearanceMatrix::clearance_safety_margin;
  IntPoint* stubEnd = new IntPoint(tip->x + ox * run, tip->y + oy * run);
  double stubSlack = slack(board, centre, stubEnd, layer, half, net, traceClass, allowance, largestHolder);
  if (stubSlack < 0 ||
      (routerPasses(board, centre, stubEnd, layer, half, net, traceClass) && !isTolerancePin(board, pin))) {
    return nullptr;
  }
  return new ExitStubs_Stub(pin, stubEnd,
                            widest(board, centre, stubEnd, layer, half, classHalf, net, traceClass, allowance), false,
                            stubSlack);
}

ExitStubs_Stub* ExitStubs::diagonalOutward(BasicBoard* board, Pin* pin, TileShape* shape, IntPoint* centre,
                                           int32_t half, int32_t classHalf, int32_t net, int32_t layer,
                                           int32_t traceClass, double cx, double cy, int32_t allowance) {
  ExitStubs_DiagonalResult* diag = diagonal(shape);
  if (diag == nullptr) {
    return nullptr;
  }
  double offset = ((centre->x - cx) * diag->dx() + (centre->y - cy) * diag->dy()) / jrt::Math::sqrt(2);
  if (jrt::Math::abs(offset) < diag->halfLength()) {
    return nullptr;
  }
  int32_t sign = offset > 0 ? 1 : -1;
  int32_t ox = sign * diag->dx();
  int32_t oy = sign * diag->dy();
  int32_t tipSteps = jrt::d2i(jrt::Math::ceil(diag->halfLength() / jrt::Math::sqrt(2)));
  IntPoint* tip = new IntPoint(centre->x + ox * tipSteps, centre->y + oy * tipSteps);
  auto* largestHolder = new jrt::Array<int32_t>(1);
  double rowSlack = slack(board, centre, tip, layer, half, net, traceClass, allowance, largestHolder);
  if (rowSlack < 0 || rowSlack >= TIGHT_SLACK) {
    return nullptr;
  }
  int32_t run = (*largestHolder)[0] + half + ClearanceMatrix::clearance_safety_margin;
  int32_t endSteps = tipSteps + jrt::d2i(jrt::Math::ceil(run / jrt::Math::sqrt(2)));
  IntPoint* stubEnd = new IntPoint(centre->x + ox * endSteps, centre->y + oy * endSteps);
  double stubSlack = slack(board, centre, stubEnd, layer, half, net, traceClass, allowance, largestHolder);
  if (stubSlack < 0 ||
      (routerPasses(board, centre, stubEnd, layer, half, net, traceClass) && !isTolerancePin(board, pin))) {
    return nullptr;
  }
  return new ExitStubs_Stub(pin, stubEnd,
                            widest(board, centre, stubEnd, layer, half, classHalf, net, traceClass, allowance), false,
                            stubSlack);
}

ExitStubs_DiagonalResult* ExitStubs::diagonal(TileShape* shape) {
  jrt::Array<FloatPoint*>* corners = shape->cornerApproxArr();
  if (corners->length < 2) {
    return nullptr;
  }
  auto extent = [corners](int32_t dx, int32_t dy) -> double {
    double min = jrt::Double::MAX_VALUE;
    double max = -jrt::Double::MAX_VALUE;
    for (FloatPoint* c : *corners) {
      double v = c->x * dx + c->y * dy;
      if (v < min) min = v;
      if (v > max) max = v;
    }
    return (max - min) / jrt::Math::sqrt(2);
  };
  double rising = extent(1, 1);
  double falling = extent(1, -1);
  double minX = jrt::Double::MAX_VALUE;
  double maxX = -jrt::Double::MAX_VALUE;
  double minY = jrt::Double::MAX_VALUE;
  double maxY = -jrt::Double::MAX_VALUE;
  for (FloatPoint* c : *corners) {
    if (c->x < minX) minX = c->x;
    if (c->x > maxX) maxX = c->x;
    if (c->y < minY) minY = c->y;
    if (c->y > maxY) maxY = c->y;
  }
  double wide = maxX - minX;
  double high = maxY - minY;
  if (jrt::Math::abs(wide - high) > 2 || jrt::Math::max(rising, falling) < 1.5 * jrt::Math::min(rising, falling)) {
    return nullptr;
  }
  return rising > falling ? new ExitStubs_DiagonalResult(1, 1, rising / 2.0, falling / 2.0)
                          : new ExitStubs_DiagonalResult(1, -1, falling / 2.0, rising / 2.0);
}

bool ExitStubs::routerPasses(BasicBoard* board, IntPoint* from, IntPoint* to, int32_t layer, int32_t half, int32_t net,
                             int32_t traceClass) {
  return board->checkPolylineTrace(new Polyline(jrt::Array<Point*>::of({from, to})), layer, half,
                                   jrt::Array<int32_t>::of({net}), traceClass);
}

bool ExitStubs::isTolerancePin(BasicBoard* board, Pin* pin) {
  return board->tolerancePins != nullptr && board->tolerancePins->contains(pin->getId());
}

std::string ExitStubs::padNumber(Pin* pin) {
  std::optional<std::string> nameOpt = pin->name();
  if (!nameOpt.has_value()) {
    return "";
  }
  const std::string& name = *nameOpt;
  int32_t at = -1;
  for (int32_t i = 0; i < static_cast<int32_t>(name.length()); i++) {
    char c = name[i];
    if (c == '@' || c == '#') {
      at = i;
      break;
    }
  }
  return at > 0 ? jrt::substring(name, 0, at) : name;
}

int32_t ExitStubs::widest(BasicBoard* board, IntPoint* from, IntPoint* to, int32_t layer, int32_t half,
                          int32_t classHalf, int32_t net, int32_t traceClass, int32_t allowance) {
  auto* holder = new jrt::Array<int32_t>(1);
  return classHalf > half && slack(board, from, to, layer, classHalf, net, traceClass, allowance, holder) >= 0
             ? classHalf
             : half;
}

IntPoint* ExitStubs::centre(Pin* pin) {
  Point* centre = pin->getCenter();
  if (auto* point = jrt::fast_cast<IntPoint>(centre)) {
    return point;
  }
  return centre->toFloat()->round();
}

double ExitStubs::slack(BasicBoard* board, IntPoint* from, IntPoint* to, int32_t layer, int32_t half, int32_t net,
                        int32_t traceClass, int32_t allowance, jrt::Array<int32_t>* largestHolder) {
  (*largestHolder)[0] = 0;
  ClearanceMatrix* matrix = board->rules->clearanceMatrix;
  int32_t search = half + matrix->maxValue(traceClass, layer) + 4 * ClearanceMatrix::clearance_safety_margin;
  IntBox* bounds = board->getBoundingBox();
  IntBox* area = new IntBox(jrt::Math::max(bounds->ll->x, jrt::Math::min(from->x, to->x) - search),
                            jrt::Math::max(bounds->ll->y, jrt::Math::min(from->y, to->y) - search),
                            jrt::Math::min(bounds->ur->x, jrt::Math::max(from->x, to->x) + search),
                            jrt::Math::min(bounds->ur->y, jrt::Math::max(from->y, to->y) + search));
  if (!bounds->contains(static_cast<Point*>(from)) || !bounds->contains(static_cast<Point*>(to))) {
    return -1.0;
  }
  double least = jrt::Double::POSITIVE_INFINITY;
  FloatPoint* a = from->toFloat();
  FloatPoint* b = to->toFloat();
  for (SearchTreeObject* entry : *board->overlappingObjects(area, layer)) {
    auto* item = jrt::fast_cast<Item>(entry);
    if (item == nullptr || !item->isTraceObstacle(net)) {
      continue;
    }
    int32_t required = ruleClearance(board, traceClass, item, layer, allowance);
    for (int32_t i = 0; i < item->tileShapeCount(); i++) {
      if (item->shapeLayer(i) != layer || !item->isTraceObstacle(net, i) || item->getTileShape(i) == nullptr ||
          item->getTileShape(i)->isEmpty()) {
        continue;
      }
      TileShape* shape = item->getTileShape(i);
      double room = segmentDistance(a, b, shape) + modelExcess(board, item, shape) - half - required;
      least = jrt::Math::min(least, room);
      if (room < TIGHT_SLACK) {
        (*largestHolder)[0] = jrt::Math::max((*largestHolder)[0], required);
      }
    }
  }
  return least;
}

double ExitStubs::pathSlack(BasicBoard* board, jrt::Array<Point*>* corners, int32_t layer, int32_t half, int32_t net,
                            int32_t traceClass, int32_t allowance) {
  double least = jrt::Double::POSITIVE_INFINITY;
  auto* holder = new jrt::Array<int32_t>(1);
  for (int32_t i = 0; i + 1 < corners->length; i++) {
    least = jrt::Math::min(least, slack(board, (*corners)[i]->toFloat()->round(), (*corners)[i + 1]->toFloat()->round(),
                                        layer, half, net, traceClass, allowance, holder));
  }
  return least;
}

int32_t ExitStubs::ruleClearance(BasicBoard* board, int32_t traceClass, Item* item, int32_t layer, int32_t allowance) {
  ClearanceMatrix* matrix = board->rules->clearanceMatrix;
  int32_t itemClass = item->clearanceClassIndex();
  // PORT NOTE: ClearanceMatrix::getName returns std::string (Java never returns null for a valid
  // class index), so the Java null check is dropped.
  std::string name = matrix->getName(itemClass);
  {
    std::smatch m;
    if (std::regex_match(name, m, TOLERANCE_COPY())) {
      int32_t original = matrix->getNo(m[1].str());
      if (original >= 0) {
        itemClass = original;
      }
    }
  }
  if (board->rules->pinClearanceSources != nullptr && board->rules->pinClearanceSources->containsKey(itemClass)) {
    PinClearanceSource* source = board->rules->pinClearanceSources->get(itemClass);
    int32_t copper = source->localClearance().has_value()
                         ? *source->localClearance()
                         : matrix->getValue(source->originalClass(), traceClass, layer, false);
    bool outer = layer == 0 || layer == matrix->getLayerCount() - 1;
    return jrt::Math::max(copper - allowance, outer ? source->maskClearance() : 0);
  }
  return matrix->getValue(itemClass, traceClass, layer, false) - allowance;
}

int32_t ExitStubs::copperAllowance(BasicBoard* board, RouterSettings* settings) {
  double epsilonUm = settings != nullptr && settings->drcEpsilonUm.has_value() ? *settings->drcEpsilonUm : 0.0;
  int32_t epsilon =
      jrt::d2i(jrt::Math::floor(board->communication->physicalToBoard(jrt::Math::max(0.0, epsilonUm), Unit::UM) + 1e-9));
  return jrt::Math::max(0, epsilon - 1);
}

int32_t ExitStubs::modelExcess(BasicBoard* board, Item* item, TileShape* shape) {
  auto* pin = jrt::fast_cast<Pin>(item);
  // PORT NOTE: Padstack::name is a non-null std::string, so Java's `name == null` test is dropped.
  if (pin == nullptr || pin->getPadstack() == nullptr || !jrt::startsWith(pin->getPadstack()->name, "RoundRect")) {
    return 0;
  }
  std::smatch m;
  const std::string& padName = pin->getPadstack()->name;
  if (!std::regex_search(padName, m, KICAD_PAD_SIZE())) {
    return 0;
  }
  double wUm = jrt::Double::parseDouble(m[1].str());
  double hUm = jrt::Double::parseDouble(m[2].str());
  int32_t w = jrt::l2i(jrt::Math::round(board->communication->physicalToBoard(wUm, Unit::UM)));
  int32_t h = jrt::l2i(jrt::Math::round(board->communication->physicalToBoard(hUm, Unit::UM)));
  IntBox* box = shape->boundingBox();
  int32_t excess = jrt::Math::max(jrt::Math::min(box->width() - w, box->height() - h),
                                  jrt::Math::min(box->width() - h, box->height() - w)) /
                   2;
  int32_t maxExcess = jrt::l2i(jrt::Math::round(board->communication->physicalToBoard(2.0, Unit::UM)));
  return excess >= 0 && excess <= maxExcess ? excess : 0;
}

double ExitStubs::segmentDistance(FloatPoint* a, FloatPoint* b, TileShape* shape) {
  jrt::Array<FloatPoint*>* corners = shape->cornerApproxArr();
  if (corners->length == 0) {
    return jrt::Double::POSITIVE_INFINITY;
  }
  if (corners->length == 1) {
    return pointSegment((*corners)[0], a, b);
  }
  if (shape->contains(a) || shape->contains(b)) {
    return 0.0;
  }
  double least = jrt::Double::POSITIVE_INFINITY;
  for (int32_t i = 0; i < corners->length; i++) {
    FloatPoint* c = (*corners)[i];
    FloatPoint* d = (*corners)[(i + 1) % corners->length];
    if (cross(a, b, c, d)) {
      return 0.0;
    }
    least = jrt::Math::min(least, jrt::Math::min(pointSegment(c, a, b), pointSegment(d, a, b)));
    least = jrt::Math::min(least, jrt::Math::min(pointSegment(a, c, d), pointSegment(b, c, d)));
  }
  return least;
}

double ExitStubs::pointSegment(FloatPoint* p, FloatPoint* a, FloatPoint* b) {
  double dx = b->x - a->x;
  double dy = b->y - a->y;
  double len = dx * dx + dy * dy;
  double t = len > 0 ? jrt::Math::max(0.0, jrt::Math::min(1.0, ((p->x - a->x) * dx + (p->y - a->y) * dy) / len)) : 0.0;
  double x = a->x + t * dx - p->x;
  double y = a->y + t * dy - p->y;
  return jrt::Math::sqrt(x * x + y * y);
}

bool ExitStubs::cross(FloatPoint* a, FloatPoint* b, FloatPoint* c, FloatPoint* d) {
  double d1 = side(c, d, a);
  double d2 = side(c, d, b);
  double d3 = side(a, b, c);
  double d4 = side(a, b, d);
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

double ExitStubs::side(FloatPoint* p, FloatPoint* q, FloatPoint* r) {
  return (q->x - p->x) * (r->y - p->y) - (q->y - p->y) * (r->x - p->x);
}

std::optional<std::string> ExitStubs::release(RoutingBoard* board, jrt::Collection<int32_t>* itemIds) {
  jrt::List<PolylineTrace*>* traces = new jrt::ArrayList<PolylineTrace*>();
  for (int32_t id : *itemIds) {
    Item* item = board->getItem(id);
    auto* trace = jrt::fast_cast<PolylineTrace>(item);
    if (trace != nullptr && trace->getFixedState() == PLACED_STATE) {
      traces->add(trace);
    }
  }
  if (traces->isEmpty()) {
    return std::nullopt;
  }
  jrt::Set<int32_t>* netSet = new jrt::TreeSet<int32_t>();
  for (PolylineTrace* t : *traces) {
    for (int32_t i = 0; i < t->netCount(); i++) {
      netSet->add(t->getNetNumber(i));
    }
  }
  int32_t openBefore = open(board, netSet);
  board->generateSnapshot();
  int32_t snapped = 0;
  for (PolylineTrace* t : *traces) {
    snapped += snapNearJoints(board, t);
  }
  jrt::List<ExitStubs_JointEntry*>* joints = new jrt::ArrayList<ExitStubs_JointEntry*>();
  for (PolylineTrace* t : *traces) {
    joints->add(new ExitStubs_JointEntry(t, ExitStubs::joints(t)));
  }
  jrt::List<PolylineTrace*>* released = new jrt::ArrayList<PolylineTrace*>();
  int32_t splits = 0;
  for (ExitStubs_JointEntry* je : *joints) {
    PolylineTrace* trace = je->trace();
    jrt::List<Point*>* points = je->points();
    trace->setFixedState(FixedState::UNFIXED);
    jrt::List<PolylineTrace*>* pieces = new jrt::ArrayList<PolylineTrace*>();
    pieces->add(trace);
    for (Point* point : *points) {
      PolylineTrace* piece = nullptr;
      for (PolylineTrace* p : *pieces) {
        if (p->isOnTheBoard() && !point->equals(p->firstCorner()) && !point->equals(p->lastCorner()) &&
            p->centreLineContains(point)) {
          piece = p;
          break;
        }
      }
      if (piece == nullptr) {
        continue;
      }
      pieces->remove(piece);
      jrt::Array<Trace*>* halves = piece->split(point);
      if (halves != nullptr) {
        for (Trace* h : *halves) {
          if (auto* pt = jrt::fast_cast<PolylineTrace>(h)) {
            pieces->add(pt);
          }
        }
        splits++;
      } else {
        pieces->add(piece);
      }
    }
    for (PolylineTrace* p : *pieces) {
      if (p->isOnTheBoard()) {
        released->add(p);
      }
    }
  }

  int32_t removed = 0;
  for (bool changed = true; changed;) {
    changed = false;
    for (PolylineTrace* piece : *new jrt::ArrayList<PolylineTrace*>(released)) {
      if (piece->isOnTheBoard() && piece->isTail() && !piece->hasInteriorContacts()) {
        board->removeItem(piece);
        removed++;
        changed = true;
      }
    }
  }
  int32_t openAfter = open(board, netSet);
  if (openAfter > openBefore) {
    board->undo(nullptr);
    for (int32_t id : *itemIds) {
      Item* item = board->getItem(id);
      if (item != nullptr) {
        item->setFixedState(PLACED_STATE);
      }
    }
    return "Exit stubs: releasing them would open " + jrt::str(openAfter - openBefore) +
           " connection(s) of their nets (" + jrt::str(openBefore) + " open before, " + jrt::str(open(board, netSet)) +
           " kept); they stay fixed and are written as protected wiring.";
  }
  board->popSnapshot();
  return "Exit stubs: " + jrt::str(traces->size()) + " released as ordinary copper, " + jrt::str(splits) +
         " split where routes join them" +
         (snapped > 0 ? " (" + jrt::str(snapped) + " route ends moved onto them)" : std::string("")) + ", " +
         jrt::str(removed) + " unused pieces removed; " + jrt::str(openAfter) + " connection(s) of their nets open.";
}

int32_t ExitStubs::snapNearJoints(RoutingBoard* board, PolylineTrace* stub) {
  jrt::Array<Point*>* line = stub->polyline()->corners();
  int32_t moved = 0;
  // PORT NOTE: Java HashSet<Item> iterates in identity-hash order (not reproducible run to run);
  // the port uses jrt::HashSet with the deterministic identity hash of jrt::Object.
  jrt::Set<Item*>* contacts = new jrt::HashSet<Item*>();
  for (int32_t i = 0; i < stub->netCount(); i++) {
    contacts->addAll(board->getConnectableItems(stub->getNetNumber(i)));
  }
  for (Item* contact : *contacts) {
    auto* other = jrt::fast_cast<PolylineTrace>(contact);
    if (other == nullptr || other == stub || other->getLayer() != stub->getLayer() || !other->isOnTheBoard() ||
        other->getFixedState() != FixedState::UNFIXED) {
      continue;
    }
    jrt::Array<Point*>* corners = other->polyline()->corners();
    for (int32_t end = 0; end < 2; end++) {
      int32_t index = (end == 0) ? 0 : corners->length - 1;
      Point* point = (*corners)[index];
      auto* grid = jrt::fast_cast<IntPoint>(point);
      if (grid != nullptr && stub->centreLineContains(grid)) {
        continue;
      }
      IntPoint* at = (grid != nullptr) ? grid : point->toFloat()->round();
      IntPoint* onto = stub->centreLineContains(at) ? at : nearestOnLine(line, at);
      if (onto == nullptr) {
        continue;
      }
      (*corners)[index] = onto;
      int32_t layer = other->getLayer();
      int32_t half = other->getHalfWidth();
      int32_t clearanceClass = other->clearanceClassIndex();
      jrt::Array<int32_t>* nets = other->netNumbers;
      board->removeItem(other);
      board->insertTraceWithoutCleaning(new Polyline(corners), layer, half, nets, clearanceClass, FixedState::UNFIXED);
      moved++;
      break;
    }
  }
  return moved;
}

IntPoint* ExitStubs::nearestOnLine(jrt::Array<Point*>* line, IntPoint* at) {
  IntPoint* best = nullptr;
  double bestDistance = 2.0 + 1e-9;
  for (int32_t i = 0; i + 1 < line->length; i++) {
    auto* a = jrt::fast_cast<IntPoint>((*line)[i]);
    auto* b = jrt::fast_cast<IntPoint>((*line)[i + 1]);
    if (a == nullptr || b == nullptr) {
      continue;
    }
    int32_t dx = jrt::Integer::signum(b->x - a->x);
    int32_t dy = jrt::Integer::signum(b->y - a->y);
    int32_t length = jrt::Math::max(jrt::Math::abs(b->x - a->x), jrt::Math::abs(b->y - a->y));
    if (length == 0 || (dx != 0 && dy != 0 && jrt::Math::abs(b->x - a->x) != jrt::Math::abs(b->y - a->y))) {
      continue;
    }
    double t = (static_cast<double>(at->x - a->x) * dx + static_cast<double>(at->y - a->y) * dy) / (dx * dx + dy * dy);
    int32_t k = jrt::l2i(jrt::Math::max(static_cast<int64_t>(0),
                                        jrt::Math::min(static_cast<int64_t>(length), jrt::Math::round(t))));
    IntPoint* q = new IntPoint(a->x + k * dx, a->y + k * dy);
    double distance = jrt::Math::sqrt(jrt::Math::pow(q->x - at->x, 2) + jrt::Math::pow(q->y - at->y, 2));
    if (distance > 0 && distance < bestDistance) {
      best = q;
      bestDistance = distance;
    }
  }
  return best;
}

jrt::List<Point*>* ExitStubs::joints(Trace* trace) {
  jrt::List<Point*>* points = new jrt::ArrayList<Point*>();
  for (Item* contact : *trace->getNormalContacts()) {
    jrt::List<Point*>* ends = new jrt::ArrayList<Point*>();
    if (auto* other = jrt::fast_cast<Trace>(contact)) {
      ends->add(other->firstCorner());
      ends->add(other->lastCorner());
    } else if (auto* drill = jrt::fast_cast<DrillItem>(contact)) {
      ends->add(drill->getCenter());
    }
    for (Point* point : *ends) {
      if (!point->equals(trace->firstCorner()) && !point->equals(trace->lastCorner()) &&
          trace->centreLineContains(point) && !points->contains(point)) {
        points->add(point);
      }
    }
  }
  return points;
}

int32_t ExitStubs::open(RoutingBoard* board, jrt::Collection<int32_t>* nets) {
  int32_t total = 0;
  for (int32_t net : *nets) {
    total += (new NetIncompletes(net, board->getConnectableItems(net), board))->count();
  }
  return total;
}

}  // namespace fr
