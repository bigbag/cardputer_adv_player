#pragma once

#include "audio_out.hpp"
#include "sd_browser.hpp"
#include "player.hpp"
#include "ui.hpp"
#include "input.hpp"
#include "settings.hpp"
#include "types.hpp"
#include "idle_timeout.hpp"
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
  void persistSettings();  // Apply settings. Write the config file immediately.
  void rememberLastPath(const char* absPath);  // Set last_path and request a save.
  void rememberBrowserLocation();
  void flushBrowserLocation(bool showError);
  void restoreBrowserLocation();
  void resumeLastTrack();  // Play the saved track at boot if the mode permits playback.
  void playSelection();
  void noteActivity(uint32_t nowMs);
  void updateDisplayPower(uint32_t nowMs);
  void updateIdlePower(uint32_t nowMs, PlayState state);

  AudioOut audio_;
  SdBrowser browser_;
  Player player_;
  Settings settings_;
  Ui ui_;
  Input input_;
  Screen screen_ = Screen::Browse;
  Screen settingsReturn_ = Screen::Browse;
  uint32_t lastActivityMs_ = 0;
  IdleTimeout idleTimeout_;
  bool browserLocationDirty_ = false;
  uint32_t browserLocationChangedAtMs_ = 0;
};
