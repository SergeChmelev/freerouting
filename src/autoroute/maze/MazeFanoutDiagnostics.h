#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Optional trace logging of fanout maze searches (Java: package-private final class). */
class MazeFanoutDiagnostics final : public virtual jrt::Object {
 private:
  AutorouteControl* ctrl;

 public:
  explicit MazeFanoutDiagnostics(AutorouteControl* ctrl) : ctrl(ctrl) {}

  bool enabled();

 private:
  std::string label();

 public:
  std::string labelForLog();

  void trace(const std::string& event, const std::string& message);
};

}  // namespace fr
