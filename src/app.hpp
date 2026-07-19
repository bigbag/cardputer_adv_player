#pragma once

#include "audio_out.hpp"
#include "sd_browser.hpp"
#include "player.hpp"
#include "ui.hpp"
#include "input.hpp"
#include "types.hpp"
#include <cstdint>

class App {
 public:
  void begin();
  void loop();

 private:
  void handle(Action a);
  void handleBrowse(Action a);
  void handlePlaying(Action a);
  void playSelection();
  void noteActivity(uint32_t nowMs);
  void updateDisplayPower(uint32_t nowMs);

  AudioOut audio_;
  SdBrowser browser_;
  Player player_;
  Ui ui_;
  Input input_;
  Screen screen_ = Screen::Browse;
  uint32_t lastActivityMs_ = 0;
};
