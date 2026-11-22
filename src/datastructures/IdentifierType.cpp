// Port of app.freerouting.datastructures.IdentifierType
#include "datastructures/IdentifierType.h"

#include "datastructures/IndentFileWriter.h"
#include "logger/FRLogger.h"

namespace fr {

namespace {
// Java name.matches("^-?\\d.*"): optional minus, an ASCII digit, then any characters except
// line terminators ('.' does not match them) up to the end of the string.
bool matchesLeadingNumber(const std::string& name) {
  size_t i = 0;
  if (i < name.size() && name[i] == '-') {
    ++i;
  }
  if (i >= name.size() || !(name[i] >= '0' && name[i] <= '9')) {
    return false;
  }
  ++i;
  for (; i < name.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(name[i]);
    if (c == '\n' || c == '\r') {
      return false;
    }
    // U+0085 (C2 85), U+2028 (E2 80 A8), U+2029 (E2 80 A9)
    if (c == 0xC2 && i + 1 < name.size() && static_cast<unsigned char>(name[i + 1]) == 0x85) {
      return false;
    }
    if (c == 0xE2 && i + 2 < name.size() && static_cast<unsigned char>(name[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(name[i + 2]) == 0xA8 ||
         static_cast<unsigned char>(name[i + 2]) == 0xA9)) {
      return false;
    }
  }
  return true;
}
}  // namespace

void IdentifierType::write(std::string name, IndentFileWriter* file) {
  // remove the double quotes from the identifiers
  while ((name.length() >= 2) && (name[0] == '"') && (name[name.length() - 1] == '"')) {
    name = name.substr(1, name.length() - 2);
  }

  // PORT NOTE: the Java IOException handler is dropped (std::ostream does not throw).
  // if the name contains our quote character, we must remove it
  if (jrt::contains(name, stringQuote)) {
    name = jrt::replace(name, stringQuote, "");
  }

  // an empty name must be written as an empty quoted string, otherwise the token is missing
  bool needQuotes = name.empty();
  // if the name contains a reserved character, we must put it into quotes
  for (const std::string& reservedChar : *reservedChars) {
    if (jrt::contains(name, reservedChar)) {
      needQuotes = true;
      break;
    }
  }

  // if the name contains a non-ASCII character, we must put it into quotes
  for (char c : name) {
    int8_t ch = static_cast<int8_t>(c);
    if (ch <= 0) {
      needQuotes = true;
      break;
    }
  }

  if (!needQuotes) {
    if (matchesLeadingNumber(name)) {
      needQuotes = true;
    }
  }
  if (needQuotes) {
    name = quote(name);
  }
  file->write(name);
}

bool IdentifierType::isLegal(const std::optional<std::string>& string) {
  if (!string.has_value()) {
    FRLogger::warn("IdentifierType.is_legal: string is null");
    return false;
  }
  for (int32_t i = 0; i < reservedChars->length; i++) {
    if (jrt::contains(*string, (*reservedChars)[i])) {
      return false;
    }
  }
  return true;
}

std::string IdentifierType::quote(const std::string& string) {
  return stringQuote + string + stringQuote;
}

}  // namespace fr
