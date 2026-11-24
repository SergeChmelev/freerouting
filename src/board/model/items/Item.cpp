// Port of app.freerouting.board.model.items.Item.
#include "board/model/items/Item.h"
#include "geometry/planar/GeomCopy.h"

#include "autoroute/ItemAutorouteInfo.h"
#include "board/actions/ItemIdGenerator.h"
#include "board/actions/ItemInfoPrinter.h"
#include "board/actions/ItemSearchTreesInfo.h"
#include "board/actions/ItemSelectionFilter.h"
#include "board/facade/BasicBoard.h"
#include "board/model/items/BoardItemType.h"
#include "board/model/items/ComponentObstacleArea.h"
#include "board/model/items/ComponentOutline.h"
#include "board/model/items/ConductionArea.h"
#include "board/model/items/Connectable.h"
#include "board/model/items/DrillItem.h"
#include "board/model/items/ObstacleArea.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/trace/PolylineTrace.h"
#include "board/model/items/Via.h"
#include "board/model/items/ViaObstacleArea.h"
#include "board/model/items/WireObstacleArea.h"
#include "board/model/structure/BoardOutline.h"
#include "board/model/structure/Component.h"
#include "board/model/structure/Components.h"
#include "board/model/structure/FixedState.h"
#include "board/model/structure/Layer.h"
#include "board/model/structure/LayerStructure.h"
#include "board/model/structure/Unit.h"
#include "board/searchtree/ExactCopper.h"
#include "board/searchtree/SearchTreeManager.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/state/BoardObservers.h"
#include "board/state/Communication.h"
#include "board/trace/PolylineTrace.h"
#include "datastructures/IdGenerator.h"
#include "datastructures/ShapeTree.h"
#include "datastructures/UndoableObjects.h"
#include "drc/ClearanceViolation.h"
#include "geometry/planar/Circle.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Shape.h"
#include "geometry/planar/TileShape.h"
#include "geometry/planar/Vector.h"
#include "logger/FRLogger.h"
#include "rules/BoardRules.h"
#include "rules/ClearanceMatrix.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "util/TextManager.h"

namespace fr {

Item::Item(jrt::Array<int32_t>* netNumbers, int32_t clearanceClassIndex, int32_t id,
           int32_t componentId, FixedState fixedState, BasicBoard* board) {
  // PORT NOTE (stage E): the item's own copy, in the item's domain (an optimizer worker item
  // lives in the candidate arena; a heap item's array is freed with it by BasicBoard::discard).
  if (netNumbers == nullptr) {
    this->netNumbers = new (jrt::Like{this}) jrt::Array<int32_t>(0);
  } else {
    this->netNumbers = new (jrt::Like{this}) jrt::Array<int32_t>(netNumbers->length);
    jrt::arraycopy(netNumbers, 0, this->netNumbers, 0, netNumbers->length);
  }
  this->clearanceClassIndex_ = clearanceClassIndex;
  this->componentId = componentId;
  this->fixedState = fixedState;
  this->board = board;
  if (id <= 0) {
    this->id = board->communication->idGenerator->newId();
  } else {
    this->id = id;
  }
}

int32_t Item::compareTo(jrt::Object* other) {
  int32_t result;
  if (auto* item = jrt::fast_cast<Item>(other)) {
    result = item->id - id;
  } else if (auto* probe = jrt::fast_cast<ItemIdProbe>(other)) {
    result = probe->id - id;  // PORT NOTE: id lookups (ItemIdProbe)
  } else {
    result = 1;
  }
  return result;
}

int32_t ItemIdProbe::compareTo(jrt::Object* other) {
  if (auto* item = jrt::fast_cast<Item>(other)) {
    return item->getId() - id;
  }
  if (auto* probe = jrt::fast_cast<ItemIdProbe>(other)) {
    return probe->id - id;
  }
  return 1;
}

BoardItemType Item::getBoardItemType() {
  if (jrt::fast_cast<Pin>(this) != nullptr) {
    return BoardItemType::PIN;
  }
  if (jrt::fast_cast<Via>(this) != nullptr) {
    return BoardItemType::VIA;
  }
  if (jrt::fast_cast<Trace>(this) != nullptr) {
    return BoardItemType::TRACE;
  }
  if (jrt::fast_cast<ConductionArea>(this) != nullptr) {
    return BoardItemType::CONDUCTION_AREA;
  }
  if (jrt::fast_cast<ViaObstacleArea>(this) != nullptr) {
    return BoardItemType::VIA_OBSTACLE_AREA;
  }
  if (jrt::fast_cast<WireObstacleArea>(this) != nullptr) {
    return BoardItemType::WIRE_OBSTACLE_AREA;
  }
  if (jrt::fast_cast<ComponentObstacleArea>(this) != nullptr) {
    return BoardItemType::COMPONENT_OBSTACLE_AREA;
  }
  if (jrt::fast_cast<ObstacleArea>(this) != nullptr) {
    return BoardItemType::OBSTACLE_AREA;
  }
  if (jrt::fast_cast<ComponentOutline>(this) != nullptr) {
    return BoardItemType::COMPONENT_OUTLINE;
  }
  if (jrt::fast_cast<BoardOutline>(this) != nullptr) {
    return BoardItemType::BOARD_OUTLINE;
  }
  return BoardItemType::OTHER;
}

bool Item::containsNet(int32_t netNumber) {
  if (netNumber <= 0) {
    return false;
  }
  for (int32_t i = 0; i < netNumbers->length; i++) {
    if ((*netNumbers)[i] == netNumber) {
      return true;
    }
  }
  return false;
}

bool Item::sharesNetNo(jrt::Array<int32_t>* netNumbers) {
  for (int32_t i = 0; i < this->netNumbers->length; i++) {
    for (int32_t j = 0; j < netNumbers->length; j++) {
      if ((*this->netNumbers)[i] == (*netNumbers)[j]) {
        return true;
      }
    }
  }
  return false;
}

TileShape* Item::getTileShape(int32_t index) {
  if (this->board == nullptr) {
    FRLogger::warn("Item.get_tile_shape: app.freerouting.board is null");
    return nullptr;
  }
  return getTreeShape(this->board->searchTreeManager->getDefaultTree(), index);
}

int32_t Item::treeShapeCount(ShapeTree* tree) {
  if (this->board == nullptr) {
    return 0;
  }
  jrt::Array<TileShape*>* precalculatedTreeShapes = this->getPrecalculatedTreeShapes(tree);
  return precalculatedTreeShapes->length;
}

TileShape* Item::getTreeShape(ShapeTree* tree, int32_t index) {
  if (this->board == nullptr) {
    return nullptr;
  }
  jrt::Array<TileShape*>* precalculatedTreeShapes = this->getPrecalculatedTreeShapes(tree);
  if (precalculatedTreeShapes == nullptr || index < 0 ||
      index >= precalculatedTreeShapes->length) {
    this->clearDerivedData();
    precalculatedTreeShapes = this->getPrecalculatedTreeShapes(tree);
  }
  if (precalculatedTreeShapes == nullptr || index < 0 ||
      index >= precalculatedTreeShapes->length) {
    return nullptr;
  }
  return (*precalculatedTreeShapes)[index];
}

jrt::Array<TileShape*>* Item::getPrecalculatedTreeShapes(ShapeTree* tree) {
  if (this->searchTreesInfo == nullptr) {
    this->searchTreesInfo = newSearchTreesInfo();
  }
  jrt::Array<TileShape*>* precalculatedTreeShapes =
      this->searchTreesInfo->getPrecalculatedTreeShapes(tree);
  if (precalculatedTreeShapes == nullptr) {
    // PORT NOTE (stage E): cache in the holder's domain; other items than traces: in the board's
    // cache region (a trace's tree shapes are freed one by one through its board's keeper).
    std::optional<jrt::DomainScope> domain;
    std::optional<ItemCacheScope> cacheScope;
    if (jrt::fast_cast<PolylineTrace>(this) != nullptr) {
      domain.emplace(this);
    } else {
      cacheScope.emplace(this);
    }
    // Java: (ShapeSearchTree) tree -- ClassCastException if it is not one.
    auto* searchTree = jrt::fast_cast<ShapeSearchTree>(tree);
    if (searchTree == nullptr && tree != nullptr) {
      throw jrt::ClassCastException("Item.getPrecalculatedTreeShapes: not a ShapeSearchTree");
    }
    if (domain.has_value()) {
      // A trace's offset shapes need many temporaries (offset and cut lines, bounding shapes,
      // intersections): computed in a region of their own, only the shapes are kept.
      jrt::LocalGeomRegion work;
      work.setTakesOps(true);
      precalculatedTreeShapes = geomcopy::keep(this->calculateTreeShapes(searchTree), this);
    } else {
      precalculatedTreeShapes = this->calculateTreeShapes(searchTree);
    }
    this->searchTreesInfo->setPrecalculatedTreeShapes(precalculatedTreeShapes, tree);
  }
  return precalculatedTreeShapes;
}

jrt::Object* Item::clone() {
  Item* dup = copy(this->getId());

  dup->onTheBoard = this->onTheBoard;
  // dup.searchTreesInfo = this.searchTreesInfo;

  return dup;
}

void Item::moveBy(Vector* vector) {
  board->itemList->saveForUndo(this);
  board->searchTreeManager->remove(this);
  this->translateBy(vector);
  board->searchTreeManager->insert(this);
  if (jrt::fast_cast<Pin>(this) != nullptr) {
    board->invalidateEdgePinNetCache();
  }

  // let the observers synchronize the changes
  if ((board->communication != nullptr) && (board->communication->observers != nullptr)) {
    board->communication->observers->notifyChanged(this);
  }
}

bool Item::sharesLayer(Item* other) {
  int32_t maxFirstLayer = jrt::Math::max(this->firstLayer(), other->firstLayer());
  int32_t minLastLayer = jrt::Math::min(this->lastLayer(), other->lastLayer());
  return maxFirstLayer <= minLastLayer;
}

int32_t Item::firstCommonLayer(Item* other) {
  int32_t maxFirstLayer = jrt::Math::max(this->firstLayer(), other->firstLayer());
  int32_t minLastLayer = jrt::Math::min(this->lastLayer(), other->lastLayer());
  if (maxFirstLayer > minLastLayer) {
    return -1;
  }
  return maxFirstLayer;
}

int32_t Item::lastCommonLayer(Item* other) {
  int32_t maxFirstLayer = jrt::Math::max(this->firstLayer(), other->firstLayer());
  int32_t minLastLayer = jrt::Math::min(this->lastLayer(), other->lastLayer());
  if (maxFirstLayer > minLastLayer) {
    return -1;
  }
  return minLastLayer;
}

std::optional<std::string> Item::componentName() {
  if (componentId <= 0) {
    return std::nullopt;
  }
  return board->components->get(componentId)->name;
}

int32_t Item::clearanceViolationCount() {
  jrt::Collection<ClearanceViolation*>* violations = this->clearanceViolations();
  return violations->size();
}

jrt::Collection<ClearanceViolation*>* Item::clearanceViolations() {
  jrt::Collection<ClearanceViolation*>* result = new (jrt::kOp) jrt::LinkedList<ClearanceViolation*>();
  if (this->board == nullptr) {
    return result;
  }
  // Both candidate selection and the narrow phase need physical shapes. Default-tree
  // shapes may already include compensation; expanding those again creates false DRC.
  ShapeSearchTree* drcTree = board->searchTreeManager->getUncompensatedTree();
  for (int32_t i = 0; i < treeShapeCount(drcTree); i++) {
    TileShape* currentTileShape = getTreeShape(drcTree, i);
    // Erik Gavriluk (freerouting.net): a pin's or via's copper (a circle, a turned pad),
    // measured where the tiles overlap (router.exact_copper).
    bool exactCopper = board->rules->isExactCopper() && !drcTree->isClearanceCompensationUsed();
    ExactCopper* exactThis = exactCopper ? drcTree->exactCopperOf(this, i) : nullptr;
    // Erik Gavriluk (freerouting.net): test only items that are obstacles to this item
    jrt::Collection<ShapeTree_TreeEntry*>* currentOverlappingItems =
        drcTree->overlappingTreeEntriesWithClearance(currentTileShape, shapeLayer(i),
                                                     jrt::DisposeOnExit(new (jrt::kOp) jrt::Array<int32_t>(0)).get(),
                                                     this->clearanceClassIndex_, this, exactThis);
    jrt::DisposeAllOnExit<ShapeTree_TreeEntry> currentOverlappingItemsOwner(currentOverlappingItems);
    for (ShapeTree_TreeEntry* currentEntry : *currentOverlappingItems) {
      auto* currentItem = jrt::fast_cast<Item>(currentEntry->object);
      if (currentItem == nullptr ||
          static_cast<jrt::Object*>(currentEntry->object) == static_cast<jrt::Object*>(this)) {
        continue;
      }
      bool isObstacle = currentItem->isObstacle(this);
      // The stretch of the outline next to an edge pin does not block the traces of its nets.
      if (isObstacle) {
        auto* edgeTrace = jrt::fast_cast<Trace>(this);
        auto* outline = jrt::fast_cast<BoardOutline>(currentItem);
        if (edgeTrace != nullptr && outline != nullptr &&
            outline->exemptsTrace(currentEntry->shapeIndexInObject, edgeTrace->netNumbers)) {
          isObstacle = false;
        }
      }
      if (isObstacle) {
        auto* outline = jrt::fast_cast<BoardOutline>(this);
        auto* edgeTrace = jrt::fast_cast<Trace>(currentItem);
        if (outline != nullptr && edgeTrace != nullptr &&
            outline->exemptsTrace(i, edgeTrace->netNumbers)) {
          isObstacle = false;
        }
      }
      if (isObstacle && jrt::fast_cast<Trace>(this) != nullptr &&
          jrt::fast_cast<Trace>(currentItem) != nullptr) {
        auto* thisTrace = jrt::fast_cast<Trace>(this);
        // Look, if both traces are connected to the same tie pin or partner pins of the same
        // net-tie cluster. In this case they are allowed to overlap without sharing a net.
        Point* contactPoint = thisTrace->firstCorner();
        bool contactFound = false;
        jrt::Collection<Item*>* currentContacts = thisTrace->getNormalContacts(contactPoint, true);
        {
          if (currentContacts->contains(currentItem)) {
            contactFound = true;
          }
        }
        if (!contactFound) {
          contactPoint = thisTrace->lastCorner();
          currentContacts = thisTrace->getNormalContacts(contactPoint, true);
          {
            if (currentContacts->contains(currentItem)) {
              contactFound = true;
            }
          }
        }
        if (contactFound) {
          for (Item* currentContact : *currentContacts) {
            if (auto* contactPin = jrt::fast_cast<Pin>(currentContact)) {
              if (currentContact->sharesNet(this) && currentContact->sharesNet(currentItem)) {
                isObstacle = false;
                break;
              }
              if (contactPin->getNetTieClusterId() > 0) {
                isObstacle = false;
                break;
              }
            }
          }
        }
      }

      // Trace-to-Pin clearance exemption at a net-tie junction:
      // A trace terminating on its net-tie target pin is allowed to overlap foreign partner
      // pins belonging to the same net-tie cluster.
      if (isObstacle && ((jrt::fast_cast<Trace>(this) != nullptr &&
                          jrt::fast_cast<Pin>(currentItem) != nullptr) ||
                         (jrt::fast_cast<Pin>(this) != nullptr &&
                          jrt::fast_cast<Trace>(currentItem) != nullptr))) {
        Trace* trace = (jrt::fast_cast<Trace>(this) != nullptr) ? jrt::fast_cast<Trace>(this)
                                                               : jrt::fast_cast<Trace>(currentItem);
        Pin* pin = (jrt::fast_cast<Pin>(this) != nullptr) ? jrt::fast_cast<Pin>(this)
                                                         : jrt::fast_cast<Pin>(currentItem);
        if (pin->getNetTieClusterId() > 0) {
          Point* p1 = trace->firstCorner();
          Point* p2 = trace->lastCorner();
          jrt::DisposeOnExit loopSource1(trace->getNormalContacts(p1, true));
          for (Item* contact : *loopSource1) {
            auto* cp = jrt::fast_cast<Pin>(contact);
            if (cp != nullptr && cp->getNetTieClusterId() == pin->getNetTieClusterId()) {
              isObstacle = false;
              break;
            }
          }
          if (isObstacle) {
            jrt::DisposeOnExit loopSource2(trace->getNormalContacts(p2, true));
            for (Item* contact : *loopSource2) {
              auto* cp = jrt::fast_cast<Pin>(contact);
              if (cp != nullptr && cp->getNetTieClusterId() == pin->getNetTieClusterId()) {
                isObstacle = false;
                break;
              }
            }
          }
        }
      }

      if (isObstacle) {
        auto* thisOutline = jrt::fast_cast<BoardOutline>(this);
        auto* thisTrace = jrt::fast_cast<Trace>(this);
        auto* otherTrace = jrt::fast_cast<Trace>(currentItem);
        auto* otherOutline = jrt::fast_cast<BoardOutline>(currentItem);
        if (thisOutline != nullptr && otherTrace != nullptr) {
          if (!thisOutline->blocksNets(otherTrace->netNumbers)) {
            isObstacle = false;
          }
        } else if (thisTrace != nullptr && otherOutline != nullptr) {
          if (!otherOutline->blocksNets(thisTrace->netNumbers)) {
            isObstacle = false;
          }
        } else if ((thisOutline != nullptr && jrt::fast_cast<Pin>(currentItem) != nullptr) ||
                   (jrt::fast_cast<Pin>(this) != nullptr && otherOutline != nullptr)) {
          BoardOutline* outline = (thisOutline != nullptr) ? thisOutline : otherOutline;
          // Use the actual pad tile-shape (not just its center) to determine containment.
          // A pin whose center is inside but whose pad shape protrudes outside (e.g. edge
          // connectors, castellated pads) must still be reported as an obstacle.
          TileShape* pinTileShape =
              (jrt::fast_cast<Pin>(this) != nullptr)
                  ? currentTileShape
                  : currentItem->getTreeShape(drcTree, currentEntry->shapeIndexInObject);
          if (pinTileShape != nullptr && outlineContainsTileShape(outline, pinTileShape)) {
            isObstacle = false;
          }
        }
      }

      if (isObstacle) {
        // Get the two shapes the clearance is calculated between
        TileShape* shape1 = currentTileShape;
        TileShape* shape2 = currentItem->getTreeShape(drcTree, currentEntry->shapeIndexInObject);
        if (shape1 == nullptr || shape2 == nullptr) {
          FRLogger::warn(jrt::format(
              "Item.clearanceViolations: unexpected null shape (%s is null) between"
              " item1=%s[id=%d, layer=%d] and item2=%s[id=%d, shapeIdx=%d]",
              shape1 == nullptr ? "shape1" : "shape2", jrt::className(this).c_str(),
              this->getId(), shapeLayer(i), jrt::className(currentItem).c_str(),
              currentItem->getId(), currentEntry->shapeIndexInObject));
          continue;
        }

        // Calculate the expected minimum clearance between these two shapes.
        // KiCad keeps no copper clearance within a net; items of one net that are still
        // obstacles to each other (a via and an own-net pad it may not overlap) only may not
        // overlap.
        bool sameNet = this->sharesNet(currentItem);
        double minimumClearance =
            sameNet ? 0
                    : board->rules->clearanceMatrix->getValue(currentItem->clearanceClassIndex_,
                                                              this->clearanceClassIndex_,
                                                              shapeLayer(i), false);

        int32_t clComp1 = static_cast<int32_t>(jrt::Math::round(0.5 * minimumClearance));
        int32_t clComp2 = static_cast<int32_t>(jrt::Math::round(minimumClearance - clComp1));

        TileShape* enlargedShape1 =
            (clComp1 > 0) ? jrt::fast_cast<TileShape>(shape1->enlarge(clComp1)) : shape1;
        TileShape* enlargedShape2 =
            (clComp2 > 0) ? jrt::fast_cast<TileShape>(shape2->enlarge(clComp2)) : shape2;

        TileShape* intersection = enlargedShape1->intersection(enlargedShape2);
        if (intersection->dimension() == 2) {
          Circle* exact1 = nullptr;
          Circle* exact2 = nullptr;
          ShapeSearchTree* defaultTree = board->searchTreeManager->getDefaultTree();
          {
            auto* drill1 = jrt::fast_cast<DrillItem>(this);
            auto* drill2 = jrt::fast_cast<DrillItem>(currentItem);
            if (drill1 != nullptr && drill2 != nullptr) {
              auto* circle1 = jrt::fast_cast<Circle>(drill1->getShapeOnLayer(shapeLayer(i)));
              if (circle1 != nullptr) {
                auto* circle2 = jrt::fast_cast<Circle>(drill2->getShapeOnLayer(shapeLayer(i)));
                if (circle2 != nullptr &&
                    defaultTree->drillHoleClearanceDelta(drill1, circle1, shapeLayer(i)) == 0 &&
                    defaultTree->drillHoleClearanceDelta(drill2, circle2, shapeLayer(i)) == 0) {
                  exact1 = circle1;
                  exact2 = circle2;
                }
              }
            }
          }
          bool exactCircles = exact1 != nullptr && exact2 != nullptr;
          double circleGap =
              exactCircles ? exact1->center->distance(exact2->center) - exact1->radius -
                                 exact2->radius
                           : 0;
          // freerouting.net (router.exact_copper): a pin's or via's copper (with the hole
          // envelope its tile carries, so the drill's hole clearance does not disappear)
          // against the other copper; a turned pad's octagon reaches past its slanted edges.
          ExactCopper* exactOther =
              exactCopper ? drcTree->exactCopperOf(currentItem, currentEntry->shapeIndexInObject)
                          : nullptr;
          bool measuredCopper = false;
          double copperGap = 0;
          if (exactThis != nullptr || exactOther != nullptr) {
            ExactCopper* copper1 = exactThis != nullptr ? exactThis : ExactCopper::of(shape1);
            ExactCopper* copper2 = exactOther != nullptr ? exactOther : ExactCopper::of(shape2);
            if (copper1 != nullptr && copper2 != nullptr) {
              measuredCopper = true;
              copperGap = ExactCopper::distance(copper1, copper2);
            }
          }
          double actualClearance =
              measuredCopper ? copperGap
              : exactCircles ? jrt::Math::max(0.0, circleGap)
                             : calculateClearanceBetweenTwoShapes(shape1, shape2, minimumClearance,
                                                                  clComp1, clComp2);

          double shortfall = minimumClearance - actualClearance;
          double boardUnitToUmFactor =
              (this->board != nullptr && this->board->communication != nullptr)
                  ? Unit_scale(1.0, this->board->communication->unit, Unit::UM) /
                        jrt::Math::max(1, this->board->communication->resolution)
                  : 1.0;
          double toleranceUm = (this->board != nullptr && this->board->rules != nullptr)
                                   ? this->board->rules->clearanceToleranceUm
                                   : 1.0;
          if (!jrt::Double::isFinite(toleranceUm) || toleranceUm < 0) {
            toleranceUm = 0.0;
          }
          double shortfallUm = shortfall * boardUnitToUmFactor;

          // Erik Gavriluk (freerouting.net): copper overlapping an obstacle that keeps no
          // clearance (a keepout of the null class, FR-100) measured 0 against 0 and was never
          // reported: e.g. a track inside mounting hole H4's keepout. An overlap deeper than the
          // tolerance is a violation whatever the clearance.
          if (shortfallUm > toleranceUm ||
              (minimumClearance <= 0 &&
               (exactCircles ? -circleGap * boardUnitToUmFactor > toleranceUm
                             : overlapsBeyond(shape1, shape2,
                                              toleranceUm / boardUnitToUmFactor)))) {
            if ((smallestClearance < 0) || (actualClearance < smallestClearance)) {
              smallestClearance = actualClearance;
            }

            ClearanceViolation* currentViolation =
                new (jrt::kOp) ClearanceViolation(this, currentItem, intersection, shapeLayer(i),
                                       minimumClearance, actualClearance);
            result->add(currentViolation);
          }
        }
      }
    }
  }
  return result;
}

bool Item::overlapsBeyond(TileShape* shape1, TileShape* shape2, double depth) {
  TileShape* shrunk = depth > 0 ? jrt::fast_cast<TileShape>(shape1->offset(-depth)) : shape1;
  return !shrunk->isEmpty() && shrunk->intersection(shape2)->dimension() == 2;
}

double Item::calculateClearanceBetweenTwoShapes(TileShape* rawShape1, TileShape* rawShape2,
                                                double minimumClearance, int32_t clComp1,
                                                int32_t clComp2) {
  if (rawShape1->intersection(rawShape2)->dimension() == 2) {
    return 0.0;
  }
  double low = 0.0;
  double high = minimumClearance;
  double sumComp = clComp1 + clComp2;
  double factor1 = (sumComp > 0) ? (static_cast<double>(clComp1) / sumComp) : 0.5;
  double factor2 = (sumComp > 0) ? (static_cast<double>(clComp2) / sumComp) : 0.5;

  for (int32_t iter = 0; iter < 16; iter++) {
    double mid = (low + high) * 0.5;
    TileShape* s1 = jrt::fast_cast<TileShape>(rawShape1->enlarge(mid * factor1));
    TileShape* s2 = jrt::fast_cast<TileShape>(rawShape2->enlarge(mid * factor2));
    if (s1->intersection(s2)->dimension() == 2) {
      high = mid;
    } else {
      low = mid;
    }
  }
  return low;
}

bool Item::outlineContainsTileShape(BoardOutline* outline, TileShape* tileShape) {
  int32_t cornerCount = tileShape->borderLineCount();
  for (int32_t ci = 0; ci < cornerCount; ci++) {
    Point* corner = tileShape->corner(ci);
    if (!outline->contains(corner)) {
      return false;
    }
  }
  return true;
}

jrt::Set<Item*>* Item::getAllContacts() {
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  if (jrt::fast_cast<Connectable>(this) == nullptr) {
    return result;
  }
  for (int32_t i = 0; i < this->tileShapeCount(); i++) {
    jrt::Collection<SearchTreeObject*>* overlappingItems =
        board->overlappingObjects(getTileShape(i), shapeLayer(i));
    for (SearchTreeObject* currentObject : *overlappingItems) {
      auto* currentItem = jrt::fast_cast<Item>(currentObject);
      if (currentItem == nullptr) {
        continue;
      }
      if (currentItem != this && jrt::fast_cast<Connectable>(currentItem) != nullptr &&
          currentItem->sharesNet(this)) {
        result->add(currentItem);
      }
    }
  }
  return result;
}

jrt::Set<Item*>* Item::getAllContacts(int32_t layer) {
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  if (jrt::fast_cast<Connectable>(this) == nullptr) {
    return result;
  }
  for (int32_t i = 0; i < this->tileShapeCount(); i++) {
    if (this->shapeLayer(i) != layer) {
      continue;
    }
    jrt::Collection<SearchTreeObject*>* overlappingItems =
        board->overlappingObjects(getTileShape(i), layer);
    for (SearchTreeObject* currentObject : *overlappingItems) {
      auto* currentItem = jrt::fast_cast<Item>(currentObject);
      if (currentItem == nullptr) {
        continue;
      }
      if (currentItem != this && jrt::fast_cast<Connectable>(currentItem) != nullptr &&
          currentItem->sharesNet(this)) {
        result->add(currentItem);
      }
    }
  }
  return result;
}

bool Item::isConnected() {
  jrt::Collection<Item*>* contacts = this->getAllContacts();
  return !contacts->isEmpty();
}

bool Item::isConnectedOnLayer(int32_t layer) {
  jrt::Collection<Item*>* contactsOnLayer = this->getAllContacts(layer);
  return !contactsOnLayer->isEmpty();
}

jrt::Set<Item*>* Item::getNormalContacts() { return new (jrt::kOp) jrt::TreeSet<Item*>(); }

jrt::Collection<Item*>* Item::normalContactsForWalk() {
  return board == nullptr ? static_cast<jrt::Collection<Item*>*>(getNormalContacts())
                          : static_cast<jrt::Collection<Item*>*>(board->cachedNormalContacts(this));
}

jrt::Set<Item*>* Item::getConnectedSet(int32_t netNumber, bool stopAtPlane) {
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  if (netNumber > 0 && !this->containsNet(netNumber)) {
    return result;
  }
  result->add(this);
  getConnectedSetRecu(result, netNumber, stopAtPlane);
  ConductionArea::addLaterReachedIslands(result, netNumber, stopAtPlane);
  return result;
}

void Item::getConnectedSetRecu(jrt::Set<Item*>* result, int32_t netNumber, bool stopAtPlane) {
  jrt::Collection<Item*>* contactList = normalContactsForWalk();
  if (contactList == nullptr) {
    return;
  }
  for (Item* currentContact : *contactList) {
    if (stopAtPlane && jrt::fast_cast<ConductionArea>(currentContact) != nullptr &&
        currentContact->getComponentId() <= 0) {
      continue;
    }
    if (netNumber > 0 && !currentContact->containsNet(netNumber)) {
      continue;
    }
    if (result->add(currentContact) || (jrt::fast_cast<ConductionArea>(currentContact) != nullptr &&
                                        jrt::fast_cast<ConductionArea>(currentContact)->isFractured())) {
      currentContact->getConnectedSetRecu(result, netNumber, stopAtPlane);
    }
  }
}

bool Item::isCycleRecu(jrt::Set<Item*>* visitedItems, Item* searchItem, Item* comeFromItem,
                       bool ignoreAreas) {
  if (ignoreAreas && jrt::fast_cast<ConductionArea>(this) != nullptr) {
    return false;
  }
  jrt::Collection<Item*>* contactList = normalContactsForWalk();
  if (contactList == nullptr) {
    return false;
  }
  for (Item* currentContact : *contactList) {
    if (currentContact == comeFromItem) {
      continue;
    }
    if (currentContact == searchItem) {
      return true;
    }
    if (visitedItems->add(currentContact)) {
      if (currentContact->isCycleRecu(visitedItems, searchItem, this, ignoreAreas)) {
        return true;
      }
    }
  }
  return false;
}

jrt::Set<Item*>* Item::getUnconnectedSet(int32_t netNumber) {
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  if (netNumber > 0 && !this->containsNet(netNumber)) {
    return result;
  }
  if (netNumber > 0) {
    result->addAll(board->getConnectableItems(netNumber));
  } else {
    for (int32_t currentNetNumber : *this->netNumbers) {
      result->addAll(board->getConnectableItems(currentNetNumber));
    }
  }
  result->removeAll(jrt::DisposeOnExit(this->getConnectedSet(netNumber)).get());
  return result;
}

jrt::Set<Item*>* Item::getConnectionItems(Item_StopConnectionOption stopOption) {
  jrt::DisposeOnExit<jrt::Set<Item*>> contactsOwner(this->getNormalContacts());
  jrt::Set<Item*>* contacts = contactsOwner.get();
  jrt::Set<Item*>* result = new (jrt::kOp) jrt::TreeSet<Item*>();
  if (auto* trace = jrt::fast_cast<Trace>(this)) {
    if (trace->hasInteriorContacts()) {
      return result;
    }
  }
  if (this->isRoutable()) {
    result->add(this);
  }
  for (Item* contactItem : *contacts) {
    Item* currentItem = contactItem;
    Point* prevContactPoint = this->normalContactPoint(currentItem);
    if (prevContactPoint == nullptr) {
      // no unique contact point
      continue;
    }
    int32_t prevContactLayer = this->firstCommonLayer(currentItem);
    if (auto* startTrace = jrt::fast_cast<Trace>(this)) {
      // Check, that there is only 1 contact at this location.
      // Only for pins and vias items of more than 1 connection
      // are collected
      jrt::Collection<Item*>* checkContacts = startTrace->getNormalContacts(prevContactPoint, false);
      if (checkContacts->size() != 1) {
        continue;
      }
    }
    // Search from currentItem along the contacts
    // until the next fork or nonroute item.
    Item* ringItem = currentItem;
    Point* ringPoint = prevContactPoint;
    int32_t ringLayer = prevContactLayer;
    int64_t ringSteps = 0;
    int64_t ringMark = 1;
    for (;;) {
      if (++ringSteps > 1 && currentItem == ringItem && prevContactLayer == ringLayer &&
          prevContactPoint->equals(ringPoint)) {
        break;  // around the ring once: every item on it is in result
      }
      if (ringSteps == ringMark) {
        ringItem = currentItem;
        ringPoint = prevContactPoint;
        ringLayer = prevContactLayer;
        ringMark *= 2;
      }
      if (!currentItem->isRoutable() || (jrt::fast_cast<Trace>(currentItem) != nullptr &&
                                         jrt::fast_cast<Trace>(currentItem)->hasInteriorContacts())) {
        // An unsplit interior junction is a fork, not a removable whole connection item.
        break;
      }
      if (jrt::fast_cast<Via>(currentItem) != nullptr) {
        if (stopOption == Item_StopConnectionOption::VIA) {
          break;
        }
        if (stopOption == Item_StopConnectionOption::FANOUT_VIA) {
          if (currentItem->isFanoutVia(result)) {
            break;
          }
        }
      }
      result->add(currentItem);
      jrt::DisposeOnExit<jrt::Collection<Item*>> currentObContactsOwner(currentItem->getNormalContacts());
      jrt::Collection<Item*>* currentObContacts = currentObContactsOwner.get();
      // filter the contacts at the previous contact point,
      // because we were already there.
      // If then there is not exactly 1 new contact left, there is
      // a stub or a fork.
      Point* nextContactPoint = nullptr;
      int32_t nextContactLayer = -1;
      Item* nextContact = nullptr;
      bool forkFound = false;
      for (Item* tmpContact : *currentObContacts) {
        int32_t tmpContactLayer = currentItem->firstCommonLayer(tmpContact);
        if (tmpContactLayer >= 0) {
          Point* tmpContactPoint = currentItem->normalContactPoint(tmpContact);
          if (tmpContactPoint == nullptr) {
            // no unique contact point
            forkFound = true;
            break;
          }
          if (prevContactLayer != tmpContactLayer || !prevContactPoint->equals(tmpContactPoint)) {
            if (nextContact != nullptr) {
              // second new contact found
              forkFound = true;
              break;
            }
            nextContact = tmpContact;
            nextContactPoint = tmpContactPoint;
            nextContactLayer = tmpContactLayer;
          }
        }
      }
      if (nextContact == nullptr || forkFound) {
        break;
      }
      currentItem = nextContact;
      prevContactPoint = nextContactPoint;
      prevContactLayer = nextContactLayer;
    }
  }
  return result;
}

bool Item::validate() {
  bool result = board->searchTreeManager->validateEntries(this);
  for (int32_t i = 0; i < this->tileShapeCount(); i++) {
    TileShape* currentShape = this->getTileShape(i);
    if (currentShape->isEmpty()) {
      FRLogger::warn("Item.validate: shape is empty");
      result = false;
    }
  }
  return result;
}

bool Item::isUserFixed() {
  return static_cast<int32_t>(fixedState) >= static_cast<int32_t>(FixedState::USER_FIXED);
}

bool Item::isDeletionForbidden() {
  // Items belonging to a component are delete_fixed.
  if (this->componentId > 0 || isUserFixed()) {
    return true;
  }
  // Also power planes are delete_fixed.
  if (auto* area = jrt::fast_cast<ConductionArea>(this)) {
    return !(*this->board->layerStructure->layers)[area->getLayer()]->isSignal;
  }
  return false;
}

bool Item::isShoveFixed() {
  return static_cast<int32_t>(this->fixedState) >= static_cast<int32_t>(FixedState::SHOVE_FIXED);
}

void Item::setFixedState(FixedState fixedState) {
  FixedState oldState = this->fixedState;
  this->fixedState = fixedState;
  noteIfPreservedChanged(oldState);
}

void Item::unfix() {
  // PORT NOTE: fixedState is never null in the port (see Item.h), `fixedState != null` is true.
  if (!FixedState_isFix(fixedState)) {
    FixedState oldState = fixedState;
    jrt::List<Point*>* junctions = nullptr;
    if (FixedState_isPreservedRoute(oldState)) {
      if (auto* trace = jrt::fast_cast<Trace>(this)) {
        if (isOnTheBoard()) {
          junctions = trace->interiorJunctions();
        }
      }
    }
    fixedState = FixedState::UNFIXED;
    noteIfPreservedChanged(oldState);
    if (junctions != nullptr && !junctions->isEmpty()) {
      static_cast<Trace*>(jrt::fast_cast<Trace>(this))->splitAt(junctions);
    }
  }
}

void Item::noteIfPreservedChanged(FixedState oldState) {
  if (jrt::fast_cast<Trace>(this) != nullptr &&
      FixedState_isPreservedRoute(oldState) != FixedState_isPreservedRoute(this->fixedState)) {
    if (this->board != nullptr) {
      this->board->noteNetAssignmentChanged();
    }
  }
}

bool Item::isPreservedRoute() { return FixedState_isPreservedRoute(this->fixedState); }

bool Item::isConnectable() {
  return (jrt::fast_cast<Connectable>(this) != nullptr) && this->netCount() > 0;
}

bool Item::removeFromNet(int32_t netNumber) {
  int32_t foundIndex = -1;
  for (int32_t i = 0; i < this->netNumbers->length; i++) {
    if ((*this->netNumbers)[i] == netNumber) {
      foundIndex = i;
    }
  }
  if (foundIndex < 0) {
    return false;
  }
  auto* newNetNoArr = new (jrt::kOp) jrt::Array<int32_t>(this->netNumbers->length - 1);
  jrt::arraycopy(this->netNumbers, 0, newNetNoArr, 0, foundIndex);
  if (foundIndex < newNetNoArr->length) {
    // copy remaining elements if present
    jrt::arraycopy(this->netNumbers, foundIndex + 1, newNetNoArr, foundIndex,
                   newNetNoArr->length - foundIndex);
  }
  this->netNumbers = newNetNoArr;
  if (this->board != nullptr) {
    this->board->noteNetAssignmentChanged();
  }
  return true;
}

void Item::setClearanceClassIndex(int32_t index) {
  if (index < 0 || index >= this->board->rules->clearanceMatrix->getClassCount()) {
    FRLogger::warn("Item.set_clearance_class_no: index out of range");
    return;
  }
  clearanceClassIndex_ = index;
  if (this->board != nullptr) {
    this->board->itemList->noteContentChange();
  }
}

void Item::changeClearanceClassIndex(int32_t index) {
  if (index < 0 || index >= this->board->rules->clearanceMatrix->getClassCount()) {
    FRLogger::warn("Item.set_clearance_class_no: index out of range");
    return;
  }
  clearanceClassIndex_ = index;
  this->clearDerivedData();
  if (this->board != nullptr) {
    this->board->noteChange(this);
  }
  if (this->board != nullptr && this->board->searchTreeManager->isClearanceCompensationUsed()) {
    // reinsert the item into the search tree, because the compensated shape has changed.
    this->board->searchTreeManager->remove(this);
    this->board->searchTreeManager->insert(this);
  }
}

void Item::assignComponentId(int32_t id) {
  componentId = id;
  clearDerivedData();
}

void Item::assignNetNo(int32_t netNumber) {
  if (!Nets::isNormalNetNumber(netNumber)) {
    return;
  }
  if (netNumber > board->rules->nets->maxNetNumber()) {
    FRLogger::warn("Item.assign_net_no: netNumber to big");
    return;
  }
  board->itemList->saveForUndo(this);
  if (netNumber <= 0) {
    netNumbers = new (jrt::kBoard) jrt::Array<int32_t>(0);
  } else {
    if (netNumbers->length == 0) {
      netNumbers = new (jrt::kBoard) jrt::Array<int32_t>(1);
    } else if (netNumbers->length > 1) {
      FRLogger::warn("Item.assign_net_no: unexpected netCount > 1");
    }
    (*netNumbers)[0] = netNumber;
  }
  if (this->board != nullptr) {
    this->board->noteNetAssignmentChanged();
  }
  if (jrt::fast_cast<Pin>(this) != nullptr) {
    board->invalidateEdgePinNetCache();
  }
}

void Item::assignNetNumbers(jrt::Array<int32_t>* netNumbers) {
  for (int32_t netNumber : *netNumbers) {
    if (!Nets::isNormalNetNumber(netNumber) || netNumber > board->rules->nets->maxNetNumber()) {
      FRLogger::warn("Item.assign_net_numbers: netNumber " + jrt::str(netNumber) +
                     " is not a board net");
      return;
    }
  }
  board->itemList->saveForUndo(this);
  this->netNumbers = netNumbers->clone();
  if (this->board != nullptr) {
    this->board->noteNetAssignmentChanged();
  }
  if (jrt::fast_cast<Pin>(this) != nullptr) {
    board->invalidateEdgePinNetCache();
  }
}

bool Item::isSelectedByFixedFilter(ItemSelectionFilter* filter) {
  bool result;
  if (this->isUserFixed()) {
    result = filter->isSelected(ItemSelectionFilter_SelectableChoices::FIXED);
  } else {
    result = filter->isSelected(ItemSelectionFilter_SelectableChoices::UNFIXED);
  }
  return result;
}

ItemSearchTreesInfo* Item::newSearchTreesInfo() {
  // PORT NOTE (stage E, E12): a temporary item of an operation region (the substitute trace
  // pieces of a shove check) is never on a board and is never disposed: its info and tree shapes
  // live in its region, with no keeper. (On the heap they leaked: 130 MB on Issue110, about 2 GB
  // on bm01 with four optimizer threads.)
  if (jrt::in_live_geom_region(this)) {
    jrt::DomainScope domain(this);
    return new (jrt::kGeom) ItemSearchTreesInfo();
  }
  // PORT NOTE (stage E): a trace's tree shapes are computed for it alone (offset shapes of its
  // polyline): its info owns them through the board's shape keeper.
  TreeShapeKeeper* keeper = jrt::fast_cast<PolylineTrace>(this) != nullptr ? board : nullptr;
  return new (jrt::kBoard) ItemSearchTreesInfo(keeper);
}

void Item::clearSearchTreeEntries() {
  jrt::dispose(this->searchTreesInfo);
  this->searchTreesInfo = nullptr;
}

void Item::setSearchTreeEntries(jrt::Array<ShapeTree_Leaf*>* treeEntries, ShapeTree* tree) {
  if (this->board == nullptr) {
    return;
  }
  if (this->searchTreesInfo == nullptr) {
    this->searchTreesInfo = newSearchTreesInfo();
  }
  this->searchTreesInfo->setTreeEntries(treeEntries, tree);
}

jrt::Array<ShapeTree_Leaf*>* Item::getSearchTreeEntries(ShapeSearchTree* tree) {
  if (this->searchTreesInfo == nullptr) {
    return nullptr;
  }
  return this->searchTreesInfo->getTreeEntries(tree);
}

void Item::setPrecalculatedTreeShapes(jrt::Array<TileShape*>* shapes, ShapeSearchTree* tree) {
  if (this->board == nullptr) {
    return;
  }
  if (this->searchTreesInfo == nullptr) {
    FRLogger::warn("Item.set_precalculated_tree_shapes searchTreesInfo not allocated");
    return;
  }
  this->searchTreesInfo->setPrecalculatedTreeShapes(geomcopy::keep(shapes, this), tree);
}

ItemAutorouteInfo* Item::getAutorouteInfo() {
  if (autorouteInfo == nullptr) {
    // PORT NOTE (stage E): a derived cache (dropped by clearAutorouteInfo / clearDerivedData):
    // in the item's domain, like its other caches.
    jrt::DomainScope domain(this);
    autorouteInfo = new (jrt::kOp) ItemAutorouteInfo(this);
  }
  return autorouteInfo;
}

void Item::clearDerivedData() {
  if (this->searchTreesInfo != nullptr) {
    this->searchTreesInfo->clearPrecalculatedTreeShapes();
  }
  autorouteInfo = nullptr;
}

std::string Item::getNetHoverInfo(const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  std::string sb;
  for (int32_t i = 0; i < this->netCount(); i++) {
    if (i > 0) {
      sb += "<br>";
    }
    Net* currentNet = board->rules->nets->get(this->getNetNumber(i));
    sb += tm->getText("net_hover_info", {currentNet->name});
  }
  return sb;
}

void Item::printNetInfo(ItemInfoPrinter* printer, const std::string& locale) {
  TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

  for (int32_t i = 0; i < this->netCount(); i++) {
    printer->append(", " + tm->getText("net") + " ");
    Net* currentNet = board->rules->nets->get(this->getNetNumber(i));
    printer->append(currentNet->name, tm->getText("net_info"), currentNet);
  }
}

void Item::printClearanceInfo(ItemInfoPrinter* printer, const std::string& locale) {
  if (this->clearanceClassIndex_ > 0) {
    TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

    printer->append(", " + tm->getText("clearanceClass") + " ");
    std::string name = board->rules->clearanceMatrix->getName(this->clearanceClassIndex_);
    printer->append(name, tm->getText("clearance_info"),
                    board->rules->clearanceMatrix->getRow(this->clearanceClassIndex_));
  }
}

void Item::printFixedInfo(ItemInfoPrinter* printer, const std::string& locale) {
  if (this->fixedState != FixedState::UNFIXED) {
    TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

    printer->append(", ");
    printer->append(tm->getText(FixedState_toString(this->fixedState)));
  }
}

void Item::printContactInfo(ItemInfoPrinter* printer, const std::string& locale) {
  jrt::Collection<Item*>* contacts = this->getNormalContacts();
  if (!contacts->isEmpty()) {
    TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

    printer->append(", " + tm->getText("contacts") + " ");
    int32_t contactCount = contacts->size();
    printer->appendItems(jrt::str(contactCount), tm->getText("contact_info"), contacts);
  }
}

void Item::printClearanceViolationInfo(ItemInfoPrinter* printer, const std::string& locale) {
  jrt::Collection<ClearanceViolation*>* clearanceViolations = this->clearanceViolations();
  if (!clearanceViolations->isEmpty()) {
    TextManager* tm = new (jrt::kBoard) TextManager(jrt::className(this), locale);

    printer->append(", ");
    int32_t violationCount = clearanceViolations->size();
    auto* violations = new (jrt::kOp) jrt::LinkedList<ItemInfoPrinter_Printable*>();
    for (ClearanceViolation* v : *clearanceViolations) {
      violations->add(v);
    }
    printer->appendObjects(jrt::str(violationCount), tm->getText("violation_info"), violations);
    if (violationCount == 1) {
      printer->append(" " + tm->getText("clearance_violation"));
    } else {
      printer->append(" " + tm->getText("clearanceViolations"));
    }
  }
}

void Item::printConnectableItemInfo(ItemInfoPrinter* printer, const std::string& locale) {
  this->printClearanceInfo(printer, locale);
  this->printFixedInfo(printer, locale);
  this->printNetInfo(printer, locale);
  this->printContactInfo(printer, locale);
  this->printClearanceViolationInfo(printer, locale);
}

void Item::printItemInfo(ItemInfoPrinter* printer, const std::string& locale) {
  this->printClearanceInfo(printer, locale);
  this->printFixedInfo(printer, locale);
  this->printClearanceViolationInfo(printer, locale);
}

bool Item::netsNormal() {
  for (int32_t i = 0; i < this->netNumbers->length; i++) {
    if (!Nets::isNormalNetNumber((*this->netNumbers)[i])) {
      return false;
    }
  }
  return true;
}

bool Item::netsEqual(jrt::Array<int32_t>* netNumbers) {
  if (this->netNumbers->length != netNumbers->length) {
    return false;
  }
  for (int32_t currentNetNumber : *netNumbers) {
    if (!this->containsNet(currentNetNumber)) {
      return false;
    }
  }
  return true;
}

bool Item::isFanoutVia(jrt::Set<Item*>* ignoreItems) {
  jrt::DisposeOnExit<jrt::Collection<Item*>> contactListOwner(this->getNormalContacts());
  jrt::Collection<Item*>* contactList = contactListOwner.get();
  for (Item* currentContact : *contactList) {
    if (jrt::fast_cast<Pin>(currentContact) != nullptr &&
        currentContact->firstLayer() == currentContact->lastLayer() &&
        jrt::DisposeOnExit(currentContact->getNormalContacts())->size() <= 1) {
      return true;
    }
    if (auto* currentTrace = jrt::fast_cast<Trace>(currentContact)) {
      if (ignoreItems != nullptr && ignoreItems->contains(currentContact)) {
        continue;
      }
      if (currentTrace->getLength() >= PROTECT_FANOUT_LENGTH * currentTrace->getHalfWidth()) {
        continue;
      }
      jrt::DisposeOnExit<jrt::Collection<Item*>> traceContactListOwner(currentTrace->getNormalContacts());
      jrt::Collection<Item*>* traceContactList = traceContactListOwner.get();
      for (Item* tmpContact : *traceContactList) {
        if (jrt::fast_cast<Pin>(tmpContact) != nullptr &&
            tmpContact->firstLayer() == tmpContact->lastLayer() &&
            jrt::DisposeOnExit(tmpContact->getNormalContacts())->size() <= 1) {
          return true;
        }
        auto* contactTrace = jrt::fast_cast<PolylineTrace>(tmpContact);
        if (contactTrace != nullptr && tmpContact->getFixedState() == FixedState::SHOVE_FIXED) {
          // look for shove fixed exit traces of SMD-pins
          if (contactTrace->cornerCount() == 2) {
            return true;
          }
        }
      }
    }
  }
  return false;
}

bool Item::hasIgnoredNets() {
  for (int32_t netNumber : *this->netNumbers) {
    Net* net = this->board->rules->nets->get(netNumber);
    if (net != nullptr && net->getNetClass() != nullptr &&
        net->getNetClass()->isIgnoredByAutorouter) {
      return true;
    }
  }

  return false;
}

std::string Item::toString() {
  std::string simpleName;

  simpleName += jrt::toLowerCase(jrt::className(this));

  if (componentId > 0) {
    simpleName += " of component #";
    simpleName += jrt::str(componentId);
  }

  return simpleName;
}

jrt::List<Net*>* Item::getAllNets() {
  jrt::List<Net*>* nets = new (jrt::kOp) jrt::ArrayList<Net*>();
  for (int32_t netNumber : *this->netNumbers) {
    Net* net = board->rules->nets->get(netNumber);
    if (net != nullptr) {
      nets->add(net);
    }
  }
  return nets;
}

std::string Item::getAllNetNames() {
  jrt::List<Net*>* nets = this->getAllNets();
  if (nets->isEmpty()) {
    return "no nets";
  }
  std::string result;
  bool first = true;
  for (Net* net : *nets) {
    if (!first) {
      result += ",";
    }
    result += net->toString();
    first = false;
  }
  return result;
}

}  // namespace fr
