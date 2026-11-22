#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/** Default parameter type used while writing a Specctra dsn-file. */
class WriteScopeParameter : public virtual jrt::Object {
 public:
  fr::BasicBoard* board;
  fr::RouterSettings* autorouteSettings;
  fr::IndentFileWriter* file;
  fr::io::CoordinateTransform* coordinateTransform;
  bool compatMode;
  fr::IdentifierType* identifierType;

  /**
   * Creates a new instance of WriteScopeParameter. If compatMode is true, only standard specctra
   * dsn scopes are written, so that any host system with a specctra interface can read them.
   * (identifierType = new IdentifierType({"(", ")", " ", ";", "-", "_"}, stringQuote))
   */
  WriteScopeParameter(fr::BasicBoard* board, fr::RouterSettings* autorouteSettings,
                      fr::IndentFileWriter* file, const std::string& stringQuote,
                      fr::io::CoordinateTransform* coordinateTransform, bool compatMode);
};

}  // namespace fr::dsn
