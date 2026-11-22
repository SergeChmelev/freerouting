#include "autoroute/maze/MazeRipupResolver.h"

#include "autoroute/expansion/CompleteExpansionRoom.h"
#include "autoroute/expansion/ExpandableObject.h"
#include "autoroute/expansion/ExpansionDoor.h"
#include "autoroute/expansion/ObstacleExpansionRoom.h"
#include "autoroute/maze/AutorouteControl.h"
#include "autoroute/maze/AutorouteEngine.h"
#include "autoroute/maze/MazeListElement.h"
#include "autoroute/maze/MazeSearchEngine.h"
#include "autoroute/path/Connection.h"
#include "board/model/items/Item.h"
#include "board/model/items/Pin.h"
#include "board/model/items/Trace.h"
#include "board/model/items/Via.h"
#include "board/model/structure/FixedState.h"
#include "board/searchtree/SearchTreeObject.h"
#include "board/searchtree/ShapeSearchTree.h"
#include "board/trace/PolylineTrace.h"
#include "geometry/planar/Direction.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/IntPoint.h"
#include "geometry/planar/Line.h"
#include "geometry/planar/Point.h"
#include "geometry/planar/Polyline.h"
#include "geometry/planar/TileShape.h"
#include "logger/FRLogger.h"
#include "settings/RouterSettings.h"

namespace fr {

double MazeRipupResolver::calcFanoutViaRipupCostFactor(Trace* trace) {
  jrt::Collection<Item*>* currentEndContacts;
  for (int32_t i = 0; i < 2; i++) {
    if (i == 0) {
      currentEndContacts = trace->getStartContacts();
    } else {
      currentEndContacts = trace->getEndContacts();
    }
    if (currentEndContacts->size() != 1) {
      continue;
    }
    Item* currentTraceContact = currentEndContacts->iterator()->next();
    bool protectFanoutVia = false;
    if (jrt::fast_cast<Pin>(currentTraceContact) != nullptr &&
        currentTraceContact->firstLayer() == currentTraceContact->lastLayer()) {
      protectFanoutVia = true;
    } else if (auto* contactTrace = jrt::fast_cast<PolylineTrace>(currentTraceContact);
               contactTrace != nullptr &&
               currentTraceContact->getFixedState() == FixedState::SHOVE_FIXED) {
      if (contactTrace->cornerCount() == 2) {
        protectFanoutVia = true;
      }
    }

    if (protectFanoutVia) {
      double fanoutViaCostFactor = trace->getHalfWidth() / trace->getLength();
      fanoutViaCostFactor *= fanoutViaCostFactor;
      fanoutViaCostFactor *= FANOUT_COST_CONSTANT;
      return jrt::Math::max(fanoutViaCostFactor, 1.0);
    }
  }
  return 1;
}

int32_t MazeRipupResolver::checkRipup(MazeListElement* listElement, Item* obstacleItem,
                                      bool doorIsSmall) {
  AutorouteControl* ctrl = search->ctrl;
  if (!obstacleItem->isRoutable()) {
    return -1;
  }
  if (!ctrl->netsNotToRip->isEmpty()) {
    for (int32_t i = 0; i < obstacleItem->netCount(); i++) {
      if (ctrl->netsNotToRip->contains(obstacleItem->getNetNumber(i))) {
        return -1;
      }
    }
  }
  if (doorIsSmall && !enterThroughSmallDoor(listElement, obstacleItem)) {
    return -1;
  }
  CompleteExpansionRoom* previousRoom = listElement->door->otherRoom(listElement->nextRoom);
  bool roomWasShoved = listElement->adjustment != MazeSearchElement_Adjustment::NONE;
  Item* previousItem = nullptr;
  if (auto* room = jrt::fast_cast<ObstacleExpansionRoom>(previousRoom)) {
    previousItem = room->getItem();
  }
  if (roomWasShoved) {
    if (previousItem != nullptr && previousItem != obstacleItem &&
        previousItem->sharesNet(obstacleItem)) {
      return -1;
    }
  } else if (previousItem == obstacleItem) {
    return MazeSearchEngine::ALREADY_RIPPED_COSTS;
  }

  double fanoutViaCostFactor = 1.0;
  double costFactor = 1;
  bool preserveFanoutProtection = !ctrl->removeUnconnectedVias &&
                                  ctrl->ripupCosts <= (ctrl->settings->getStartRipupCosts() * 2);
  if (auto* obstacleTrace = jrt::fast_cast<Trace>(obstacleItem)) {
    costFactor = obstacleTrace->getHalfWidth();
    if (preserveFanoutProtection) {
      fanoutViaCostFactor = calcFanoutViaRipupCostFactor(obstacleTrace);
    }
  } else if (jrt::fast_cast<Via>(obstacleItem) != nullptr) {
    bool lookIfFanoutVia = preserveFanoutProtection;
    jrt::Collection<Item*>* contactList = obstacleItem->getNormalContacts();
    int32_t contactCount = 0;
    for (Item* currentContact : *contactList) {
      auto* obstacleTrace = jrt::fast_cast<Trace>(currentContact);
      if (obstacleTrace == nullptr || currentContact->isUserFixed()) {
        return -1;
      }
      ++contactCount;
      costFactor = jrt::Math::max(costFactor, static_cast<double>(obstacleTrace->getHalfWidth()));
      if (lookIfFanoutVia && !ctrl->isFanout) {
        double currentFanoutViaCostFactor = calcFanoutViaRipupCostFactor(obstacleTrace);
        if (currentFanoutViaCostFactor > 1) {
          fanoutViaCostFactor = currentFanoutViaCostFactor;
          lookIfFanoutVia = false;
        }
      }
    }
    if (fanoutViaCostFactor <= 1) {
      costFactor *= 0.5 * jrt::Math::max(contactCount - 1, 0);
    }
  }

  double ripupCost = ctrl->ripupCosts * costFactor;
  double detour = 1;
  double traceLength = 0;
  double minTraceLength = 0;
  int32_t itemCount = 0;
  std::string connectionItemIds = "[]";
  if (fanoutViaCostFactor <= 1 && !ctrl->isFanout) {
    Connection* obstacleConnection = Connection::get(obstacleItem);
    if (obstacleConnection != nullptr) {
      detour = obstacleConnection->getDetour();
      traceLength = obstacleConnection->traceLength();
      itemCount = obstacleConnection->itemList->size();
      if (obstacleConnection->startPoint != nullptr && obstacleConnection->endPoint != nullptr) {
        minTraceLength = obstacleConnection->startPoint->toFloat()->distance(
            obstacleConnection->endPoint->toFloat());
      }
      std::string sb = "[";
      for (Item* connectionItem : *obstacleConnection->itemList) {
        if (sb.length() > 1) {
          sb += ",";
        }
        sb += jrt::str(connectionItem->getId());
      }
      sb += "]";
      connectionItemIds = sb;
    }
  }
  bool randomize = ctrl->ripupPassNo >= 4 && ctrl->ripupPassNo % 3 != 0;
  if (randomize) {
    double randomNumber = search->randomGenerator->nextDouble();
    double randomFactor = 0.5 + randomNumber * randomNumber;
    detour *= randomFactor;
  }
  ripupCost /= detour;
  ripupCost *= fanoutViaCostFactor;
  int32_t result = jrt::Math::max(jrt::d2i(ripupCost), 1);
  const int32_t maxRipupCosts = jrt::Integer::MAX_VALUE / 100;
  result = jrt::Math::min(result, maxRipupCosts);
  jrt::Array<int32_t>* nets = new jrt::Array<int32_t>(obstacleItem->netCount());
  for (int32_t i = 0; i < nets->length; i++) {
    (*nets)[i] = obstacleItem->getNetNumber(i);
  }
  if (FRLogger::isTraceEnabled()) {
    // Java: java.util.Arrays.toString(int[])
    std::string netsString = "[";
    for (int32_t i = 0; i < nets->length; i++) {
      if (i > 0) {
        netsString += ", ";
      }
      netsString += jrt::str((*nets)[i]);
    }
    netsString += "]";
    FRLogger::trace("CHECK_RIPUP net=" + jrt::str(ctrl->netNumber) +
                    ", obstacle_id=" + jrt::str(obstacleItem->getId()) +
                    ", obstacle_nets=" + netsString + ", connectionItems=" + connectionItemIds +
                    ", halfWidth=" + jrt::str(costFactor) +
                    ", ripupCosts=" + jrt::str(ctrl->ripupCosts) +
                    ", traceLength=" + jrt::str(traceLength) +
                    ", minTraceLength=" + jrt::str(minTraceLength) +
                    ", itemCount=" + jrt::str(itemCount) + ", detour=" + jrt::str(detour) +
                    ", result=" + jrt::str(result));
  }
  return result;
}

bool MazeRipupResolver::checkLeavingRippedItem(MazeListElement* listElement) {
  auto* currentDoor = jrt::fast_cast<ExpansionDoor>(listElement->door);
  if (currentDoor == nullptr) {
    return false;
  }
  CompleteExpansionRoom* fromRoom = currentDoor->otherRoom(listElement->nextRoom);
  auto* obstacleRoom = jrt::fast_cast<ObstacleExpansionRoom>(fromRoom);
  if (obstacleRoom == nullptr) {
    return false;
  }
  Item* currentItem = obstacleRoom->getItem();
  if (!currentItem->isRoutable()) {
    return false;
  }
  return enterThroughSmallDoor(listElement, currentItem);
}

bool MazeRipupResolver::enterThroughSmallDoor(MazeListElement* listElement, Item* ignoreItem) {
  if (listElement->door->getDimension() != 1) {
    return false;
  }
  TileShape* doorShape = listElement->door->getShape();
  Line* doorLine = nullptr;
  FloatPoint* prevCorner = doorShape->cornerApprox(0);
  int32_t cornerCount = doorShape->borderLineCount();
  for (int32_t i = 1; i < cornerCount; i++) {
    FloatPoint* nextCorner = doorShape->cornerApprox(i);
    if (nextCorner->distanceSquare(prevCorner) > 1) {
      doorLine = doorShape->borderLine(i - 1);
      break;
    }
    prevCorner = nextCorner;
  }
  if (doorLine == nullptr) {
    return false;
  }

  IntPoint* doorCenter = doorShape->centreOfGravity()->round();
  int32_t currentLayer = listElement->nextRoom->getLayer();
  int32_t checkRadius = (*search->ctrl->compensatedTraceHalfWidth)[currentLayer] +
                        AutorouteEngine::TRACE_WIDTH_TOLERANCE;
  jrt::Array<Line*>* lines = new jrt::Array<Line*>(3);
  (*lines)[0] = doorLine->translate(checkRadius);
  (*lines)[1] = new Line(doorCenter, doorLine->direction()->turn45Degree(2));
  (*lines)[2] = doorLine->translate(-checkRadius);

  Polyline* checkPolyline = new Polyline(lines);
  TileShape* checkShape = checkPolyline->offsetShape(checkRadius, 0);
  jrt::Array<int32_t>* ignoreNetNos = new jrt::Array<int32_t>(1);
  (*ignoreNetNos)[0] = search->ctrl->netNumber;
  jrt::Set<SearchTreeObject*>* overlappingObjects = new jrt::TreeSet<SearchTreeObject*>();
  search->autorouteEngine->autorouteSearchTree->overlappingObjects(checkShape, currentLayer,
                                                                   ignoreNetNos, overlappingObjects);

  for (SearchTreeObject* currentObject : *overlappingObjects) {
    auto* currentItem = jrt::fast_cast<Item>(currentObject);
    if (currentItem == nullptr || currentItem == ignoreItem) {
      continue;
    }
    if (!currentItem->sharesNet(ignoreItem)) {
      return false;
    }
    if (!currentItem->getNormalContacts()->contains(ignoreItem)) {
      return false;
    }
  }
  return true;
}

}  // namespace fr
