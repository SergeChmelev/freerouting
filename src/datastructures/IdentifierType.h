// Port of app.freerouting.datastructures.IdentifierType
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/** Describes legal identifiers together with the character used for string quotes. */
class IdentifierType : public virtual jrt::Object {
 public:
  /**
   * Defines the reserved characters and the string for quoting identifiers containing reserved
   * characters for a new instance of Identifier.
   */
  IdentifierType(jrt::Array<std::string>* reservedChars, const std::string& stringQuote)
      : stringQuote(stringQuote), reservedChars(reservedChars) {}

  /**
   * Writes name after putting it into quotes, if it contains reserved characters or blanks.
   * PORT NOTE: Java takes an OutputStreamWriter; all callers pass an IndentFileWriter.
   */
  void write(std::string name, IndentFileWriter* file);

 private:
  /** Looks, if string does not contain reserved characters or blanks. */
  bool isLegal(const std::optional<std::string>& string);

  /** Puts string into quotes. */
  std::string quote(const std::string& string);

  const std::string stringQuote;
  jrt::Array<std::string>* reservedChars;
};

}  // namespace fr
