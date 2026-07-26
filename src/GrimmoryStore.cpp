#include "GrimmoryStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <utility>

namespace {
std::string normalizeUrl(std::string url) {
  while (!url.empty() && (url.back() == '/' || url.back() == ' ' || url.back() == '\n' || url.back() == '\r'))
    url.pop_back();
  if (url.rfind("https://", 0) != 0) return {};
  return url;
}
}  // namespace

void GrimmoryStore::toJson(JsonDocument& doc) const {
  doc["baseUrl"] = normalizeUrl(config_.baseUrl);
  doc["username"] = config_.username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(config_.password);
  doc["downloadFolder"] = "/Grimmory";
}

bool GrimmoryStore::fromJson(JsonVariantConst doc) {
  GrimmoryConfig parsed;
  parsed.baseUrl = normalizeUrl(doc["baseUrl"] | "");
  parsed.username = doc["username"] | "";
  bool needsResave = false;
  parsed.password = extractPassword(doc, needsResave);
  parsed.downloadFolder = "/Grimmory";
  config_ = std::move(parsed);
  if (needsResave) saveToFile();
  return true;
}

bool GrimmoryStore::loadFromFile() {
  if (!PersistableStore<GrimmoryStore>::loadFromFile()) {
    LOG_DBG("GRM", "No Grimmory configuration found");
    return false;
  }
  return true;
}

bool GrimmoryStore::setConfig(GrimmoryConfig config) {
  config.baseUrl = normalizeUrl(std::move(config.baseUrl));
  if (config.baseUrl.empty()) {
    LOG_ERR("GRM", "Rejected Grimmory configuration: HTTPS URL required");
    return false;
  }
  config.downloadFolder = "/Grimmory";
  config_ = std::move(config);
  if (!saveToFile()) {
    LOG_ERR("GRM", "Failed to persist Grimmory configuration");
    return false;
  }
  return true;
}
