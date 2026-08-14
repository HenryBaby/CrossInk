#include "QuickActions.h"

#include <I18n.h>

#include <vector>

#include "GlobalActions.h"
#include "components/OptionPopup.h"

namespace QuickActions {
void showConfiguredPopup(OptionPopup& popup, const std::function<void()>& requestUpdate, ActionHandler actionHandler,
                         ActionFilter actionFilter) {
  std::vector<std::string> labels;
  std::vector<uint8_t> actions;
  labels.reserve(std::size(SETTINGS.quickActionSlots));
  actions.reserve(std::size(SETTINGS.quickActionSlots));
  for (const uint8_t action : SETTINGS.quickActionSlots) {
    const auto shortcutAction = static_cast<CrossPointSettings::SHORT_PWRBTN>(action);
    if (action == CrossPointSettings::IGNORE || !isActionAvailable(action) ||
        (actionFilter && !actionFilter(shortcutAction))) {
      continue;
    }
    labels.emplace_back(I18N.get(actionLabel(action)));
    actions.push_back(action);
  }
  if (actions.empty()) return;
  popup.show(StrId::STR_QUICK_ACTIONS, labels, 0,
             [actions = std::move(actions), actionHandler = std::move(actionHandler)](const int selected) {
               if (selected >= 0 && static_cast<size_t>(selected) < actions.size()) {
                 const auto action = static_cast<CrossPointSettings::SHORT_PWRBTN>(actions[selected]);
                 if (actionHandler) {
                   actionHandler(action);
                 } else {
                   dispatchShortcutAction(action);
                 }
               }
             });
  requestUpdate();
}
}  // namespace QuickActions
