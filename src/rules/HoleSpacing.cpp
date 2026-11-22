// Port of app.freerouting.rules.HoleSpacing
#include "rules/HoleSpacing.h"

#include "board/facade/BasicBoard.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/Item.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "core/library/BoardLibrary.h"
#include "core/library/Padstack.h"
#include "core/library/Padstacks.h"
#include "datastructures/ShapeTree.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "rules/BoardRules.h"

namespace fr {

bool HoleSpacing::hasHole(Padstack* padstack) {
  return padstack != nullptr && (padstack->holeOnly || padstack->fromLayer() < padstack->toLayer()) &&
         padstack->getDrillRadius() > 0;
}

double HoleSpacing::gap(FloatPoint* center1, double radius1, FloatPoint* center2,
                        double radius2) {
  return center1->distance(center2) - radius1 - radius2;
}

bool HoleSpacing::clear(BasicBoard* board, Padstack* padstack, Point* location, Item* self,
                        jrt::Collection<Item*>* ignore) {
  if (board == nullptr || board->rules == nullptr) {
    return true;
  }
  int32_t minimum = board->rules->getMinHoleToHole();
  if (minimum <= 0 || !hasHole(padstack)) {
    return true;
  }
  return firstTooClose(board, location->toFloat(), padstack->getDrillRadius(),
                       padstack->fromLayer(), padstack->toLayer(), minimum, self,
                       ignore) == nullptr;
}

jrt::List<HoleSpacing_TooCloseItem*>* HoleSpacing::tooClose(BasicBoard* board, FloatPoint* center,
                                                            double radius, int32_t fromLayer,
                                                            int32_t toLayer, double minimum,
                                                            Item* self) {
  auto* result = new jrt::ArrayList<HoleSpacing_TooCloseItem*>();
  visit(board, center, radius, fromLayer, toLayer, minimum, self, nullptr,
        [result](DrillItem* item, double gap) {
          result->add(new HoleSpacing_TooCloseItem(item, gap));
          return true;
        });
  return result;
}

DrillItem* HoleSpacing::firstTooClose(BasicBoard* board, FloatPoint* center, double radius,
                                      int32_t fromLayer, int32_t toLayer, double minimum,
                                      Item* self, jrt::Collection<Item*>* ignore) {
  DrillItem* found = nullptr;
  visit(board, center, radius, fromLayer, toLayer, minimum, self, ignore,
        [&found](DrillItem* item, double gap) {
          found = item;
          return false;
        });
  return found;
}

void HoleSpacing::visit(BasicBoard* board, FloatPoint* center, double radius, int32_t fromLayer,
                        int32_t toLayer, double minimum, Item* self,
                        jrt::Collection<Item*>* ignore,
                        std::function<bool(DrillItem*, double)> report) {
  double maxOtherDrill =
      (board != nullptr && board->library != nullptr && board->library->padstacks != nullptr)
          ? board->library->padstacks->getMaxDrillRadius()
          : 0.0;
  int32_t reach = jrt::d2i(jrt::Math::ceil(radius + minimum + maxOtherDrill)) + 1;
  IntPoint* c = center->round();
  IntBox* box = new IntBox(c->x - reach, c->y - reach, c->x + reach, c->y + reach);
  auto* entries = new jrt::ArrayList<ShapeTree_TreeEntry*>();
  board->searchTreeManager->getDefaultTree()->overlappingTreeEntries(box, -1, entries);
  jrt::Set<DrillItem*>* reported = nullptr;
  for (ShapeTree_TreeEntry* entry : *entries) {
    auto* other = dynamic_cast<DrillItem*>(entry->object);
    if (other == nullptr || static_cast<Item*>(other) == self || other->lastLayer() < fromLayer ||
        other->firstLayer() > toLayer) {
      continue;
    }
    Padstack* otherPadstack = other->getPadstack();
    if (otherPadstack == nullptr || !hasHole(otherPadstack)) {
      continue;
    }
    if (ignore != nullptr && ignore->contains(other)) {
      continue;
    }
    double gap = HoleSpacing::gap(center, radius, other->getCenter()->toFloat(),
                                  otherPadstack->getDrillRadius());
    if (gap >= minimum) {
      continue;
    }
    if (reported != nullptr && reported->contains(other)) {
      continue;
    }
    if (reported == nullptr) {
      reported = new jrt::HashSet<DrillItem*>();
    }
    reported->add(other);
    if (!report(other, gap)) {
      return;
    }
  }
}

}  // namespace fr
