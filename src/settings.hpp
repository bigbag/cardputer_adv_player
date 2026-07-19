#pragma once

#include "types.hpp"
#include "theme.hpp"
#include <cstdint>

class Settings {
 public:
  void load();
  void save() const;

  SettingsSnapshot snapshot() const;

  int volumePercent() const { return volume_; }
  void setVolumePercent(int v);
  void adjustVolume(int delta);

  OutputRoute route() const { return route_; }
  void setRoute(OutputRoute r);
  void toggleRoute();

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

  // Theme, Output, Volume, Brightness, Scr timeout, Auto-next
  static constexpr size_t kCount = 6;
  size_t cursor() const { return cursor_; }
  void moveCursor(int delta);

  void formatValue(size_t index, char* buf, size_t cap) const;
  const char* label(size_t index) const;

 private:
  int volume_ = 50;
  OutputRoute route_ = OutputRoute::Speaker;
  uint8_t brightness_ = 128;
  uint32_t displayTimeoutMs_ = 10000;
  bool autoNext_ = true;
  size_t themeIndex_ = 0;
  size_t cursor_ = 0;
};
