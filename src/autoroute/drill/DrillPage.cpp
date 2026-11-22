#include "autoroute/drill/DrillPage.h"

#include "autoroute/AutorouteDiagnostic.h"
#include "autoroute/drill/ExpansionDrill.h"
#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "autoroute/maze/MazeSearchElement.h"
#include "board/facade/RoutingBoard.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "core/library/Padstack.h"
#include "datastructures/ShapeTree.h"
#include "geometry/planar/ConvexShape.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntBox.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/PolylineArea.h"
#include "geometry/planar/PolylineShape.h"
#include "geometry/planar/TileShape.h"
#include "rules/BoardRules.h"
#include "rules/Net.h"
#include "rules/NetClass.h"
#include "rules/Nets.h"
#include "rules/ViaInfo.h"
#include "rules/ViaRule.h"

namespace fr {

DrillPage::DrillPage(IntBox* shape, RoutingBoard* board) {
  this->shape = shape;
  this->board = board;
  mazeSearchElements = new jrt::Array<MazeSearchElement*>(board->getLayerCount());
  for (int32_t i = 0; i < mazeSearchElements->length; i++) {
    (*mazeSearchElements)[i] = new MazeSearchElement();
  }
}

Point* DrillPage::calcPinCenterInDrill(TileShape* drillShape, int32_t layer, RoutingBoard* board) {
  jrt::Collection<Item*>* overlappingItems = board->overlappingItems(drillShape, layer);
  Point* result = nullptr;
  for (Item* currentItem : *overlappingItems) {
    if (auto* currentPin = jrt::fast_cast<Pin>(currentItem)) {
      if (currentPin->drillAllowed() && drillShape->containsInside(currentPin->getCenter())) {
        result = currentPin->getCenter();
      }
    }
  }
  return result;
}

void DrillPage::addPlanePinCandidates(AutorouteEngine* autorouteEngine, TileShape* drillShape,
                                      jrt::Collection<Pin*>* planePins, Net* net) {
  if (planePins->isEmpty() || net->getNetClass() == nullptr) {
    return;
  }
  ViaRule* viaRule = net->getNetClass()->getViaRule();
  if (viaRule == nullptr || viaRule->viaCount() == 0) {
    return;
  }
  ConvexShape* viaShape = viaRule->getVia(0)->getPadstack()->getShape(0);
  if (viaShape == nullptr) {
    return;
  }
  ConvexShape* shrunk = drillShape->shrink(0.5 * viaShape->maxWidth());
  TileShape* inner = jrt::fast_cast<TileShape>(shrunk);
  if (inner == nullptr || inner->isEmpty()) {
    return;
  }
  for (Pin* pin : *planePins) {
    Point* location = inner->nearestPointApprox(pin->getCenter()->toFloat())->round();
    if (!drillShape->contains(location)) {
      continue;
    }
    ExpansionDrill* drill = new ExpansionDrill(drillShape, location, 0, this->board->getLayerCount() - 1);
    if (drill->calculateExpansionRooms(autorouteEngine)) {
      this->drills->add(drill);
    }
  }
}

jrt::Collection<ExpansionDrill*>* DrillPage::getDrills(AutorouteEngine* autorouteEngine, bool attachSmd) {
  return getDrills(autorouteEngine, attachSmd, true);
}

jrt::Collection<ExpansionDrill*>* DrillPage::getDrills(AutorouteEngine* autorouteEngine, bool attachSmd,
                                                       bool centreOfGravityCandidate) {
  if (this->drills == nullptr || autorouteEngine->getNetNumber() != this->netNumber ||
      this->attachSmdCached != attachSmd || this->centreOfGravityCandidateCached != centreOfGravityCandidate) {
    this->netNumber = autorouteEngine->getNetNumber();
    this->attachSmdCached = attachSmd;
    this->centreOfGravityCandidateCached = centreOfGravityCandidate;
    this->drills = new jrt::LinkedList<ExpansionDrill*>();
    ShapeSearchTree* searchTree = autorouteEngine->autorouteSearchTree;
    jrt::Collection<ShapeTree_TreeEntry*>* overlaps = new jrt::LinkedList<ShapeTree_TreeEntry*>();
    searchTree->overlappingTreeEntries(this->shape, -1, overlaps);
    jrt::Collection<TileShape*>* cutoutShapes = new jrt::LinkedList<TileShape*>();
    // drills on top of existing vias are used in the ripup algorithm
    TileShape* prevObstacleShape = IntBox::EMPTY();
    Net* net = this->board->rules->nets->get(this->netNumber);
    bool planeNet = net != nullptr && net->containsPlane();
    jrt::Collection<Pin*>* planePins = new jrt::LinkedList<Pin*>();
    for (ShapeTree_TreeEntry* currentEntry : *overlaps) {
      Item* currentItem = jrt::fast_cast<Item>(currentEntry->object);
      if (currentItem == nullptr) {
        continue;
      }
      if (planeNet) {
        Pin* planePin = jrt::fast_cast<Pin>(currentItem);
        if (planePin != nullptr && planePin->firstLayer() == planePin->lastLayer() &&
            planePin->containsNet(this->netNumber) && !planePins->contains(planePin)) {
          planePins->add(planePin);
        }
      }
      if (currentItem->isDrillable(this->netNumber)) {
        continue;
      }
      if (auto* pin = jrt::fast_cast<Pin>(currentItem)) {
        if (attachSmd && pin->drillAllowed() && pin->containsNet(this->netNumber) && !pin->isPlaneEscapeObstacle()) {
          continue;
        }
      }
      TileShape* currentObstacleShape = currentItem->getTreeShape(searchTree, currentEntry->shapeIndexInObject);
      if (!prevObstacleShape->contains(currentObstacleShape)) {
        // Checked to avoid multiple cutout for example for vias with the same shape on all
        // layers.
        TileShape* currentCutoutShape = currentObstacleShape->intersection(this->shape);
        if (currentCutoutShape->dimension() == 2) {
          cutoutShapes->add(currentCutoutShape);
        }
      }
      prevObstacleShape = currentObstacleShape;
    }
    // PORT NOTE: Java passes the TileShape[] covariantly as PolylineShape[].
    jrt::Array<PolylineShape*>* holes = new jrt::Array<PolylineShape*>(cutoutShapes->size());
    jrt::Iterator<TileShape*>* it = cutoutShapes->iterator();
    for (int32_t i = 0; i < holes->length; i++) {
      (*holes)[i] = it->next();
    }
    PolylineArea* shapeWithHoles = new PolylineArea(this->shape, holes);
    jrt::Array<TileShape*>* drillShapes = shapeWithHoles->splitToConvex(autorouteEngine->stoppableThread);
    if (drillShapes == nullptr) {
      return this->drills;
    }

    // Use the center points of these drill shapes to try making a via.
    int32_t drillFirstLayer = 0;
    int32_t drillLastLayer = this->board->getLayerCount() - 1;
    for (int32_t i = 0; i < drillShapes->length; i++) {
      TileShape* currentDrillShape = (*drillShapes)[i];
      Point* pinCenter = nullptr;
      if (attachSmd) {
        pinCenter = calcPinCenterInDrill(currentDrillShape, drillFirstLayer, autorouteEngine->board);
        if (pinCenter == nullptr) {
          pinCenter = calcPinCenterInDrill(currentDrillShape, drillLastLayer, autorouteEngine->board);
        }
      }
      Point* currentDrillLocation = pinCenter;
      if (currentDrillLocation == nullptr) {
        currentDrillLocation = currentDrillShape->centreOfGravity()->round();
      }
      ExpansionDrill* newDrill =
          new ExpansionDrill(currentDrillShape, currentDrillLocation, drillFirstLayer, drillLastLayer);
      if (newDrill->calculateExpansionRooms(autorouteEngine)) {
        this->drills->add(newDrill);
      }
      addPlanePinCandidates(autorouteEngine, currentDrillShape, planePins, net);
      if (centreOfGravityCandidate && pinCenter != nullptr) {
        Point* centreOfGravity = currentDrillShape->centreOfGravity()->round();
        if (!centreOfGravity->equals(pinCenter)) {
          ExpansionDrill* cogDrill =
              new ExpansionDrill(currentDrillShape, centreOfGravity, drillFirstLayer, drillLastLayer);
          if (cogDrill->calculateExpansionRooms(autorouteEngine)) {
            this->drills->add(cogDrill);
          }
        }
      }
    }
  }
  return this->drills;
}

TileShape* DrillPage::getShape() { return this->shape; }

void DrillPage::reset() {
  if (this->drills != nullptr) {
    for (ExpansionDrill* currentDrill : *this->drills) {
      currentDrill->reset();
    }
  }
  for (MazeSearchElement* currentInfo : *mazeSearchElements) {
    currentInfo->reset();
  }
}

void DrillPage::emitDiagnostics(AutorouteDiagnostic_Sink* sink, double intensity) {
  if (sink == nullptr || intensity <= 0 || drills == nullptr) {
    return;
  }
  for (ExpansionDrill* currentDrill : *drills) {
    currentDrill->emitDiagnostic(sink, intensity);
  }
}

int32_t DrillPage::getId() {
  // Stable hash of shape and netNumber
  return 31 * shape->getId() + netNumber;
}

}  // namespace fr
