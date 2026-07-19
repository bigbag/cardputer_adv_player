#pragma once

#include "types.hpp"
#include "theme.hpp"
#include <cstdint>

// Persistent app settings (NVS when available).
class Settings {
 public:
  void load();
  void save() const;

  SettingsSnapshot snapshot() const;

  int volumePercent() const { return volumePercent_; }
  void setVolumePercent(int v);

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

  // Setting rows: Volume, Brightness, Scr timeout, Auto-next, Theme
  static constexpr size_t kCount = 5;
  size_t cursor() const { return cursor_; }
  void moveCursor(int delta);

  void formatValue(size_t index, char* buf, size_t cap) const;
  const char* label(size_t index) const;

 private:
  int volumePercent_ = 40;
  uint8_t brightness_ = 128;
  uint32_t displayTimeoutMs_ = 10000;
  bool autoNext_ = true;
  size_t themeIndex_ = 0;
  size_t cursor_ = 0;
};
