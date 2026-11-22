// Port of app.freerouting.util.TextManager
#include "util/TextManager.h"

#include <cctype>
#include <ctime>

namespace fr {

TextManager::TextManager(const std::string& baseName, const std::string& locale)
    : currentLocale(locale), currentBaseName(baseName) {
  // PORT NOTE: loadResourceBundle(baseName) dropped - there are no ResourceBundles in the port.
}

std::string TextManager::convertInstantToString(int64_t instant) {
  return convertInstantToString(instant, "yyyyMMdd_HHmmss");
}

std::string TextManager::convertInstantToString(int64_t instant, const std::string& format) {
  // PORT NOTE: minimal DateTimeFormatter.ofPattern: supports y, M, d, H, m, s, S letters
  // (numeric forms) and '...' quoted literals; other characters are copied literally.
  std::time_t secs = static_cast<std::time_t>(jrt::Math::floorDiv(instant, static_cast<int64_t>(1000)));
  int32_t millis = static_cast<int32_t>(jrt::Math::floorMod(instant, static_cast<int64_t>(1000)));
  std::tm tm{};
  localtime_r(&secs, &tm);
  std::string result;
  size_t i = 0;
  while (i < format.size()) {
    char c = format[i];
    if (c == '\'') {
      size_t j = i + 1;
      if (j < format.size() && format[j] == '\'') {
        result += '\'';
        i = j + 1;
        continue;
      }
      while (j < format.size() && format[j] != '\'') {
        result += format[j];
        ++j;
      }
      i = j + 1;
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c))) {
      size_t j = i;
      while (j < format.size() && format[j] == c) {
        ++j;
      }
      int32_t count = static_cast<int32_t>(j - i);
      int64_t value = -1;
      switch (c) {
        case 'y':
        case 'u':
          value = tm.tm_year + 1900;
          if (count == 2) {
            value = value % 100;
          }
          break;
        case 'M':
          value = tm.tm_mon + 1;
          break;
        case 'd':
          value = tm.tm_mday;
          break;
        case 'H':
          value = tm.tm_hour;
          break;
        case 'm':
          value = tm.tm_min;
          break;
        case 's':
          value = tm.tm_sec;
          break;
        case 'S': {
          std::string frac = jrt::format("%03d", millis);
          while (static_cast<int32_t>(frac.size()) < count) {
            frac += '0';
          }
          result += frac.substr(0, count);
          i = j;
          continue;
        }
        default:
          break;
      }
      if (value < 0) {
        result += format.substr(i, count);
      } else {
        std::string digits = std::to_string(value);
        while (static_cast<int32_t>(digits.size()) < count) {
          digits = "0" + digits;
        }
        result += digits;
      }
      i = j;
      continue;
    }
    result += c;
    ++i;
  }
  return result;
}

std::string TextManager::generateRandomAlphanumericString(int32_t length) {
  std::string characters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  // PORT NOTE: ThreadLocalRandom -> a process-wide jrt::Random.
  static jrt::Random* random = new (jrt::kOp) jrt::Random();
  std::string randomString;
  for (int32_t i = 0; i < length; i++) {
    int32_t index = jrt::d2i(static_cast<double>(characters.length()) * random->nextDouble());
    randomString += characters[index];
  }
  return randomString;
}

namespace {
bool isBlank(const std::string& s) {
  for (char c : s) {
    if (!jrt::Character::isWhitespace(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

// Parses a signed integer number at s[pos..], advancing pos. Returns false on failure.
bool parseSignedDigits(const std::string& s, size_t& pos, std::string& out) {
  size_t start = pos;
  if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
    ++pos;
  }
  size_t digitsStart = pos;
  while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
    ++pos;
  }
  if (pos == digitsStart) {
    pos = start;
    return false;
  }
  out = s.substr(start, pos - start);
  return true;
}

// Minimal java.time.Duration.parse for the "PT[nH][nM][n[.f]S]" strings produced by
// convertFromTimespanToDurationFormat. Returns Duration.getSeconds() or nullopt on parse error.
std::optional<int64_t> parseIsoDurationSeconds(const std::string& text) {
  if (text.size() < 3 || jrt::toUpperCase(text.substr(0, 2)) != "PT") {
    return std::nullopt;
  }
  size_t pos = 2;
  bool any = false;
  __int128 nanos = 0;
  const __int128 NANOS = 1000000000;
  bool hourSeen = false;
  bool minuteSeen = false;
  while (pos < text.size()) {
    std::string num;
    if (!parseSignedDigits(text, pos, num)) {
      return std::nullopt;
    }
    std::string fraction;
    if (pos < text.size() && (text[pos] == '.' || text[pos] == ',')) {
      ++pos;
      size_t fs = pos;
      while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        ++pos;
      }
      fraction = text.substr(fs, pos - fs);
      if (fraction.empty() || fraction.size() > 9) {
        return std::nullopt;
      }
    }
    if (pos >= text.size()) {
      return std::nullopt;
    }
    char unit = static_cast<char>(std::toupper(static_cast<unsigned char>(text[pos])));
    ++pos;
    int64_t value;
    try {
      value = jrt::Long::parseLong(num);
    } catch (const jrt::Exception&) {
      return std::nullopt;
    }
    if (unit == 'H' && fraction.empty() && !hourSeen && !minuteSeen) {
      hourSeen = true;
      nanos += static_cast<__int128>(value) * 3600 * NANOS;
    } else if (unit == 'M' && fraction.empty() && !minuteSeen) {
      minuteSeen = true;
      nanos += static_cast<__int128>(value) * 60 * NANOS;
    } else if (unit == 'S' && pos == text.size()) {
      __int128 f = 0;
      if (!fraction.empty()) {
        std::string padded = fraction;
        while (padded.size() < 9) {
          padded += '0';
        }
        f = std::stoll(padded);
        if (!num.empty() && num[0] == '-') {
          f = -f;
        }
      }
      nanos += static_cast<__int128>(value) * NANOS + f;
    } else {
      return std::nullopt;
    }
    any = true;
  }
  if (!any) {
    return std::nullopt;
  }
  // getSeconds(): floor division
  __int128 secs = nanos / NANOS;
  if (nanos % NANOS != 0 && nanos < 0) {
    secs -= 1;
  }
  return static_cast<int64_t>(secs);
}
}  // namespace

std::optional<int64_t> TextManager::parseTimespanString(
    const std::optional<std::string>& timespanString) {
  if (!timespanString.has_value() || isBlank(*timespanString)) {
    return std::nullopt;
  }
  return parseIsoDurationSeconds(convertFromTimespanToDurationFormat(*timespanString));
}

std::string TextManager::convertFromTimespanToDurationFormat(const std::string& timespanString) {
  std::vector<std::string> parts = jrt::split(timespanString, ':');
  if (timespanString.empty()) {
    parts = {std::string()};  // Java "".split(":") == [""]
  }
  std::string durationString = "PT";
  if (parts.size() == 3) {
    durationString += parts[0] + "H" + parts[1] + "M" + parts[2] + "S";
  } else if (parts.size() == 2) {
    durationString += parts[0] + "M" + parts[1] + "S";
  } else if (parts.size() == 1) {
    durationString += parts[0] + "S";
  }
  return durationString;
}

std::string TextManager::shortenString(const std::string& text, int32_t peakCharacterCount) {
  if (static_cast<int32_t>(text.length()) > peakCharacterCount * 2) {
    return jrt::substring(text, 0, peakCharacterCount) + "..." +
           jrt::substring(text, static_cast<int32_t>(text.length()) - peakCharacterCount);
  }
  return text;
}

std::optional<std::string> TextManager::removeQuotes(const std::optional<std::string>& text) {
  if (!text.has_value() || text->length() < 2) {
    return text;
  }
  return jrt::startsWith(*text, "\"") && jrt::endsWith(*text, "\"")
             ? std::optional<std::string>(text->substr(1, text->length() - 2))
             : text;
}

std::string TextManager::unescapeUnicode(const std::string& text) {
  std::string result;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '\\' && i + 5 < text.size() + 0 && text[i + 1] == 'u' &&
        std::isxdigit(static_cast<unsigned char>(text[i + 2])) &&
        std::isxdigit(static_cast<unsigned char>(text[i + 3])) &&
        std::isxdigit(static_cast<unsigned char>(text[i + 4])) &&
        std::isxdigit(static_cast<unsigned char>(text[i + 5]))) {
      uint32_t cp = static_cast<uint32_t>(std::stoul(text.substr(i + 2, 4), nullptr, 16));
      // encode the UTF-16 code unit as UTF-8 (lone surrogates are encoded as is)
      if (cp < 0x80) {
        result += static_cast<char>(cp);
      } else if (cp < 0x800) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
      } else {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
      }
      i += 6;
    } else {
      result += text[i];
      ++i;
    }
  }
  return result;
}

std::string TextManager::longToHexadecimalString(int64_t longValue) {
  return jrt::format("0x%016llX", static_cast<unsigned long long>(longValue));
}

int64_t TextManager::hexadecimalStringToLong(const std::string& hexString) {
  std::string digits;
  int radix;
  if (jrt::startsWith(hexString, "0x") || jrt::startsWith(hexString, "0X")) {
    digits = hexString.substr(2);
    radix = 16;
  } else {
    digits = hexString;
    radix = 10;
  }
  // Long.parseUnsignedLong
  if (digits.empty()) {
    throw jrt::NumberFormatException("For input string: \"" + digits + "\"");
  }
  size_t start = 0;
  if (digits[0] == '+') {
    start = 1;
    if (digits.size() == 1) {
      throw jrt::NumberFormatException("For input string: \"" + digits + "\"");
    }
  } else if (digits[0] == '-') {
    throw jrt::NumberFormatException("Illegal leading minus sign on unsigned string " + digits +
                                     ".");
  }
  unsigned __int128 value = 0;
  for (size_t k = start; k < digits.size(); ++k) {
    int d;
    char c = digits[k];
    if (c >= '0' && c <= '9') {
      d = c - '0';
    } else if (c >= 'a' && c <= 'z') {
      d = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'Z') {
      d = c - 'A' + 10;
    } else {
      d = 99;
    }
    if (d >= radix) {
      throw jrt::NumberFormatException("For input string: \"" + digits + "\"");
    }
    value = value * radix + d;
    if (value > static_cast<unsigned __int128>(UINT64_MAX)) {
      throw jrt::NumberFormatException("String value " + digits +
                                       " exceeds range of unsigned long.");
    }
  }
  return static_cast<int64_t>(static_cast<uint64_t>(value));
}

std::string TextManager::getText(const std::string& key, const std::vector<std::string>& args) {
  // PORT NOTE: no ResourceBundles in the port, lookupMessage(key) is always null.
  return key;
}

void TextManager::setLocale(const std::string& locale) {
  currentLocale = locale;
  // PORT NOTE: loadResourceBundle(currentBaseName) dropped.
}

}  // namespace fr
