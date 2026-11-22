#include "settings/sources/ApiSettings.h"

#include "settings/RouterSettings.h"

namespace fr {

ApiSettings::ApiSettings(RouterSettings* settings)
    : settings(settings != nullptr ? settings : new RouterSettings()) {}

}  // namespace fr
