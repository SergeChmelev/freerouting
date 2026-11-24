#include "util/JsonReader.h"

namespace fr {

namespace {

bool isLiteralChar(char c) {
  // Gson JsonReader.isLiteral: characters that end an unquoted literal
  switch (c) {
    case '/':
    case '\\':
    case ';':
    case '#':
    case '=':
    case '{':
    case '}':
    case '[':
    case ']':
    case ':':
    case ',':
    case ' ':
    case '\t':
    case '\f':
    case '\r':
    case '\n':
      return false;
    default:
      return true;
  }
}

/** Whether a literal is a JSON number (Gson JsonReader.peekNumber). */
bool isJsonNumber(const std::string& s) {
  std::size_t i = 0;
  std::size_t n = s.size();
  if (i < n && s[i] == '-') i++;
  if (i >= n || !jrt::Character::isDigit(s[i])) return false;
  if (s[i] == '0') {
    i++;
  } else {
    while (i < n && jrt::Character::isDigit(s[i])) i++;
  }
  if (i < n && s[i] == '.') {
    i++;
    if (i >= n || !jrt::Character::isDigit(s[i])) return false;
    while (i < n && jrt::Character::isDigit(s[i])) i++;
  }
  if (i < n && (s[i] == 'e' || s[i] == 'E')) {
    i++;
    if (i < n && (s[i] == '+' || s[i] == '-')) i++;
    if (i >= n || !jrt::Character::isDigit(s[i])) return false;
    while (i < n && jrt::Character::isDigit(s[i])) i++;
  }
  return i == n;
}

bool keyword(const std::string& s, const char* word) { return jrt::equalsIgnoreCase(s, word); }

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

}  // namespace

double JsonValue::getAsDouble() {
  if (kind == Kind::NUMBER) {
    return number;
  }
  return jrt::Double::parseDouble(text);
}

JsonValue* JsonValue::get(const std::string& name) {
  for (auto& member : members_) {
    if (member.first == name) {
      return member.second;
    }
  }
  return nullptr;
}

JsonValue* JsonReader::parseString(const std::string& text) {
  JsonReader reader(text);
  if (reader.peek() < 0) {
    return new (jrt::kBoard) JsonValue(JsonValue::Kind::NUL);  // Gson: an empty document
  }
  JsonValue* root = reader.readValue();
  if (!root->isJsonNull() && reader.peek() >= 0) {
    throw JsonParseException("Did not consume the entire document.");
  }
  return root;
}

void JsonReader::syntaxError(const std::string& message) {
  int32_t line = 1;
  int32_t column = 1;
  for (std::size_t i = 0; i < pos && i < text.size(); i++) {
    if (text[i] == '\n') {
      line++;
      column = 1;
    } else {
      column++;
    }
  }
  throw JsonParseException("com.google.gson.stream.MalformedJsonException: " + message +
                           " at line " + jrt::str(line) + " column " + jrt::str(column));
}

int32_t JsonReader::peek() {
  while (pos < text.size()) {
    char c = text[pos];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      pos++;
    } else if (c == '#' || (c == '/' && pos + 1 < text.size() && text[pos + 1] == '/')) {
      while (pos < text.size() && text[pos] != '\n') pos++;
    } else if (c == '/' && pos + 1 < text.size() && text[pos + 1] == '*') {
      std::size_t end = text.find("*/", pos + 2);
      if (end == std::string::npos) {
        syntaxError("Unterminated comment");
      }
      pos = end + 2;
    } else {
      return static_cast<unsigned char>(c);
    }
  }
  return -1;
}

JsonValue* JsonReader::readValue() {
  int32_t c = peek();
  if (c < 0) {
    syntaxError("End of input");
  }
  if (c == '{') {
    pos++;
    return readObject();
  }
  if (c == '[') {
    pos++;
    return readArray();
  }
  if (c == '"' || c == '\'') {
    pos++;
    auto* value = new (jrt::kBoard) JsonValue(JsonValue::Kind::STRING);
    value->text = readQuoted(static_cast<char>(c));
    return value;
  }
  std::string literal = readUnquoted();
  if (literal.empty()) {
    syntaxError("Expected value");
  }
  if (keyword(literal, "true") || keyword(literal, "false")) {
    auto* value = new (jrt::kBoard) JsonValue(JsonValue::Kind::BOOLEAN);
    value->boolean = keyword(literal, "true");
    value->text = value->boolean ? "true" : "false";
    return value;
  }
  if (keyword(literal, "null")) {
    return new (jrt::kBoard) JsonValue(JsonValue::Kind::NUL);
  }
  if (isJsonNumber(literal)) {
    auto* value = new (jrt::kBoard) JsonValue(JsonValue::Kind::NUMBER);
    value->number = jrt::Double::parseDouble(literal);
    value->text = literal;
    return value;
  }
  // Gson (lenient): any other unquoted literal is a string
  auto* value = new (jrt::kBoard) JsonValue(JsonValue::Kind::STRING);
  value->text = literal;
  return value;
}

JsonValue* JsonReader::readObject() {
  auto* object = new (jrt::kBoard) JsonValue(JsonValue::Kind::OBJECT);
  if (peek() == '}') {
    pos++;
    return object;
  }
  while (true) {
    int32_t c = peek();
    std::string name;
    if (c == '"' || c == '\'') {
      pos++;
      name = readQuoted(static_cast<char>(c));
    } else {
      name = readUnquoted();
      if (name.empty()) {
        syntaxError("Expected name");
      }
    }
    c = peek();
    if (c == ':') {
      pos++;
    } else if (c == '=') {  // lenient: '=' or '=>'
      pos++;
      if (pos < text.size() && text[pos] == '>') pos++;
    } else {
      syntaxError("Expected ':'");
    }
    JsonValue* value = readValue();
    bool replaced = false;
    for (auto& member : object->members_) {
      if (member.first == name) {
        member.second = value;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      object->members_.emplace_back(name, value);
    }
    c = peek();
    if (c == '}') {
      pos++;
      return object;
    }
    if (c == ',' || c == ';') {
      pos++;
      continue;
    }
    syntaxError("Unterminated object");
  }
}

JsonValue* JsonReader::readArray() {
  auto* array = new (jrt::kBoard) JsonValue(JsonValue::Kind::ARRAY);
  if (peek() == ']') {
    pos++;
    return array;
  }
  while (true) {
    array->elements_.push_back(readValue());
    int32_t c = peek();
    if (c == ']') {
      pos++;
      return array;
    }
    if (c == ',' || c == ';') {
      pos++;
      continue;
    }
    syntaxError("Unterminated array");
  }
}

std::string JsonReader::readQuoted(char quote) {
  std::string result;
  while (pos < text.size()) {
    char c = text[pos++];
    if (c == quote) {
      return result;
    }
    if (c != '\\') {
      result += c;
      continue;
    }
    if (pos >= text.size()) {
      break;
    }
    char e = text[pos++];
    switch (e) {
      case 'u': {
        if (pos + 4 > text.size()) {
          syntaxError("Unterminated escape sequence");
        }
        uint32_t cp = 0;
        for (int i = 0; i < 4; i++) {
          char h = text[pos++];
          cp <<= 4;
          if (h >= '0' && h <= '9') {
            cp |= static_cast<uint32_t>(h - '0');
          } else if (h >= 'a' && h <= 'f') {
            cp |= static_cast<uint32_t>(h - 'a' + 10);
          } else if (h >= 'A' && h <= 'F') {
            cp |= static_cast<uint32_t>(h - 'A' + 10);
          } else {
            syntaxError("Malformed Unicode escape");
          }
        }
        // a surrogate pair written as two escapes
        if (cp >= 0xD800 && cp < 0xDC00 && pos + 6 <= text.size() && text[pos] == '\\' &&
            text[pos + 1] == 'u') {
          uint32_t low = 0;
          bool ok = true;
          for (int i = 0; i < 4; i++) {
            char h = text[pos + 2 + i];
            low <<= 4;
            if (h >= '0' && h <= '9') {
              low |= static_cast<uint32_t>(h - '0');
            } else if (h >= 'a' && h <= 'f') {
              low |= static_cast<uint32_t>(h - 'a' + 10);
            } else if (h >= 'A' && h <= 'F') {
              low |= static_cast<uint32_t>(h - 'A' + 10);
            } else {
              ok = false;
            }
          }
          if (ok && low >= 0xDC00 && low < 0xE000) {
            pos += 6;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
        }
        appendUtf8(result, cp);
        break;
      }
      case 't':
        result += '\t';
        break;
      case 'b':
        result += '\b';
        break;
      case 'n':
        result += '\n';
        break;
      case 'r':
        result += '\r';
        break;
      case 'f':
        result += '\f';
        break;
      case '\n':
      case '\'':
      case '"':
      case '\\':
      case '/':
        result += e;
        break;
      default:
        syntaxError("Invalid escape sequence");
    }
  }
  syntaxError("Unterminated string");
}

std::string JsonReader::readUnquoted() {
  std::size_t start = pos;
  while (pos < text.size() && isLiteralChar(text[pos])) {
    pos++;
  }
  return text.substr(start, pos - start);
}

}  // namespace fr
