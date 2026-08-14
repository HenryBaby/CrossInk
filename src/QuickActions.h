#pragma once

#include <CrossInkHalFrontlight.h>
#include <HalGPIO.h>
#include <HalTiltSensor.h>
#include <I18n.h>

#include <array>
#include <functional>

#include "CrossPointSettings.h"

class OptionPopup;

// One source of truth for the shortcut that opens Quick Actions.  UI and web
// settings call this after changing an action, so the persisted state cannot
// end up with two physical gestures claiming the same menu.
namespace QuickActions {
enum class Trigger : uint8_t { None = 0, ShortPower, LongPower, LongBack, LongMenu };

inline constexpr std::array<StrId, CrossPointSettings::QUICK_ACTION_SLOT_ACTION_COUNT> actionLabels = {
    StrId::STR_IGNORE,
    StrId::STR_SLEEP,
    StrId::STR_PAGE_TURN,
    StrId::STR_FORCE_REFRESH,
    StrId::STR_CHANGE_FONT,
    StrId::STR_TOGGLE_GUIDE_DOTS,
    StrId::STR_TOGGLE_BIONIC_READING,
    StrId::STR_TOGGLE_BOOKMARK,
    StrId::STR_SYNC_PROGRESS,
    StrId::STR_MARK_FINISHED,
    StrId::STR_READING_STATS,
    StrId::STR_SCREENSHOT_BUTTON,
    StrId::STR_CYCLE_PAGE_TURN,
    StrId::STR_FILE_TRANSFER,
    StrId::STR_TILT_PAGE_TURN,
    StrId::STR_READER_DARK_MODE,
    StrId::STR_FOOTNOTES,
    StrId::STR_BROWSE_FILES,
    StrId::STR_CALIBRE_WIRELESS,
    StrId::STR_JOIN_NETWORK,
    StrId::STR_CREATE_HOTSPOT,
    StrId::STR_SAVE_CLIPPING,
    StrId::STR_LOOKUP};

inline bool supportsTiltPageTurn() { return halTiltSensor.isAvailable(); }

inline bool isActionAvailable(const uint8_t action) {
  if (action == CrossPointSettings::TOGGLE_FRONTLIGHT) return Frontlight.present();
  if (action == CrossPointSettings::TOGGLE_TOUCHSCREEN) return gpio.hasTouch();
  if (action < CrossPointSettings::QUICK_ACTION_SLOT_ACTION_COUNT) {
    return action != CrossPointSettings::TOGGLE_TILT_PAGE_TURN || supportsTiltPageTurn();
  }
  return action == CrossPointSettings::TOGGLE_HOME_BUTTON_IN_READER && gpio.hasHomeKey();
}

inline StrId actionLabel(const uint8_t action) {
  if (action < CrossPointSettings::QUICK_ACTION_SLOT_ACTION_COUNT) return actionLabels[action];
  if (action == CrossPointSettings::TOGGLE_FRONTLIGHT) return StrId::STR_TOGGLE_FRONTLIGHT;
  if (action == CrossPointSettings::TOGGLE_TOUCHSCREEN) return StrId::STR_TOGGLE_TOUCHSCREEN;
  return StrId::STR_HOME_BUTTON_LOCK;
}

inline void synchronize(CrossPointSettings& settings, Trigger preferred = Trigger::None) {
  const bool shortPower = settings.shortPwrBtn == CrossPointSettings::QUICK_ACTIONS;
  const bool longPower = settings.longPwrBtn == CrossPointSettings::QUICK_ACTIONS;
  const bool longBack = settings.longPressBackAction == CrossPointSettings::LONG_MENU_QUICK_ACTIONS;
  const bool longMenu = settings.longPressMenuAction == CrossPointSettings::LONG_MENU_QUICK_ACTIONS;

  Trigger owner = preferred;
  if (owner == Trigger::None) {
    if (shortPower)
      owner = Trigger::ShortPower;
    else if (longPower)
      owner = Trigger::LongPower;
    else if (longBack)
      owner = Trigger::LongBack;
    else if (longMenu)
      owner = Trigger::LongMenu;
  }

  if (owner != Trigger::ShortPower && shortPower) settings.shortPwrBtn = CrossPointSettings::IGNORE;
  if (owner != Trigger::LongPower && longPower) settings.longPwrBtn = CrossPointSettings::IGNORE;
  if (owner != Trigger::LongBack && longBack) settings.longPressBackAction = CrossPointSettings::LONG_MENU_OFF;
  if (owner != Trigger::LongMenu && longMenu) settings.longPressMenuAction = CrossPointSettings::LONG_MENU_OFF;
  settings.quickActionsTrigger = static_cast<uint8_t>(owner);
}

inline void applyTrigger(CrossPointSettings& settings, const Trigger trigger) {
  if (settings.shortPwrBtn == CrossPointSettings::QUICK_ACTIONS) settings.shortPwrBtn = CrossPointSettings::IGNORE;
  if (settings.longPwrBtn == CrossPointSettings::QUICK_ACTIONS) settings.longPwrBtn = CrossPointSettings::IGNORE;
  if (settings.longPressBackAction == CrossPointSettings::LONG_MENU_QUICK_ACTIONS) {
    settings.longPressBackAction = CrossPointSettings::LONG_MENU_OFF;
  }
  if (settings.longPressMenuAction == CrossPointSettings::LONG_MENU_QUICK_ACTIONS) {
    settings.longPressMenuAction = CrossPointSettings::LONG_MENU_OFF;
  }

  if (trigger == Trigger::ShortPower) settings.shortPwrBtn = CrossPointSettings::QUICK_ACTIONS;
  if (trigger == Trigger::LongPower) settings.longPwrBtn = CrossPointSettings::QUICK_ACTIONS;
  if (trigger == Trigger::LongBack) settings.longPressBackAction = CrossPointSettings::LONG_MENU_QUICK_ACTIONS;
  if (trigger == Trigger::LongMenu) settings.longPressMenuAction = CrossPointSettings::LONG_MENU_QUICK_ACTIONS;
  settings.quickActionsTrigger = static_cast<uint8_t>(trigger);
}

inline Trigger triggerForSetting(uint8_t CrossPointSettings::* member) {
  if (member == &CrossPointSettings::shortPwrBtn) return Trigger::ShortPower;
  if (member == &CrossPointSettings::longPwrBtn) return Trigger::LongPower;
  if (member == &CrossPointSettings::longPressBackAction) return Trigger::LongBack;
  if (member == &CrossPointSettings::longPressMenuAction) return Trigger::LongMenu;
  return Trigger::None;
}

inline void settingChanged(CrossPointSettings& settings, uint8_t CrossPointSettings::* member) {
  const Trigger trigger = triggerForSetting(member);
  if (trigger == Trigger::None) return;
  const bool selected = (trigger == Trigger::ShortPower || trigger == Trigger::LongPower)
                            ? settings.*member == CrossPointSettings::QUICK_ACTIONS
                            : settings.*member == CrossPointSettings::LONG_MENU_QUICK_ACTIONS;
  synchronize(settings, selected ? trigger : Trigger::None);
}

using ActionHandler = std::function<void(CrossPointSettings::SHORT_PWRBTN)>;
using ActionFilter = std::function<bool(CrossPointSettings::SHORT_PWRBTN)>;

void showConfiguredPopup(OptionPopup& popup, const std::function<void()>& requestUpdate,
                         ActionHandler actionHandler = {}, ActionFilter actionFilter = {});
}  // namespace QuickActions
