#include "BookMoveUtils.h"

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>

#include "BookmarkStore.h"
#include "ClippingStore.h"
#include "CrossPointState.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "util/StringUtils.h"

namespace {
constexpr char READ_FOLDER[] = "/Read";
constexpr size_t READ_FOLDER_MAX_BYTES = 127;

bool isPathUnderFolder(const std::string& path, const std::string& folder) {
  if (folder == "/") return !path.empty() && path[0] == '/';
  if (path.rfind(folder, 0) != 0) return false;
  return path.size() == folder.size() || path[folder.size()] == '/';
}

std::string firstChildFolderUnder(const std::string& path, const std::string& folder) {
  const size_t childStart = folder == "/" ? 1 : folder.size() + 1;
  if (path.size() <= childStart) return "";
  const size_t childEnd = path.find('/', childStart);
  if (childEnd == std::string::npos) return "";
  return path.substr(childStart, childEnd - childStart);
}

std::string readFolderForBook(const std::string& srcPath, const std::string& author) {
  std::string matchedBaseFolder;
  for (const auto& server : OPDS_STORE.getServers()) {
    if (server.folderOrganization != OpdsFolderOrganization::AUTHOR) continue;
    const std::string baseFolder = normalizeOpdsDownloadFolder(server.downloadFolder);
    if (!isPathUnderFolder(srcPath, baseFolder) || baseFolder.size() <= matchedBaseFolder.size()) continue;
    matchedBaseFolder = baseFolder;
  }
  if (matchedBaseFolder.empty()) return READ_FOLDER;

  const std::string folderAuthor = author.empty() ? firstChildFolderUnder(srcPath, matchedBaseFolder) : author;
  constexpr size_t readPrefixBytes = sizeof(READ_FOLDER);
  if (folderAuthor.empty() || readPrefixBytes >= READ_FOLDER_MAX_BYTES) return READ_FOLDER;
  const size_t authorBudget = READ_FOLDER_MAX_BYTES - readPrefixBytes;
  const std::string authorFolder = StringUtils::sanitizeFilename(folderAuthor, authorBudget);
  return authorFolder.empty() ? READ_FOLDER : std::string(READ_FOLDER) + "/" + authorFolder;
}
}

namespace BookMoveUtils {

std::string buildReadFolderDestination(const std::string& srcPath, const std::string& author) {
  const size_t lastSlash = srcPath.rfind('/');
  const std::string filename = (lastSlash != std::string::npos) ? srcPath.substr(lastSlash + 1) : srcPath;

  const std::string readFolder = readFolderForBook(srcPath, author);
  Storage.mkdir(readFolder.c_str());
  std::string dstPath = readFolder + "/" + filename;
  if (!Storage.exists(dstPath.c_str())) {
    return dstPath;
  }

  const size_t dotPos = filename.rfind('.');
  const std::string base = (dotPos != std::string::npos) ? filename.substr(0, dotPos) : filename;
  const std::string ext = (dotPos != std::string::npos) ? filename.substr(dotPos) : "";
  int suffix = 2;
  do {
    dstPath = readFolder + "/" + base + " (" + std::to_string(suffix) + ")" + ext;
    suffix++;
  } while (Storage.exists(dstPath.c_str()) && suffix < 100);
  return dstPath;
}

bool migrateMovedEpubState(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                           const std::string& title, const std::string& author, const bool keepInRecents) {
  bool ok = true;

  const std::string newCachePath = Epub::cachePathForFilePath(newPath, "/.crosspoint");
  if (!oldCachePath.empty() && Storage.exists(oldCachePath.c_str())) {
    if (!Storage.rename(oldCachePath.c_str(), newCachePath.c_str())) {
      LOG_ERR("BookMove", "Failed to rename cache dir %s -> %s (non-fatal)", oldCachePath.c_str(),
              newCachePath.c_str());
      ok = false;
    }
  }

  if (!BookmarkStore::migrateForFilePath(oldPath, newPath, title, author, "epub")) {
    LOG_ERR("BookMove", "Failed to migrate bookmarks for moved book %s -> %s", oldPath.c_str(), newPath.c_str());
    ok = false;
  }

  if (!ClippingStore::migrateForFilePath(oldPath, newPath, title, author, "epub")) {
    LOG_ERR("BookMove", "Failed to migrate clippings for moved book %s -> %s", oldPath.c_str(), newPath.c_str());
    ok = false;
  }

  if (keepInRecents) {
    RECENT_BOOKS.updatePath(oldPath, newPath, oldCachePath, newCachePath);
  } else {
    RECENT_BOOKS.removeByPath(oldPath);
    RECENT_BOOKS.removeByPath(newPath);
  }

  if (APP_STATE.openEpubPath == oldPath) {
    APP_STATE.openEpubPath = newPath;
    APP_STATE.saveToFile();
  }

  return ok;
}

}  // namespace BookMoveUtils
