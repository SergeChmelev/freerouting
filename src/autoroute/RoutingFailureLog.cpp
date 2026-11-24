#include "autoroute/RoutingFailureLog.h"

#include "board/model/items/Item.h"

namespace fr {

RoutingFailureLog* RoutingFailureLog::deepCopy() { return deepCopy(nullptr); }

RoutingFailureLog* RoutingFailureLog::deepCopy(jrt::IdentityHashMap<Item*, Item*>* itemMap) {
  RoutingFailureLog* copy = new (jrt::kBoard) RoutingFailureLog();
  for (auto* e : *this->failures->entrySet()) {
    RoutingFailureLog_ItemFailureInfo* src = e->getValue();
    Item* item = src->item;
    if (itemMap != nullptr) {
      Item* mapped = itemMap->get(item);
      if (mapped != nullptr) {
        item = mapped;
      }
    }
    RoutingFailureLog_ItemFailureInfo* info = new (jrt::kOp) RoutingFailureLog_ItemFailureInfo(*src);
    info->item = item;
    copy->failures->put(e->getKey(), info);
  }
  return copy;
}

RoutingFailureLog::RoutingFailureLog() {
  this->failures = new (jrt::kBoard) jrt::ConcurrentHashMap<int32_t, RoutingFailureLog_ItemFailureInfo*>();
}

void RoutingFailureLog::recordFailure(Item* item, int32_t passNo, AutorouteAttemptState state,
                                      const std::string& reason) {
  if (item == nullptr) {
    return;
  }

  // Java: failures.compute(item.getId(), ...)
  int32_t key = item->getId();
  RoutingFailureLog_ItemFailureInfo* existing = failures->get(key);
  if (existing == nullptr) {
    existing = new (jrt::kOp) RoutingFailureLog_ItemFailureInfo(item);
  }
  existing->recordFailure(passNo, state, reason);
  failures->put(key, existing);
}

bool RoutingFailureLog::shouldSkip(Item* item) {
  if (item == nullptr) {
    return false;
  }

  RoutingFailureLog_ItemFailureInfo* info = failures->get(item->getId());
  return info != nullptr && info->shouldGiveUp();
}

jrt::List<RoutingFailureLog_ItemFailureInfo*>* RoutingFailureLog::getUnroutableItems() {
  jrt::List<RoutingFailureLog_ItemFailureInfo*>* unroutable = new (jrt::kOp) jrt::ArrayList<RoutingFailureLog_ItemFailureInfo*>();
  for (RoutingFailureLog_ItemFailureInfo* info : *failures->values()) {
    if (info->shouldGiveUp()) {
      unroutable->add(info);
    }
  }
  return unroutable;
}

bool RoutingFailureLog::hasUnroutableItems() {
  for (RoutingFailureLog_ItemFailureInfo* info : *failures->values()) {
    if (info->shouldGiveUp()) {
      return true;
    }
  }
  return false;
}

int32_t RoutingFailureLog::getFailureCount(Item* item) {
  if (item == nullptr) {
    return 0;
  }
  RoutingFailureLog_ItemFailureInfo* info = failures->get(item->getId());
  return info != nullptr ? info->failureCount : 0;
}

void RoutingFailureLog::clear() { failures->clear(); }

RoutingFailureLog_ItemFailureInfo::RoutingFailureLog_ItemFailureInfo(Item* item) {
  this->item = item;
  this->itemClassName = jrt::className(item);
  this->netNumber = item->netCount() > 0 ? item->getNetNumber(0) : -1;
  this->failureCount = 0;
  this->lastFailureState = std::nullopt;
  this->lastFailureReason = "";
  this->lastAttemptPass = 0;
}

void RoutingFailureLog_ItemFailureInfo::recordFailure(int64_t passNo, AutorouteAttemptState state,
                                                      const std::string& reason) {
  // PORT NOTE: Java's `reason != null ? reason : ""` - C++ strings are never null.
  this->failureCount++;
  this->lastAttemptPass = passNo;
  this->lastFailureState = state;
  this->lastFailureReason = reason;
}

bool RoutingFailureLog_ItemFailureInfo::shouldGiveUp() {
  return failureCount >= RoutingFailureLog::FAILURE_THRESHOLD;
}

std::string RoutingFailureLog_ItemFailureInfo::toString() {
  return itemClassName + " (net #" + jrt::str(netNumber) + ", " + jrt::str(failureCount) + " failures)";
}

RoutingFailureLog::~RoutingFailureLog() {
  for (auto* e : failures->entryVector()) jrt::dispose(e->getValue());
  jrt::disposeMapAndEntries(failures);
}

}  // namespace fr
