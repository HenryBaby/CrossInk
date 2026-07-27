#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <base64.h>
#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>
#endif
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <strings.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <utility>

#include "AppVersion.h"
#include "HttpHeaderUtils.h"
#include "network/WifiPowerSaveGuard.h"

namespace {
constexpr size_t PROGRESS_UPDATE_BYTES = 64 * 1024;
constexpr uint32_t PROGRESS_UPDATE_MS = 250;
constexpr int HTTP_RX_BUF = 4096;
constexpr int HTTP_TX_BUF = 1024;
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr int HTTP_READ_POLL_TIMEOUT_MS = 5000;
constexpr uint32_t DOWNLOAD_IDLE_TIMEOUT_MS = 30000;
constexpr size_t DEFAULT_DOWNLOAD_BUFFER_SIZE = 2048;
constexpr uint8_t MAX_REDIRECTS = 5;

void logNetworkState(const char* phase) {
  LOG_DBG("HTTP", "%s: heap free=%u maxAlloc=%u wifi=%d rssi=%d", phase, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<int>(WiFi.status()), WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
}

void logDownloadState(const char* phase, const size_t downloaded, const size_t total, const uint32_t idleMs) {
  LOG_ERR("HTTP", "%s after %zu/%zu bytes (idle=%lu ms, timeout=%lu ms)", phase, downloaded, total,
          static_cast<unsigned long>(idleMs), static_cast<unsigned long>(DOWNLOAD_IDLE_TIMEOUT_MS));
  logNetworkState(phase);
}

bool isRedirect(const int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

struct ResponseHeaders {
  std::string redirectLocation;
  std::string* responseFilename = nullptr;
};

esp_err_t captureResponseHeaders(esp_http_client_event_t* evt) {
  auto* headers = static_cast<ResponseHeaders*>(evt->user_data);
  if (evt->event_id != HTTP_EVENT_ON_HEADER || headers == nullptr || evt->header_key == nullptr ||
      evt->header_value == nullptr) {
    return ESP_OK;
  }

  if (strcasecmp(evt->header_key, "Location") == 0) {
    headers->redirectLocation.assign(evt->header_value);
  } else if (headers->responseFilename != nullptr && strcasecmp(evt->header_key, "Content-Disposition") == 0) {
    if (!HttpHeaderUtils::extractContentDispositionFilename(evt->header_value, *headers->responseFilename)) {
      headers->responseFilename->clear();
    }
  }
  return ESP_OK;
}

struct ParsedUrl {
  bool https = false;
  std::string host;
  std::string path;
  uint16_t port = 80;
};

bool parseUrl(const std::string& url, ParsedUrl& out) {
  const size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return false;

  const std::string scheme = url.substr(0, schemeEnd);
  out.https = scheme == "https";
  if (!out.https && scheme != "http") return false;

  const size_t hostStart = schemeEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  const std::string hostPort =
      url.substr(hostStart, pathStart == std::string::npos ? std::string::npos : pathStart - hostStart);
  out.path = pathStart == std::string::npos ? "/" : url.substr(pathStart);
  out.port = out.https ? 443 : 80;
  out.host.clear();

  size_t portSep = std::string::npos;
  if (!hostPort.empty() && hostPort.front() == '[') {
    const size_t close = hostPort.find(']');
    if (close == std::string::npos || close == 1) return false;
    out.host = hostPort.substr(1, close - 1);
    if (close + 1 < hostPort.size()) {
      if (hostPort[close + 1] != ':') return false;
      portSep = close + 1;
    }
  } else {
    if (hostPort.find(':') != hostPort.rfind(':')) return false;
    portSep = hostPort.rfind(':');
  }
  if (portSep != std::string::npos) {
    if (out.host.empty()) out.host = hostPort.substr(0, portSep);
    const std::string portText = hostPort.substr(portSep + 1);
    if (portText.empty()) return false;
    uint32_t parsedPort = 0;
    for (const char c : portText) {
      if (c < '0' || c > '9') return false;
      parsedPort = parsedPort * 10 + static_cast<uint32_t>(c - '0');
      if (parsedPort > UINT16_MAX) return false;
    }
    if (parsedPort == 0) return false;
    out.port = static_cast<uint16_t>(parsedPort);
  } else {
    if (out.host.empty()) out.host = hostPort;
  }

  return !out.host.empty() && !out.path.empty();
}

struct DateHeader {
  char value[30] = {};
};

esp_err_t captureDateHeader(esp_http_client_event_t* evt) {
  auto* date = static_cast<DateHeader*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_HEADER && date != nullptr && evt->header_key != nullptr &&
      evt->header_value != nullptr && strcasecmp(evt->header_key, "Date") == 0) {
    const size_t length = std::strlen(evt->header_value);
    if (length == sizeof(date->value) - 1) std::memcpy(date->value, evt->header_value, sizeof(date->value));
  }
  return ESP_OK;
}

bool parseUnsigned(const char* text, size_t len, int& value) {
  if (len == 0 || len > 4) return false;
  int result = 0;
  for (size_t i = 0; i < len; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(text[i]))) return false;
    result = result * 10 + text[i] - '0';
  }
  value = result;
  return true;
}

int monthNumber(const char* month) {
  static constexpr const char* names[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int i = 0; i < 12; ++i)
    if (std::strncmp(month, names[i], 3) == 0) return i + 1;
  return 0;
}

std::time_t daysFromCivil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<std::time_t>(era * 146097 + static_cast<int>(doe) - 719468) * 86400;
}

bool sameOrigin(const ParsedUrl& a, const ParsedUrl& b) {
  return a.https == b.https && a.port == b.port && strcasecmp(a.host.c_str(), b.host.c_str()) == 0;
}

const char* schemeName(const ParsedUrl& url) { return url.https ? "https" : "http"; }

std::string buildRedirectUrl(const std::string& baseUrl, const std::string& location) {
  if (location.starts_with("http://") || location.starts_with("https://")) return location;

  ParsedUrl base;
  if (!parseUrl(baseUrl, base)) return location;

  std::string origin = base.https ? "https://" : "http://";
  origin += base.host;
  if ((base.https && base.port != 443) || (!base.https && base.port != 80)) {
    origin += ":";
    origin += std::to_string(base.port);
  }

  if (!location.empty() && location[0] == '/') return origin + location;

  const size_t lastSlash = base.path.rfind('/');
  const std::string parent = lastSlash == std::string::npos ? "/" : base.path.substr(0, lastSlash + 1);
  return origin + parent + location;
}

bool isCancelRequested(bool* cancelFlag, const HttpDownloader::CancelCallback& shouldCancel) {
  if (cancelFlag && *cancelFlag) return true;
  if (shouldCancel && shouldCancel()) {
    if (cancelFlag) *cancelFlag = true;
    return true;
  }
  return false;
}

class ProgressNotifier {
 public:
  explicit ProgressNotifier(const HttpDownloader::ProgressCallback& progress) : progress_(&progress) {}

  void setTotal(const size_t total) { total_ = total; }

  void notify(size_t downloaded, bool force) {
    if (!progress_ || !*progress_ || total_ == 0) return;

    const uint32_t now = millis();
    if (force || downloaded == total_ || downloaded - lastProgressBytes_ >= PROGRESS_UPDATE_BYTES ||
        now - lastProgressMs_ >= PROGRESS_UPDATE_MS) {
      lastProgressBytes_ = downloaded;
      lastProgressMs_ = now;
      (*progress_)(downloaded, total_);
    }
  }

 private:
  size_t total_ = 0;
  size_t lastProgressBytes_ = 0;
  uint32_t lastProgressMs_ = 0;
  const HttpDownloader::ProgressCallback* progress_ = nullptr;
};

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  HttpDownloader::CancelCallback shouldCancel;
  size_t resumeOffset = 0;
  size_t downloaded = 0;
  size_t total = 0;
  bool rangeIgnored = false;
  std::string* responseFilename = nullptr;
};

void setRequestHeaders(esp_http_client_handle_t client, const std::string& username, const std::string& password,
                       const std::string& bearerToken, size_t resumeOffset, bool sendAuthorization) {
  esp_http_client_set_header(client, "User-Agent", "CrossInk-ESP32-" CROSSINK_VERSION);
  esp_http_client_set_header(client, "Connection", "close");
  if (resumeOffset > 0) {
    char rangeHeader[40];
    snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%zu-", resumeOffset);
    esp_http_client_set_header(client, "Range", rangeHeader);
    LOG_DBG("HTTP", "Resuming download at byte %zu", resumeOffset);
  }
  if (!bearerToken.empty() && sendAuthorization) {
    const String header = String("Bearer ") + bearerToken.c_str();
    esp_http_client_set_header(client, "Authorization", header.c_str());
  } else if (sendAuthorization) {
    const std::string credentials = username + ":" + password;
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }
}

void logTlsError(esp_http_client_handle_t client, const char* phase) {
  int tlsError = 0;
  int tlsFlags = 0;
  const esp_err_t err = esp_http_client_get_and_clear_last_tls_error(client, &tlsError, &tlsFlags);
  if (err != ESP_OK || tlsError != 0 || tlsFlags != 0) {
    const int tlsCode = tlsError < 0 ? -tlsError : tlsError;
    LOG_ERR("HTTP", "%s TLS error: err=%s mbedtls=0x%x flags=0x%x", phase, esp_err_to_name(err), tlsCode, tlsFlags);
  }
}

#if defined(FREEINK_NET_WOLFSSL)
HttpDownloader::DownloadError runGetWolfSsl(const std::string& url, const std::string& username,
                                            const std::string& password, const std::string& bearerToken, Sink& sink,
                                            const size_t bufferSize, const char* caCert) {
  (void)bufferSize;  // SecureHttpClient owns one fixed 1024-byte streaming buffer.
  std::string currentUrl = url;

  ParsedUrl credentialOrigin;
  const bool hasCredentials =
      ((!username.empty() && !password.empty()) || !bearerToken.empty()) && parseUrl(url, credentialOrigin);
  ProgressNotifier progressNotifier(sink.progress);

  for (uint8_t hop = 0; hop < MAX_REDIRECTS; ++hop) {
    ParsedUrl currentOrigin;
    const bool currentParsed = parseUrl(currentUrl, currentOrigin);
    const bool sendAuthorization = hasCredentials && currentParsed && sameOrigin(currentOrigin, credentialOrigin);

    freeink::SecureHttpClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    const bool credentialed = !username.empty() || !password.empty() || !bearerToken.empty();
    if (currentParsed && currentOrigin.https && caCert != nullptr)
      http.setCACert(caCert);
    else if (currentParsed && currentOrigin.https && credentialed) {
      LOG_ERR("HTTP", "Refusing credentialed HTTPS request without CA certificate");
      return HttpDownloader::HTTP_ERROR;
    } else if (currentParsed && currentOrigin.https) {
      LOG_DBG("HTTP", "Using insecure HTTPS for uncredentialed request");
      http.setInsecure();
    }
    if (!http.begin(currentUrl)) {
      LOG_ERR("HTTP", "wolfSSL rejected URL: %s", currentUrl.c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    // Replace SecureHttpClient's built-in User-Agent so strict servers receive
    // exactly one header while retaining CrossInk's device/version identity.
    http.setUserAgent("CrossInk-ESP32-" CROSSINK_VERSION);
    if (sink.resumeOffset > 0) {
      char rangeHeader[40];
      snprintf(rangeHeader, sizeof(rangeHeader), "bytes=%zu-", sink.resumeOffset);
      http.addHeader("Range", rangeHeader);
      LOG_DBG("HTTP", "Resuming download at byte %zu", sink.resumeOffset);
    }
    if (sendAuthorization && !bearerToken.empty()) {
      http.addHeader("Authorization", std::string("Bearer ") + bearerToken);
    } else if (sendAuthorization) {
      const std::string credentials = username + ":" + password;
      const String encoded = base64::encode(credentials.c_str());
      http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
    }

    LOG_DBG("HTTP", "wolfSSL GET: %s", currentUrl.c_str());
    const int status = http.GET(
        [&http, &sink, &progressNotifier](const uint8_t* data, const size_t len) {
          const int responseStatus = http.getStatus();
          const bool isResumeResponse = sink.resumeOffset > 0 && responseStatus == 206;
          if (responseStatus != 200 && !isResumeResponse) return true;
          if (sink.resumeOffset > 0 && !isResumeResponse) {
            sink.rangeIgnored = true;
            return false;
          }

          if (sink.downloaded < sink.resumeOffset) sink.downloaded = sink.resumeOffset;
          if (sink.total == 0 && http.hasContentLength()) {
            sink.total = sink.resumeOffset + http.getContentLength();
            progressNotifier.setTotal(sink.total);
            LOG_DBG("HTTP", "Content-Length: %zu", sink.total);
          }
          if (!sink.write(data, len)) return false;
          sink.downloaded += len;
          progressNotifier.notify(sink.downloaded, false);
          return true;
        },
        [&sink]() { return isCancelRequested(sink.cancelFlag, sink.shouldCancel); });

    if (http.aborted()) return HttpDownloader::ABORTED;
    if (sink.rangeIgnored) {
      LOG_DBG("HTTP", "Server ignored range request; restarting download");
      sink.resumeOffset = 0;
      return HttpDownloader::HTTP_ERROR;
    }
    if (status < 0) {
      LOG_ERR("HTTP", "wolfSSL request failed: %s", currentUrl.c_str());
      logNetworkState("wolfSSL request failure");
      return HttpDownloader::HTTP_ERROR;
    }

    if (isRedirect(status)) {
      const std::string location = http.getHeader("location");
      if (location.empty()) {
        LOG_ERR("HTTP", "Redirect missing Location header");
        return HttpDownloader::HTTP_ERROR;
      }

      const std::string redirectUrl = buildRedirectUrl(currentUrl, location);
      ParsedUrl redirect;
      if (!parseUrl(redirectUrl, redirect)) {
        LOG_ERR("HTTP", "Rejected redirect with unsupported Location");
        return HttpDownloader::HTTP_ERROR;
      }
      if (currentParsed && currentOrigin.https && !redirect.https) {
        LOG_ERR("HTTP", "Rejected HTTPS downgrade redirect to %s", redirect.host.c_str());
        return HttpDownloader::HTTP_ERROR;
      }
      if (!bearerToken.empty() ? false : (hasCredentials && !sameOrigin(redirect, credentialOrigin))) {
        LOG_ERR("HTTP", "Rejected credentialed redirect to different origin: %s://%s:%u", schemeName(redirect),
                redirect.host.c_str(), redirect.port);
        return HttpDownloader::HTTP_ERROR;
      }
      currentUrl = redirectUrl;
      LOG_DBG("HTTP", "Redirecting to: %s", redirect.host.c_str());
      continue;
    }

    const bool isResumeResponse = sink.resumeOffset > 0 && status == 206;
    if (status != 200 && !isResumeResponse) {
      LOG_ERR("HTTP", "Unexpected status: %d", status);
      return HttpDownloader::HTTP_ERROR;
    }
    if (http.callbackAborted()) {
      LOG_ERR("HTTP", "Write failed after %zu/%zu bytes", sink.downloaded, sink.total);
      return HttpDownloader::FILE_ERROR;
    }
    if (!http.responseComplete()) {
      LOG_ERR("HTTP", "Incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      return HttpDownloader::HTTP_ERROR;
    }

    if (sink.total == 0 && http.hasContentLength()) {
      sink.total = sink.resumeOffset + http.getContentLength();
      progressNotifier.setTotal(sink.total);
    }
    if (sink.responseFilename != nullptr) {
      const std::string contentDisposition = http.getHeader("content-disposition");
      if (!HttpHeaderUtils::extractContentDispositionFilename(contentDisposition, *sink.responseFilename)) {
        sink.responseFilename->clear();
      }
    }
    progressNotifier.notify(sink.downloaded, true);
    return HttpDownloader::OK;
  }

  LOG_ERR("HTTP", "Redirect limit exceeded");
  return HttpDownloader::HTTP_ERROR;
}
#endif

HttpDownloader::DownloadError runGetDefault(const std::string& url, const std::string& username,
                                            const std::string& password, const std::string& bearerToken, Sink& sink,
                                            const size_t bufferSize, const char* caCert) {
  std::string currentUrl = url;

  ParsedUrl credentialOrigin;
  const bool hasCredentials =
      ((!username.empty() && !password.empty()) || !bearerToken.empty()) && parseUrl(url, credentialOrigin);

  for (uint8_t hop = 0; hop < MAX_REDIRECTS; ++hop) {
    ParsedUrl currentOrigin;
    const bool currentParsed = parseUrl(currentUrl, currentOrigin);
    const bool sendAuthorization = hasCredentials && currentParsed && sameOrigin(currentOrigin, credentialOrigin);
    if (sink.responseFilename != nullptr) sink.responseFilename->clear();
    ResponseHeaders responseHeaders;
    responseHeaders.responseFilename = sink.responseFilename;

    esp_http_client_config_t config = {};
    config.url = currentUrl.c_str();
    config.buffer_size = HTTP_RX_BUF;
    config.buffer_size_tx = HTTP_TX_BUF;
    config.timeout_ms = HTTP_TIMEOUT_MS;
    if (caCert)
      config.cert_pem = caCert;
    else
      config.crt_bundle_attach = esp_crt_bundle_attach;
    config.keep_alive_enable = false;
    config.event_handler = captureResponseHeaders;
    config.user_data = &responseHeaders;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
      LOG_ERR("HTTP", "Client init failed");
      logNetworkState("Client init failure");
      return HttpDownloader::HTTP_ERROR;
    }

    setRequestHeaders(client, username, password, bearerToken, sink.resumeOffset, sendAuthorization);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "Open failed: %s", esp_err_to_name(err));
      logTlsError(client, "Open failure");
      logNetworkState("Open failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    int64_t responseLength = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (responseLength < 0) {
      LOG_ERR("HTTP", "Fetch headers failed: %lld", static_cast<long long>(responseLength));
      logNetworkState("Fetch headers failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    if (isRedirect(status)) {
      if (responseHeaders.redirectLocation.empty()) {
        LOG_ERR("HTTP", "Redirect missing Location header");
        logNetworkState("Redirect missing Location");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }

      const std::string redirectUrl = buildRedirectUrl(currentUrl, responseHeaders.redirectLocation);
      ParsedUrl redirect;
      if (!parseUrl(redirectUrl, redirect)) {
        LOG_ERR("HTTP", "Rejected redirect with unsupported Location");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (currentParsed && currentOrigin.https && !redirect.https) {
        LOG_ERR("HTTP", "Rejected HTTPS downgrade redirect to %s", redirect.host.c_str());
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (!bearerToken.empty() ? false : (hasCredentials && !sameOrigin(redirect, credentialOrigin))) {
        LOG_ERR("HTTP", "Rejected credentialed redirect to different origin: %s://%s:%u", schemeName(redirect),
                redirect.host.c_str(), redirect.port);
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      currentUrl = redirectUrl;
      LOG_DBG("HTTP", "Redirecting to: %s", redirect.host.c_str());
      esp_http_client_cleanup(client);
      continue;
    }

    const bool isResumeResponse = sink.resumeOffset > 0 && status == 206;
    if (status != 200 && !isResumeResponse) {
      LOG_ERR("HTTP", "Unexpected status: %d", status);
      logNetworkState("Unexpected status");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (sink.resumeOffset > 0 && !isResumeResponse) {
      LOG_DBG("HTTP", "Server ignored range request; restarting download");
      sink.rangeIgnored = true;
      sink.resumeOffset = 0;
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    const size_t bodyLength = responseLength > 0 ? static_cast<size_t>(responseLength) : 0;
    sink.total = bodyLength > 0 ? sink.resumeOffset + bodyLength : 0;
    sink.downloaded = sink.resumeOffset;
    if (sink.total > 0) {
      LOG_DBG("HTTP", "Content-Length: %zu", sink.total);
    } else {
      LOG_DBG("HTTP", "Content-Length: unknown");
    }
#ifdef ESP_ERR_HTTP_EAGAIN
    err = esp_http_client_set_timeout_ms(client, HTTP_READ_POLL_TIMEOUT_MS);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "Failed to set read timeout: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
#endif

    auto buffer = makeUniqueNoThrow<char[]>(bufferSize);
    if (!buffer) {
      LOG_ERR("HTTP", "Failed to allocate %zu byte download buffer", bufferSize);
      logNetworkState("Download buffer allocation failure");
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }

    ProgressNotifier progressNotifier(sink.progress);
    progressNotifier.setTotal(sink.total);
    LOG_DBG("HTTP", "Reading body: buffer=%zu bytes", bufferSize);
#ifdef ESP_ERR_HTTP_EAGAIN
    uint32_t lastReadMs = millis();
#endif
    while (true) {
      if (isCancelRequested(sink.cancelFlag, sink.shouldCancel)) {
        esp_http_client_cleanup(client);
        return HttpDownloader::ABORTED;
      }

      const int bytesRead = esp_http_client_read(client, buffer.get(), bufferSize);
      if (bytesRead < 0) {
#ifdef ESP_ERR_HTTP_EAGAIN
        if (bytesRead == -ESP_ERR_HTTP_EAGAIN) {
          const uint32_t idleMs = millis() - lastReadMs;
          if (idleMs >= DOWNLOAD_IDLE_TIMEOUT_MS) {
            logDownloadState("Read timed out", sink.downloaded, sink.total, idleMs);
            esp_http_client_cleanup(client);
            return HttpDownloader::HTTP_ERROR;
          }
          delay(1);
          continue;
        }
#endif
        LOG_ERR("HTTP", "Read error after %zu/%zu bytes", sink.downloaded, sink.total);
        logNetworkState("Read error");
        esp_http_client_cleanup(client);
        return HttpDownloader::HTTP_ERROR;
      }
      if (bytesRead == 0) break;

      if (!sink.write(reinterpret_cast<const uint8_t*>(buffer.get()), static_cast<size_t>(bytesRead))) {
        LOG_ERR("HTTP", "Write failed after %zu/%zu bytes", sink.downloaded, sink.total);
        logNetworkState("Write failure");
        esp_http_client_cleanup(client);
        return HttpDownloader::FILE_ERROR;
      }

      sink.downloaded += static_cast<size_t>(bytesRead);
#ifdef ESP_ERR_HTTP_EAGAIN
      lastReadMs = millis();
#endif
      if (sink.total > 0 && sink.total <= PROGRESS_UPDATE_BYTES) {
        LOG_DBG("HTTP", "Read progress: %zu/%zu bytes", sink.downloaded, sink.total);
      }
      progressNotifier.notify(sink.downloaded, false);
      if (sink.total > 0 && sink.downloaded >= sink.total) break;
      delay(0);
    }

    const bool complete = esp_http_client_is_complete_data_received(client);
    esp_http_client_cleanup(client);
    progressNotifier.notify(sink.downloaded, true);
    if (!complete) {
      LOG_ERR("HTTP", "Incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      logNetworkState("Incomplete transfer");
      return HttpDownloader::HTTP_ERROR;
    }

    return HttpDownloader::OK;
  }

  LOG_ERR("HTTP", "Redirect limit exceeded");
  logNetworkState("Redirect limit exceeded");
  return HttpDownloader::HTTP_ERROR;
}

HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     const std::string& bearerToken, Sink& sink, const size_t bufferSize,
                                     const HttpDownloader::Transport transport, const char* caCert) {
#if defined(FREEINK_NET_WOLFSSL)
  if (transport == HttpDownloader::Transport::WOLFSSL) {
    return runGetWolfSsl(url, username, password, bearerToken, sink, bufferSize, caCert);
  }
#else
  (void)transport;
#endif
  return runGetDefault(url, username, password, bearerToken, sink, bufferSize, caCert);
}
}  // namespace

bool HttpDownloader::parseHttpDate(const std::string& value, std::time_t& outTime) {
  if (value.size() != 29 || value[3] != ',' || value[4] != ' ' || value[7] != ' ' || value[11] != ' ' ||
      value[16] != ' ' || value[19] != ':' || value[22] != ':' || value[25] != ' ' || value.substr(26) != "GMT")
    return false;
  static constexpr const char* weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  bool validWeekday = false;
  for (const char* weekday : weekdays) {
    if (value.compare(0, 3, weekday) == 0) {
      validWeekday = true;
      break;
    }
  }
  if (!validWeekday) return false;
  int day = 0, year = 0, hour = 0, minute = 0, second = 0;
  if (!parseUnsigned(value.c_str() + 5, 2, day) || !parseUnsigned(value.c_str() + 12, 4, year) ||
      !parseUnsigned(value.c_str() + 17, 2, hour) || !parseUnsigned(value.c_str() + 20, 2, minute) ||
      !parseUnsigned(value.c_str() + 23, 2, second) || monthNumber(value.c_str() + 8) == 0 || day < 1 || day > 31 ||
      hour > 23 || minute > 59 || second > 59 || year < 1970)
    return false;
  const int month = monthNumber(value.c_str() + 8);
  static constexpr int daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
  const int maxDay = month == 2 && leapYear ? 29 : daysPerMonth[month - 1];
  if (day > maxDay) return false;
  outTime = daysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) + hour * 3600 + minute * 60 +
            second;
  return outTime > 0;
}

bool HttpDownloader::fetchHttpDate(const std::string& url, std::time_t& outTime) {
  ParsedUrl parsed;
  if (!parseUrl(url, parsed)) return false;
  std::string httpUrl = "http://";
  if (parsed.host.find(':') != std::string::npos)
    httpUrl += "[" + parsed.host + "]";
  else
    httpUrl += parsed.host;
  httpUrl += "/";

  DateHeader date;
  esp_http_client_config_t config = {};
  config.url = httpUrl.c_str();
  config.buffer_size = 512;
  config.buffer_size_tx = 256;
  config.timeout_ms = 5000;
  config.keep_alive_enable = false;
  config.disable_auto_redirect = true;
  config.event_handler = captureDateHeader;
  config.user_data = &date;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return false;
  const esp_err_t openResult = esp_http_client_open(client, 0);
  if (openResult != ESP_OK) {
    esp_http_client_cleanup(client);
    return false;
  }
  const int64_t headers = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);
  if (headers < 0 || status < 100 || status >= 600 || date.value[0] == '\0') return false;
  return parseHttpDate(date.value, outTime);
}

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  return fetchUrl(
      url, [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; }, username,
      password);
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  outContent.clear();
  return fetchUrl(
      url,
      [&outContent](const uint8_t* data, size_t len) {
        outContent.append(reinterpret_cast<const char*>(data), len);
        return true;
      },
      username, password);
}

bool HttpDownloader::postJson(const std::string& url, const std::string& json, std::string& outContent, size_t maxBytes,
                              const char* caCert, Transport transport) {
  outContent.clear();
  if (json.size() > maxBytes) return false;
#if defined(FREEINK_NET_WOLFSSL)
  if (transport == Transport::WOLFSSL) {
    ParsedUrl parsed;
    if (!parseUrl(url, parsed)) {
      LOG_ERR("HTTP", "wolfSSL POST rejected URL");
      return false;
    }
    if (parsed.https && caCert == nullptr) {
      LOG_ERR("HTTP", "Refusing credentialed wolfSSL POST without CA certificate");
      return false;
    }
    freeink::SecureHttpClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (parsed.https) http.setCACert(caCert);
    if (!http.begin(url)) {
      LOG_ERR("HTTP", "wolfSSL rejected POST URL: %s", url.c_str());
      return false;
    }
    http.setUserAgent("CrossInk-ESP32-" CROSSINK_VERSION);
    http.addHeader("Content-Type", "application/json");
    const int status = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(json.data()), json.size(),
                                        [&outContent, maxBytes](const uint8_t* data, size_t len) {
                                          if (outContent.size() + len > maxBytes) return false;
                                          outContent.append(reinterpret_cast<const char*>(data), len);
                                          return true;
                                        });
    const bool complete = http.responseComplete();
    const bool callbackAborted = http.callbackAborted();
    http.end();
    if (status < 200 || status >= 300 || callbackAborted || !complete) {
      LOG_ERR("HTTP", "wolfSSL POST failed: status=%d complete=%d bytes=%zu", status, complete, outContent.size());
      return false;
    }
    return true;
  }
#else
  (void)transport;
#endif
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  if (caCert)
    config.cert_pem = caCert;
  else
    config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  auto* client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "POST client init failed");
    return false;
  }
  esp_http_client_set_header(client, "Content-Type", "application/json");
  if (esp_http_client_open(client, json.size()) != ESP_OK) {
    LOG_ERR("HTTP", "POST open failed");
    esp_http_client_cleanup(client);
    return false;
  }
  size_t written = 0;
  while (written < json.size()) {
    const int n = esp_http_client_write(client, json.data() + written, json.size() - written);
    if (n <= 0) {
      LOG_ERR("HTTP", "POST short write");
      esp_http_client_cleanup(client);
      return false;
    }
    written += static_cast<size_t>(n);
  }
  const int64_t length = esp_http_client_fetch_headers(client);
  const int status = esp_http_client_get_status_code(client);
  if (length < 0 || status < 200 || status >= 300) {
    esp_http_client_cleanup(client);
    return false;
  }
  auto buffer = makeUniqueNoThrow<uint8_t[]>(1024);
  if (!buffer) {
    LOG_ERR("HTTP", "POST response buffer allocation failed");
    esp_http_client_cleanup(client);
    return false;
  }
  int read = 0;
  while ((read = esp_http_client_read(client, reinterpret_cast<char*>(buffer.get()), 1024)) > 0) {
    if (outContent.size() + static_cast<size_t>(read) > maxBytes) {
      esp_http_client_cleanup(client);
      return false;
    }
    outContent.append(reinterpret_cast<char*>(buffer.get()), static_cast<size_t>(read));
  }
  esp_http_client_cleanup(client);
  return read == 0;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  return streamUrl(url, onData, nullptr, username, password) == OK;
}

HttpDownloader::DownloadError HttpDownloader::streamUrl(const std::string& url, const DataCallback& onData,
                                                        ProgressCallback progress, const std::string& username,
                                                        const std::string& password, DownloadOptions options) {
  WifiPowerSaveGuard wifiPowerSaveGuard;
  (void)wifiPowerSaveGuard;

  LOG_DBG("HTTP", "Fetching: %s", url.c_str());

  if (!onData) {
    LOG_ERR("HTTP", "Fetch failed: missing data callback");
    return HTTP_ERROR;
  }

  Sink sink;
  sink.write = onData;
  sink.progress = std::move(progress);
  sink.shouldCancel = std::move(options.shouldCancel);
  const std::string bearerToken = options.bearerToken;
  const size_t bufferSize = options.bufferSize > 0 ? options.bufferSize : DEFAULT_DOWNLOAD_BUFFER_SIZE;
  return runGet(url, username, password, bearerToken, sink, bufferSize, options.transport, options.caCert);
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             DownloadOptions options) {
  WifiPowerSaveGuard wifiPowerSaveGuard;
  (void)wifiPowerSaveGuard;

  const size_t bufferSize = options.bufferSize > 0 ? options.bufferSize : DEFAULT_DOWNLOAD_BUFFER_SIZE;
  size_t resumeOffset = 0;
  if (options.resumePartial && Storage.exists(destPath.c_str())) {
    FsFile existingFile;
    if (Storage.openFileForRead("HTTP", destPath.c_str(), existingFile)) {
      resumeOffset = existingFile.fileSize();
      existingFile.close();
    }
  }

  LOG_DBG("HTTP", "Downloading: %s", url.c_str());
  LOG_DBG("HTTP", "Destination: %s", destPath.c_str());
  LOG_DBG("HTTP", "Timeout: %d ms buffer=%zu bytes", HTTP_TIMEOUT_MS, bufferSize);

  if (resumeOffset == 0 && Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.shouldCancel = std::move(options.shouldCancel);
  sink.resumeOffset = resumeOffset;
  sink.responseFilename = options.responseFilename;
  const std::string bearerToken = options.bearerToken;

  FsFile file;
  bool fileOpen = false;
  auto openOutputFile = [&]() {
    if (fileOpen) return true;
    if (sink.resumeOffset > 0) {
      file = Storage.open(destPath.c_str(), O_WRONLY | O_APPEND);
    } else {
      fileOpen = Storage.openFileForWrite("HTTP", destPath.c_str(), file);
      if (!fileOpen) {
        LOG_ERR("HTTP", "Failed to open file for writing");
        return false;
      }
    }
    fileOpen = file;
    if (!fileOpen) {
      LOG_ERR("HTTP", "Failed to open file for writing");
    }
    return fileOpen;
  };

  sink.write = [&](const uint8_t* data, size_t len) { return openOutputFile() && file.write(data, len) == len; };

  DownloadError result =
      runGet(url, username, password, bearerToken, sink, bufferSize, options.transport, options.caCert);
  if (sink.rangeIgnored) {
    if (fileOpen) {
      file.close();
      fileOpen = false;
    }
    Storage.remove(destPath.c_str());
    sink.rangeIgnored = false;
    sink.resumeOffset = 0;
    sink.downloaded = 0;
    sink.total = 0;
    sink.write = [&](const uint8_t* data, size_t len) { return openOutputFile() && file.write(data, len) == len; };
    result = runGet(url, username, password, bearerToken, sink, bufferSize, options.transport, options.caCert);
  }

  if (fileOpen) {
    file.flush();
    file.close();
  }

  if (result != OK) {
    LOG_ERR("HTTP", "Transfer failed: error=%d downloaded=%zu expected=%zu preservePartial=%d resumePartial=%d",
            static_cast<int>(result), sink.downloaded, sink.total, options.preservePartial, options.resumePartial);
    if (result == ABORTED || !options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return result;
  }

  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "Download failed: no data received");
    if (!options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return HTTP_ERROR;
  }

  if (sink.total > 0 && sink.downloaded != sink.total) {
    LOG_ERR("HTTP", "Size mismatch: got %zu, expected %zu", sink.downloaded, sink.total);
    if (!options.preservePartial) {
      Storage.remove(destPath.c_str());
    }
    return HTTP_ERROR;
  }

  LOG_DBG("HTTP", "Downloaded %zu bytes", sink.downloaded);
  return OK;
}
