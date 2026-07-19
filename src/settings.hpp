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

  size_t themeIndex() const { return themeIndex_; }
  void setThemeIndex(size_t i);
  void cycleTheme(int delta);
  const Theme& theme() const { return themes::get(themeIndex_); }

  static constexpr size_t kCount = 5;
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
  size_t themeIndex_ = 0;
  size_t cursor_ = 0;
};
