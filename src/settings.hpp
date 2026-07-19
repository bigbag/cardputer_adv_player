#pragma once

#include "types.hpp"
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
  // Cycle: 5s → 10s → 30s → 60s → never(0) → 5s …
  void cycleDisplayTimeout();

  bool autoNext() const { return autoNext_; }
  void setAutoNext(bool on);
  void toggleAutoNext();

  // Setting rows for UI (0..kSettingCount-1)
  static constexpr size_t kCount = 4;
  size_t cursor() const { return cursor_; }
  void moveCursor(int delta);

  // Human labels for current values
  void formatValue(size_t index, char* buf, size_t cap) const;
  const char* label(size_t index) const;

 private:
  int volumePercent_ = 40;
  uint8_t brightness_ = 128;
  uint32_t displayTimeoutMs_ = 10000;
  bool autoNext_ = true;
  size_t cursor_ = 0;
};
