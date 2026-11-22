#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::io {

/**
 * Lightweight, immutable snapshot of the information that can be extracted from a board design
 * file's header and structure sections without constructing a full BasicBoard (Java record).
 *
 * hostCad / hostVersion are nullable in Java (absent parser scope) -> std::optional.
 * routerSettings is nullable; only present when autoroute settings are available.
 */
class BoardMetadata : public virtual jrt::Object {
 public:
  BoardMetadata(std::optional<std::string> hostCad, std::optional<std::string> hostVersion,
                int32_t layerCount, fr::Unit unit, int32_t resolution,
                fr::AngleRestriction snapAngle, fr::RouterSettings* routerSettings)
      : hostCad_(std::move(hostCad)),
        hostVersion_(std::move(hostVersion)),
        layerCount_(layerCount),
        unit_(unit),
        resolution_(resolution),
        snapAngle_(snapAngle),
        routerSettings_(routerSettings) {}

  std::optional<std::string> hostCad() { return hostCad_; }
  std::optional<std::string> hostVersion() { return hostVersion_; }
  int32_t layerCount() { return layerCount_; }
  fr::Unit unit() { return unit_; }
  int32_t resolution() { return resolution_; }
  fr::AngleRestriction snapAngle() { return snapAngle_; }
  fr::RouterSettings* routerSettings() { return routerSettings_; }

  bool equals(jrt::Object* o) override;
  int32_t hashCode() override;
  std::string toString() override;

 private:
  std::optional<std::string> hostCad_;
  std::optional<std::string> hostVersion_;
  int32_t layerCount_;
  fr::Unit unit_;
  int32_t resolution_;
  fr::AngleRestriction snapAngle_;
  fr::RouterSettings* routerSettings_;
};

}  // namespace fr::io
