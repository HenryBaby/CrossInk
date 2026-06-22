#include "OpdsServerStore.h"

#include <HalStorage.h>
#include <JsonSettingsIO.h>
#include <Logging.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "util/StringUtils.h"

OpdsServerStore OpdsServerStore::instance;

namespace {
constexpr char OPDS_FILE_JSON[] = "/.crosspoint/opds.json";
constexpr char FILENAME_FORMAT_AUTHOR_TITLE[] = "author_title";
constexpr char FILENAME_FORMAT_TITLE_AUTHOR[] = "title_author";
constexpr char FILENAME_FORMAT_TITLE[] = "title";
constexpr char FILENAME_FORMAT_SERVER_FILENAME[] = "server_filename";
constexpr char FOLDER_ORGANIZATION_FLAT[] = "flat";
constexpr char FOLDER_ORGANIZATION_AUTHOR[] = "author";
constexpr size_t MAX_OPDS_DOWNLOAD_FOLDER_BYTES = 127;
}  // namespace

const char* opdsFilenameFormatToJson(const OpdsFilenameFormat format) {
  switch (format) {
    case OpdsFilenameFormat::SERVER_FILENAME:
      return FILENAME_FORMAT_SERVER_FILENAME;
    case OpdsFilenameFormat::TITLE:
      return FILENAME_FORMAT_TITLE;
    case OpdsFilenameFormat::TITLE_AUTHOR:
      return FILENAME_FORMAT_TITLE_AUTHOR;
    case OpdsFilenameFormat::AUTHOR_TITLE:
    default:
      return FILENAME_FORMAT_AUTHOR_TITLE;
  }
}

OpdsFilenameFormat opdsFilenameFormatFromJson(const char* value) {
  if (value && strcmp(value, FILENAME_FORMAT_TITLE_AUTHOR) == 0) {
    return OpdsFilenameFormat::TITLE_AUTHOR;
  }
  if (value && strcmp(value, FILENAME_FORMAT_TITLE) == 0) {
    return OpdsFilenameFormat::TITLE;
  }
  if (value && strcmp(value, FILENAME_FORMAT_SERVER_FILENAME) == 0) {
    return OpdsFilenameFormat::SERVER_FILENAME;
  }
  return OpdsFilenameFormat::AUTHOR_TITLE;
}

const char* opdsFolderOrganizationToJson(const OpdsFolderOrganization organization) {
  switch (organization) {
    case OpdsFolderOrganization::AUTHOR:
      return FOLDER_ORGANIZATION_AUTHOR;
    case OpdsFolderOrganization::FLAT:
    default:
      return FOLDER_ORGANIZATION_FLAT;
  }
}

OpdsFolderOrganization opdsFolderOrganizationFromJson(const char* value) {
  if (value && strcmp(value, FOLDER_ORGANIZATION_AUTHOR) == 0) {
    return OpdsFolderOrganization::AUTHOR;
  }
  return OpdsFolderOrganization::FLAT;
}

std::string normalizeOpdsDownloadFolder(std::string folder) {
  if (folder.empty()) return "/";

  for (char& c : folder) {
    if (c == '\\') c = '/';
  }

  std::string normalized = "/";
  size_t pos = 0;
  while (pos < folder.size()) {
    while (pos < folder.size() && folder[pos] == '/') {
      pos++;
    }
    const size_t start = pos;
    while (pos < folder.size() && folder[pos] != '/') {
      pos++;
    }
    if (start == pos) continue;

    const std::string segment = folder.substr(start, pos - start);
    if (segment == "." || segment == "..") continue;

    const size_t separatorBytes = normalized.size() > 1 ? 1 : 0;
    if (normalized.size() + separatorBytes >= MAX_OPDS_DOWNLOAD_FOLDER_BYTES) break;
    const size_t segmentBudget = MAX_OPDS_DOWNLOAD_FOLDER_BYTES - normalized.size() - separatorBytes;
    std::string cleanSegment = StringUtils::sanitizeFilename(segment, segmentBudget);
    if (cleanSegment.size() > segmentBudget) {
      cleanSegment.resize(segmentBudget);
    }
    if (cleanSegment.empty()) continue;

    if (normalized.size() > 1) normalized += "/";
    normalized += cleanSegment;
  }

  return normalized.empty() ? "/" : normalized;
}

bool OpdsServerStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return JsonSettingsIO::saveOpds(*this, OPDS_FILE_JSON);
}

bool OpdsServerStore::loadFromFile() {
  if (Storage.exists(OPDS_FILE_JSON)) {
    String json = Storage.readFile(OPDS_FILE_JSON);
    if (!json.isEmpty()) {
      // resave flag is set when passwords were stored in plaintext and need re-obfuscation
      bool resave = false;
      bool result = JsonSettingsIO::loadOpds(*this, json.c_str(), &resave);
      if (result && resave) {
        LOG_DBG("OPS", "Resaving JSON with obfuscated passwords");
        saveToFile();
      }
      return result;
    }
  }

  // No opds.json found — attempt one-time migration from the legacy single-server
  // fields in CrossPointSettings (opdsServerUrl/opdsUsername/opdsPassword).
  if (migrateFromSettings()) {
    LOG_DBG("OPS", "Migrated legacy OPDS settings");
    return true;
  }

  return false;
}

bool OpdsServerStore::migrateFromSettings() {
  if (strlen(SETTINGS.opdsServerUrl) == 0) {
    return false;
  }

  OpdsServer server;
  server.name = "OPDS Server";
  server.url = SETTINGS.opdsServerUrl;
  server.username = SETTINGS.opdsUsername;
  server.password = SETTINGS.opdsPassword;
  servers.push_back(std::move(server));

  if (saveToFile()) {
    // Clear legacy fields so migration won't run again on next boot
    SETTINGS.opdsServerUrl[0] = '\0';
    SETTINGS.opdsUsername[0] = '\0';
    SETTINGS.opdsPassword[0] = '\0';
    SETTINGS.saveToFile();
    LOG_DBG("OPS", "Migrated single-server OPDS config to opds.json");
    return true;
  }

  // Save failed — roll back in-memory state so we don't have a partial migration
  servers.clear();
  return false;
}

bool OpdsServerStore::addServer(const OpdsServer& server) {
  if (servers.size() >= MAX_SERVERS) {
    LOG_DBG("OPS", "Cannot add more servers, limit of %zu reached", MAX_SERVERS);
    return false;
  }

  servers.push_back(server);
  LOG_DBG("OPS", "Added server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::updateServer(size_t index, const OpdsServer& server) {
  if (index >= servers.size()) {
    return false;
  }

  servers[index] = server;
  LOG_DBG("OPS", "Updated server: %s", server.name.c_str());
  return saveToFile();
}

bool OpdsServerStore::removeServer(size_t index) {
  if (index >= servers.size()) {
    return false;
  }

  LOG_DBG("OPS", "Removed server: %s", servers[index].name.c_str());
  servers.erase(servers.begin() + static_cast<ptrdiff_t>(index));
  return saveToFile();
}

const OpdsServer* OpdsServerStore::getServer(size_t index) const {
  if (index >= servers.size()) {
    return nullptr;
  }
  return &servers[index];
}
