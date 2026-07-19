#include "settings.hpp"
#include "config.hpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

#include <SD.h>
#include <FS.h>

// Papyrix-style persistence:
//   mkdir hidden dir → write *.tmp → flush/sync → size check → remove final → rename tmp
// Keeps a half-written file from wiping good settings.

static constexpr const char* kConfigTmp = "/.asvmp3/config.cfg.tmp";

void Settings::applyDefaults() {
  volume_ = cfg::kDefaultVolumePercent;
  brightness_ = cfg::kDisplayBrightness;
  displayTimeoutMs_ = cfg::kDisplayTimeoutMs;
  autoNext_ = true;
  themeIndex_ = 0;
  cursor_ = 0;
  lastPath_[0] = '\0';
}

void Settings::setLastPath(const char* absPath) {
  if (!absPath) absPath = "";
  if (std::strcmp(lastPath_, absPath) == 0) return;
  std::strncpy(lastPath_, absPath, cfg::kMaxPathLen - 1);
  lastPath_[cfg::kMaxPathLen - 1] = '\0';
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
  // Paths need up to kMaxPathLen; other values are short.
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
  } else if (std::strcmp(key, "autonext") == 0) {
    autoNext_ = (std::strcmp(val, "1") == 0 || std::strcmp(val, "true") == 0 ||
                 std::strcmp(val, "on") == 0 || std::strcmp(val, "yes") == 0);
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
  } else {
    return false;
  }
  return true;
}

void Settings::load() {
  applyDefaults();

  // Prefer hidden path; migrate legacy root file if present.
  const char* path = kConfigPath;
  if (!SD.exists(path) && SD.exists("/asvmp3.cfg")) {
    path = "/asvmp3.cfg";
    Serial.println("[cfg] migrating /asvmp3.cfg → /.asvmp3/config.cfg");
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

  Serial.printf("[cfg] loaded %s (%d keys) vol=%d bright=%u timeout=%lu theme=%s autonext=%d\n",
                path, parsed, volume_, brightness_,
                static_cast<unsigned long>(displayTimeoutMs_),
                themes::name(themeIndex_), autoNext_ ? 1 : 0);

  // If we loaded legacy path, rewrite to hidden location.
  if (path != kConfigPath) {
    save();
    SD.remove("/asvmp3.cfg");
  }
}

bool Settings::save() {
  clamp();

  // 1) Ensure hidden directory exists (FAT "hidden" via leading '.').
  if (!SD.exists(kConfigDir)) {
    if (!SD.mkdir(kConfigDir)) {
      Serial.printf("[cfg] SAVE FAIL mkdir %s\n", kConfigDir);
      return false;
    }
    Serial.printf("[cfg] created %s\n", kConfigDir);
  }

  // 2) Write temp file first (never leave a half-written final).
  if (SD.exists(kConfigTmp)) {
    SD.remove(kConfigTmp);
  }

  File f = SD.open(kConfigTmp, FILE_WRITE);
  if (!f) {
    Serial.printf("[cfg] SAVE FAIL open %s\n", kConfigTmp);
    return false;
  }

  // Explicit content — enough bytes that a short write is obvious.
  size_t written = 0;
  auto wr = [&](const char* s) {
    size_t n = std::strlen(s);
    size_t w = f.write(reinterpret_cast<const uint8_t*>(s), n);
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
  std::snprintf(line, sizeof(line), "theme=%s\n", themes::name(themeIndex_));
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "autonext=%s\n", autoNext_ ? "on" : "off");
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "last_path=%s\n", lastPath_);
  ok = ok && wr(line);

  f.flush();
#if defined(ESP32)
  // Best-effort fsync if underlying FsFile exposes it via FILE*
#endif
  const size_t sz = f.size();
  f.close();

  // Minimum plausible size (header + 5 keys).
  constexpr size_t kMinBytes = 40;
  if (!ok || written < kMinBytes || sz < kMinBytes) {
    Serial.printf("[cfg] SAVE FAIL short write %u/%u — discard tmp\n",
                  static_cast<unsigned>(sz), static_cast<unsigned>(kMinBytes));
    SD.remove(kConfigTmp);
    return false;
  }

  // 3) Commit: remove stale final, rename tmp → final (SdFat/SD can't always rename-over).
  if (SD.exists(kConfigPath)) {
    if (!SD.remove(kConfigPath)) {
      Serial.printf("[cfg] SAVE FAIL remove old %s\n", kConfigPath);
      SD.remove(kConfigTmp);
      return false;
    }
  }
  if (!SD.rename(kConfigTmp, kConfigPath)) {
    // Fallback: copy if rename unsupported
    Serial.println("[cfg] rename failed — copy fallback");
    File src = SD.open(kConfigTmp, FILE_READ);
    File dst = SD.open(kConfigPath, FILE_WRITE);
    if (!src || !dst) {
      if (src) src.close();
      if (dst) dst.close();
      SD.remove(kConfigTmp);
      Serial.println("[cfg] SAVE FAIL copy fallback");
      return false;
    }
    uint8_t buf[64];
    while (src.available()) {
      int n = src.read(buf, sizeof(buf));
      if (n <= 0) break;
      dst.write(buf, n);
    }
    dst.flush();
    dst.close();
    src.close();
    SD.remove(kConfigTmp);
  }

  Serial.printf("[cfg] saved %s vol=%d bright=%u timeout=%lu theme=%s autonext=%s (%u bytes)\n",
                kConfigPath, volume_, static_cast<unsigned>(brightness_),
                static_cast<unsigned long>(displayTimeoutMs_),
                themes::name(themeIndex_), autoNext_ ? "on" : "off",
                static_cast<unsigned>(sz));
  return true;
}

SettingsSnapshot Settings::snapshot() const {
  SettingsSnapshot s{};
  s.volumePercent = volume_;
  s.brightness = brightness_;
  s.displayTimeoutMs = displayTimeoutMs_;
  s.autoNext = autoNext_;
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

void Settings::setAutoNext(bool on) { autoNext_ = on; }
void Settings::toggleAutoNext() { autoNext_ = !autoNext_; }

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
    default:
      buf[0] = '\0';
      break;
  }
}
