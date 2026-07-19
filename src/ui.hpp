#pragma once
#include "types.hpp"
#include <cstdint>

class Ui {
 public:
  void begin();
  void showToast(const char* text, uint32_t nowMs);
  void render(Screen screen,
              const BrowseSnapshot& browse,
              const PlayerSnapshot& player,
              uint32_t nowMs);

 private:
  void drawBrowse(const BrowseSnapshot& b);
  void drawPlaying(const PlayerSnapshot& p);
  void drawHint(const char* text);
  void drawToastIfAny(uint32_t nowMs);

  Toast toast_{};
  Screen last_{Screen::Browse};
};
