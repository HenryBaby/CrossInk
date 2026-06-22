#include "HttpHeaderUtils.h"

#include <algorithm>
#include <cctype>

namespace {
constexpr size_t MAX_HEADER_FILENAME_BYTES = 512;

bool equalsIgnoreCase(const std::string_view left, const std::string_view right) {
  if (left.size() != right.size()) return false;
  for (size_t i = 0; i < left.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(left[i])) !=
        std::tolower(static_cast<unsigned char>(right[i]))) {
      return false;
    }
  }
  return true;
}

std::string_view trim(const std::string_view value) {
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) ++start;

  size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return value.substr(start, end - start);
}

bool findParameter(const std::string_view header, const std::string_view target, std::string_view& value,
                   bool& quoted) {
  size_t pos = header.find(';');
  while (pos != std::string_view::npos && pos < header.size()) {
    ++pos;
    while (pos < header.size() && std::isspace(static_cast<unsigned char>(header[pos]))) ++pos;

    const size_t keyStart = pos;
    while (pos < header.size() && header[pos] != '=' && header[pos] != ';') ++pos;
    if (pos >= header.size() || header[pos] != '=') continue;

    const std::string_view key = trim(header.substr(keyStart, pos - keyStart));
    ++pos;
    while (pos < header.size() && std::isspace(static_cast<unsigned char>(header[pos]))) ++pos;

    quoted = pos < header.size() && header[pos] == '"';
    if (quoted) {
      const size_t valueStart = ++pos;
      bool escaped = false;
      while (pos < header.size()) {
        const char c = header[pos];
        if (!escaped && c == '"') break;
        escaped = !escaped && c == '\\';
        if (c != '\\') escaped = false;
        ++pos;
      }
      if (pos >= header.size()) return false;
      value = header.substr(valueStart, pos - valueStart);
    } else {
      const size_t valueStart = pos;
      while (pos < header.size() && header[pos] != ';') ++pos;
      value = trim(header.substr(valueStart, pos - valueStart));
    }

    if (equalsIgnoreCase(key, target)) return true;
    if (pos < header.size() && header[pos] == '"') ++pos;
    while (pos < header.size() && header[pos] != ';') ++pos;
  }
  return false;
}

bool appendChecked(std::string& output, const char c) {
  const auto byte = static_cast<unsigned char>(c);
  if (byte == 0 || byte == '\r' || byte == '\n' || output.size() >= MAX_HEADER_FILENAME_BYTES) return false;
  output.push_back(c);
  return true;
}

bool decodeQuotedFilename(const std::string_view value, std::string& output) {
  if (value.size() > MAX_HEADER_FILENAME_BYTES) return false;
  output.clear();
  output.reserve(value.size());
  bool escaped = false;
  for (const char c : value) {
    if (!escaped && c == '\\') {
      escaped = true;
      continue;
    }
    if (!appendChecked(output, c)) return false;
    escaped = false;
  }
  return !escaped && !output.empty();
}

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

bool decodeExtendedFilename(const std::string_view value, std::string& output) {
  const size_t firstQuote = value.find('\'');
  const size_t secondQuote =
      firstQuote == std::string_view::npos ? std::string_view::npos : value.find('\'', firstQuote + 1);
  if (firstQuote == std::string_view::npos || secondQuote == std::string_view::npos) return false;

  const std::string_view charset = value.substr(0, firstQuote);
  if (!equalsIgnoreCase(charset, "UTF-8") && !equalsIgnoreCase(charset, "UTF8")) return false;

  const std::string_view encoded = value.substr(secondQuote + 1);
  output.clear();
  output.reserve(std::min(encoded.size(), MAX_HEADER_FILENAME_BYTES));
  for (size_t i = 0; i < encoded.size(); ++i) {
    char decoded = encoded[i];
    if (decoded == '%') {
      if (i + 2 >= encoded.size()) return false;
      const int high = hexValue(encoded[i + 1]);
      const int low = hexValue(encoded[i + 2]);
      if (high < 0 || low < 0) return false;
      decoded = static_cast<char>((high << 4) | low);
      i += 2;
    }
    if (!appendChecked(output, decoded)) return false;
  }
  return !output.empty();
}
}  // namespace

namespace HttpHeaderUtils {

bool extractContentDispositionFilename(const std::string_view header, std::string& filename) {
  filename.clear();

  std::string_view value;
  bool quoted = false;
  if (findParameter(header, "filename*", value, quoted) && !quoted && decodeExtendedFilename(value, filename)) {
    return true;
  }

  if (!findParameter(header, "filename", value, quoted)) return false;
  return decodeQuotedFilename(value, filename);
}

}  // namespace HttpHeaderUtils
