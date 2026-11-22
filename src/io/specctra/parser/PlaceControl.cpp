#include "io/specctra/parser/PlaceControl.h"

#include "board/facade/RoutingBoard.h"
#include "board/model/structure/Components.h"
#include "io/specctra/parser/BoardParserCallback.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

bool PlaceControl::readFlipStyleRotateFirst(IJFlexScanner* scanner) {
  try {
    bool result = false;
    jrt::Object* nextToken = scanner->nextToken();
    if (nextToken == ROTATE_FIRST()) {
      result = true;
    }
    nextToken = scanner->nextToken();
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("Structure.read_flip_style: closing bracket expected at '" +
                     scanner->getScopeIdentifier() + "'");
      return false;
    }
    return result;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Structure.read_flip_style: IO error scanning file", &e);
    return false;
  }
}

bool PlaceControl::readScope(ReadScopeParameter* scopeParameter) {
  bool flipStyleRotateFirst = false;
  jrt::Object* nextToken = nullptr;
  for (;;) {
    jrt::Object* prevToken = nextToken;
    try {
      nextToken = scopeParameter->scanner->nextToken();
    } catch (const jrt::IOException& e) {
      FRLogger::error("PlaceControl.read_scope: IO error scanning file", &e);
      return false;
    }
    if (nextToken == nullptr) {
      FRLogger::warn("PlaceControl.read_scope: unexpected end of file at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    if (nextToken == CLOSED_BRACKET()) {
      // end of scope
      break;
    }
    if (prevToken == OPEN_BRACKET()) {
      if (nextToken == FLIP_STYLE()) {
        flipStyleRotateFirst = readFlipStyleRotateFirst(scopeParameter->scanner);
      }
    }
  }
  if (flipStyleRotateFirst) {
    scopeParameter->boardHandling->getRoutingBoard()->components->setFlipStyleRotateFirst(true);
  }
  return true;
}

}  // namespace fr::dsn
