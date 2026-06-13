#pragma once

#include <string>

namespace BookMoveUtils {

std::string buildReadFolderDestination(const std::string& srcPath, const std::string& author = "");
bool migrateMovedEpubState(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                           const std::string& title, const std::string& author, bool keepInRecents);

}  // namespace BookMoveUtils
