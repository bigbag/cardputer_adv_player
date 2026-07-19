#pragma once

#include "types.hpp"
#include "theme.hpp"
#include <cstdint>

// All settings live in a plain-text file on the SD card (hidden dir):
//   /.asvmp3/config.cfg
// Loaded after SD mount; saved on Settings exit and volume changes.
class Settings {
 public:
  void load();   // uses defaults if file missing / SD not ready
  bool save();   // write /.asvmp3/config.cfg ; false if SD unavailable

  SettingsSnapshot snapshot() const;

  int volumePercent() const { return volume_; }
  void setVolumePercent(int v);
  void adjustVolume(int delta);

  uint8_t brightness() const { return brightness_; }
  void setBrightness(uint8_t b);
  void adjustBrightness(int delta);

  uint32_t displayTimeoutMs() const { return displayTimeoutMs_; }
  void cycleDisplayTimeout();

  bool autoNext() const { return autoNext_; }
  void setAutoNext(bool on);
  void toggleAutoNext();

  OnBootMode onBoot() const { return onBoot_; }
  void setOnBoot(OnBootMode m);
  void cycleOnBoot(int delta);  // +1 / -1 through Play → Browse → Off

  size_t themeIndex() const { return themeIndex_; }
  void setThemeIndex(size_t i);
  void cycleTheme(int delta);
  const Theme& theme() const { return themes::get(themeIndex_); }

  // Absolute SD path of last played audio ("/Music/a.mp3"), or empty.
  const char* lastPath() const { return lastPath_; }
  void setLastPath(const char* absPath);  // no-op if unchanged

  const BrowserLocation& browserLocation() const { return browserLocation_; }
  void setBrowserLocation(const BrowserLocation& location);

  static constexpr size_t kCount = 6;
  size_t cursor() const { return cursor_; }
  void moveCursor(int delta);

  void formatValue(size_t index, char* buf, size_t cap) const;
  const char* label(size_t index) const;

  static constexpr const char* kConfigDir = "/.asvmp3";
  static constexpr const char* kConfigPath = "/.asvmp3/config.cfg";

 private:
  void clamp();
  void applyDefaults();
  bool parseLine(const char* line);

  int volume_ = 45;
  uint8_t brightness_ = 128;
  uint32_t displayTimeoutMs_ = 10000;
  bool autoNext_ = true;
  OnBootMode onBoot_ = OnBootMode::Browse;
  size_t themeIndex_ = 0;
  size_t cursor_ = 0;
  char lastPath_[cfg::kMaxPathLen]{};
  BrowserLocation browserLocation_{};
};
