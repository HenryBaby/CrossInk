#include "GrimmorySettingsActivity.h"

#include "I18n.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
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
  requestUpdate();
}
void GrimmorySettingsActivity::editField(size_t index) {
  std::string* value = index == 0 ? &edit.baseUrl : index == 1 ? &edit.username : &edit.password;
  const char* title = index == 0   ? tr(STR_GRIMMORY_URL)
                      : index == 1 ? tr(STR_GRIMMORY_USERNAME)
                                   : tr(STR_GRIMMORY_PASSWORD);
  auto handler = [this, value](const ActivityResult& r) {
    if (!r.isCancelled) {
      *value = std::get<KeyboardResult>(r.data).text;
      save();
    }
    requestUpdate();
  };
  startActivityForResult(std::make_unique<KeyboardEntryActivity>(
                             renderer, mappedInput, title, *value, 127,
                             index == 0 ? InputType::Url : (index == 2 ? InputType::Password : InputType::Text)),
                         handler);
}
void GrimmorySettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selected < 3)
      editField(selected);
    else {
      activityManager.goToGrimmoryBrowser();
    }
    return;
  }
  nav.onNext([this] {
    selected = (selected + 1) % 4;
    requestUpdate();
  });
  nav.onPrevious([this] {
    selected = (selected + 3) % 4;
    requestUpdate();
  });
}
void GrimmorySettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& m = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, m.topPadding, renderer.getScreenWidth(), m.headerHeight}, tr(STR_GRIMMORY_SETTINGS));
  const StrId ids[] = {STR_GRIMMORY_URL, STR_GRIMMORY_USERNAME, STR_GRIMMORY_PASSWORD, STR_GRIMMORY_OPEN};
  GUI.drawList(
      renderer,
      Rect{0, m.topPadding + m.headerHeight, renderer.getScreenWidth(),
           renderer.getScreenHeight() - m.headerHeight - m.topPadding},
      4, (int)selected, [&](int i) { return std::string(I18N.get(ids[i])); }, nullptr, nullptr,
      [this](int i) {
        if (i == 0) return edit.baseUrl;
        if (i == 1) return edit.username;
        if (i == 2) return edit.password.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
        return std::string();
      },
      true);
  if (error) GUI.drawPopup(renderer, tr(STR_SAVE_PROGRESS_FAILED));
  renderer.displayBuffer();
}
