#include "GrimmoryClient.h"

#include <ArduinoJson.h>
#include <Logging.h>

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
                                Grimmory::kMaxLoginResponseBytes, GrimmoryTls::kLetsEncryptRoots))
    return false;
  Grimmory::LoginResult result;
  if (!Grimmory::parseLoginResponse(response, result)) return false;
  token_ = std::move(result.accessToken);
  return true;
}

bool GrimmoryClient::listPage(size_t page, std::vector<Grimmory::BookEntry>& entries, size_t& total) {
  if (token_.empty() || page > 100000) return false;
  std::string response;
  const std::string url = joinUrl(baseUrl_, "/api/v1/books/page?sort=addedOn,asc&page=" + std::to_string(page));
  HttpDownloader::DownloadOptions opts;
  opts.bearerToken = token_;
  opts.caCert = GrimmoryTls::kLetsEncryptRoots;
  opts.transport = HttpDownloader::Transport::ESP_HTTP;
  const auto transfer = HttpDownloader::streamUrl(
      url,
      [&response](const uint8_t* d, size_t n) {
        if (response.size() + n > Grimmory::kMaxPageResponseBytes) return false;
        response.append(reinterpret_cast<const char*>(d), n);
        return true;
      },
      nullptr, "", "", opts);
  if (transfer != HttpDownloader::OK) {
    LOG_ERR("GRM", "Book page request failed (page=%zu, error=%d)", page, static_cast<int>(transfer));
    return false;
  }
  if (!Grimmory::parsePageResponse(response, entries, total)) {
    LOG_ERR("GRM", "Book page response could not be parsed (page=%zu, bytes=%zu)", page, response.size());
    return false;
  }
  return true;
}

HttpDownloader::DownloadError GrimmoryClient::download(int id, const std::string& path, bool* cancel,
                                                       HttpDownloader::ProgressCallback progress,
                                                       HttpDownloader::CancelCallback shouldCancel) {
  if (token_.empty() || id < 0) return HttpDownloader::HTTP_ERROR;
  HttpDownloader::DownloadOptions opts;
  opts.bearerToken = token_;
  opts.caCert = GrimmoryTls::kLetsEncryptRoots;
  opts.transport = HttpDownloader::Transport::ESP_HTTP;
  opts.shouldCancel = std::move(shouldCancel);
  if (!opts.shouldCancel) opts.shouldCancel = [cancel] { return cancel && *cancel; };
  return HttpDownloader::downloadToFile(joinUrl(baseUrl_, "/api/v1/books/" + std::to_string(id) + "/download"), path,
                                        std::move(progress), cancel, "", "", opts);
}
