#include "io/KiCadNetClassNames.h"

#include "rules/BoardRules.h"
#include "rules/NetClasses.h"

namespace fr::io {

fr::NetClass* KiCadNetClassNames::resolveNetClass(fr::BoardRules* rules, const std::string& name) {
  if (isKiCadDefaultNetClassName(name)) {
    return rules->getDefaultNetClass();
  }
  return rules->netClasses->get(name);
}

}  // namespace fr::io
