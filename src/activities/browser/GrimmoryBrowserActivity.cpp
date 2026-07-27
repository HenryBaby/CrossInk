#include "GrimmoryBrowserActivity.h"

#include <esp_sntp.h>
#include <sys/time.h>

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
#include "network/HttpDownloader.h"

namespace {
constexpr int kVisibleRows = 23;

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
    return true;
  }

  std::time_t httpTime = 0;
  if (HttpDownloader::fetchHttpDate(GRIMMORY_STORE.config().baseUrl, httpTime) && httpTime >= kPlausibleEpoch) {
    timeval networkTime{httpTime, 0};
    if (settimeofday(&networkTime, nullptr) != 0) {
      LOG_ERR("GRM", "HTTP Date fallback settimeofday failed");
      return false;
    }
    syncedThisBoot = true;
    LOG_ERR("GRM", "NTP unavailable; using unauthenticated HTTP Date fallback");
    return true;
  }
  LOG_ERR("GRM", "NTP sync and HTTP Date fallback failed");
  return false;
}
}  // namespace

void GrimmoryBrowserActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseLoadedFont(renderer);
  state = BrowserState::CHECK_WIFI;
  books.clear();
  page = selected = total = 0;
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
  } else {
    state = BrowserState::BROWSING;
  }
  requestUpdate();
}

size_t GrimmoryBrowserActivity::rowCount() const {
  const size_t pageCount = total == 0 ? 0 : (total + Grimmory::kPageSize - 1) / Grimmory::kPageSize;
  return books.size() + (page > 0 ? 1 : 0) + (page + 1 < pageCount ? 1 : 0);
}

bool GrimmoryBrowserActivity::rowIsNavigation(const size_t row) const {
  return (page > 0 && row == 0) || row >= (page > 0 ? 1 : 0) + books.size();
}

bool GrimmoryBrowserActivity::rowIsNextPage(const size_t row) const {
  const size_t pageCount = total == 0 ? 0 : (total + Grimmory::kPageSize - 1) / Grimmory::kPageSize;
  return page + 1 < pageCount && row == (page > 0 ? 1 : 0) + books.size();
}

size_t GrimmoryBrowserActivity::rowBookIndex(const size_t row) const { return row - (page > 0 ? 1 : 0); }
void GrimmoryBrowserActivity::loadPage(size_t requestedPage) {
  const size_t maxPage = total == 0 ? 0 : (total - 1) / Grimmory::kPageSize;
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
  if (selected >= rowCount() || rowIsNavigation(selected)) return;
  const auto& b = books[rowBookIndex(selected)];
  downloadTitle = b.title;
  const std::string folder = GRIMMORY_STORE.config().downloadFolder;
  const std::string path = folder == "/" ? "/" + b.filename : folder + "/" + b.filename;
  const std::string part = path + ".part";
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
    if (selected < rowCount() && rowIsNavigation(selected))
      loadPage(rowIsNextPage(selected) ? page + 1 : page - 1);
    else
      download();
    return;
  }
  nav.onNextRelease([this] {
    const size_t count = rowCount();
    if (count > 0) selected = ButtonNavigator::nextIndex(selected, count);
    requestUpdate();
  });
  nav.onPreviousRelease([this] {
    const size_t count = rowCount();
    if (count > 0) selected = ButtonNavigator::previousIndex(selected, count);
    requestUpdate();
  });
  nav.onNextContinuous([this] {
    const size_t count = rowCount();
    if (count > 0) {
      selected = ButtonNavigator::nextPageIndex(selected, count, kVisibleRows);
      requestUpdate();
    }
  });
  nav.onPreviousContinuous([this] {
    const size_t count = rowCount();
    if (count > 0) {
      selected = ButtonNavigator::previousPageIndex(selected, count, kVisibleRows);
      requestUpdate();
    }
  });
}
void GrimmoryBrowserActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, renderer.getScreenWidth(), m.headerHeight}, tr(STR_GRIMMORY));
  if (state == BrowserState::CHECK_WIFI || state == BrowserState::WIFI_SELECTION || state == BrowserState::LOGIN ||
      state == BrowserState::LOADING) {
    const char* status = state == BrowserState::CHECK_WIFI ? tr(STR_CHECKING_WIFI)
                         : state == BrowserState::LOGIN    ? tr(STR_GRIMMORY_LOGIN)
                                                           : tr(STR_LOADING);
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, status);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == BrowserState::ERROR) {
    GUI.drawPopup(renderer, error.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == BrowserState::DOWNLOADING) {
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DOWNLOADING));
    const auto title = renderer.truncatedText(UI_10_FONT_ID, downloadTitle.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, title.c_str());
    if (downloadTotal > 0)
      GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 + 20, pageWidth - 100, 20}, downloadDone, downloadTotal);
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    const char* confirmLabel = selected < rowCount() && rowIsNavigation(selected) ? tr(STR_OPEN) : tr(STR_DOWNLOAD);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    const size_t count = rowCount();
    if (count == 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2, tr(STR_GRIMMORY_NO_BOOKS));
    } else {
      const int rowTop = 60;
      const int rowHeight = 30;
      const int visibleStart = static_cast<int>(selected / kVisibleRows) * kVisibleRows;
      renderer.fillRect(0, rowTop + static_cast<int>(selected - visibleStart) * rowHeight - 2,
                        renderer.getScreenWidth() - 1, rowHeight);
      for (size_t i = visibleStart; i < count && i < visibleStart + kVisibleRows; ++i) {
        char text[Grimmory::kMaxTitleBytes + Grimmory::kMaxAuthorBytes + 8];
        if (rowIsNavigation(i)) {
          snprintf(text, sizeof(text), "> %s", rowIsNextPage(i) ? tr(STR_GRIMMORY_NEXT) : tr(STR_GRIMMORY_PREV));
        } else {
          const auto& book = books[rowBookIndex(i)];
          if (book.author.empty())
            snprintf(text, sizeof(text), "%s", book.title.c_str());
          else
            snprintf(text, sizeof(text), "%s - %s", book.title.c_str(), book.author.c_str());
        }
        const auto item = renderer.truncatedText(UI_10_FONT_ID, text, renderer.getScreenWidth() - 40);
        renderer.drawText(UI_10_FONT_ID, 20, rowTop + static_cast<int>(i - visibleStart) * rowHeight, item.c_str(),
                          i != selected);
      }
    }
  }
  renderer.displayBuffer();
}
