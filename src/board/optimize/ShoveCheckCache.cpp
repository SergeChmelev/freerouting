#include "board/optimize/ShoveCheckCache.h"

#include <algorithm>
#include <cstring>
#include <typeinfo>

#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/structure/ShapeEntrySide.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntDirection.h"
#include "geometry/planar/IntOctagon.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Simplex.h"
#include "geometry/planar/TileShape.h"

namespace fr {

// Java: ThreadLocal.withInitial(ShoveCheckCache::new) - one cache per thread (optimizer workers).
ShoveCheckCache* ShoveCheckCache::CURRENT() {
  return jrt::perThread<ShoveCheckCache>([] { return new (jrt::kBoard) ShoveCheckCache(); });
}

ShoveCheckCache* ShoveCheckCache::forThread() { return CURRENT(); }

uint32_t ShoveCheckCache::findSlot(const int32_t* key, int32_t len, int32_t hash,
                                   bool& found) const {
  const uint32_t mask = (uint32_t{1} << kSlotBits) - 1;
  uint32_t i = (static_cast<uint32_t>(hash) * 0x9E3779B1u) >> (32 - kSlotBits);
  for (;; i = (i + 1) & mask) {
    uint64_t s = slots_[i];
    if (static_cast<uint32_t>(s >> 32) != generation_) {
      found = false;
      return i;
    }
    const Rec& r = recs_[static_cast<uint32_t>(s) - 1];
    if (r.hash == hash && static_cast<int32_t>(r.len) == len &&
        std::memcmp(keyInts_.data() + r.off, key, sizeof(int32_t) * len) == 0) {
      found = true;
      return i;
    }
  }
}

ShoveCheckCache_Entry* ShoveCheckCache::tryGet(RoutingBoard* board, int64_t contentStamp,
                                               ShoveCheckCache_Key* key) {
  reset(board, contentStamp);
  if (recs_.empty()) {
    return nullptr;
  }
  int32_t buf[ShoveCheckCache_Key::kMaxInts];
  int32_t len = key->serialize(buf);
  bool found;
  uint32_t slot = findSlot(buf, len, key->hashCode(), found);
  if (!found) {
    return nullptr;
  }
  const Rec& r = recs_[static_cast<uint32_t>(slots_[slot]) - 1];
  if (hitEntry_ == nullptr) {
    hitEntry_ = new (jrt::kBoard) ShoveCheckCache_Entry(false, 0, false, -1, false, -1);
  }
  hitEntry_->result = r.result;
  hitEntry_->ids = r.ids;
  hitEntry_->wroteObstacle = r.wroteObstacle;
  hitEntry_->obstacle = r.obstacle;
  hitEntry_->wroteLayer = r.wroteLayer;
  hitEntry_->layer = r.layer;
  return hitEntry_;
}

void ShoveCheckCache::put(RoutingBoard* board, int64_t contentStamp, ShoveCheckCache_Key* key,
                          ShoveCheckCache_Entry* entry) {
  reset(board, contentStamp);
  if (static_cast<int32_t>(recs_.size()) >= MAX_ENTRIES) {
    clearEntries();
    board->getShoveCacheObstacles()->clear();
    return;
  }
  if (slots_.empty()) {
    slots_.assign(std::size_t{1} << kSlotBits, 0);
  }
  int32_t buf[ShoveCheckCache_Key::kMaxInts];
  int32_t len = key->serialize(buf);
  int32_t hash = key->hashCode();
  bool found;
  uint32_t slot = findSlot(buf, len, hash, found);
  Rec* r;
  if (found) {
    r = &recs_[static_cast<uint32_t>(slots_[slot]) - 1];  // Java: put replaces the value
  } else {
    Rec fresh{};
    fresh.off = static_cast<uint32_t>(keyInts_.size());
    fresh.len = static_cast<uint32_t>(len);
    fresh.hash = hash;
    keyInts_.insert(keyInts_.end(), buf, buf + len);
    recs_.push_back(fresh);
    slots_[slot] = (static_cast<uint64_t>(generation_) << 32) | static_cast<uint32_t>(recs_.size());
    r = &recs_.back();
  }
  r->result = entry->result;
  r->ids = entry->ids;
  r->wroteObstacle = entry->wroteObstacle;
  r->obstacle = entry->obstacle;
  r->wroteLayer = entry->wroteLayer;
  r->layer = entry->layer;
}

void ShoveCheckCache::clearEntries() {
  recs_.clear();
  keyInts_.clear();
  if (++generation_ == 0) {  // the tags wrapped: wipe the table once
    std::fill(slots_.begin(), slots_.end(), 0);
    generation_ = 1;
  }
}

int32_t ShoveCheckCache_Key::serialize(int32_t* out) const {
  int32_t n = 0;
  out[n++] = kind;
  out[n++] = lineCount;
  out[n++] = sideNo;
  out[n++] = static_cast<int32_t>(sideX);
  out[n++] = static_cast<int32_t>(sideX >> 32);
  out[n++] = static_cast<int32_t>(sideY);
  out[n++] = static_cast<int32_t>(sideY >> 32);
  out[n++] = sideHasPoint ? 1 : 0;
  out[n++] = dirX;
  out[n++] = dirY;
  out[n++] = netCount;
  out[n++] = net0;
  out[n++] = net1;
  out[n++] = layer;
  out[n++] = clearanceClass;
  out[n++] = depth;
  out[n++] = viaDepth;
  out[n++] = springOverDepth;
  out[n++] = timed ? 1 : 0;
  // coords: Arrays.equals compares the lengths too (the kind and line count fix them)
  int32_t m = coords == nullptr ? 0 : coords->length;
  for (int32_t i = 0; i < m; i++) out[n++] = (*coords)[i];
  return n;
}

ShoveCheckCache_Key* ShoveCheckCache_Key::heapCopy() {
  jrt::HeapScope heap;  // kOp (the coords clone) on the heap too
  auto* copy = new (jrt::kBoard) ShoveCheckCache_Key(*this);
  copy->coords = this->coords->clone();
  return copy;
}

int32_t ShoveCheckCache::obstacleIndex(RoutingBoard* board, Item* obstacle) {
  jrt::List<Item*>* table = board->getShoveCacheObstacles();
  int32_t index = table->size() - 1;
  if (index >= 0 && table->get(index) == obstacle) {
    return index;
  }
  table->add(obstacle);
  return table->size() - 1;
}

void ShoveCheckCache::reset(RoutingBoard* board, int64_t contentStamp) {
  if (contentStamp == stamp && owner != nullptr && owner == board) {
    return;
  }
  if (!recs_.empty()) {
    clearEntries();
  }
  board->getShoveCacheObstacles()->clear();
  stamp = contentStamp;
  owner = board;
}

bool ShoveCheckCache::supported(TileShape* shape, Direction* dir, jrt::Array<int32_t>* nets) {
  return (jrt::fast_cast<IntOctagon>(shape) != nullptr || jrt::fast_cast<IntBox>(shape) != nullptr ||
          integerSimplex(shape)) &&
         (dir == nullptr || jrt::fast_cast<IntDirection>(dir) != nullptr) &&
         (nets == nullptr || nets->length <= 2);
}

bool ShoveCheckCache::integerSimplex(TileShape* shape) {
  auto* s = jrt::fast_cast<Simplex>(shape);
  if (s == nullptr || typeid(*shape) != typeid(Simplex)) {
    return false;
  }
  int32_t n = s->borderLineCount();
  if (n == 0 || n > MAX_LINES) {
    return false;
  }
  for (int32_t i = 0; i < n; i++) {
    Line* line = s->borderLine(i);
    if (jrt::fast_cast<IntPoint>(line->a) == nullptr ||
        jrt::fast_cast<IntPoint>(line->b) == nullptr) {
      return false;
    }
  }
  return true;
}

jrt::Array<int32_t>* ShoveCheckCache::primitive(int32_t x, int32_t y) {
  int32_t g = static_cast<int32_t>(
      gcd(jrt::Math::abs(static_cast<int64_t>(x)), jrt::Math::abs(static_cast<int64_t>(y))));
  return g == 0 ? jrt::Array<int32_t>::of({0, 0}) : jrt::Array<int32_t>::of({x / g, y / g});
}

int64_t ShoveCheckCache::gcd(int64_t a, int64_t b) {
  while (b != 0) {
    int64_t t = b;
    b = a % b;
    a = t;
  }
  return a;
}

// ---------------------------------------------------------------------------------------------
// ShoveCheckCache.Key

ShoveCheckCache_Key::ShoveCheckCache_Key(TileShape* shape, ShapeEntrySide* fromSide,
                                         Direction* dir, int32_t layer,
                                         jrt::Array<int32_t>* nets, int32_t clearanceClass,
                                         int32_t depth, int32_t viaDepth, int32_t springOverDepth,
                                         bool timed) {
  if (auto* o = jrt::fast_cast<IntOctagon>(shape)) {
    this->kind = 1;
    this->lineCount = 0;
    this->coords = jrt::Array<int32_t>::of({o->leftX, o->bottomY, o->rightX, o->topY,
                                            o->upperLeftDiagonalX, o->lowerRightDiagonalX,
                                            o->lowerLeftDiagonalX, o->upperRightDiagonalX});
  } else if (auto* b = jrt::fast_cast<IntBox>(shape)) {
    this->kind = 2;
    this->lineCount = 0;
    this->coords = jrt::Array<int32_t>::of({b->ll->x, b->ll->y, b->ur->x, b->ur->y});
  } else {
    Simplex* x = jrt::fast_cast<Simplex>(shape);
    this->kind = 3;
    this->lineCount = x->borderLineCount();
    this->coords = new (jrt::kOp) jrt::Array<int32_t>(4 * this->lineCount);
    for (int32_t i = 0; i < this->lineCount; i++) {
      Line* line = x->borderLine(i);
      IntPoint* a = jrt::fast_cast<IntPoint>(line->a);
      IntPoint* bp = jrt::fast_cast<IntPoint>(line->b);
      (*this->coords)[4 * i] = a->x;
      (*this->coords)[4 * i + 1] = a->y;
      (*this->coords)[4 * i + 2] = bp->x;
      (*this->coords)[4 * i + 3] = bp->y;
    }
  }

  if (fromSide == nullptr) {
    this->sideNo = jrt::Integer::MIN_VALUE;
    this->sideX = 0;
    this->sideY = 0;
    this->sideHasPoint = false;
  } else {
    this->sideNo = fromSide->no;
    FloatPoint* p = fromSide->borderIntersection;
    this->sideHasPoint = (p != nullptr);
    this->sideX = p == nullptr ? 0 : jrt::Double::doubleToLongBits(p->x);
    this->sideY = p == nullptr ? 0 : jrt::Double::doubleToLongBits(p->y);
  }

  if (auto* d = jrt::fast_cast<IntDirection>(dir)) {
    jrt::Array<int32_t>* prim = ShoveCheckCache::primitive(d->x, d->y);
    this->dirX = (*prim)[0];
    this->dirY = (*prim)[1];
  } else {
    this->dirX = 0;
    this->dirY = 0;
  }

  this->netCount = (nets == nullptr) ? 0 : nets->length;
  this->net0 = (nets != nullptr && nets->length > 0) ? (*nets)[0] : 0;
  this->net1 = (nets != nullptr && nets->length > 1) ? (*nets)[1] : 0;
  this->layer = layer;
  this->clearanceClass = clearanceClass;
  this->depth = depth;
  this->viaDepth = viaDepth;
  this->springOverDepth = springOverDepth;
  this->timed = timed;

  int32_t h = 17;
  h = 31 * h + kind;
  h = 31 * h + lineCount;
  h = 31 * h + jrt::Arrays::hashCode(coords);
  h = 31 * h + sideNo;
  h = 31 * h + jrt::Long::hashCode(sideX);
  h = 31 * h + jrt::Long::hashCode(sideY);
  h = 31 * h + (sideHasPoint ? 1 : 0);
  h = 31 * h + dirX;
  h = 31 * h + dirY;
  h = 31 * h + netCount;
  h = 31 * h + net0;
  h = 31 * h + net1;
  h = 31 * h + this->layer;
  h = 31 * h + this->clearanceClass;
  h = 31 * h + this->depth;
  h = 31 * h + this->viaDepth;
  h = 31 * h + this->springOverDepth;
  h = 31 * h + (this->timed ? 1 : 0);
  this->hash = h;
}

bool ShoveCheckCache_Key::equals(jrt::Object* obj) {
  if (this == obj) {
    return true;
  }
  auto* o = jrt::fast_cast<ShoveCheckCache_Key>(obj);
  if (o == nullptr) {
    return false;
  }
  return hash == o->hash && kind == o->kind && lineCount == o->lineCount &&
         jrt::Arrays::equals(coords, o->coords) && sideNo == o->sideNo && sideX == o->sideX &&
         sideY == o->sideY && sideHasPoint == o->sideHasPoint && dirX == o->dirX &&
         dirY == o->dirY && netCount == o->netCount && net0 == o->net0 && net1 == o->net1 &&
         layer == o->layer && clearanceClass == o->clearanceClass && depth == o->depth &&
         viaDepth == o->viaDepth && springOverDepth == o->springOverDepth && timed == o->timed;
}

}  // namespace fr
