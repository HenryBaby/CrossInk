#pragma once
#include <string>
#include <vector>

#include "GrimmoryParser.h"
#include "network/HttpDownloader.h"

class GrimmoryClient {
 public:
  explicit GrimmoryClient(const std::string& baseUrl) : baseUrl_(baseUrl) {}
  bool login(const std::string& user, const std::string& password);
  bool listPage(size_t page, std::vector<Grimmory::BookEntry>& entries, size_t& total);
  HttpDownloader::DownloadError download(int id, const std::string& path, bool* cancel = nullptr,
                                         HttpDownloader::ProgressCallback progress = nullptr,
                                         HttpDownloader::CancelCallback shouldCancel = nullptr);

 private:
  std::string baseUrl_, token_;
};
