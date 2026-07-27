#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Grimmory {

constexpr size_t kMaxLoginResponseBytes = 8192;
// Keep the listing small enough that the response and TLS buffers can coexist
// on the ESP32-C3 heap without triggering geometric std::string growth.
constexpr size_t kMaxPageResponseBytes = 12288;
constexpr size_t kPageSize = 5;
constexpr size_t kMaxEntries = kPageSize;
constexpr size_t kMaxTitleBytes = 96;
constexpr size_t kMaxAuthorBytes = 96;
constexpr size_t kMaxFilenameBytes = 128;

struct LoginResult {
  std::string accessToken;
  std::string refreshToken;
  int expires = 0;
};

struct BookEntry {
  int id = 0;
  std::string title;
  std::string author;
  std::string filename;
};

bool parseLoginResponse(std::string_view json, LoginResult& result);
bool parsePageResponse(std::string_view json, std::vector<BookEntry>& entries, size_t& totalElements);

// Returns a safe basename ending in .epub, or an empty string for invalid names.
std::string sanitizeEpubFilename(std::string_view serverName);

}  // namespace Grimmory
