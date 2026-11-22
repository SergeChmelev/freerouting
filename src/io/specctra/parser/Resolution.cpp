#include "io/specctra/parser/Resolution.h"

#include "board/model/structure/Unit.h"
#include "board/state/Communication.h"
#include "datastructures/IndentFileWriter.h"
#include "io/specctra/parser/IJFlexScanner.h"
#include "io/specctra/parser/ReadScopeParameter.h"
#include "logger/FRLogger.h"

namespace fr::dsn {

void Resolution::writeScope(fr::IndentFileWriter* file, fr::Communication* boardCommunication) {
  file->newLine();
  file->write("(resolution ");
  file->write(fr::Unit_toString(boardCommunication->unit));
  file->write(" ");
  file->write(jrt::str(boardCommunication->resolution));
  file->write(")");
}

bool Resolution::readScope(ReadScopeParameter* scopeParameter) {
  try {
    // read the unit
    jrt::Object* nextToken = scopeParameter->scanner->nextToken();
    auto* unitString = jrt::fast_cast<JString>(nextToken);
    if (unitString == nullptr) {
      FRLogger::warn("Resolution.read_scope: string expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    std::optional<fr::Unit> unit = fr::Unit_fromString(unitString->value);
    if (!unit.has_value()) {
      // PORT NOTE: Java assigns null to scopeParameter.unit before returning false; the C++ field
      // is not nullable, so it keeps its previous value (the read fails either way).
      FRLogger::warn("Resolution.read_scope: unit mil, inch or mm expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    scopeParameter->unit = *unit;
    // read the scale factor
    nextToken = scopeParameter->scanner->nextToken();
    auto* scale = jrt::fast_cast<JInteger>(nextToken);
    if (scale == nullptr) {
      FRLogger::warn("Resolution.read_scope: integer expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    scopeParameter->resolution = scale->value;
    // overread the closing bracket
    nextToken = scopeParameter->scanner->nextToken();
    if (nextToken != CLOSED_BRACKET()) {
      FRLogger::warn("Resolution.read_scope: closing bracket expected at '" +
                     scopeParameter->scanner->getScopeIdentifier() + "'");
      return false;
    }
    return true;
  } catch (const jrt::IOException& e) {
    FRLogger::error("Resolution.read_scope: IO error scanning file", &e);
    return false;
  }
}

}  // namespace fr::dsn
