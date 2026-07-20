#pragma once

#include <string>
#include <string_view>

namespace HttpHeaderUtils {

bool extractContentDispositionFilename(std::string_view header, std::string& filename);

}  // namespace HttpHeaderUtils
