#include "GrimmoryClient.h"

#include <ArduinoJson.h>
#include <Logging.h>

#include <cstring>

#include "Memory.h"
#include "network/GrimmoryTls.h"
#include "network/HttpDownloader.h"

namespace {
std::string joinUrl(const std::string& base, const std::string& path) {
  if (!base.empty() && base.back() == '/') return base.substr(0, base.size() - 1) + path;
  return base + path;
}
}  // namespace

bool GrimmoryClient::login(const std::string& user, const std::string& password) {
  auto doc = makeUniqueNoThrow<JsonDocument>();
  if (!doc) {
    LOG_ERR("GRM", "Failed to allocate login JSON document");
    return false;
  }
  (*doc)["username"] = user;
  (*doc)["password"] = password;
  std::string body;
  serializeJson(*doc, body);
  if (body.size() > 512) {
    LOG_ERR("GRM", "Login request exceeds JSON size limit");
    return false;
  }
  std::string response;
  if (!HttpDownloader::postJson(joinUrl(baseUrl_, "/api/v1/auth/login"), body, response,
                                Grimmory::kMaxLoginResponseBytes, GrimmoryTls::kLetsEncryptRoots,
                                HttpDownloader::Transport::WOLFSSL))
    return false;
  Grimmory::LoginResult result;
  if (!Grimmory::parseLoginResponse(response, result)) return false;
  token_ = std::move(result.accessToken);
  return true;
}

bool GrimmoryClient::listPage(size_t page, std::vector<Grimmory::BookEntry>& entries, size_t& total) {
  if (token_.empty() || page > 100000) return false;
  auto response = makeUniqueNoThrow<char[]>(Grimmory::kMaxPageResponseBytes + 1);
  if (!response) {
    LOG_ERR("GRM", "Failed to allocate book page response buffer");
    return false;
  }
  size_t responseSize = 0;
  const std::string url = joinUrl(baseUrl_, "/api/v1/app/books?fileType=EPUB&sort=addedOn&dir=asc&size=" +
                                                std::to_string(Grimmory::kPageSize) + "&page=" + std::to_string(page));
  HttpDownloader::DownloadOptions opts;
  opts.bearerToken = token_;
  opts.caCert = GrimmoryTls::kLetsEncryptRoots;
  opts.transport = HttpDownloader::Transport::WOLFSSL;
  const auto transfer = HttpDownloader::streamUrl(
      url,
      [&response, &responseSize](const uint8_t* d, size_t n) {
        if (n > Grimmory::kMaxPageResponseBytes - responseSize) return false;
        memcpy(response.get() + responseSize, d, n);
        responseSize += n;
        return true;
      },
      nullptr, "", "", opts);
  if (transfer != HttpDownloader::OK) {
    LOG_ERR("GRM", "Book page request failed (page=%zu, error=%d)", page, static_cast<int>(transfer));
    return false;
  }
  if (!Grimmory::parsePageResponse(std::string_view(response.get(), responseSize), entries, total)) {
    LOG_ERR("GRM", "Book page response could not be parsed (page=%zu, bytes=%zu)", page, responseSize);
    return false;
  }
  LOG_DBG("GRM", "Book page loaded: page=%zu bytes=%zu entries=%zu total=%zu", page, responseSize, entries.size(),
          total);
  return true;
}

HttpDownloader::DownloadError GrimmoryClient::download(int id, const std::string& path, bool* cancel,
                                                       HttpDownloader::ProgressCallback progress,
                                                       HttpDownloader::CancelCallback shouldCancel) {
  if (token_.empty() || id < 0) return HttpDownloader::HTTP_ERROR;
  HttpDownloader::DownloadOptions opts;
  opts.bearerToken = token_;
  opts.caCert = GrimmoryTls::kLetsEncryptRoots;
  opts.transport = HttpDownloader::Transport::WOLFSSL;
  opts.shouldCancel = std::move(shouldCancel);
  if (!opts.shouldCancel) opts.shouldCancel = [cancel] { return cancel && *cancel; };
  return HttpDownloader::downloadToFile(joinUrl(baseUrl_, "/api/v1/books/" + std::to_string(id) + "/download"), path,
                                        std::move(progress), cancel, "", "", opts);
}
