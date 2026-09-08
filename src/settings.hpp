#pragma once

#include "types.hpp"
#include "theme.hpp"
#include <cstdint>

// The SD card stores settings in /.asvmp3/config.cfg.
// App loads settings after the SD mount attempt.
// App requests a save when a setting changes.
class Settings {
 public:
  void load();   // Use defaults if the file or SD card is not available.
  bool save();   // Write /.asvmp3/config.cfg through a checked temporary file.
                 // Keep the previous config as a recovery backup. False on failure.

  SettingsSnapshot snapshot() const;

  int volumePercent() const { return volume_; }
  void setVolumePercent(int v);
  void adjustVolume(int delta);

  uint8_t brightness() const { return brightness_; }
  void setBrightness(uint8_t b);
  void adjustBrightness(int delta);

  uint32_t displayTimeoutMs() const { return displayTimeoutMs_; }
  void cycleDisplayTimeout();

  uint32_t idleTimeoutMs() const { return idleTimeoutMs_; }
  void cycleIdleTimeout(int delta);

  bool autoNext() const { return autoNext_; }
  void setAutoNext(bool on);
  void toggleAutoNext();

  OnBootMode onBoot() const { return onBoot_; }
  void setOnBoot(OnBootMode m);
  void cycleOnBoot(int delta);  // +1 selects Play, Browse, Off. -1 reverses the order.

  size_t themeIndex() const { return themeIndex_; }
  void setThemeIndex(size_t i);
  void cycleTheme(int delta);
  const Theme& theme() const { return themes::get(themeIndex_); }

  // Return the absolute path of the last track, such as "/Music/a.mp3".
  // Return an empty string if no path exists.
  const char* lastPath() const { return lastPath_; }
  void setLastPath(const char* absPath);  // Do nothing if the path does not change.
  // The saved playback position for lastPath_ in milliseconds.
  // setLastPath resets the position when the path changes.
  uint32_t lastPositionMs() const { return lastPositionMs_; }
  void setLastPositionMs(uint32_t positionMs);

  const BrowserLocation& browserLocation() const { return browserLocation_; }
  void setBrowserLocation(const BrowserLocation& location);

  static constexpr size_t kCount = 7;
  size_t cursor() const { return cursor_; }
  void moveCursor(int delta);

  void formatValue(size_t index, char* buf, size_t cap) const;
  const char* label(size_t index) const;

  static constexpr const char* kConfigDir = "/.asvmp3";
  static constexpr const char* kConfigPath = "/.asvmp3/config.cfg";
  static constexpr const char* kConfigTmpPath = "/.asvmp3/config.cfg.tmp";
  static constexpr const char* kConfigBackupPath = "/.asvmp3/config.cfg.bak";
  static constexpr const char* kConfigLegacyPath = "/asvmp3.cfg";

 private:
  void clamp();
  void applyDefaults();
  bool parseLine(const char* line);

  int volume_ = 45;
  uint8_t brightness_ = 128;
  uint32_t displayTimeoutMs_ = 10000;
  uint32_t idleTimeoutMs_ = 0;
  bool autoNext_ = true;
  OnBootMode onBoot_ = OnBootMode::Browse;
  size_t themeIndex_ = 0;
  size_t cursor_ = 0;
  char lastPath_[cfg::kMaxPathLen]{};
  uint32_t lastPositionMs_ = 0;
  BrowserLocation browserLocation_{};
};
