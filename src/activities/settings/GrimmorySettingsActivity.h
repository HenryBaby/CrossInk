#pragma once
#include "GrimmoryStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
class GrimmorySettingsActivity final : public Activity {
 public:
  GrimmorySettingsActivity(GfxRenderer& r, MappedInputManager& i) : Activity("GrimmorySettings", r, i) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator nav;
  GrimmoryConfig edit;
  size_t selected = 0;
  bool error = false;
  void editField(size_t index);
  void save();
  size_t getMenuItemCount() const;
};
