#include "autoroute/pipeline/BallEscapeOrder.h"

#include "autoroute/pipeline/BallFields.h"
#include "autoroute/pipeline/TwoLayerBga.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/structure/LayerStructure.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"

namespace fr {

bool BallEscapeOrder_Entry::equals(jrt::Object* o) {
  if (o == this) return true;
  auto* other = jrt::fast_cast<BallEscapeOrder_Entry>(o);
  if (other == nullptr) return false;
  return ringKey_ == other->ringKey_ && position_ == other->position_ &&
         jrt::equalsOf(item_, other->item_);
}

int32_t BallEscapeOrder_Entry::hashCode() {
  int32_t h = 0;
  h = jrt::hashCombine(h, jrt::hashOf(ringKey_));
  h = jrt::hashCombine(h, jrt::hashOf(position_));
  h = jrt::hashCombine(h, jrt::hashOf(item_));
  return h;
}

std::string BallEscapeOrder_Entry::toString() {
  return "Entry[ringKey=" + jrt::str(ringKey_) + ", position=" + jrt::str(position_) +
         ", item=" + jrt::str(static_cast<jrt::Object*>(item_)) + "]";
}

jrt::Set<std::string>* BallEscapeOrder::MODES() {
  static jrt::Set<std::string>* v = jrt::onHeap([&] { return new (jrt::kBoard) jrt::HashSet<std::string>({"auto", "off", "outer_first", "inner_first"}); });
  return v;
}

int32_t BallEscapeOrder::getBalls() { return ringByPin->size(); }

std::optional<int32_t> BallEscapeOrder::getRing(int32_t pinId) { return ringByPin->getOpt(pinId); }

std::string BallEscapeOrder::checkMode(const std::optional<std::string>& modeIn) {
  std::string mode;
  if (!modeIn.has_value()) {
    mode = "auto";
  } else {
    mode = *modeIn;
  }
  if (!MODES()->contains(mode)) {
    throw jrt::IllegalArgumentException(
        "router.autorouter.ball_escape_order must be auto, off, outer_first or inner_first");
  }
  return mode;
}

BallEscapeOrder* BallEscapeOrder::of(BasicBoard* board, const std::optional<std::string>& modeIn) {
  std::string mode = checkMode(modeIn);
  if ("auto" == mode) {
    if (board == nullptr) {
      return nullptr;
    }
    mode = board->layerStructure->signalLayerCount() == 2 ? "inner_first" : "off";
  }
  if ("off" == mode || board == nullptr) {
    return nullptr;
  }
  // One walk over the board's pins, grouped by part in the board's item order
  jrt::Map<int32_t, jrt::List<Pin*>*>* byPart = new (jrt::kOp) jrt::HashMap<int32_t, jrt::List<Pin*>*>();
  jrt::DisposeOnExit loopSource1(board->getPins());
  for (Pin* pin : *loopSource1) {
    int32_t part = pin->getComponentId();
    if (part <= 0) {
      continue;
    }
    byPart->computeIfAbsent(part, [](const int32_t&) -> jrt::List<Pin*>* { return new (jrt::kOp) jrt::ArrayList<Pin*>(); })
        ->add(pin);
  }
  jrt::Map<int32_t, int32_t>* rings = new (jrt::kOp) jrt::HashMap<int32_t, int32_t>();
  for (jrt::List<Pin*>* pins : *byPart->values()) {
    if (pins->size() < BallFields::MIN_BALLS || !TwoLayerBga::isBallGrid(pins)) {
      continue;
    }
    jrt::List<FloatPoint*>* centres = new (jrt::kOp) jrt::ArrayList<FloatPoint*>(pins->size());
    for (Pin* p : *pins) {
      centres->add(p->getCenter()->toFloat());
    }
    jrt::Array<int32_t>* ring = BallEscapeOrder::rings(centres);
    for (int32_t i = 0; i < pins->size(); i++) {
      rings->put(pins->get(i)->getId(), (*ring)[i]);
    }
  }
  return rings->isEmpty() ? nullptr : new (jrt::kOp) BallEscapeOrder(rings, "outer_first" == mode);
}

jrt::Array<int32_t>* BallEscapeOrder::rings(jrt::List<FloatPoint*>* centres) {
  double pitch = jrt::Double::MAX_VALUE;
  for (int32_t i = 0; i < centres->size(); i++) {
    for (int32_t j = i + 1; j < centres->size(); j++) {
      double d = centres->get(i)->distance(centres->get(j));
      if (d > 0 && d < pitch) {
        pitch = d;
      }
    }
  }
  double x0 = jrt::Double::MAX_VALUE;
  double x1 = -jrt::Double::MAX_VALUE;
  double y0 = jrt::Double::MAX_VALUE;
  double y1 = -jrt::Double::MAX_VALUE;
  for (FloatPoint* c : *centres) {
    if (c->x < x0) {
      x0 = c->x;
    }
    if (c->x > x1) {
      x1 = c->x;
    }
    if (c->y < y0) {
      y0 = c->y;
    }
    if (c->y > y1) {
      y1 = c->y;
    }
  }
  auto* result = new (jrt::kOp) jrt::Array<int32_t>(centres->size());
  for (int32_t i = 0; i < centres->size(); i++) {
    FloatPoint* c = centres->get(i);
    double edge = jrt::Math::min(jrt::Math::min(c->x - x0, x1 - c->x), jrt::Math::min(c->y - y0, y1 - c->y));
    (*result)[i] = pitch == jrt::Double::MAX_VALUE ? 0 : jrt::l2i(jrt::Math::round(edge / pitch));
  }
  return result;
}

int32_t BallEscapeOrder::moveToFront(jrt::List<Item*>* items) {
  jrt::List<BallEscapeOrder_Entry*>* front = new (jrt::kOp) jrt::ArrayList<BallEscapeOrder_Entry*>();
  jrt::List<Item*>* rest = new (jrt::kOp) jrt::ArrayList<Item*>(items->size());
  for (int32_t i = 0; i < items->size(); i++) {
    Item* item = items->get(i);
    auto* pin = jrt::fast_cast<Pin>(item);
    std::optional<int32_t> ring = (pin != nullptr) ? getRing(pin->getId()) : std::nullopt;
    if (ring.has_value()) {
      int32_t ringKey = outerFirst ? *ring : -*ring;
      front->add(new (jrt::kOp) BallEscapeOrder_Entry(ringKey, i, item));
    } else {
      rest->add(item);
    }
  }
  if (front->isEmpty()) {
    return 0;
  }
  front->sort(jrt::thenComparing<BallEscapeOrder_Entry*>(
      jrt::comparingInt<BallEscapeOrder_Entry*>([](BallEscapeOrder_Entry* e) { return e->ringKey(); }),
      jrt::comparingInt<BallEscapeOrder_Entry*>([](BallEscapeOrder_Entry* e) { return e->position(); })));
  items->clear();
  for (BallEscapeOrder_Entry* e : *front) {
    items->add(e->item());
  }
  items->addAll(rest);
  return front->size();
}

}  // namespace fr
