// Port of app.freerouting.util.TextManager
#pragma once

#include "fwd.h"
#include "jrt/jrt.h"

namespace fr {

/**
 * Headless-safe manager for the application's localized text resources and static utilities.
 *
 * PORT NOTE: java.util.Locale -> std::string language tag (e.g. "en"); Class baseClass -> its
 * name. ResourceBundles are not available in the port: getText(key, args) returns the key (with
 * Java's MessageFormat-free fallback) - it is only used by GUI info printing. java.time.Instant ->
 * int64_t epoch ms. decryptAes256Cbc (javax.crypto, API keys only) is dropped.
 */
class TextManager : public virtual jrt::Object {
 public:
  /** Creates a text manager for the given base resource bundle name and locale. */
  TextManager(const std::string& baseName, const std::string& locale);

  /** Formats an instant (epoch ms) using the default timestamp pattern "yyyyMMdd_HHmmss". */
  static std::string convertInstantToString(int64_t instant);

  /** Formats an instant (epoch ms) using the given DateTimeFormatter pattern (local time zone). */
  static std::string convertInstantToString(int64_t instant, const std::string& format);

  /** Generates a random alphanumeric string of the requested length. */
  static std::string generateRandomAlphanumericString(int32_t length);

  /**
   * Parses a human-readable timespan string (HH:mm:ss, mm:ss or ss) into seconds, or nullopt if
   * parsing fails.
   */
  static std::optional<int64_t> parseTimespanString(const std::optional<std::string>& timespanString);

  /** Converts a colon-separated timespan into an ISO-8601 duration string. */
  static std::string convertFromTimespanToDurationFormat(const std::string& timespanString);

  /** Shortens a string by replacing the middle part with dots. */
  static std::string shortenString(const std::string& text, int32_t peakCharacterCount);

  /** Removes quotes from the beginning and end of a string. */
  static std::optional<std::string> removeQuotes(const std::optional<std::string>& text);

  /** Unescapes \\uXXXX sequences in a string. */
  static std::string unescapeUnicode(const std::string& text);

  /** Formats a long value as a fixed-width uppercase hexadecimal string ("0x%016X"). */
  static std::string longToHexadecimalString(int64_t longValue);

  /** Parses a hexadecimal (0x prefix) or decimal string into an unsigned long value. */
  static int64_t hexadecimalStringToLong(const std::string& hexString);

  /** Returns the localized text for key, formatted with args. */
  std::string getText(const std::string& key, const std::vector<std::string>& args = {});

  std::string getLocale() { return currentLocale; }

  void setLocale(const std::string& locale);

 private:
  std::string currentLocale;
  std::string currentBaseName;
};

}  // namespace fr
