#include "GrimmorySettingsActivity.h"

#include "I18n.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

size_t GrimmorySettingsActivity::getMenuItemCount() const { return GRIMMORY_STORE.hasConfig() ? 5 : 4; }

void GrimmorySettingsActivity::onEnter() {
  Activity::onEnter();
  edit = GRIMMORY_STORE.config();
  selected = 0;
  error = false;
  requestUpdate();
}
void GrimmorySettingsActivity::onExit() { Activity::onExit(); }
void GrimmorySettingsActivity::save() {
  error = !GRIMMORY_STORE.setConfig(edit);
  if (!error) edit = GRIMMORY_STORE.config();
  requestUpdate();
}
void GrimmorySettingsActivity::editField(size_t index) {
  std::string* value = index == 0   ? &edit.baseUrl
                       : index == 1 ? &edit.username
                       : index == 2 ? &edit.password
                                    : &edit.downloadFolder;
  const char* title = index == 0   ? tr(STR_GRIMMORY_URL)
                      : index == 1 ? tr(STR_GRIMMORY_USERNAME)
                      : index == 2 ? tr(STR_GRIMMORY_PASSWORD)
                                   : tr(STR_DOWNLOAD_FOLDER);
  const std::string initialValue = index == 0 && value->empty() ? "https://" : *value;
  auto handler = [this, value, index](const ActivityResult& r) {
    if (!r.isCancelled) {
      *value = std::get<KeyboardResult>(r.data).text;
      if (index == 0 && (*value == "https://" || *value == "http://")) value->clear();
      save();
    }
    requestUpdate();
  };
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                             renderer, mappedInput, title, initialValue, index == 0 || index == 3 ? 127 : 63,
                             index == 0 ? InputType::Url : (index == 2 ? InputType::Password : InputType::Text)),
                         handler);
}
void GrimmorySettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selected < 4)
      editField(selected);
    else if (selected == 4) {
      if (GRIMMORY_STORE.removeConfig()) {
        finish();
      } else {
        error = true;
        requestUpdate();
      }
    }
    return;
  }
  nav.onNext([this] {
    selected = (selected + 1) % getMenuItemCount();
    requestUpdate();
  });
  nav.onPrevious([this] {
    selected = (selected + getMenuItemCount() - 1) % getMenuItemCount();
    requestUpdate();
  });
}
void GrimmorySettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, pageWidth, m.headerHeight}, tr(STR_GRIMMORY_SETTINGS));
  const int contentTop = m.topPadding + m.headerHeight + m.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - m.buttonHintsHeight - m.verticalSpacing * 2;
  const StrId ids[] = {StrId::STR_GRIMMORY_URL, StrId::STR_GRIMMORY_USERNAME, StrId::STR_GRIMMORY_PASSWORD,
                       StrId::STR_DOWNLOAD_FOLDER};
  const int menuItems = static_cast<int>(getMenuItemCount());
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, menuItems, (int)selected,
      [&](int i) { return i < 4 ? std::string(I18N.get(ids[i])) : std::string(tr(STR_DELETE_SERVER)); }, nullptr,
      nullptr,
      [this](int i) {
        if (i == 0) return edit.baseUrl.empty() ? std::string(tr(STR_NOT_SET)) : edit.baseUrl;
        if (i == 1) return edit.username.empty() ? std::string(tr(STR_NOT_SET)) : edit.username;
        if (i == 2) return edit.password.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        if (i == 3)
          return edit.downloadFolder.empty() ? std::string("/Grimmory")
                                             : normalizeOpdsDownloadFolder(edit.downloadFolder);
        return std::string();
      },
      true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (error) GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
  renderer.displayBuffer();
}
