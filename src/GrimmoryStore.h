#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

struct GrimmoryConfig {
  std::string baseUrl;
  std::string username;
  std::string password;
  std::string downloadFolder = "/Grimmory";
};

class GrimmoryStore : public PersistableStore<GrimmoryStore> {
 public:
  static const char* getFilePath() { return "/.crosspoint/grimmory.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
  bool loadFromFile();
  const GrimmoryConfig& config() const { return config_; }
  bool setConfig(GrimmoryConfig config);
  bool removeConfig();
  bool hasConfig() const { return !config_.baseUrl.empty() || !config_.username.empty() || !config_.password.empty(); }
  bool isConfigured() const {
    return !config_.baseUrl.empty() && !config_.username.empty() && !config_.password.empty();
  }

 private:
  GrimmoryStore() = default;
  GrimmoryConfig config_;
  friend class PersistableStore<GrimmoryStore>;
};

#define GRIMMORY_STORE GrimmoryStore::getInstance()
