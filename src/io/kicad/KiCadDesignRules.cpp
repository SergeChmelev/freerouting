#include "io/kicad/KiCadDesignRules.h"

#include <charconv>
#include <cmath>
#include <vector>

#include "io/kicad/KiCadRuleCondition.h"
#include "util/FileText.h"

namespace fr {

namespace {

/**
 * An S-expression as KiCadDesignRules.SexprReader returns it: a list (Java List<Object>) or a
 * symbol / string (Java String).
 */
// PORT NOTE: Java returns a tree of List<Object> and String; the port uses this value type.
struct Sexpr {
  bool isList = false;
  std::string atom;
  std::vector<Sexpr> list;
};

/** String.valueOf(object): a String as it is, a List as "[a, b, [c]]". */
std::string valueOf(const Sexpr& e) {
  if (!e.isList) {
    return e.atom;
  }
  std::string result = "[";
  for (std::size_t i = 0; i < e.list.size(); i++) {
    if (i > 0) result += ", ";
    result += valueOf(e.list[i]);
  }
  return result + "]";
}

/**
 * Java String.format(Locale.ROOT, "%.<precision>g", value): rounded half up from the shortest
 * decimal representation of the value, trailing zeros kept, scientific notation below 10^-4 and
 * from 10^precision on.
 */
std::string javaFormatG(double value, int precision) {
  if (std::isnan(value)) return "NaN";
  if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";
  std::string sign = std::signbit(value) ? "-" : "";
  double magnitude = std::fabs(value);
  std::string digits;
  int exponent = 0;
  if (magnitude == 0) {
    digits = "0";
  } else {
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof buf, magnitude, std::chars_format::scientific);
    std::string sci(buf, res.ptr);
    auto e = sci.find('e');
    for (std::size_t i = 0; i < e; i++) {
      if (sci[i] != '.') digits += sci[i];
    }
    exponent = std::atoi(sci.c_str() + e + 1);
    if (static_cast<int>(digits.size()) > precision) {
      bool up = digits[precision] >= '5';
      digits.resize(precision);
      if (up) {
        int i = precision - 1;
        while (i >= 0 && digits[i] == '9') {
          digits[i] = '0';
          i--;
        }
        if (i >= 0) {
          digits[i]++;
        } else {
          digits = "1" + digits.substr(0, precision - 1);
          exponent++;
        }
      }
    }
  }
  while (static_cast<int>(digits.size()) < precision) digits += '0';
  if (magnitude == 0 || (exponent >= -4 && exponent < precision)) {
    if (exponent >= 0) {
      std::string result = digits.substr(0, exponent + 1);
      if (exponent + 1 < precision) {
        result += "." + digits.substr(exponent + 1);
      }
      return sign + result;
    }
    return sign + "0." + std::string(-exponent - 1, '0') + digits;
  }
  std::string result = digits.substr(0, 1);
  if (precision > 1) {
    result += "." + digits.substr(1);
  }
  std::string exp = std::to_string(std::abs(exponent));
  if (exp.size() < 2) exp = "0" + exp;
  return sign + result + "e" + (exponent < 0 ? "-" : "+") + exp;
}

/** A minimal S-expression reader: lists, symbols and double-quoted strings, # comments. */
class SexprReader {
 public:
  explicit SexprReader(const std::string& text) : text(text) {}

  std::vector<Sexpr> readAll() {
    std::vector<Sexpr> result;
    while (true) {
      skip();
      if (pos >= text.size()) {
        return result;
      }
      result.push_back(read());
    }
  }

 private:
  void skip() {
    while (pos < text.size()) {
      char c = text[pos];
      if (jrt::Character::isWhitespace(static_cast<unsigned char>(c))) {
        pos++;
      } else if (c == '#') {
        while (pos < text.size() && text[pos] != '\n') {
          pos++;
        }
      } else {
        return;
      }
    }
  }

  Sexpr read() {
    skip();
    if (pos >= text.size()) {
      throw jrt::IllegalArgumentException("unexpected end of the rules file");
    }
    char c = text[pos];
    if (c == '(') {
      pos++;
      Sexpr list;
      list.isList = true;
      while (true) {
        skip();
        if (pos >= text.size()) {
          throw jrt::IllegalArgumentException("missing ')' in the rules file");
        }
        if (text[pos] == ')') {
          pos++;
          return list;
        }
        list.list.push_back(read());
      }
    }
    if (c == ')') {
      throw jrt::IllegalArgumentException("unexpected ')' in the rules file");
    }
    Sexpr atom;
    if (c == '"') {
      pos++;
      while (pos < text.size() && text[pos] != '"') {
        char current = text[pos++];
        if (current == '\\' && pos < text.size()) {
          current = text[pos++];
        }
        atom.atom += current;
      }
      if (pos >= text.size()) {
        throw jrt::IllegalArgumentException("unterminated string in the rules file");
      }
      pos++;
      return atom;
    }
    std::size_t start = pos;
    while (pos < text.size() && !jrt::Character::isWhitespace(static_cast<unsigned char>(text[pos])) &&
           text[pos] != '(' && text[pos] != ')') {
      pos++;
    }
    atom.atom = text.substr(start, pos - start);
    return atom;
  }

  const std::string& text;
  std::size_t pos = 0;
};

KiCadDesignRules_Rule* parseRule(const Sexpr& expression) {
  const std::vector<Sexpr>& list = expression.list;
  std::string name = list.size() > 1 ? valueOf(list[1]) : "";
  std::optional<std::string> layer;
  KiCadRuleCondition* condition = nullptr;
  std::optional<double> clearance;
  std::optional<double> creepage;
  bool ignored = false;
  auto* other = new (jrt::kBoard) jrt::ArrayList<std::string>();
  auto* warnings = new (jrt::kBoard) jrt::ArrayList<std::string>();
  for (std::size_t e = std::min<std::size_t>(2, list.size()); e < list.size(); e++) {
    const Sexpr& element = list[e];
    if (!element.isList || element.list.empty()) {
      warnings->add("unexpected '" + valueOf(element) + "'");
      continue;
    }
    const std::vector<Sexpr>& clause = element.list;
    std::string keyword = valueOf(clause.front());
    if (keyword == "condition") {
      condition = KiCadRuleCondition::parse(clause.size() > 1 ? valueOf(clause[1]) : "");
    } else if (keyword == "layer") {
      layer = clause.size() > 1 ? std::optional<std::string>(valueOf(clause[1])) : std::nullopt;
    } else if (keyword == "severity") {
      ignored = clause.size() > 1 && valueOf(clause[1]) == "ignore";
    } else if (keyword == "constraint") {
      std::string type = clause.size() > 1 ? valueOf(clause[1]) : "";
      std::optional<double> min;
      for (std::size_t a = std::min<std::size_t>(2, clause.size()); a < clause.size(); a++) {
        const Sexpr& argument = clause[a];
        if (argument.isList && argument.list.size() > 1 && valueOf(argument.list.front()) == "min") {
          min = KiCadRuleCondition::parseLength(valueOf(argument.list[1]), warnings);
          if (!min.has_value()) {
            warnings->add("minimum '" + valueOf(argument.list[1]) + "' of constraint " + type +
                          " not read");
          }
        }
      }
      if (type == "clearance" && min.has_value()) {
        clearance = !clearance.has_value() ? *min : jrt::Math::max(*clearance, *min);
      } else if (type == "creepage" && min.has_value()) {
        creepage = !creepage.has_value() ? *min : jrt::Math::max(*creepage, *min);
      } else {
        other->add(type + (min.has_value() ? " " + KiCadDesignRules::formatMm(*min) : ""));
      }
    } else {
      warnings->add("clause '" + keyword + "' not read");
    }
  }
  return new (jrt::kBoard)
      KiCadDesignRules_Rule(name, layer, condition, clearance, creepage, ignored, other, warnings);
}

}  // namespace

KiCadDesignRules* KiCadDesignRules::read(const std::string& path) {
  return parse(FileText::readString(path), FileText::fileName(path));
}

KiCadDesignRules* KiCadDesignRules::parse(const std::string& text, const std::string& sourceName) {
  std::vector<Sexpr> expressions = SexprReader(text).readAll();
  auto* rules = new (jrt::kBoard) jrt::ArrayList<Rule*>();
  auto* warnings = new (jrt::kBoard) jrt::ArrayList<std::string>();
  for (const Sexpr& expression : expressions) {
    if (!expression.isList || expression.list.empty()) {
      warnings->add("unexpected '" + valueOf(expression) + "' at top level, ignored");
      continue;
    }
    std::string head = valueOf(expression.list.front());
    if (head == "version") {
      continue;
    }
    if (head != "rule") {
      warnings->add("'(" + head + " ...)' is not a rule and is not translated");
      continue;
    }
    rules->add(parseRule(expression));
  }
  return new (jrt::kBoard) KiCadDesignRules(sourceName, rules, warnings);
}

std::string KiCadDesignRules::formatMm(double um) { return javaFormatG(um / 1000.0, 4) + " mm"; }

}  // namespace fr
