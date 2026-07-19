#include "settings.hpp"
#include "config.hpp"
#include <cstdio>
#include <cstring>

#ifndef UNIT_TEST
#include <Preferences.h>
static Preferences prefs;
static constexpr const char* kNs = "mp3player";
#endif

void Settings::load() {
  volumePercent_ = cfg::kDefaultVolumePercent;
  brightness_ = cfg::kDisplayBrightness;
  displayTimeoutMs_ = cfg::kDisplayTimeoutMs;
  autoNext_ = true;
  cursor_ = 0;

#ifndef UNIT_TEST
  if (!prefs.begin(kNs, true)) {
    return;
  }
  volumePercent_ = prefs.getInt("vol", volumePercent_);
  brightness_ = prefs.getUChar("bright", brightness_);
  displayTimeoutMs_ = prefs.getUInt("timeout", displayTimeoutMs_);
  autoNext_ = prefs.getBool("autonext", autoNext_);
  prefs.end();

  if (volumePercent_ < 0) volumePercent_ = 0;
  if (volumePercent_ > 100) volumePercent_ = 100;
#endif
}

void Settings::save() const {
#ifndef UNIT_TEST
  if (!prefs.begin(kNs, false)) return;
  prefs.putInt("vol", volumePercent_);
  prefs.putUChar("bright", brightness_);
  prefs.putUInt("timeout", displayTimeoutMs_);
  prefs.putBool("autonext", autoNext_);
  prefs.end();
#endif
}

SettingsSnapshot Settings::snapshot() const {
  SettingsSnapshot s{};
  s.volumePercent = volumePercent_;
  s.brightness = brightness_;
  s.displayTimeoutMs = displayTimeoutMs_;
  s.autoNext = autoNext_;
  s.cursor = cursor_;
  return s;
}

void Settings::setVolumePercent(int v) {
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  volumePercent_ = v;
}

void Settings::setBrightness(uint8_t b) {
  brightness_ = b;
}

void Settings::adjustBrightness(int delta) {
  int v = static_cast<int>(brightness_) + delta;
  if (v < 10) v = 10;
  if (v > 255) v = 255;
  brightness_ = static_cast<uint8_t>(v);
}

void Settings::cycleDisplayTimeout() {
  // 5s, 10s, 30s, 60s, never
  if (displayTimeoutMs_ == 5000) {
    displayTimeoutMs_ = 10000;
  } else if (displayTimeoutMs_ == 10000) {
    displayTimeoutMs_ = 30000;
  } else if (displayTimeoutMs_ == 30000) {
    displayTimeoutMs_ = 60000;
  } else if (displayTimeoutMs_ == 60000) {
    displayTimeoutMs_ = 0;  // never
  } else {
    displayTimeoutMs_ = 5000;
  }
}

void Settings::setAutoNext(bool on) { autoNext_ = on; }

void Settings::toggleAutoNext() { autoNext_ = !autoNext_; }

void Settings::moveCursor(int delta) {
  int c = static_cast<int>(cursor_) + delta;
  if (c < 0) c = static_cast<int>(kCount) - 1;
  if (c >= static_cast<int>(kCount)) c = 0;
  cursor_ = static_cast<size_t>(c);
}

const char* Settings::label(size_t index) const {
  switch (index) {
    case 0: return "Volume";
    case 1: return "Brightness";
    case 2: return "Scr timeout";
    case 3: return "Auto-next";
    default: return "?";
  }
}

void Settings::formatValue(size_t index, char* buf, size_t cap) const {
  if (!buf || cap == 0) return;
  switch (index) {
    case 0:
      snprintf(buf, cap, "%d%%", volumePercent_);
      break;
    case 1:
      // brightness 10..255 → rough percent
      snprintf(buf, cap, "%d%%",
               static_cast<int>((static_cast<int>(brightness_) * 100 + 127) / 255));
      break;
    case 2:
      if (displayTimeoutMs_ == 0) {
        snprintf(buf, cap, "never");
      } else {
        snprintf(buf, cap, "%lus", static_cast<unsigned long>(displayTimeoutMs_ / 1000));
      }
      break;
    case 3:
      snprintf(buf, cap, "%s", autoNext_ ? "ON" : "OFF");
      break;
    default:
      buf[0] = '\0';
      break;
  }
}
