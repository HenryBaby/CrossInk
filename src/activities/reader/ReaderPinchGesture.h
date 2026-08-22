#pragma once

#include <cmath>
#include <cstdint>

// Fixed-size one-step recognizer. It is intentionally independent from input,
// fonts, and time so it can be host-tested without reader activity state.
class ReaderPinchGesture {
 public:
  enum class Action : uint8_t { None, Increase, Decrease };

  static constexpr uint8_t MIN_DISTANCE_CHANGE_PX = 20;
  static constexpr uint8_t SCALE_PERCENT = 110;

  bool isActive() const { return active_; }

  void reset() {
    active_ = false;
    stepApplied_ = false;
    translationLocked_ = false;
    startDistanceSq_ = 0;
    startCenterXTwice_ = 0;
    startCenterYTwice_ = 0;
  }

  // Returns exactly one action per two-contact sequence. Call reset when the
  // touch controller reports fewer than two contacts.
  Action update(const int x1, const int y1, const int x2, const int y2) {
    const uint32_t distanceSq = separationSq(x1, y1, x2, y2);
    if (!active_) {
      active_ = true;
      stepApplied_ = false;
      translationLocked_ = false;
      startDistanceSq_ = distanceSq;
      startCenterXTwice_ = static_cast<int64_t>(x1) + x2;
      startCenterYTwice_ = static_cast<int64_t>(y1) + y2;
      return Action::None;
    }
    if (stepApplied_ || translationLocked_) return Action::None;

    const uint32_t startDistance = distanceFromSq(startDistanceSq_);
    const uint32_t currentDistance = distanceFromSq(distanceSq);
    const uint32_t distanceChange =
        startDistance > currentDistance ? startDistance - currentDistance : currentDistance - startDistance;
    const int64_t centerDeltaX = static_cast<int64_t>(x1) + x2 - startCenterXTwice_;
    const int64_t centerDeltaY = static_cast<int64_t>(y1) + y2 - startCenterYTwice_;
    const uint64_t centerTravelTwiceSq = centerDeltaX * centerDeltaX + centerDeltaY * centerDeltaY;
    const uint64_t distanceChangeSq = static_cast<uint64_t>(distanceChange) * distanceChange;

    // A swipe moves the two-contact midpoint farther than it changes the
    // contact separation. Once that evidence is clear, keep the sequence out
    // of pinch handling until both contacts leave the screen.
    // centerDelta* is twice the midpoint travel, so both comparisons scale the
    // right-hand side by 2^2 rather than halving (and rounding) the deltas.
    if (centerTravelTwiceSq > 4 * distanceChangeSq &&
        centerTravelTwiceSq >= 4 * static_cast<uint64_t>(MIN_DISTANCE_CHANGE_PX) * MIN_DISTANCE_CHANGE_PX) {
      translationLocked_ = true;
      return Action::None;
    }
    if (distanceChange < MIN_DISTANCE_CHANGE_PX) return Action::None;

    constexpr uint64_t scaleSquared = static_cast<uint64_t>(SCALE_PERCENT) * SCALE_PERCENT;
    constexpr uint64_t percentSquared = 100ULL * 100ULL;

    const uint64_t current = distanceSq;
    const uint64_t start = startDistanceSq_;
    if (current > start && current * percentSquared >= start * scaleSquared) {
      stepApplied_ = true;
      return Action::Increase;
    }
    if (current < start && current * scaleSquared <= start * percentSquared) {
      stepApplied_ = true;
      return Action::Decrease;
    }
    return Action::None;
  }

 private:
  static uint32_t separationSq(const int x1, const int y1, const int x2, const int y2) {
    const int64_t dx = static_cast<int64_t>(x2) - x1;
    const int64_t dy = static_cast<int64_t>(y2) - y1;
    return static_cast<uint32_t>(dx * dx + dy * dy);
  }

  static uint32_t distanceFromSq(const uint32_t distanceSq) {
    return static_cast<uint32_t>(std::sqrt(static_cast<double>(distanceSq)));
  }

  bool active_ = false;
  bool stepApplied_ = false;
  bool translationLocked_ = false;
  uint32_t startDistanceSq_ = 0;
  int64_t startCenterXTwice_ = 0;
  int64_t startCenterYTwice_ = 0;
};
