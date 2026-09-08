#include "settings.hpp"
#include "config.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cerrno>

#include <SD.h>
#include <FS.h>

// Save settings through a temporary file.
// Create the directory if necessary.
// Write the temporary file and check the exact byte count and final size.
// Rename the current config to the backup before promoting the temporary file.
// A failed promotion restores the backup, so a complete config always survives.
// Load the canonical config, else the backup, else the legacy root file.
static constexpr uint32_t kIdleTimeouts[] = {0, 300000, 1800000, 3600000};

void Settings::applyDefaults() {
  volume_ = cfg::kDefaultVolumePercent;
  brightness_ = cfg::kDisplayBrightness;
  displayTimeoutMs_ = cfg::kDisplayTimeoutMs;
  idleTimeoutMs_ = 0;
  autoNext_ = true;
  onBoot_ = OnBootMode::Browse;
  themeIndex_ = 0;
  cursor_ = 0;
  lastPath_[0] = '\0';
  lastPositionMs_ = 0;
  browserLocation_ = {};
}

void Settings::setLastPath(const char* absPath) {
  if (!absPath) absPath = "";
  if (std::strcmp(lastPath_, absPath) == 0) return;
  std::strncpy(lastPath_, absPath, cfg::kMaxPathLen - 1);
  lastPath_[cfg::kMaxPathLen - 1] = '\0';
  lastPositionMs_ = 0;  // A new file never keeps the previous position.
}

void Settings::setLastPositionMs(uint32_t positionMs) { lastPositionMs_ = positionMs; }

void Settings::setBrowserLocation(const BrowserLocation& location) {
  if (location.path[0] == '/') {
    std::strncpy(browserLocation_.path, location.path,
                 sizeof(browserLocation_.path) - 1);
  } else {
    browserLocation_.path[0] = '\0';
  }
  browserLocation_.path[sizeof(browserLocation_.path) - 1] = '\0';
  std::strncpy(browserLocation_.item, location.item,
               sizeof(browserLocation_.item) - 1);
  browserLocation_.item[sizeof(browserLocation_.item) - 1] = '\0';
}

void Settings::clamp() {
  if (volume_ < 0) volume_ = 0;
  if (volume_ > 100) volume_ = 100;
  if (brightness_ < 10) brightness_ = 10;
  if (themeIndex_ >= themes::kCount) themeIndex_ = 0;
}

bool Settings::parseLine(const char* line) {
  while (*line && std::isspace(static_cast<unsigned char>(*line))) ++line;
  if (*line == '\0' || *line == '#' || *line == ';') return false;

  const char* eq = std::strchr(line, '=');
  if (!eq) return false;

  char key[24];
  // Paths need space for kMaxPathLen bytes. Other values are short.
  char val[cfg::kMaxPathLen];
  size_t klen = static_cast<size_t>(eq - line);
  while (klen > 0 && std::isspace(static_cast<unsigned char>(line[klen - 1]))) --klen;
  if (klen == 0 || klen >= sizeof(key)) return false;
  std::memcpy(key, line, klen);
  key[klen] = '\0';

  const char* v = eq + 1;
  while (*v && std::isspace(static_cast<unsigned char>(*v))) ++v;
  size_t vlen = std::strlen(v);
  while (vlen > 0 && std::isspace(static_cast<unsigned char>(v[vlen - 1]))) --vlen;
  if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
  std::memcpy(val, v, vlen);
  val[vlen] = '\0';

  for (char* p = key; *p; ++p) {
    *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
  }

  if (std::strcmp(key, "volume") == 0 || std::strcmp(key, "vol") == 0) {
    volume_ = std::atoi(val);
  } else if (std::strcmp(key, "brightness") == 0 || std::strcmp(key, "bright") == 0) {
    brightness_ = static_cast<uint8_t>(std::atoi(val));
  } else if (std::strcmp(key, "timeout") == 0 || std::strcmp(key, "timeout_ms") == 0) {
    displayTimeoutMs_ = static_cast<uint32_t>(std::strtoul(val, nullptr, 10));
  } else if (std::strcmp(key, "idle_timeout_ms") == 0) {
    char* end = nullptr;
    errno = 0;
    const unsigned long timeout = std::strtoul(val, &end, 10);
    idleTimeoutMs_ = 0;
    if (errno == 0 && std::isdigit(static_cast<unsigned char>(val[0])) &&
        end != val && *end == '\0') {
      for (uint32_t option : kIdleTimeouts) {
        if (timeout == option) idleTimeoutMs_ = option;
      }
    }
  } else if (std::strcmp(key, "autonext") == 0) {
    autoNext_ = (std::strcmp(val, "1") == 0 || std::strcmp(val, "true") == 0 ||
                 std::strcmp(val, "on") == 0 || std::strcmp(val, "yes") == 0);
  } else if (std::strcmp(key, "on_boot") == 0 || std::strcmp(key, "onboot") == 0) {
    for (char* p = val; *p; ++p) {
      *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    }
    if (std::strcmp(val, "browse") == 0 || std::strcmp(val, "reveal") == 0) {
      onBoot_ = OnBootMode::Browse;
    } else if (std::strcmp(val, "off") == 0 || std::strcmp(val, "none") == 0 ||
               std::strcmp(val, "false") == 0 || std::strcmp(val, "0") == 0) {
      onBoot_ = OnBootMode::Off;
    } else {
      // "play", "resume", and unknown values select Play for legacy compatibility.
      onBoot_ = OnBootMode::Play;
    }
  } else if (std::strcmp(key, "theme") == 0) {
    if (std::isdigit(static_cast<unsigned char>(val[0]))) {
      themeIndex_ = static_cast<size_t>(std::atoi(val));
    } else {
      for (size_t i = 0; i < themes::kCount; ++i) {
        const char* n = themes::name(i);
        bool match = true;
        for (size_t k = 0;; ++k) {
          char a = val[k];
          char b = n[k];
          if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
          if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
          if (a != b) {
            match = false;
            break;
          }
          if (a == '\0') break;
        }
        if (match) {
          themeIndex_ = i;
          break;
        }
      }
    }
  } else if (std::strcmp(key, "last_path") == 0 || std::strcmp(key, "lastpath") == 0) {
    if (val[0] == '/') {
      std::strncpy(lastPath_, val, cfg::kMaxPathLen - 1);
      lastPath_[cfg::kMaxPathLen - 1] = '\0';
    } else {
      lastPath_[0] = '\0';
    }
  } else if (std::strcmp(key, "last_position_ms") == 0) {
    // Accept digits only. A sign, trailing text, or overflow keeps the old value.
    char* end = nullptr;
    errno = 0;
    const unsigned long value = std::strtoul(val, &end, 10);
    const bool valid = errno == 0 && end != val && *end == '\0' &&
                       std::isdigit(static_cast<unsigned char>(val[0])) &&
                       value <= 0xFFFFFFFFul;
    if (valid) lastPositionMs_ = static_cast<uint32_t>(value);
  } else if (std::strcmp(key, "browser_path") == 0) {
    if (val[0] == '/') {
      std::strncpy(browserLocation_.path, val, sizeof(browserLocation_.path) - 1);
      browserLocation_.path[sizeof(browserLocation_.path) - 1] = '\0';
    } else {
      browserLocation_.path[0] = '\0';
    }
  } else if (std::strcmp(key, "browser_item") == 0) {
    std::strncpy(browserLocation_.item, val, sizeof(browserLocation_.item) - 1);
    browserLocation_.item[sizeof(browserLocation_.item) - 1] = '\0';
  } else {
    return false;
  }
  return true;
}

void Settings::load() {
  applyDefaults();

  // Load the canonical config. Without it, prefer the recovery backup over
  // the legacy root file. A temporary file is never a config.
  const char* path = kConfigPath;
  bool migrating = false;
  if (!SD.exists(kConfigPath)) {
    if (SD.exists(kConfigBackupPath)) {
      path = kConfigBackupPath;
      Serial.println("[cfg] config missing — loading recovery backup");
    } else if (SD.exists(kConfigLegacyPath)) {
      path = kConfigLegacyPath;
      migrating = true;
      Serial.println("[cfg] migrating /asvmp3.cfg → /.asvmp3/config.cfg");
    }
  }

  if (!SD.exists(path)) {
    Serial.printf("[cfg] no %s — defaults (will create on save)\n", kConfigPath);
    return;
  }

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[cfg] open read failed %s\n", path);
    return;
  }

  char line[cfg::kMaxPathLen + 24];
  size_t n = 0;
  int parsed = 0;
  while (f.available()) {
    int c = f.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c == '\n' || n + 1 >= sizeof(line)) {
      line[n] = '\0';
      if (n > 0 && parseLine(line)) ++parsed;
      n = 0;
      if (c != '\n') {
        while (f.available()) {
          int d = f.read();
          if (d == '\n' || d < 0) break;
        }
      }
      continue;
    }
    line[n++] = static_cast<char>(c);
  }
  if (n > 0) {
    line[n] = '\0';
    if (parseLine(line)) ++parsed;
  }
  f.close();
  clamp();

  const char* bootStr = "play";
  if (onBoot_ == OnBootMode::Browse) bootStr = "browse";
  else if (onBoot_ == OnBootMode::Off) bootStr = "off";
  Serial.printf("[cfg] loaded %s (%d keys) vol=%d bright=%u timeout=%lu theme=%s autonext=%d on_boot=%s\n",
                path, parsed, volume_, brightness_,
                static_cast<unsigned long>(displayTimeoutMs_),
                themes::name(themeIndex_), autoNext_ ? 1 : 0, bootStr);

  // Remove the legacy file only after the new config is complete on the card.
  // A failed save keeps it for the next boot.
  if (migrating) {
    if (save()) {
      SD.remove(kConfigLegacyPath);
    } else {
      Serial.println("[cfg] migration save failed — keeping /asvmp3.cfg");
    }
  }
}

bool Settings::save() {
  clamp();

  // Create the config directory. The browser skips names that start with ".".
  if (!SD.exists(kConfigDir)) {
    if (!SD.mkdir(kConfigDir)) {
      Serial.printf("[cfg] SAVE FAIL mkdir %s\n", kConfigDir);
      return false;
    }
    Serial.printf("[cfg] created %s\n", kConfigDir);
  }

  // Write the temporary file before replacing the config file.
  if (SD.exists(kConfigTmpPath)) {
    SD.remove(kConfigTmpPath);
  }

  File f = SD.open(kConfigTmpPath, FILE_WRITE);
  if (!f) {
    Serial.printf("[cfg] SAVE FAIL open %s\n", kConfigTmpPath);
    return false;
  }

  // Track the exact byte count to detect an incomplete write.
  size_t written = 0;
  size_t expected = 0;
  auto wr = [&](const char* s) {
    const size_t n = std::strlen(s);
    expected += n;
    const size_t w = f.write(reinterpret_cast<const uint8_t*>(s), n);
    written += w;
    return w == n;
  };

  char line[cfg::kMaxPathLen + 24];
  bool ok = true;
  ok = ok && wr("# asvmp3 settings\n");
  std::snprintf(line, sizeof(line), "volume=%d\n", volume_);
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "brightness=%u\n", static_cast<unsigned>(brightness_));
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "timeout_ms=%lu\n",
                static_cast<unsigned long>(displayTimeoutMs_));
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "idle_timeout_ms=%lu\n",
                static_cast<unsigned long>(idleTimeoutMs_));
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "theme=%s\n", themes::name(themeIndex_));
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "autonext=%s\n", autoNext_ ? "on" : "off");
  ok = ok && wr(line);
  const char* bootStr = "play";
  if (onBoot_ == OnBootMode::Browse) bootStr = "browse";
  else if (onBoot_ == OnBootMode::Off) bootStr = "off";
  std::snprintf(line, sizeof(line), "on_boot=%s\n", bootStr);
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "last_path=%s\n", lastPath_);
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "last_position_ms=%lu\n",
                static_cast<unsigned long>(lastPositionMs_));
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "browser_path=%s\n", browserLocation_.path);
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "browser_item=%s\n", browserLocation_.item);
  ok = ok && wr(line);

  f.flush();
  const size_t sz = f.size();
  f.close();

  // Exact counts and final size prove the temporary file is complete.
  if (!ok || written != expected || sz != expected) {
    Serial.printf("[cfg] SAVE FAIL short write %u/%u — discard tmp\n",
                  static_cast<unsigned>(sz), static_cast<unsigned>(expected));
    SD.remove(kConfigTmpPath);
    return false;
  }

  // Keep the current config as the recovery backup while promoting the new one.
  const bool hadMain = SD.exists(kConfigPath);
  if (hadMain) {
    if (SD.exists(kConfigBackupPath)) SD.remove(kConfigBackupPath);
    if (!SD.rename(kConfigPath, kConfigBackupPath)) {
      Serial.printf("[cfg] SAVE FAIL backup rename %s\n", kConfigPath);
      SD.remove(kConfigTmpPath);
      return false;
    }
  }
  if (!SD.rename(kConfigTmpPath, kConfigPath)) {
    Serial.println("[cfg] SAVE FAIL promotion — restoring backup");
    SD.remove(kConfigTmpPath);
    if (hadMain) SD.rename(kConfigBackupPath, kConfigPath);
    return false;
  }
  if (hadMain) SD.remove(kConfigBackupPath);

  Serial.printf("[cfg] saved %s vol=%d bright=%u timeout=%lu theme=%s autonext=%s on_boot=%s (%u bytes)\n",
                kConfigPath, volume_, static_cast<unsigned>(brightness_),
                static_cast<unsigned long>(displayTimeoutMs_),
                themes::name(themeIndex_), autoNext_ ? "on" : "off", bootStr,
                static_cast<unsigned>(sz));
  return true;
}

SettingsSnapshot Settings::snapshot() const {
  SettingsSnapshot s{};
  s.volumePercent = volume_;
  s.brightness = brightness_;
  s.displayTimeoutMs = displayTimeoutMs_;
  s.idleTimeoutMs = idleTimeoutMs_;
  s.autoNext = autoNext_;
  s.onBoot = onBoot_;
  s.themeIndex = themeIndex_;
  s.cursor = cursor_;
  return s;
}

void Settings::setVolumePercent(int v) {
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  volume_ = v;
}

void Settings::adjustVolume(int delta) { setVolumePercent(volume_ + delta); }

void Settings::setBrightness(uint8_t b) { brightness_ = b; }

void Settings::adjustBrightness(int delta) {
  int v = static_cast<int>(brightness_) + delta;
  if (v < 10) v = 10;
  if (v > 255) v = 255;
  brightness_ = static_cast<uint8_t>(v);
}

void Settings::cycleDisplayTimeout() {
  if (displayTimeoutMs_ == 5000) {
    displayTimeoutMs_ = 10000;
  } else if (displayTimeoutMs_ == 10000) {
    displayTimeoutMs_ = 30000;
  } else if (displayTimeoutMs_ == 30000) {
    displayTimeoutMs_ = 60000;
  } else if (displayTimeoutMs_ == 60000) {
    displayTimeoutMs_ = 0;
  } else {
    displayTimeoutMs_ = 5000;
  }
}

void Settings::cycleIdleTimeout(int delta) {
  constexpr int count = sizeof(kIdleTimeouts) / sizeof(kIdleTimeouts[0]);
  int index = 0;
  while (index < count - 1 && kIdleTimeouts[index] != idleTimeoutMs_) ++index;
  index = (index + (delta < 0 ? count - 1 : 1)) % count;
  idleTimeoutMs_ = kIdleTimeouts[index];
}

void Settings::setAutoNext(bool on) { autoNext_ = on; }
void Settings::toggleAutoNext() { autoNext_ = !autoNext_; }

void Settings::setOnBoot(OnBootMode m) { onBoot_ = m; }

void Settings::cycleOnBoot(int delta) {
  // Cycle order: Play (0), Browse (1), Off (2).
  int i = static_cast<int>(onBoot_) + delta;
  constexpr int n = 3;
  while (i < 0) i += n;
  while (i >= n) i -= n;
  onBoot_ = static_cast<OnBootMode>(i);
}

void Settings::setThemeIndex(size_t i) {
  if (i >= themes::kCount) i = 0;
  themeIndex_ = i;
}

void Settings::cycleTheme(int delta) {
  int i = static_cast<int>(themeIndex_) + delta;
  while (i < 0) i += static_cast<int>(themes::kCount);
  while (i >= static_cast<int>(themes::kCount)) i -= static_cast<int>(themes::kCount);
  themeIndex_ = static_cast<size_t>(i);
}

void Settings::moveCursor(int delta) {
  int c = static_cast<int>(cursor_) + delta;
  if (c < 0) c = static_cast<int>(kCount) - 1;
  if (c >= static_cast<int>(kCount)) c = 0;
  cursor_ = static_cast<size_t>(c);
}

const char* Settings::label(size_t index) const {
  switch (index) {
    case 0: return "Theme";
    case 1: return "Volume";
    case 2: return "Brightness";
    case 3: return "Scr timeout";
    case 4: return "Auto-next";
    case 5: return "On boot";
    case 6: return "Idle off";
    default: return "?";
  }
}

void Settings::formatValue(size_t index, char* buf, size_t cap) const {
  if (!buf || cap == 0) return;
  switch (index) {
    case 0:
      snprintf(buf, cap, "%s", themes::name(themeIndex_));
      break;
    case 1:
      snprintf(buf, cap, "%d%%", volume_);
      break;
    case 2:
      snprintf(buf, cap, "%d%%",
               static_cast<int>((static_cast<int>(brightness_) * 100 + 127) / 255));
      break;
    case 3:
      if (displayTimeoutMs_ == 0) {
        snprintf(buf, cap, "never");
      } else {
        snprintf(buf, cap, "%lus", static_cast<unsigned long>(displayTimeoutMs_ / 1000));
      }
      break;
    case 4:
      snprintf(buf, cap, "%s", autoNext_ ? "ON" : "OFF");
      break;
    case 5:
      if (onBoot_ == OnBootMode::Browse) {
        snprintf(buf, cap, "browse");
      } else if (onBoot_ == OnBootMode::Off) {
        snprintf(buf, cap, "off");
      } else {
        snprintf(buf, cap, "play");
      }
      break;
    case 6:
      if (idleTimeoutMs_ == 0) {
        snprintf(buf, cap, "never");
      } else {
        snprintf(buf, cap, "%lum", static_cast<unsigned long>(idleTimeoutMs_ / 60000));
      }
      break;
    default:
      buf[0] = '\0';
      break;
  }
}
