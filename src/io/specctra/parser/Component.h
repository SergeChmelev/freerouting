#pragma once

#include "fwd.h"
#include "io/specctra/parser/ScopeKeyword.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Handles the placement data of a library component. */
class Component : public ScopeKeyword {
 public:
  /** Creates a new instance of Component. */
  Component() : ScopeKeyword("component") {}

  /** Used also when reading a session file. Throws jrt::IOException. */
  static ComponentPlacement* readScope(IJFlexScanner* scanner);

  /** Throws jrt::IOException. */
  static void writeScope(WriteScopeParameter* scopeParameter, fr::Component* component);

  /** Overwrites the function read_scope in ScopeKeyword. */
  bool readScope(ReadScopeParameter* scopeParameter) override;

 private:
  static void writePinInfo(WriteScopeParameter* scopeParameter, fr::Component* component,
                           int32_t pinIndex);

  static void writeKeepoutInfos(WriteScopeParameter* scopeParameter, fr::Component* component);

  static fr::ObstacleArea* getKeepout(fr::BasicBoard* board, int32_t componentId,
                                      const std::string& name);

  static ComponentPlacement_ComponentLocation* readPlaceScope(IJFlexScanner* scanner);

  /** Throws jrt::IOException. */
  static ComponentPlacement_ItemClearanceInfo* readItemClearanceInfo(IJFlexScanner* scanner);

  /** Throws jrt::IOException. */
  static bool readLockType(IJFlexScanner* scanner);
};

}  // namespace fr::dsn
