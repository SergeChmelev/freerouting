#include "io/kicad/KiCadRuleCondition.h"

#include <array>

namespace fr {

namespace {

using Tri = KiCadRuleCondition_Tri;
using ItemType = KiCadRuleCondition_ItemType;
using CItem = KiCadRuleCondition_Item;
using Value = KiCadRuleCondition_Value;
using Node = KiCadRuleCondition_Node;

// Set.of("netclass", "netname", "type", "pad_type", "reference", "parent", "pad_number")
const std::array<const char*, 7> PROPERTIES = {"netclass",  "netname", "type",      "pad_type",
                                               "reference", "parent",  "pad_number"};
// Set.of("memberoffootprint", "hasnetclass", "isplated")
const std::array<const char*, 3> FUNCTIONS = {"memberoffootprint", "hasnetclass", "isplated"};

int32_t ordinal(Tri tri) { return static_cast<int32_t>(tri); }

/** The length of the UTF-8 sequence that starts with this byte. */
std::size_t codePointLength(const std::string& s, std::size_t i) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
  return i + n <= s.size() ? n : s.size() - i;
}

bool sameChar(char a, char b, bool caseSensitive) {
  if (caseSensitive) {
    return a == b;
  }
  return jrt::Character::toLowerCase(static_cast<unsigned char>(a)) ==
         jrt::Character::toLowerCase(static_cast<unsigned char>(b));
}

/** "*" any sequence, "?" one character, everything else literally (the whole text must match). */
bool globMatch(const std::string& glob, const std::string& text, bool caseSensitive) {
  std::size_t g = 0;
  std::size_t t = 0;
  std::size_t starG = std::string::npos;
  std::size_t starT = 0;
  while (t < text.size()) {
    if (g < glob.size() && glob[g] == '*') {
      starG = g++;
      starT = t;
    } else if (g < glob.size() && glob[g] == '?') {
      g++;
      t += codePointLength(text, t);
    } else if (g < glob.size() && sameChar(glob[g], text[t], caseSensitive)) {
      g++;
      t++;
    } else if (starG != std::string::npos) {
      g = starG + 1;
      starT += codePointLength(text, starT);
      t = starT;
    } else {
      return false;
    }
  }
  while (g < glob.size() && glob[g] == '*') {
    g++;
  }
  return g == glob.size();
}

/** Java String.split(","): trailing empty strings are removed. */
std::vector<std::string> splitComma(const std::string& s) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    std::size_t comma = s.find(',', start);
    if (comma == std::string::npos) {
      parts.push_back(s.substr(start));
      break;
    }
    parts.push_back(s.substr(start, comma - start));
    start = comma + 1;
  }
  if (s.empty()) {
    return parts;  // "".split(",") is [""]
  }
  while (!parts.empty() && parts.back().empty()) {
    parts.pop_back();
  }
  return parts;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Syntax tree

class KiCadRuleCondition_Or : public KiCadRuleCondition_Node {
 public:
  KiCadRuleCondition_Or(Node* left, Node* right) : left(left), right(right) {}
  Value eval(CItem* a, CItem* b) override {
    return Value::Bool(KiCadRuleCondition_Tri_or_(KiCadRuleCondition::truth(left->eval(a, b)),
                                                  KiCadRuleCondition::truth(right->eval(a, b))));
  }
  void collectUnsupported(jrt::List<std::string>* out) override {
    left->collectUnsupported(out);
    right->collectUnsupported(out);
  }
  void signature(CItem* item, std::string& out) override {
    left->signature(item, out);
    right->signature(item, out);
  }
  Node* left;
  Node* right;
};

class KiCadRuleCondition_And : public KiCadRuleCondition_Node {
 public:
  KiCadRuleCondition_And(Node* left, Node* right) : left(left), right(right) {}
  Value eval(CItem* a, CItem* b) override {
    return Value::Bool(KiCadRuleCondition_Tri_and_(KiCadRuleCondition::truth(left->eval(a, b)),
                                                   KiCadRuleCondition::truth(right->eval(a, b))));
  }
  void collectUnsupported(jrt::List<std::string>* out) override {
    left->collectUnsupported(out);
    right->collectUnsupported(out);
  }
  void signature(CItem* item, std::string& out) override {
    left->signature(item, out);
    right->signature(item, out);
  }
  Node* left;
  Node* right;
};

class KiCadRuleCondition_Not : public KiCadRuleCondition_Node {
 public:
  explicit KiCadRuleCondition_Not(Node* operand) : operand(operand) {}
  Value eval(CItem* a, CItem* b) override {
    return Value::Bool(KiCadRuleCondition_Tri_not_(KiCadRuleCondition::truth(operand->eval(a, b))));
  }
  void collectUnsupported(jrt::List<std::string>* out) override {
    operand->collectUnsupported(out);
  }
  void signature(CItem* item, std::string& out) override { operand->signature(item, out); }
  Node* operand;
};

class KiCadRuleCondition_Literal : public KiCadRuleCondition_Node {
 public:
  explicit KiCadRuleCondition_Literal(const Value& value) : value(value) {}
  Value eval(CItem* a, CItem* b) override { return value; }
  Value value;
};

/** A part of the expression Freerouting does not evaluate. */
class KiCadRuleCondition_Unknown : public KiCadRuleCondition_Node {
 public:
  Value eval(CItem* a, CItem* b) override { return Value::Undefined(); }
};

namespace {

bool isLiteralOf(Node* node, Value::Kind kind) {
  auto* literal = dynamic_cast<KiCadRuleCondition_Literal*>(node);
  return literal != nullptr && literal->value.kind == kind;
}

}  // namespace

/** {@code A.Property} or {@code B.Property}. */
class KiCadRuleCondition_Member : public KiCadRuleCondition_Node {
 public:
  KiCadRuleCondition_Member(bool sideA, const std::string& name, const std::string& text)
      : sideA(sideA), name(name), text_(text) {}

  Value eval(CItem* a, CItem* b) override {
    if (!KiCadRuleCondition::isProperty(name)) {
      return Value::Undefined();
    }
    return Value::Prop(name, sideA ? a : b);
  }

  std::string text(CItem* item) {
    std::optional<std::string> value = KiCadRuleCondition::isProperty(name)
                                           ? KiCadRuleCondition::propertyText(name, item)
                                           : std::nullopt;
    return !value.has_value() ? std::string(1, '\0') : jrt::toLowerCase(*value);
  }

  void collectUnsupported(jrt::List<std::string>* out) override {
    if (!KiCadRuleCondition::isProperty(name)) {
      out->add("'" + text_ + "' (property not known to Freerouting)");
    }
  }

  bool sideA;
  std::string name;
  std::string text_;  // PORT NOTE: record component `text` collides with the method text(Item)
};

class KiCadRuleCondition_Compare : public KiCadRuleCondition_Node {
 public:
  KiCadRuleCondition_Compare(const std::string& op, Node* left, Node* right, const std::string& text)
      : op(op), left(left), right(right), text(text) {}

  Value eval(CItem* a, CItem* b) override {
    return Value::Bool(KiCadRuleCondition::compare(op, left->eval(a, b), right->eval(a, b)));
  }

  void collectUnsupported(jrt::List<std::string>* out) override {
    int32_t before = out->size();
    left->collectUnsupported(out);
    right->collectUnsupported(out);
    bool equality = op == "==" || op == "!=";
    bool textual = isText(left) && isText(right);
    bool numeric = isLiteralOf(left, Value::Kind::NUM) && isLiteralOf(right, Value::Kind::NUM);
    if (out->size() == before && !(equality && textual) && !numeric) {
      out->add("'" + text + "' (comparison not known to Freerouting)");
    }
  }

  static bool isText(Node* node) {
    return dynamic_cast<KiCadRuleCondition_Member*>(node) != nullptr ||
           isLiteralOf(node, Value::Kind::STR);
  }

  void signature(CItem* item, std::string& out) override {
    // A property compared with a literal gives one result per item; a property compared with
    // a property of the other item needs the value itself.
    auto* m = dynamic_cast<KiCadRuleCondition_Member*>(left);
    auto* n = dynamic_cast<KiCadRuleCondition_Member*>(right);
    if (m != nullptr && n != nullptr) {
      out += m->text(item);
      out += '\x01';
      out += n->text(item);
      out += '\x01';
    } else {
      out += jrt::str(ordinal(KiCadRuleCondition::truth(eval(item, item))));
      left->signature(item, out);
      right->signature(item, out);
    }
  }

  std::string op;
  Node* left;
  Node* right;
  std::string text;
};

/** {@code A.function('argument')}. */
class KiCadRuleCondition_Call : public KiCadRuleCondition_Node {
 public:
  KiCadRuleCondition_Call(bool sideA, const std::string& name, const std::vector<std::string>& args,
                          const std::string& text)
      : sideA(sideA), name(name), args(args), text(text) {}

  Value eval(CItem* a, CItem* b) override {
    CItem* item = sideA ? a : b;
    if (item == nullptr) {
      throw jrt::NullPointerException("item");
    }
    bool pad = item->type() == ItemType::PAD;
    std::string arg = args.empty() ? std::string() : args.front();
    Tri result;
    if (name == "memberoffootprint") {
      if (jrt::startsWith(arg, "${")) {
        result = Tri::UNKNOWN;  // component class selector
      } else if (arg.find(':') != std::string::npos) {
        // KiCad tests the library identifier (case-sensitively) when the selector has a ':',
        // and the reference otherwise, both with wxString::Matches (testFootprintSelector in
        // pcbexpr_functions.cpp)
        result = KiCadRuleCondition_Tri_of(pad && item->libraryId().has_value() &&
                                           KiCadRuleCondition::matches(arg, *item->libraryId(), true));
      } else {
        result = KiCadRuleCondition_Tri_of(pad && item->footprint().has_value() &&
                                           KiCadRuleCondition::matches(arg, *item->footprint(), true));
      }
    } else if (name == "hasnetclass") {
      // NETCLASS::ContainsNetclassWithName: wxString::Matches on each constituent
      bool found = false;
      for (const std::string& constituent : *KiCadRuleCondition::constituents(item->netClass())) {
        found |= KiCadRuleCondition::matches(arg, constituent, true);
      }
      result = KiCadRuleCondition_Tri_of(found);
    } else if (name == "isplated") {
      result = KiCadRuleCondition_Tri_of(item->type() == ItemType::VIA || (pad && !item->smd()));
    } else {
      result = Tri::UNKNOWN;
    }
    return Value::Bool(result);
  }

  void collectUnsupported(jrt::List<std::string>* out) override {
    if (!KiCadRuleCondition::isFunction(name)) {
      out->add("'" + text + "' (function not known to Freerouting)");
    } else if (name == "memberoffootprint" && !args.empty() && jrt::startsWith(args.front(), "${")) {
      out->add("'" + text + "' (component class selector)");
    }
  }

  void signature(CItem* item, std::string& out) override {
    out += jrt::str(ordinal(KiCadRuleCondition::truth(eval(item, item))));
  }

  bool sideA;
  std::string name;
  std::vector<std::string> args;
  std::string text;
};

// ---------------------------------------------------------------------------------------------
// Parser

class KiCadRuleCondition_Parser {
 public:
  explicit KiCadRuleCondition_Parser(const std::string& text) : text(text) {}

  bool isFinished() {
    skipSpaces();
    return pos >= text.size();
  }

  std::string rest() { return text.substr(pos); }

  Node* parseExpression() {
    Node* left = parseAnd();
    while (accept("||")) {
      left = new (jrt::kBoard) KiCadRuleCondition_Or(left, parseAnd());
    }
    return left;
  }

  bool sawUnit = false;

 private:
  void skipSpaces() {
    while (pos < text.size() && jrt::Character::isWhitespace(static_cast<unsigned char>(text[pos]))) {
      pos++;
    }
  }

  bool accept(const std::string& token) {
    skipSpaces();
    if (text.compare(pos, token.size(), token) == 0 && pos + token.size() <= text.size()) {
      pos += token.size();
      return true;
    }
    return false;
  }

  bool startsWithAt(const std::string& token) {
    return pos + token.size() <= text.size() && text.compare(pos, token.size(), token) == 0;
  }

  Node* parseAnd() {
    Node* left = parseUnary();
    while (accept("&&")) {
      left = new (jrt::kBoard) KiCadRuleCondition_And(left, parseUnary());
    }
    return left;
  }

  Node* parseUnary() {
    skipSpaces();
    if (pos < text.size() && text[pos] == '!' && !startsWithAt("!=")) {
      pos++;
      return new (jrt::kBoard) KiCadRuleCondition_Not(parseUnary());
    }
    return parseComparison();
  }

  Node* parseComparison() {
    std::size_t start = pos;
    Node* left = parseSum();
    for (const char* op : {"==", "!=", "<=", ">=", "<", ">"}) {
      if (accept(op)) {
        Node* right = parseSum();
        return new (jrt::kBoard)
            KiCadRuleCondition_Compare(op, left, right, jrt::trim(text.substr(start, pos - start)));
      }
    }
    return left;
  }

  Node* parseSum() {
    Node* left = parseProduct();
    while (true) {
      skipSpaces();
      if (accept("+")) {
        left = arithmetic(left, parseProduct(), '+');
      } else if (pos < text.size() && text[pos] == '-') {
        pos++;
        left = arithmetic(left, parseProduct(), '-');
      } else {
        return left;
      }
    }
  }

  Node* parseProduct() {
    Node* left = parsePrimary();
    while (true) {
      if (accept("*")) {
        left = arithmetic(left, parsePrimary(), '*');
      } else if (accept("/")) {
        left = arithmetic(left, parsePrimary(), '/');
      } else {
        return left;
      }
    }
  }

  /** Folds arithmetic on constants; anything else is unknown. */
  static Node* arithmetic(Node* left, Node* right, char op) {
    if (isLiteralOf(left, Value::Kind::NUM) && isLiteralOf(right, Value::Kind::NUM)) {
      double a = static_cast<KiCadRuleCondition_Literal*>(left)->value.num;
      double b = static_cast<KiCadRuleCondition_Literal*>(right)->value.num;
      double value;
      switch (op) {
        case '+':
          value = a + b;
          break;
        case '-':
          value = a - b;
          break;
        case '*':
          value = a * b;
          break;
        default:
          value = a / b;
          break;
      }
      return new (jrt::kBoard) KiCadRuleCondition_Literal(Value::Num(value));
    }
    return new (jrt::kBoard) KiCadRuleCondition_Unknown();
  }

  Node* parsePrimary() {
    skipSpaces();
    if (pos >= text.size()) {
      throw jrt::IllegalArgumentException("unexpected end");
    }
    char c = text[pos];
    if (c == '(') {
      pos++;
      Node* inner = parseExpression();
      if (!accept(")")) {
        throw jrt::IllegalArgumentException("missing ')'");
      }
      return inner;
    }
    if (c == '\'' || c == '"') {
      return new (jrt::kBoard) KiCadRuleCondition_Literal(Value::Str(parseString()));
    }
    if (c == '-') {
      pos++;
      Node* operand = parsePrimary();
      return arithmetic(new (jrt::kBoard) KiCadRuleCondition_Literal(Value::Num(0)), operand, '-');
    }
    if (jrt::Character::isDigit(static_cast<unsigned char>(c)) || c == '.') {
      return parseNumber();
    }
    if (jrt::Character::isLetter(static_cast<unsigned char>(c)) || c == '_') {
      std::size_t start = pos;
      std::string object = parseIdentifier();
      if (!accept(".")) {
        return new (jrt::kBoard) KiCadRuleCondition_Unknown();  // a bare identifier (e.g. a layer name)
      }
      std::string member = parseIdentifier();
      bool sideA = object == "A" || object == "AB";
      bool known = sideA || object == "B";
      if (accept("(")) {
        std::vector<std::string> args;
        if (!accept(")")) {
          do {
            skipSpaces();
            if (pos < text.size() && (text[pos] == '\'' || text[pos] == '"')) {
              args.push_back(parseString());
            } else {
              parseSum();
              args.push_back("");
            }
          } while (accept(","));
          if (!accept(")")) {
            throw jrt::IllegalArgumentException("missing ')'");
          }
        }
        std::string callText = text.substr(start, pos - start);
        std::string name = known ? jrt::toLowerCase(member) : "?" + object;
        return new (jrt::kBoard) KiCadRuleCondition_Call(sideA, name, args, callText);
      }
      std::string memberText = text.substr(start, pos - start);
      return new (jrt::kBoard) KiCadRuleCondition_Member(
          sideA, known ? jrt::toLowerCase(member) : "?" + object, memberText);
    }
    throw jrt::IllegalArgumentException("unexpected '" + std::string(1, c) + "'");
  }

  std::string parseIdentifier() {
    skipSpaces();
    std::size_t start = pos;
    while (pos < text.size() &&
           (jrt::Character::isLetterOrDigit(static_cast<unsigned char>(text[pos])) || text[pos] == '_')) {
      pos++;
    }
    if (start == pos) {
      throw jrt::IllegalArgumentException("identifier expected");
    }
    return text.substr(start, pos - start);
  }

  std::string parseString() {
    char quote = text[pos++];
    std::string result;
    while (pos < text.size() && text[pos] != quote) {
      result += text[pos++];
    }
    if (pos >= text.size()) {
      throw jrt::IllegalArgumentException("unterminated string");
    }
    pos++;
    return result;
  }

  /** A number with an optional KiCad length unit, in micrometres (millimetres if none). */
  Node* parseNumber() {
    std::size_t start = pos;
    while (pos < text.size() && (jrt::Character::isDigit(static_cast<unsigned char>(text[pos])) || text[pos] == '.')) {
      pos++;
    }
    double value;
    try {
      value = jrt::Double::parseDouble(text.substr(start, pos - start));
    } catch (const jrt::NumberFormatException& e) {
      throw jrt::IllegalArgumentException("bad number");
    }
    skipSpaces();
    std::size_t unitStart = pos;
    while (pos < text.size() && jrt::Character::isLetter(static_cast<unsigned char>(text[pos]))) {
      pos++;
    }
    std::string unit = jrt::toLowerCase(text.substr(unitStart, pos - unitStart));
    double scale;
    if (unit.empty() || unit == "mm") {
      scale = 1000.0;
    } else if (unit == "mil" || unit == "mils" || unit == "thou") {
      scale = 25.4;
    } else if (unit == "in" || unit == "inch") {
      scale = 25400.0;
    } else if (unit == "um") {
      scale = 1.0;
    } else {
      scale = jrt::Double::NaN;
    }
    if (jrt::Double::isNaN(scale)) {
      return new (jrt::kBoard) KiCadRuleCondition_Unknown();  // deg, ps, fs...: not a length
    }
    sawUnit |= !unit.empty();
    return new (jrt::kBoard) KiCadRuleCondition_Literal(Value::Num(value * scale));
  }

  const std::string& text;
  std::size_t pos = 0;
};

// ---------------------------------------------------------------------------------------------
// KiCadRuleCondition

KiCadRuleCondition::KiCadRuleCondition(const std::string& expression)
    : expression_(expression), root(nullptr) {
  unsupportedParts_ = new (jrt::kBoard) jrt::ArrayList<std::string>();
  Node* parsed;
  try {
    KiCadRuleCondition_Parser parser(expression_);
    parsed = parser.parseExpression();
    if (!parser.isFinished()) {
      throw jrt::IllegalArgumentException("unexpected '" + parser.rest() + "'");
    }
  } catch (const jrt::IllegalArgumentException& e) {
    unsupportedParts_->add("unparsable condition (" + e.getMessage() + ")");
    parsed = new (jrt::kBoard) KiCadRuleCondition_Unknown();
  }
  this->root = parsed;
  root->collectUnsupported(unsupportedParts_);
}

KiCadRuleCondition* KiCadRuleCondition::parse(const std::optional<std::string>& expression) {
  return new (jrt::kBoard) KiCadRuleCondition(expression.has_value() ? jrt::trim(*expression) : "");
}

jrt::List<std::string>* KiCadRuleCondition::unsupportedParts() {
  auto* copy = new (jrt::kOp) jrt::ArrayList<std::string>();
  for (const std::string& part : *unsupportedParts_) {
    copy->add(part);
  }
  return copy;
}

KiCadRuleCondition_Tri KiCadRuleCondition::evaluate(Item* a, Item* b) {
  if (expression_.empty()) {
    return Tri::TRUE_;
  }
  return KiCadRuleCondition_Tri_or_(truth(root->eval(a, b)), truth(root->eval(b, a)));
}

std::string KiCadRuleCondition::signature(Item* item) {
  std::string result;
  root->signature(item, result);
  return result;
}

jrt::List<std::string>* KiCadRuleCondition::namesMatchingNothing(jrt::Collection<Item*>* items) {
  std::vector<Node*> tests;
  collectNameTests(root, tests);
  auto* result = new (jrt::kOp) jrt::ArrayList<std::string>();
  for (Node* test : tests) {
    bool found = false;
    for (Item* item : *items) {
      if (nameTest(test, item) != Tri::FALSE_) {
        found = true;
        break;
      }
    }
    if (!found) {
      auto* c = dynamic_cast<KiCadRuleCondition_Compare*>(test);
      result->add("'" + (c != nullptr ? c->text : static_cast<KiCadRuleCondition_Call*>(test)->text) +
                  "'");
    }
  }
  return result;
}

namespace {

bool isNameProperty(Node* node) {
  auto* m = dynamic_cast<KiCadRuleCondition_Member*>(node);
  return m != nullptr && (m->name == "netclass" || m->name == "netname");
}

bool isNameComparison(KiCadRuleCondition_Compare* c) {
  if (c->op != "==" && c->op != "!=") {
    return false;
  }
  return (isNameProperty(c->left) && isLiteralOf(c->right, Value::Kind::STR)) ||
         (isNameProperty(c->right) && isLiteralOf(c->left, Value::Kind::STR));
}

}  // namespace

void KiCadRuleCondition::collectNameTests(Node* node, std::vector<Node*>& out) {
  if (auto* o = dynamic_cast<KiCadRuleCondition_Or*>(node)) {
    collectNameTests(o->left, out);
    collectNameTests(o->right, out);
  } else if (auto* a = dynamic_cast<KiCadRuleCondition_And*>(node)) {
    collectNameTests(a->left, out);
    collectNameTests(a->right, out);
  } else if (auto* n = dynamic_cast<KiCadRuleCondition_Not*>(node)) {
    collectNameTests(n->operand, out);
  } else if (auto* c = dynamic_cast<KiCadRuleCondition_Compare*>(node)) {
    if (isNameComparison(c)) {
      out.push_back(c);
    }
  } else if (auto* call = dynamic_cast<KiCadRuleCondition_Call*>(node)) {
    if ((call->name == "memberoffootprint" || call->name == "hasnetclass") && !call->args.empty() &&
        !jrt::startsWith(call->args.front(), "${")) {
      out.push_back(call);
    }
  }
}

KiCadRuleCondition_Tri KiCadRuleCondition::nameTest(Node* test, Item* item) {
  if (auto* c = dynamic_cast<KiCadRuleCondition_Compare*>(test)) {
    return compare("==", c->left->eval(item, item), c->right->eval(item, item));
  }
  return truth(test->eval(item, item));
}

// ------------------------------------------------------------------------------------------
// Values

KiCadRuleCondition_Tri KiCadRuleCondition::truth(const Value& value) {
  switch (value.kind) {
    case Value::Kind::BOOL:
      return value.boolValue;
    case Value::Kind::NUM:
      return KiCadRuleCondition_Tri_of(value.num != 0);
    default:
      return Tri::UNKNOWN;
  }
}

bool KiCadRuleCondition::isProperty(const std::string& name) {
  for (const char* p : PROPERTIES) {
    if (name == p) return true;
  }
  return false;
}

bool KiCadRuleCondition::isFunction(const std::string& name) {
  for (const char* f : FUNCTIONS) {
    if (name == f) return true;
  }
  return false;
}

std::optional<std::string> KiCadRuleCondition::propertyText(const std::string& name, Item* item) {
  if (item == nullptr) {
    throw jrt::NullPointerException("item");
  }
  bool pad = item->type() == ItemType::PAD;
  if (name == "netclass") return item->netClass();
  if (name == "netname") return item->netName();
  if (name == "type") return KiCadRuleCondition_ItemType_kicadName(item->type());
  if (name == "pad_type") {
    return pad ? std::optional<std::string>(item->smd() ? "SMD" : "Through-hole") : std::nullopt;
  }
  if (name == "reference" || name == "pad_number") {
    return pad ? (name == "reference" ? item->footprint() : item->padNumber()) : std::nullopt;
  }
  // the parent of a track is the board, named by its UUID: it never equals a reference
  if (name == "parent") return pad ? item->footprint() : std::string("\0board", 6);
  return std::nullopt;
}

bool KiCadRuleCondition::matches(const std::string& pattern, const std::string& text,
                                 bool caseSensitive) {
  if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos) {
    return caseSensitive ? pattern == text : jrt::equalsIgnoreCase(pattern, text);
  }
  return globMatch(pattern, text, caseSensitive);
}

bool KiCadRuleCondition::wildcardMatches(const std::string& glob, const std::string& text,
                                         bool caseSensitive) {
  return globMatch(glob, text, caseSensitive);
}

jrt::List<std::string>* KiCadRuleCondition::constituents(const std::string& netClass) {
  auto* result = new (jrt::kOp) jrt::ArrayList<std::string>();
  for (const std::string& part : splitComma(netClass)) {
    result->add(jrt::trim(part));
  }
  return result;
}

std::optional<bool> KiCadRuleCondition::propertyEquals(const Value& prop, const std::string& literal) {
  std::optional<std::string> text = propertyText(prop.text, prop.item);
  if (!text.has_value()) {
    return std::nullopt;
  }
  if (prop.text == "netclass") {
    for (const std::string& constituent : *constituents(*text)) {
      if (matches(literal, constituent)) {
        return true;
      }
    }
  }
  return matches(literal, *text);
}

KiCadRuleCondition_Tri KiCadRuleCondition::compare(const std::string& op, const Value& left,
                                                   const Value& right) {
  using K = Value::Kind;
  if (left.kind == K::STR && right.kind == K::PROP) {
    return compare(op, right, left);
  }
  bool equality = op == "==" || op == "!=";
  if (left.kind == K::PROP && right.kind == K::STR && equality) {
    std::optional<bool> equal = propertyEquals(left, right.text);
    if (!equal.has_value()) {
      return Tri::FALSE_;  // KiCad: an undefined value is neither equal nor unequal
    }
    return KiCadRuleCondition_Tri_of(*equal == (op == "=="));
  }
  if (left.kind == K::PROP && right.kind == K::PROP && equality) {
    std::optional<std::string> a = propertyText(left.text, left.item);
    std::optional<std::string> b = propertyText(right.text, right.item);
    if (!a.has_value() || !b.has_value()) {
      return Tri::FALSE_;
    }
    return KiCadRuleCondition_Tri_of(jrt::equalsIgnoreCase(*a, *b) == (op == "=="));
  }
  if (left.kind == K::STR && right.kind == K::STR && equality) {
    return KiCadRuleCondition_Tri_of(matches(right.text, left.text) == (op == "=="));
  }
  if (left.kind == K::NUM && right.kind == K::NUM) {
    double a = left.num;
    double b = right.num;
    bool result;
    if (op == "==") {
      result = a == b;
    } else if (op == "!=") {
      result = a != b;
    } else if (op == "<") {
      result = a < b;
    } else if (op == ">") {
      result = a > b;
    } else if (op == "<=") {
      result = a <= b;
    } else {
      result = a >= b;
    }
    return KiCadRuleCondition_Tri_of(result);
  }
  return Tri::UNKNOWN;
}

// ------------------------------------------------------------------------------------------

std::optional<double> KiCadRuleCondition::parseLength(const std::string& text,
                                                      jrt::List<std::string>* warnings) {
  KiCadRuleCondition_Parser parser(text);
  try {
    Node* node = parser.parseExpression();
    if (parser.isFinished()) {
      Value value = node->eval(nullptr, nullptr);
      if (value.kind == Value::Kind::NUM) {
        if (!parser.sawUnit) {
          warnings->add("value '" + text + "' has no unit and is read as millimetres");
        }
        return value.num;
      }
    }
  } catch (const jrt::IllegalArgumentException& e) {
    // not a constant length
  } catch (const jrt::NullPointerException& e) {
    // not a constant length
  }
  return std::nullopt;
}

jrt::LinkedHashSet<std::string>* KiCadRuleCondition::supportedNames() {
  // PORT NOTE: Java iterates the Set.of(...) sets in an unspecified order; the port uses the order
  // in which they are written.
  auto* result = new (jrt::kOp) jrt::LinkedHashSet<std::string>();
  for (const char* p : PROPERTIES) result->add(p);
  for (const char* f : FUNCTIONS) result->add(f);
  return result;
}

}  // namespace fr
