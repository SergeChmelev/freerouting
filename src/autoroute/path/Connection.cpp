#include "autoroute/path/Connection.h"

#include "autoroute/ItemAutorouteInfo.h"
#include "board/model/items/Item.h"
#include "board/model/items/Trace.h"
#include "geometry/planar/FloatPoint.h"
#include "geometry/planar/Point.h"

namespace fr {

Connection* Connection::get(Item* item) {
  if (!item->isRoutable()) {
    return nullptr;
  }
  Connection* precalculatedConnection = item->getAutorouteInfo()->getPrecalculatedConnection();
  if (precalculatedConnection != nullptr) {
    return precalculatedConnection;
  }
  jrt::Set<Item*>* contacts = item->getNormalContacts();
  jrt::Set<Item*>* connectionItems = new (jrt::kOp) jrt::TreeSet<Item*>();
  connectionItems->add(item);

  Point* startPoint = nullptr;
  int32_t startLayer = 0;
  Point* endPoint = nullptr;
  int32_t endLayer = 0;

  for (Item* currentItem : *contacts) {
    Point* prevContactPoint = item->normalContactPoint(currentItem);
    if (prevContactPoint == nullptr) {
      // no unique contact point
      continue;
    }
    int32_t prevContactLayer = item->firstCommonLayer(currentItem);
    bool forkFound = false;
    if (auto* startTrace = jrt::fast_cast<Trace>(item)) {
      // Check, that there is only 1 contact at this location.
      // Only for pins and vias items of more than 1 connection
      // are collected
      jrt::Collection<Item*>* checkContacts = startTrace->getNormalContacts(prevContactPoint, false);
      if (checkContacts->size() != 1) {
        forkFound = true;
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
      if (++ringSteps > 1 && currentItem == ringItem && prevContactLayer == ringLayer
          && prevContactPoint->equals(ringPoint)) {
        break;
      }
      if (ringSteps == ringMark) {
        ringItem = currentItem;
        ringPoint = prevContactPoint;
        ringLayer = prevContactLayer;
        ringMark *= 2;
      }
      if (!currentItem->isRoutable() || forkFound) {
        // connection ends
        if (startPoint == nullptr) {
          startPoint = prevContactPoint;
          startLayer = prevContactLayer;
        } else if (!prevContactPoint->equals(startPoint)) {
          endPoint = prevContactPoint;
          endLayer = prevContactLayer;
        }
        break;
      }
      connectionItems->add(currentItem);
      jrt::Collection<Item*>* currentItemContacts = currentItem->getNormalContacts();
      // filter the contacts at the previous contact point,
      // because we were already there.
      // If then there is not exactly 1 new contact left, there is
      // a stub or a fork.
      Point* nextContactPoint = nullptr;
      int32_t nextContactLayer = -1;
      Item* nextContact = nullptr;
      for (Item* tmpContact : *currentItemContacts) {
        int32_t tmpContactLayer = currentItem->firstCommonLayer(tmpContact);
        if (tmpContactLayer >= 0) {
          Point* tmpContactPoint = currentItem->normalContactPoint(tmpContact);
          if (tmpContactPoint == nullptr) {
            // no unique contact point
            forkFound = true;
            break;
          }
          if (prevContactLayer != tmpContactLayer || !prevContactPoint->equals(tmpContactPoint)) {
            nextContactPoint = tmpContactPoint;
            nextContactLayer = tmpContactLayer;
            if (nextContact != nullptr) {
              // second new contact found
              forkFound = true;
              break;
            }
            nextContact = tmpContact;
          }
        }
      }
      if (nextContact == nullptr) {
        break;
      }
      currentItem = nextContact;
      prevContactPoint = nextContactPoint;
      prevContactLayer = nextContactLayer;
    }
  }
  Connection* result = new (jrt::kOp) Connection(startPoint, startLayer, endPoint, endLayer, connectionItems);
  for (Item* currentItem : *connectionItems) {
    currentItem->getAutorouteInfo()->setPrecalculatedConnection(result);
  }
  return result;
}

double Connection::traceLength() {
  double result = 0;
  for (Item* currentItem : *itemList) {
    if (auto* trace = jrt::fast_cast<Trace>(currentItem)) {
      result += trace->getLength();
    }
  }
  return result;
}

double Connection::getDetour() {
  if (startPoint == nullptr || endPoint == nullptr) {
    return jrt::Integer::MAX_VALUE;
  }
  double minTraceLength = startPoint->toFloat()->distance(endPoint->toFloat());
  return (this->traceLength() + DETOUR_ADD) / (minTraceLength + DETOUR_ADD)
         + DETOUR_ITEM_COST * (itemList->size() - 1);
}

}  // namespace fr
