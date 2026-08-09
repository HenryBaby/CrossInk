#pragma once

#include <HalDisplay.h>

#include <cstdint>

// Keep network/menu transitions on the normal waveform used by upstream
// CrossPoint Reader v1.5.0. The state argument is retained so callers can share the
// helper without carrying transition bookkeeping in each activity.
class ScreenTransitionRefresh {
 public:
  HalDisplay::RefreshMode modeFor(const uint8_t) const { return HalDisplay::FAST_REFRESH; }
};
