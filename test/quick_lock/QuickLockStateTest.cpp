#include <gtest/gtest.h>

#include <cstdint>

#include "ButtonShortcutController.h"
#include "QuickLockState.h"

TEST(QuickLockState, NeverTimeoutDoesNotSleep) {
  QuickLockState state;
  state.toggle(100U);
  EXPECT_FALSE(state.shouldSleep(100000U, 0U));
}

TEST(QuickLockState, TimeoutUsesWraparoundSafeElapsedTime) {
  QuickLockState state;
  state.toggle(UINT32_MAX - 20U);
  EXPECT_FALSE(state.shouldSleep(10U, 40U));
  EXPECT_TRUE(state.shouldSleep(25U, 40U));
}

TEST(ButtonShortcutController, QuickLockChordFiresOnceUntilFullRelease) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  EXPECT_EQ(controller.update(10U, true, true, false, false, Action::QuickLock).event, Event::QuickLockChanged);
  EXPECT_TRUE(controller.isQuickLocked());
  EXPECT_EQ(controller.update(11U, true, true, false, false, Action::QuickLock).event, Event::None);
  EXPECT_TRUE(controller.update(12U, false, false, false, false, Action::QuickLock).consumeInput);
  EXPECT_EQ(controller.update(13U, true, true, false, false, Action::QuickLock).event, Event::QuickLockChanged);
  EXPECT_FALSE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, LockedStateBlocksOtherChordsAndShortPower) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  EXPECT_EQ(controller.update(10U, true, true, false, false, Action::QuickLock).event, Event::QuickLockChanged);
  controller.update(11U, false, false, false, false, Action::QuickLock);
  const auto blocked = controller.update(12U, true, true, false, false, Action::Screenshot);
  EXPECT_EQ(blocked.event, Event::None);
  EXPECT_TRUE(blocked.consumeInput);
  controller.update(13U, false, false, false, false, Action::Screenshot);
  EXPECT_EQ(controller.update(14U, false, false, true, false, Action::Screenshot).event, Event::None);
  EXPECT_TRUE(controller.isQuickLocked());
  EXPECT_EQ(controller.update(15U, true, true, false, false, Action::QuickLock).event, Event::QuickLockChanged);
  EXPECT_FALSE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, ShortPowerQuickLockOnlyUnlocksOnShortPowerRelease) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  EXPECT_EQ(controller.update(10U, false, false, true, true, Action::Disabled).event, Event::QuickLockChanged);
  EXPECT_EQ(controller.quickLockTrigger(), QuickLockTrigger::ShortPower);
  EXPECT_EQ(controller.update(11U, true, true, false, false, Action::Screenshot).event, Event::None);
  controller.update(12U, false, false, false, false, Action::Screenshot);
  EXPECT_EQ(controller.update(13U, false, false, true, false, Action::Disabled).event, Event::QuickLockChanged);
  EXPECT_FALSE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, LongPowerRequiresReleaseBeforeItCanUnlock) {
  ButtonShortcutController controller;

  controller.toggleQuickLock(10U, QuickLockTrigger::LongPower, true);
  EXPECT_TRUE(controller.isQuickLocked());
  EXPECT_FALSE(controller.tryUnlockLongPower(11U, true));
  EXPECT_FALSE(controller.tryUnlockLongPower(12U, false));
  EXPECT_TRUE(controller.tryUnlockLongPower(13U, true));
  EXPECT_FALSE(controller.isQuickLocked());
}

TEST(ButtonShortcutController, PageTurnChordEmitsPageTurn) {
  ButtonShortcutController controller;
  using Action = ButtonShortcutController::ChordAction;
  using Event = ButtonShortcutController::Event;

  EXPECT_EQ(controller.update(1U, true, true, false, false, Action::PageTurn).event, Event::PageTurn);
}

TEST(ButtonShortcutController, EveryChordActionConsumesBothReleaseOrders) {
  using Action = ButtonShortcutController::ChordAction;
  constexpr Action actions[] = {
      Action::Screenshot,       Action::QuickLock,           Action::Sleep,
      Action::PageTurn,         Action::ToggleBookmark,      Action::ReadingStats,
      Action::MarkFinished,     Action::ForceRefresh,        Action::ToggleFont,
      Action::ToggleGuideDots,  Action::ToggleBionicReading, Action::CyclePageTurn,
      Action::SyncProgress,     Action::FileTransfer,        Action::CalibreWireless,
      Action::JoinNetwork,      Action::CreateHotspot,       Action::ToggleDarkMode,
      Action::Footnotes,        Action::FileBrowser,         Action::CreateClipping,
      Action::LookupWord,       Action::ToggleHomeButton,    Action::QuickActions,
      Action::ToggleFrontlight, Action::ToggleTouchscreen,
  };

  for (const auto action : actions) {
    for (const bool releasePowerFirst : {false, true}) {
      ButtonShortcutController controller;

      EXPECT_NE(controller.update(1U, true, true, false, false, action).event, ButtonShortcutController::Event::None);
      const auto firstRelease = controller.update(2U, !releasePowerFirst, releasePowerFirst, false, false, action);
      EXPECT_EQ(firstRelease.event, ButtonShortcutController::Event::None);
      EXPECT_TRUE(firstRelease.consumeInput);
      const auto secondRelease = controller.update(3U, false, false, false, false, action);
      EXPECT_EQ(secondRelease.event, ButtonShortcutController::Event::None);
      EXPECT_TRUE(secondRelease.consumeInput);
      const auto afterRelease = controller.update(4U, false, false, false, false, action);
      EXPECT_EQ(afterRelease.consumeInput, action == Action::QuickLock);
    }
  }
}
