#pragma once

#include <optional>
#include <string>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * One {@code (rule ...)} of the file (Java record).
 *
 * name: the rule name; layer: the argument of its {@code (layer ...)} clause, or null for all
 * layers; condition: its condition, or null if it has none (it then applies to every pair);
 * clearanceUm: its minimum clearance in micrometres, or null if it sets none; creepageUm: its
 * minimum creepage in micrometres, or null; ignored: true for {@code (severity ignore)};
 * otherConstraints: the constraints Freerouting does not translate, as written; warnings: what
 * could not be read in the rule.
 */
class KiCadDesignRules_Rule : public virtual jrt::Object {
 public:
  KiCadDesignRules_Rule(const std::string& name, const std::optional<std::string>& layer,
                        KiCadRuleCondition* condition, std::optional<double> clearanceUm,
                        std::optional<double> creepageUm, bool ignored,
                        jrt::List<std::string>* otherConstraints, jrt::List<std::string>* warnings)
      : name_(name),
        layer_(layer),
        condition_(condition),
        clearanceUm_(clearanceUm),
        creepageUm_(creepageUm),
        ignored_(ignored),
        otherConstraints_(otherConstraints),
        warnings_(warnings) {}

  std::string name() { return name_; }
  const std::optional<std::string>& layer() { return layer_; }
  KiCadRuleCondition* condition() { return condition_; }
  std::optional<double> clearanceUm() { return clearanceUm_; }
  std::optional<double> creepageUm() { return creepageUm_; }
  bool ignored() { return ignored_; }
  jrt::List<std::string>* otherConstraints() { return otherConstraints_; }
  jrt::List<std::string>* warnings() { return warnings_; }

 private:
  std::string name_;
  std::optional<std::string> layer_;
  KiCadRuleCondition* condition_;
  std::optional<double> clearanceUm_;
  std::optional<double> creepageUm_;
  bool ignored_;
  jrt::List<std::string>* otherConstraints_;
  jrt::List<std::string>* warnings_;
};

/**
 * The custom design rules of a KiCad board, read from its {@code .kicad_dru} file.
 *
 * <p>KiCad writes only the net class clearances into a Specctra design: the custom rules (a larger
 * clearance between a mains class and the rest of the board, a smaller one under a fine-pitch
 * part...) are lost on the way. This class reads them back; KiCadDesignRulesTranslator turns their
 * clearance constraints into clearance classes of the loaded board.
 *
 * <p>Only the rules matter here, in file order (a later matching rule overrides an earlier one, as
 * in KiCad). What is read but not translated (constraints other than a minimum clearance, the
 * severity) is kept, so that the translator can report it.
 */
class KiCadDesignRules final : public virtual jrt::Object {
 public:
  using Rule = KiCadDesignRules_Rule;

  /** The file name, for messages. */
  std::string sourceName() { return sourceName_; }

  /** The rules, in file order. */
  jrt::List<Rule*>* rules() { return rules_; }

  /** What could not be read outside of the rules. */
  jrt::List<std::string>* warnings() { return warnings_; }

  /** Reads a {@code .kicad_dru} file (throws IOException, NoSuchFileException). */
  static KiCadDesignRules* read(const std::string& path);

  /**
   * Parses the text of a {@code .kicad_dru} file.
   *
   * @throws IllegalArgumentException if the text is not a list of S-expressions
   */
  static KiCadDesignRules* parse(const std::string& text, const std::string& sourceName);

  /** A length in micrometres, written in millimetres for messages ("%.4g mm"). */
  static std::string formatMm(double um);

 private:
  KiCadDesignRules(const std::string& sourceName, jrt::List<Rule*>* rules,
                   jrt::List<std::string>* warnings)
      : sourceName_(sourceName), rules_(rules), warnings_(warnings) {}

  std::string sourceName_;
  jrt::List<Rule*>* rules_;
  jrt::List<std::string>* warnings_;
};

}  // namespace fr
