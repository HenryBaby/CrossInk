#include "GrimmoryParser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

namespace Grimmory {
namespace {
bool stringField(std::string_view obj, std::string_view key, size_t cap, std::string& out) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t p = obj.find(needle);
  if (p == std::string_view::npos) return false;
  size_t i = obj.find(':', p + needle.size());
  if (i == std::string_view::npos) return false;
  while (++i < obj.size() && std::isspace(static_cast<unsigned char>(obj[i]))) {
  }
  if (i >= obj.size() || obj[i] != '"') return false;
  ++i;
  std::string value;
  while (i < obj.size()) {
    char c = obj[i++];
    if (c == '"') {
      out = std::move(value);
      return out.size() <= cap;
    }
    if (c == '\\' && i < obj.size()) c = obj[i++];
    value.push_back(c);
    if (value.size() > cap) return false;
  }
  return false;
}

bool firstStringArrayValue(std::string_view obj, std::string_view key, size_t cap, std::string& out) {
  const std::string needle = "\"" + std::string(key) + "\"";
  const size_t p = obj.find(needle);
  if (p == std::string_view::npos) return false;
  size_t i = obj.find(':', p + needle.size());
  if (i == std::string_view::npos) return false;
  while (++i < obj.size() && std::isspace(static_cast<unsigned char>(obj[i]))) {
  }
  if (i >= obj.size() || obj[i] != '[') return false;
  while (++i < obj.size() && std::isspace(static_cast<unsigned char>(obj[i]))) {
  }
  if (i >= obj.size() || obj[i] != '"') return false;
  ++i;
  std::string value;
  while (i < obj.size()) {
    char c = obj[i++];
    if (c == '"') {
      out = std::move(value);
      return out.size() <= cap;
    }
    if (c == '\\' && i < obj.size()) c = obj[i++];
    value.push_back(c);
    if (value.size() > cap) return false;
  }
  return false;
}

bool intField(std::string_view obj, std::string_view key, int& out) {
  const std::string needle = "\"" + std::string(key) + "\"";
  size_t i = obj.find(needle);
  if (i == std::string_view::npos) return false;
  i = obj.find(':', i + needle.size());
  if (i == std::string_view::npos) return false;
  while (++i < obj.size() && std::isspace(static_cast<unsigned char>(obj[i]))) {
  }
  size_t end = i;
  while (end < obj.size() && std::isdigit(static_cast<unsigned char>(obj[end]))) ++end;
  if (end == i) return false;
  out = std::atoi(std::string(obj.substr(i, end - i)).c_str());
  return true;
}

bool hasEpubExtension(std::string_view value) {
  constexpr std::string_view suffix = ".epub";
  if (value.size() < suffix.size()) return false;
  const size_t start = value.size() - suffix.size();
  for (size_t i = 0; i < suffix.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(value[start + i])) != suffix[i]) return false;
  }
  return true;
}

bool objectAt(std::string_view text, size_t start, size_t& end) {
  if (start >= text.size() || text[start] != '{') return false;
  int depth = 0;
  bool quote = false;
  bool escaped = false;
  for (size_t i = start; i < text.size(); ++i) {
    const char c = text[i];
    if (quote) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quote = false;
      continue;
    }
    if (c == '"')
      quote = true;
    else if (c == '{')
      ++depth;
    else if (c == '}' && --depth == 0) {
      end = i + 1;
      return true;
    }
  }
  return false;
}
}  // namespace

bool parseLoginResponse(const std::string_view json, LoginResult& result) {
  if (json.empty() || json.size() > kMaxLoginResponseBytes) return false;
  LoginResult parsed;
  if (!stringField(json, "accessToken", 512, parsed.accessToken) || parsed.accessToken.empty()) return false;
  stringField(json, "refreshToken", 512, parsed.refreshToken);
  intField(json, "expires", parsed.expires);
  result = std::move(parsed);
  return true;
}

bool parsePageResponse(const std::string_view json, std::vector<BookEntry>& entries, size_t& totalElements) {
  if (json.empty() || json.size() > kMaxPageResponseBytes) return false;
  const size_t content = json.find("\"content\"");
  if (content == std::string_view::npos) return false;
  const size_t open = json.find('[', content);
  if (open == std::string_view::npos) return false;
  size_t i = open + 1;
  std::vector<BookEntry> parsed;
  parsed.reserve(kMaxEntries);
  while (i < json.size() && json[i] != ']') {
    while (i < json.size() && (std::isspace(static_cast<unsigned char>(json[i])) || json[i] == ',')) ++i;
    if (i >= json.size() || json[i] == ']') break;
    size_t end = 0;
    if (!objectAt(json, i, end)) return false;
    BookEntry book;
    if (intField(json.substr(i, end - i), "id", book.id)) {
      std::string metadata;
      const auto m = json.substr(i, end - i).find("\"metadata\"");
      if (m != std::string_view::npos) {
        size_t mo = json.substr(i, end - i).find('{', m);
        size_t me = 0;
        if (mo != std::string_view::npos && objectAt(json.substr(i, end - i), mo, me))
          metadata.assign(json.substr(i + mo, me - mo));
      }
      const auto obj = json.substr(i, end - i);
      if (metadata.empty() || !stringField(metadata, "title", kMaxTitleBytes, book.title)) book.title.clear();
      if (!metadata.empty() && !firstStringArrayValue(metadata, "authors", kMaxAuthorBytes, book.author))
        stringField(metadata, "author", kMaxAuthorBytes, book.author);
      const auto pf = obj.find("\"primaryFile\"");
      bool hasPrimaryFilename = false;
      if (pf != std::string_view::npos) {
        const auto colon = obj.find(':', pf);
        size_t valueStart = colon == std::string_view::npos ? obj.size() : colon + 1;
        while (valueStart < obj.size() && std::isspace(static_cast<unsigned char>(obj[valueStart]))) ++valueStart;
        size_t fe = 0;
        if (valueStart < obj.size() && obj[valueStart] == '{' && objectAt(obj, valueStart, fe)) {
          hasPrimaryFilename =
              stringField(obj.substr(valueStart, fe - valueStart), "fileName", kMaxFilenameBytes, book.filename) &&
              !book.filename.empty();
        }
      }
      // Grimmory can expose non-EPUB primary files.  Download-only v1 must
      // never offer those; when a filename is absent, derive a safe name from
      // the title instead.
      if (hasPrimaryFilename && !hasEpubExtension(book.filename)) {
        i = end;
        continue;
      }
      if (!hasPrimaryFilename && !book.title.empty()) book.filename = book.title + ".epub";
      book.filename = sanitizeEpubFilename(book.filename);
      if (!book.title.empty() && !book.filename.empty() && parsed.size() < kMaxEntries)
        parsed.push_back(std::move(book));
    }
    i = end;
  }
  totalElements = 0;
  // Current Grimmory nests pagination metadata under `page`; older servers
  // exposed totalElements at the response root, so retain that fallback.
  const auto pageKey = json.find("\"page\"");
  if (pageKey != std::string_view::npos) {
    const auto pageOpen = json.find('{', pageKey);
    size_t pageEnd = 0;
    if (pageOpen != std::string_view::npos && objectAt(json, pageOpen, pageEnd)) {
      int n = 0;
      if (intField(json.substr(pageOpen, pageEnd - pageOpen), "totalElements", n) && n >= 0)
        totalElements = static_cast<size_t>(n);
    }
  }
  if (totalElements == 0) {
    const auto total = json.find("\"totalElements\"");
    if (total != std::string_view::npos) {
      int n = 0;
      if (intField(json.substr(total), "totalElements", n) && n >= 0) totalElements = static_cast<size_t>(n);
    }
  }
  entries = std::move(parsed);
  return i < json.size() && json[i] == ']';
}

std::string sanitizeEpubFilename(std::string_view name) {
  std::string out;
  for (char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    if (std::iscntrl(static_cast<unsigned char>(c))) c = '_';
    out.push_back(c);
    if (out.size() >= kMaxFilenameBytes) break;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  if (out.empty() || out == "." || out == "..") return {};
  const auto dot = out.find_last_of('.');
  if (dot == std::string::npos || !hasEpubExtension(out)) {
    if (dot != std::string::npos) out.erase(dot);
    out += ".epub";
  }
  if (out == ".epub" || out.find("..") != std::string::npos) return {};
  return out;
}
}  // namespace Grimmory
