#pragma once

// PORT EXTENSION (not a Java class): the small part of Gson (com.google.gson.JsonParser /
// JsonElement) that the KiCad project reader (io/kicad/KiCadProject) relies on.
//
// Semantics kept from Gson:
//  * JsonParser.parseString is lenient: besides strict JSON it accepts // /* */ and # comments,
//    single-quoted strings, unquoted names and values, NaN / Infinity; the port accepts the same
//    (unquoted values are numbers when Gson would read them as such, strings otherwise).
//  * Numbers are kept as double (JsonPrimitive.getAsDouble); a number is a number, a quoted
//    number is a string (isNumber() false).
//  * An empty document is JsonNull; text after the root value is an error.
//  * JsonObject.get(name) returns null (nullptr) for a missing member; for a duplicated name the
//    last value wins (Gson's JsonObject.add replaces).
// PORT NOTE: the error messages say what is wrong and where (line, column) like Gson's
// MalformedJsonException, but not in Gson's exact words.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "jrt/jrt.h"

namespace fr {

class JsonValue : public virtual jrt::Object {
 public:
  enum class Kind : int32_t { NUL, BOOLEAN, NUMBER, STRING, ARRAY, OBJECT };

  explicit JsonValue(Kind kind) : kind(kind) {}

  bool isJsonNull() { return kind == Kind::NUL; }
  bool isJsonPrimitive() {
    return kind == Kind::BOOLEAN || kind == Kind::NUMBER || kind == Kind::STRING;
  }
  bool isJsonObject() { return kind == Kind::OBJECT; }
  bool isJsonArray() { return kind == Kind::ARRAY; }
  /** For a primitive: whether it is a number. */
  bool isNumber() { return kind == Kind::NUMBER; }
  bool isString() { return kind == Kind::STRING; }
  bool isBoolean() { return kind == Kind::BOOLEAN; }

  /** JsonPrimitive.getAsDouble: the number, or a string parsed as a number. */
  double getAsDouble();
  std::string getAsString() { return text; }
  bool getAsBoolean() { return boolean; }

  /** JsonObject.get: the member of this name, or nullptr if there is none (or not an object). */
  JsonValue* get(const std::string& name);
  /** The members of an object, in file order (a duplicated name keeps its first position). */
  const std::vector<std::pair<std::string, JsonValue*>>& members() { return members_; }
  /** The elements of an array. */
  const std::vector<JsonValue*>& elements() { return elements_; }

  const Kind kind;
  bool boolean = false;
  double number = 0;
  std::string text;  // string value, or the number as written
  std::vector<std::pair<std::string, JsonValue*>> members_;
  std::vector<JsonValue*> elements_;
};

/** JsonParser.parseString (lenient). Throws JsonParseException on malformed text. */
class JsonReader final {
 public:
  /** Gson's JsonParseException (and its subclass JsonSyntaxException). */
  class JsonParseException : public jrt::RuntimeException {
   public:
    explicit JsonParseException(std::string_view m) : jrt::RuntimeException(m) {}
  };

  /** Parses a JSON document; the values are allocated with the kBoard tag. */
  static JsonValue* parseString(const std::string& text);

 private:
  explicit JsonReader(const std::string& text) : text(text) {}

  JsonValue* readValue();
  JsonValue* readObject();
  JsonValue* readArray();
  std::string readQuoted(char quote);
  std::string readUnquoted();
  /** Skips blanks and comments; returns the next character, or -1 at the end. */
  int32_t peek();
  [[noreturn]] void syntaxError(const std::string& message);

  const std::string& text;
  std::size_t pos = 0;
};

}  // namespace fr
