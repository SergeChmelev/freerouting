#include "io/specctra/parser/WriteScopeParameter.h"

#include "datastructures/IdentifierType.h"

namespace fr::dsn {

WriteScopeParameter::WriteScopeParameter(fr::BasicBoard* board,
                                         fr::RouterSettings* autorouteSettings,
                                         fr::IndentFileWriter* file,
                                         const std::string& stringQuote,
                                         fr::io::CoordinateTransform* coordinateTransform,
                                         bool compatMode)
    : board(board),
      autorouteSettings(autorouteSettings),
      file(file),
      coordinateTransform(coordinateTransform),
      compatMode(compatMode) {
  jrt::Array<std::string>* reservedChars =
      jrt::Array<std::string>::of({"(", ")", " ", ";", "-", "_"});
  identifierType = new fr::IdentifierType(reservedChars, stringQuote);
}

}  // namespace fr::dsn
