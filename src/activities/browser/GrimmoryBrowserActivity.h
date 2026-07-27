#pragma once
#include <memory>
#include <vector>

#include "GrimmoryParser.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
class GrimmoryBrowserActivity final : public Activity {
 public:
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOGIN, LOADING, BROWSING, DOWNLOADING, ERROR };
  GrimmoryBrowserActivity(GfxRenderer& r, MappedInputManager& i) : Activity("GrimmoryBrowser", r, i) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator nav;
  BrowserState state = BrowserState::CHECK_WIFI;
  std::vector<Grimmory::BookEntry> books;
  size_t page = 0, selected = 0, total = 0;
  std::string error;
  std::string downloadTitle;
  bool cancel = false;
  size_t downloadDone = 0, downloadTotal = 0;
  void load();
  void download();
  void checkWifi();
  void onWifi(bool connected);
  void loadPage(size_t requestedPage);
  size_t rowCount() const;
  bool rowIsNavigation(size_t row) const;
  bool rowIsNextPage(size_t row) const;
  size_t rowBookIndex(size_t row) const;
};
