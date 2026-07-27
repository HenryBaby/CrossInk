#include "GrimmoryStore.h"

#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <utility>

#include "OpdsServerStore.h"

namespace {
std::string trimUrl(std::string url) {
  while (!url.empty() && (url.back() == '/' || url.back() == ' ' || url.back() == '\n' || url.back() == '\r'))
    url.pop_back();
  return url;
}

std::string normalizeUrl(std::string url) {
  url = trimUrl(std::move(url));
  if (url.empty()) return {};
  if (url.rfind("https://", 0) != 0) return {};
  return url;
}
}  // namespace

void GrimmoryStore::toJson(JsonDocument& doc) const {
  doc["baseUrl"] = normalizeUrl(config_.baseUrl);
  doc["username"] = config_.username;
  doc["password_obf"] = obfuscation::obfuscateToBase64(config_.password);
  doc["downloadFolder"] = normalizeOpdsDownloadFolder(config_.downloadFolder);
}

bool GrimmoryStore::fromJson(JsonVariantConst doc) {
  GrimmoryConfig parsed;
  parsed.baseUrl = normalizeUrl(doc["baseUrl"] | "");
  parsed.username = doc["username"] | "";
  bool needsResave = false;
  parsed.password = extractPassword(doc, needsResave);
  const std::string folder = doc["downloadFolder"] | std::string("/Grimmory");
  const std::string normalizedFolder = normalizeOpdsDownloadFolder(folder);
  parsed.downloadFolder = folder.size() <= 127 && normalizedFolder.size() <= 127 ? normalizedFolder : "/Grimmory";
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
  config.baseUrl = trimUrl(std::move(config.baseUrl));
  if (!config.baseUrl.empty() && config.baseUrl.rfind("https://", 0) != 0) {
    LOG_ERR("GRM", "Rejected Grimmory configuration: HTTPS URL required");
    return false;
  }
  const std::string normalizedFolder = normalizeOpdsDownloadFolder(config.downloadFolder);
  if (config.downloadFolder.size() > 127 || normalizedFolder.size() > 127) {
    LOG_ERR("GRM", "Rejected Grimmory configuration: download folder too long");
    return false;
  }
  config.downloadFolder = normalizedFolder;
  std::swap(config_, config);
  if (!saveToFile()) {
    std::swap(config_, config);
    LOG_ERR("GRM", "Failed to persist Grimmory configuration");
    return false;
  }
  return true;
}

bool GrimmoryStore::removeConfig() {
  if (Storage.exists(getFilePath()) && !Storage.remove(getFilePath())) {
    LOG_ERR("GRM", "Failed to remove Grimmory configuration");
    return false;
  }
  config_ = GrimmoryConfig{};
  return true;
}
