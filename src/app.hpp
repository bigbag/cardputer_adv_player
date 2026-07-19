#pragma once

#include "audio_out.hpp"
#include "sd_browser.hpp"
#include "player.hpp"
#include "ui.hpp"
#include "input.hpp"
#include "settings.hpp"
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
  void handleSettings(Action a);
  void openSettings();
  void closeSettings();
  void applySettings();
  void persistSettings();  // apply + write SD immediately
  void playSelection();
  void noteActivity(uint32_t nowMs);
  void updateDisplayPower(uint32_t nowMs);

  AudioOut audio_;
  SdBrowser browser_;
  Player player_;
  Settings settings_;
  Ui ui_;
  Input input_;
  Screen screen_ = Screen::Browse;
  Screen settingsReturn_ = Screen::Browse;
  uint32_t lastActivityMs_ = 0;
};
