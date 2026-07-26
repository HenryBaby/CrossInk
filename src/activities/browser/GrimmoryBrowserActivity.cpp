#include "GrimmoryBrowserActivity.h"

#include <esp_sntp.h>

#include <ctime>

#include "GrimmoryStore.h"
#include "HalStorage.h"
#include "I18n.h"
#include "Logging.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "WiFi.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/GrimmoryClient.h"

namespace {
bool ensureSystemTime() {
  static bool syncedThisBoot = false;
  if (syncedThisBoot) return true;

  if (esp_sntp_enabled()) esp_sntp_stop();
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.nist.gov");
  esp_sntp_init();

  constexpr int kMaxRetries = 50;  // 5 seconds max
  int retry = 0;
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < kMaxRetries) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    ++retry;
  }
  constexpr std::time_t kPlausibleEpoch = 1767225600;  // 2026-01-01 UTC
  const bool synced = sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED && std::time(nullptr) >= kPlausibleEpoch;
  if (esp_sntp_enabled()) esp_sntp_stop();
  if (synced) {
    syncedThisBoot = true;
    LOG_INF("GRM", "NTP time synced");
  } else {
    LOG_ERR("GRM", "NTP sync failed; refusing authenticated request");
  }
  return synced;
}
}  // namespace

void GrimmoryBrowserActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseLoadedFont(renderer);
  state = BrowserState::CHECK_WIFI;
  checkWifi();
}
void GrimmoryBrowserActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}
void GrimmoryBrowserActivity::load() {
  if (!GRIMMORY_STORE.isConfigured()) {
    state = BrowserState::ERROR;
    error = tr(STR_GRIMMORY_AUTH_ERROR);
    requestUpdate();
    return;
  }
  if (!ensureSystemTime()) {
    state = BrowserState::ERROR;
    error = tr(STR_GRIMMORY_AUTH_ERROR);
    requestUpdate();
    return;
  }
  GrimmoryClient c(GRIMMORY_STORE.config().baseUrl);
  state = BrowserState::LOGIN;
  if (!c.login(GRIMMORY_STORE.config().username, GRIMMORY_STORE.config().password)) {
    state = BrowserState::ERROR;
    error = tr(STR_GRIMMORY_AUTH_ERROR);
    requestUpdate();
    return;
  }
  state = BrowserState::LOADING;
  requestUpdate();
  if (!c.listPage(page, books, total)) {
    state = BrowserState::ERROR;
    error = tr(STR_GRIMMORY_LIST_ERROR);
  } else
    state = BrowserState::BROWSING;
  requestUpdate();
}
void GrimmoryBrowserActivity::loadPage(size_t requestedPage) {
  const size_t maxPage = total == 0 ? 0 : (total - 1) / 20;
  page = requestedPage > maxPage ? maxPage : requestedPage;
  selected = 0;
  load();
}
void GrimmoryBrowserActivity::checkWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    load();
    return;
  }
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifi(!result.isCancelled); });
}
void GrimmoryBrowserActivity::onWifi(bool connected) {
  if (connected)
    load();
  else {
    state = BrowserState::ERROR;
    error = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}
void GrimmoryBrowserActivity::download() {
  if (selected >= books.size()) return;
  const auto& b = books[selected];
  const std::string path = GRIMMORY_STORE.config().downloadFolder + "/" + b.filename;
  const std::string part = path + ".part";
  const std::string folder = GRIMMORY_STORE.config().downloadFolder;
  if (!Storage.exists(folder.c_str()) && !Storage.mkdir(folder.c_str())) {
    LOG_ERR("GRM", "Failed to create Grimmory download folder");
    state = BrowserState::ERROR;
    error = tr(STR_GRIMMORY_DOWNLOAD_FAILED);
    requestUpdate();
    return;
  }
  const std::string backup = path + ".bak";
  if (!Storage.exists(path.c_str()) && Storage.exists(backup.c_str()) &&
      !Storage.rename(backup.c_str(), path.c_str())) {
    LOG_ERR("GRM", "Failed to recover Grimmory backup");
    state = BrowserState::ERROR;
    error = tr(STR_GRIMMORY_DOWNLOAD_FAILED);
    requestUpdate();
    return;
  }
  if (!ensureSystemTime()) {
    state = BrowserState::ERROR;
    error = tr(STR_GRIMMORY_DOWNLOAD_FAILED);
    requestUpdate();
    return;
  }
  GrimmoryClient c(GRIMMORY_STORE.config().baseUrl);
  if (!c.login(GRIMMORY_STORE.config().username, GRIMMORY_STORE.config().password)) {
    state = BrowserState::ERROR;
    error = tr(STR_GRIMMORY_DOWNLOAD_FAILED);
    return;
  }
  state = BrowserState::DOWNLOADING;
  cancel = false;
  downloadDone = downloadTotal = 0;
  requestUpdateAndWait();
  const auto result = c.download(
      b.id, part, &cancel,
      [this](size_t done, size_t totalBytes) {
        downloadDone = done;
        downloadTotal = totalBytes;
        requestUpdate(true);
      },
      [this] {
        mappedInput.update();
        if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
          cancel = true;
          return true;
        }
        return cancel;
      });
  bool installed = false;
  if (result == HttpDownloader::OK) {
    if (Storage.exists(backup.c_str()) && Storage.exists(path.c_str()) && !Storage.remove(backup.c_str()))
      LOG_ERR("GRM", "Failed to clear stale Grimmory backup");
    if (Storage.exists(path.c_str()) && !Storage.rename(path.c_str(), backup.c_str())) {
      LOG_ERR("GRM", "Failed to back up existing book");
    } else if (Storage.rename(part.c_str(), path.c_str())) {
      Storage.remove(backup.c_str());
      installed = true;
    } else {
      LOG_ERR("GRM", "Failed to install downloaded book");
      if (Storage.exists(backup.c_str()) && !Storage.rename(backup.c_str(), path.c_str()))
        LOG_ERR("GRM", "Failed to roll back Grimmory backup");
    }
  }
  if (installed)
    state = BrowserState::BROWSING;
  else {
    if (Storage.exists(part.c_str())) Storage.remove(part.c_str());
    state = BrowserState::ERROR;
    error = result == HttpDownloader::ABORTED ? tr(STR_CANCEL) : tr(STR_GRIMMORY_DOWNLOAD_FAILED);
  }
  requestUpdate();
}
void GrimmoryBrowserActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (state == BrowserState::DOWNLOADING) {
      cancel = true;
      return;
    }
    finish();
    return;
  }
  if (state == BrowserState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) load();
    return;
  }
  if (state == BrowserState::DOWNLOADING) return;
  if (state != BrowserState::BROWSING) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    download();
    return;
  }
  nav.onNextPress([this] {
    if (!books.empty()) selected = (selected + 1) % books.size();
    requestUpdate();
  });
  nav.onPreviousPress([this] {
    if (!books.empty()) selected = (selected + books.size() - 1) % books.size();
    requestUpdate();
  });
  nav.onNextContinuous([this] {
    if (page + 1 < (total + 19) / 20) loadPage(page + 1);
  });
  nav.onPreviousContinuous([this] {
    if (page > 0) loadPage(page - 1);
  });
}
void GrimmoryBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, renderer.getScreenWidth(), m.headerHeight}, tr(STR_GRIMMORY));
  if (state == BrowserState::ERROR) {
    GUI.drawPopup(renderer, error.c_str());
  } else if (state == BrowserState::DOWNLOADING) {
    char msg[64];
    snprintf(msg, sizeof(msg), "%s %zu/%zu", tr(STR_DOWNLOADING), downloadDone, downloadTotal);
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, msg);
  } else {
    GUI.drawList(
        renderer,
        Rect{0, m.topPadding + m.headerHeight, renderer.getScreenWidth(), renderer.getScreenHeight() - m.headerHeight},
        books.size(), (int)selected, [this](int i) { return books[i].title; }, nullptr, nullptr,
        [this](int i) { return books[i].author; }, true);
    char pageLabel[48];
    const size_t pageCount = total == 0 ? 1 : (total + 19) / 20;
    snprintf(pageLabel, sizeof(pageLabel), "%s %zu/%zu", tr(STR_NEXT_PAGE), page + 1, pageCount);
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() - 16, pageLabel);
  }
  renderer.displayBuffer();
}
