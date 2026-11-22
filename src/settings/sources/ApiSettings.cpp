#include "settings/sources/ApiSettings.h"

#include "settings/RouterSettings.h"

namespace fr {

ApiSettings::ApiSettings(RouterSettings* settings)
    : settings(settings != nullptr ? settings : new (jrt::kBoard) RouterSettings()) {}

}  // namespace fr
