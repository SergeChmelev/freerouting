#pragma once

#include <istream>

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr::dsn {

/**
 * Reads a Specctra .rules file and applies the parsed rules directly to a BasicBoard, without any
 * GUI dependency. Public read entry point for Specctra rules files.
 *
 * PORT NOTE: InputStream -> std::istream&; Java's null-stream checks cannot occur and are dropped;
 * closeQuietly is a no-op (the caller owns the stream).
 */
class RulesReader final : public virtual jrt::Object {
 public:
  /**
   * Reads the rules from in and applies them to board. Returns true if the rules were parsed and
   * applied successfully; false on any parse or I/O error. A designName mismatch is only logged.
   */
  static bool read(std::istream& in, const std::string& designName, fr::BasicBoard* board);

  /** As above, also applying parsed autoroute_settings to the optional (nullable) targetSettings. */
  static bool read(std::istream& in, const std::string& designName, fr::BasicBoard* board,
                   fr::RouterSettings* targetSettings);

  /** Reads only the RouterSettings from a rules file stream; null if not found or on error. */
  static fr::RouterSettings* readRouterSettings(std::istream& in);

 private:
  RulesReader() = default;

  static LayerStructure* discoverLayerStructure(jrt::Array<int8_t>* data);

  static void applyRules(jrt::Collection<Rule*>* rules, fr::BasicBoard* board,
                         const std::optional<std::string>& layerName);

  static void applyLayerRules(IJFlexScanner* scanner, fr::BasicBoard* board);

  static void applyViaInfo(IJFlexScanner* scanner, fr::BasicBoard* board);

  static void applyViaRule(IJFlexScanner* scanner, fr::BasicBoard* board);

  static void applyNetClass(IJFlexScanner* scanner, LayerStructure* layerStructure,
                            fr::BasicBoard* board);

  // PORT NOTE: closing a std::istream owned by the caller is a no-op.
  static void closeQuietly(std::istream& stream) {}
};

}  // namespace fr::dsn
