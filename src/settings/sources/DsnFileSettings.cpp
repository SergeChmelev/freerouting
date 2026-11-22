#include "settings/sources/DsnFileSettings.h"

#include "io/BoardMetadata.h"
#include "io/BoardReadResult.h"
#include "io/specctra/DsnReader.h"
#include "logger/FRLogger.h"
#include "settings/RouterSettings.h"

namespace fr {

DsnFileSettings::DsnFileSettings(std::istream& inputStream, const std::string& filename) {
  this->filename = filename;
  io::BoardReadResult* result = dsn::DsnReader::readMetadata(inputStream);
  RouterSettings* extracted = nullptr;
  int32_t layerCount = 0;
  if (auto* s = dynamic_cast<io::BoardReadResult_Success*>(result);
      s != nullptr && s->metadata() != nullptr) {
    extracted = s->metadata()->routerSettings();  // null when no (autorouteSettings ...) block
    layerCount = s->metadata()->layerCount();
  }

  // Start with whatever the DSN's autoroute block provided (or a blank slate).
  RouterSettings* rs = extracted != nullptr ? extracted : new RouterSettings();

  // Always seed the layer arrays from the actual DSN layer count so that any board
  // with more or fewer than 2 layers gets correctly-sized arrays in the merged result,
  // well before applyBoardSpecificOptimizations() is called.
  if (layerCount > 0 && rs->getLayerCount() == 0) {
    rs->setLayerCount(layerCount);
  }

  this->settings = rs;
  FRLogger::debug("Loaded router settings from DSN file: " + filename + " (" +
                  jrt::str(layerCount) + " layers)");
}

}  // namespace fr
