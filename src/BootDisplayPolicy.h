#pragma once

#include <cstdint>

// How the device is coming back to life, resolved once at boot.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  Network,         // minimal boot directly into a memory-intensive network activity
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

// X3 can resume differentially only when its saved framebuffer is available to
// restore the controller baseline. Ordinary sleep screens remain physically on
// the panel but have no saved frame, so arm the driver's clean first refresh.
constexpr bool useSeamlessDisplayInit(BootResume resume, bool isX3, bool hasSavedSleepFrame) {
  if (resume == BootResume::Splash) return false;
  return !(resume == BootResume::SplashlessWake && isX3 && !hasSavedSleepFrame);
}
