#pragma once

class HalGPIO {
 public:
  bool hasTouch() const { return false; }
};

inline HalGPIO gpio;
