#pragma once

class GfxRenderer;
class HalGPIO;

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down };
  enum class SwipeDir { None, Left, Right, Up, Down };
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO&, const GfxRenderer& renderer) : renderer(renderer) {}

  const GfxRenderer& getRenderer() const { return renderer; }

  bool wasScreenTouchDown(int& x, int& y) const {
    if (!touchDown) return false;
    x = touchX;
    y = touchY;
    touchDown = false;
    return true;
  }

  bool wasScreenTapped(int& x, int& y) const {
    if (!touchRelease) return false;
    touchRelease = false;
    if (suppressTouchTap) {
      suppressTouchTap = false;
      return false;
    }
    x = touchX;
    y = touchY;
    return true;
  }

  SwipeDir wasSwipe() const { return SwipeDir::None; }
  bool wasPressed(Button) const { return false; }
  bool wasReleased(Button) const { return false; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const {
    return {back, confirm, previous, next};
  }

  void suppressNextTouchTap() { suppressTouchTap = true; }
  void suppressNextConfirmRelease() {}
  void suppressNextBackRelease() {}

  void injectTouchDown(const int x, const int y) {
    touchX = x;
    touchY = y;
    touchDown = true;
  }

  void injectTouchRelease(const int x, const int y) {
    touchX = x;
    touchY = y;
    touchRelease = true;
  }

 private:
  const GfxRenderer& renderer;
  mutable bool touchDown = false;
  mutable bool touchRelease = false;
  mutable bool suppressTouchTap = false;
  mutable int touchX = 0;
  mutable int touchY = 0;
};
