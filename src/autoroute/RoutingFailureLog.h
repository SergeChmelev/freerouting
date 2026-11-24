#pragma once

#include <optional>

#include "autoroute/AutorouteAttemptState.h"
#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Information about routing failures for a specific item. */
class RoutingFailureLog_ItemFailureInfo : public virtual jrt::Object {
 public:
  Item* item;  // PORT NOTE (stage E): may be freed later (removed for good): never dereferenced
  std::string itemClassName;  // for toString
  int32_t netNumber;
  int32_t failureCount;
  // PORT NOTE: nullable enum (initially null in Java) -> std::optional.
  std::optional<AutorouteAttemptState> lastFailureState;
  std::string lastFailureReason;
  int64_t lastAttemptPass;

  /** Creates failure tracking info for an item. */
  explicit RoutingFailureLog_ItemFailureInfo(Item* item);

  /** Records a failure for this item. */
  virtual void recordFailure(int64_t passNo, AutorouteAttemptState state,
                             const std::string& reason);

  /** Checks if this item should be given up on (failure count reached threshold). */
  virtual bool shouldGiveUp();

  std::string toString() override;
};

/**
 * Logbook for tracking routing failures per item. Helps detect when the router is stuck
 * attempting the same impossible connection repeatedly.
 */
class RoutingFailureLog : public virtual jrt::Object {
 public:
  // PORT NOTE (board deepCopy hook): independent deep copy. The ItemFailureInfo objects are copied;
  // their `item` references are mapped through itemMap when given (Java serialization would
  // point them at the copied board's items), else kept (shared with the source).
  RoutingFailureLog* deepCopy();
  RoutingFailureLog* deepCopy(jrt::IdentityHashMap<Item*, Item*>* itemMap);
  /** Failure threshold - give up after this many failures for the same item. */
  static constexpr int32_t FAILURE_THRESHOLD = 50;

 private:
  /** Map of item ID to failure information. */
  jrt::ConcurrentHashMap<int32_t, RoutingFailureLog_ItemFailureInfo*>* failures;

 public:
  RoutingFailureLog();

  /** Records a routing failure for an item. */
  virtual void recordFailure(Item* item, int32_t passNo, AutorouteAttemptState state,
                             const std::string& reason);

  /** Checks if an item should be skipped due to exceeding the failure threshold. */
  virtual bool shouldSkip(Item* item);

  /** Gets all items that have exceeded the failure threshold. */
  virtual jrt::List<RoutingFailureLog_ItemFailureInfo*>* getUnroutableItems();

  /** Checks if there are any unroutable items. */
  virtual bool hasUnroutableItems();

  /** Gets the failure count for a specific item, or 0 if no failures recorded. */
  virtual int32_t getFailureCount(Item* item);

  /** Clears all failure records. */
  virtual void clear();
};

}  // namespace fr
