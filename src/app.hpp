#pragma once

#include "audio_out.hpp"
#include "sd_browser.hpp"
#include "player.hpp"
#include "ui.hpp"
#include "input.hpp"
#include "settings.hpp"
#include "types.hpp"
#include "idle_timeout.hpp"
#include "bookmark_checkpoint.hpp"
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
  void handleRecent(Action a);

  void openSettings();
  void closeSettings();
  void applySettings();
  void persistSettings();  // Apply settings. Write the config file immediately.
  bool updateBookmark(uint32_t nowMs);
  bool saveSettings(bool showError);
  void rememberBrowserLocation();
  void flushBrowserLocation(bool showError);
  void restoreBrowserLocation();
  void resumeLastTrack();  // Open the saved track at boot, paused at its saved position.
  void playSelection();
  void playRecent();
  BrowseSnapshot viewSnapshot();
  BrowseSnapshot recentSnapshot();

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
  Screen systemReturn_ = Screen::Browse;
  Screen recentReturn_ = Screen::Browse;
  size_t recentCursor_ = 0;
  DirEntry recentEntries_[cfg::kRecentCount]{};

  uint32_t lastActivityMs_ = 0;
  IdleTimeout idleTimeout_;
  BookmarkCheckpoint bookmarkCheckpoint_;
  bool browserLocationDirty_ = false;
  uint32_t browserLocationChangedAtMs_ = 0;
};
