// Port of app.freerouting.core.library.LogicalParts
#include "core/library/LogicalParts.h"

#include "core/library/LogicalPart.h"
#include "logger/FRLogger.h"

namespace fr {

LogicalParts::LogicalParts() : partArr(new jrt::ArrayList<LogicalPart*>()) {}

LogicalPart* LogicalParts::add(const std::string& name,
                               jrt::Array<LogicalPart_PartPin*>* partPinArr) {
  jrt::Arrays::sort<LogicalPart_PartPin*>(partPinArr);
  auto* newPart = new LogicalPart(name, partArr->size() + 1, partPinArr);
  partArr->add(newPart);
  return newPart;
}

LogicalPart* LogicalParts::get(const std::string& name) {
  // Match exact case first so parts differing only in case stay distinct
  for (LogicalPart* currentPart : *this->partArr) {
    if (currentPart != nullptr && currentPart->name == name) {
      return currentPart;
    }
  }
  for (LogicalPart* currentPart : *this->partArr) {
    if (currentPart != nullptr && jrt::equalsIgnoreCase(currentPart->name, name)) {
      return currentPart;
    }
  }
  return nullptr;
}

LogicalPart* LogicalParts::get(int32_t partId) {
  if (partId < 1 || partId > partArr->size()) {
    return nullptr;
  }
  LogicalPart* result = partArr->get(partId - 1);
  if (result != nullptr && result->id != partId) {
    FRLogger::warn("LogicalParts.get: inconsistent part ID");
  }
  return result;
}

int32_t LogicalParts::count() { return partArr->size(); }

}  // namespace fr
