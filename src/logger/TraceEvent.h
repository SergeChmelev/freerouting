// Port of app.freerouting.logger.TraceEvent
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Payload describing an interesting trace event.
 * PORT NOTE: java.time.Instant timestamp -> int64_t epoch milliseconds.
 */
class TraceEvent : public virtual jrt::Object {
 public:
  TraceEvent(const std::string& method, const std::string& operation, const std::string& message,
             const std::string& impactedItems, jrt::Array<Point*>* impactedPoints,
             int64_t timestamp)
      : method(method),
        operation(operation),
        message(message),
        impactedItems(impactedItems),
        impactedPoints(impactedPoints),
        timestamp(timestamp) {}

  std::string getMethod() { return method; }
  std::string getOperation() { return operation; }
  std::string getMessage() { return message; }
  std::string getImpactedItems() { return impactedItems; }
  jrt::Array<Point*>* getImpactedPoints() { return impactedPoints; }
  int64_t getTimestamp() { return timestamp; }

 private:
  const std::string method;
  const std::string operation;
  const std::string message;
  const std::string impactedItems;
  jrt::Array<Point*>* impactedPoints;
  const int64_t timestamp;
};

}  // namespace fr
