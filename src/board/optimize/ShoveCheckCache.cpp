#include "board/optimize/ShoveCheckCache.h"

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

ShoveCheckCache*& ShoveCheckCache::CURRENT() {
  static ShoveCheckCache* v = new ShoveCheckCache();
  return v;
}

ShoveCheckCache* ShoveCheckCache::forThread() { return CURRENT(); }

ShoveCheckCache_Entry* ShoveCheckCache::tryGet(RoutingBoard* board, int64_t contentStamp,
                                               ShoveCheckCache_Key* key) {
  reset(board, contentStamp);
  return entries->get(key);
}

void ShoveCheckCache::put(RoutingBoard* board, int64_t contentStamp, ShoveCheckCache_Key* key,
                          ShoveCheckCache_Entry* entry) {
  reset(board, contentStamp);
  if (entries->size() >= MAX_ENTRIES) {
    entries->clear();
    board->getShoveCacheObstacles()->clear();
    return;
  }
  entries->put(key, entry);
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
  if (!entries->isEmpty()) {
    entries->clear();
  }
  board->getShoveCacheObstacles()->clear();
  stamp = contentStamp;
  owner = board;
}

bool ShoveCheckCache::supported(TileShape* shape, Direction* dir, jrt::Array<int32_t>* nets) {
  return (dynamic_cast<IntOctagon*>(shape) != nullptr || dynamic_cast<IntBox*>(shape) != nullptr ||
          integerSimplex(shape)) &&
         (dir == nullptr || dynamic_cast<IntDirection*>(dir) != nullptr) &&
         (nets == nullptr || nets->length <= 2);
}

bool ShoveCheckCache::integerSimplex(TileShape* shape) {
  auto* s = dynamic_cast<Simplex*>(shape);
  if (s == nullptr || typeid(*shape) != typeid(Simplex)) {
    return false;
  }
  int32_t n = s->borderLineCount();
  if (n == 0 || n > MAX_LINES) {
    return false;
  }
  for (int32_t i = 0; i < n; i++) {
    Line* line = s->borderLine(i);
    if (dynamic_cast<IntPoint*>(line->a) == nullptr ||
        dynamic_cast<IntPoint*>(line->b) == nullptr) {
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
  if (auto* o = dynamic_cast<IntOctagon*>(shape)) {
    this->kind = 1;
    this->lineCount = 0;
    this->coords = jrt::Array<int32_t>::of({o->leftX, o->bottomY, o->rightX, o->topY,
                                            o->upperLeftDiagonalX, o->lowerRightDiagonalX,
                                            o->lowerLeftDiagonalX, o->upperRightDiagonalX});
  } else if (auto* b = dynamic_cast<IntBox*>(shape)) {
    this->kind = 2;
    this->lineCount = 0;
    this->coords = jrt::Array<int32_t>::of({b->ll->x, b->ll->y, b->ur->x, b->ur->y});
  } else {
    Simplex* x = dynamic_cast<Simplex*>(shape);
    this->kind = 3;
    this->lineCount = x->borderLineCount();
    this->coords = new jrt::Array<int32_t>(4 * this->lineCount);
    for (int32_t i = 0; i < this->lineCount; i++) {
      Line* line = x->borderLine(i);
      IntPoint* a = dynamic_cast<IntPoint*>(line->a);
      IntPoint* bp = dynamic_cast<IntPoint*>(line->b);
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

  if (auto* d = dynamic_cast<IntDirection*>(dir)) {
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
  auto* o = dynamic_cast<ShoveCheckCache_Key*>(obj);
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
