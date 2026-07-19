#include "sd_browser.hpp"
#include "path_utils.hpp"
#include "config.hpp"

#include <SD.h>
#include <SPI.h>
#include <cstring>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstdio>

// ESP32 Arduino SD mounts at this VFS path (SD.begin default).
static constexpr const char* kSdMount = "/sd";

static int cmpInsensitive(const char* a, const char* b) {
  while (*a && *b) {
    int ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
    int cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
    if (ca != cb) return ca - cb;
    ++a;
    ++b;
  }
  return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

static bool isAudioName(const char* name) {
  return path::hasExtInsensitive(name, ".mp3") || path::hasExtInsensitive(name, ".wav");
}

// Build VFS absolute path: "/sd" + path_  (path_ always starts with '/').
static bool makeVfsPath(char* out, size_t cap, const char* relPath) {
  if (!out || cap < 4 || !relPath) return false;
  if (relPath[0] == '/' && relPath[1] == '\0') {
    // root
    std::snprintf(out, cap, "%s", kSdMount);
    return true;
  }
  // "/sd" + "/Music/foo"
  int n = std::snprintf(out, cap, "%s%s", kSdMount, relPath);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool SdBrowser::begin() {
  pinMode(cfg::kSdCs, OUTPUT);
  digitalWrite(cfg::kSdCs, HIGH);
  SPI.begin(cfg::kSdSck, cfg::kSdMiso, cfg::kSdMosi, cfg::kSdCs);

  // Try slow first (most reliable), then step up if the card accepts it.
  static const uint32_t kSpeeds[] = {
      cfg::kSdSpiHz,  // 4 MHz default
      10000000u,
      15000000u,
  };

  sdOk_ = false;
  for (uint32_t hz : kSpeeds) {
    SD.end();
    delay(20);
    if (SD.begin(cfg::kSdCs, SPI, hz)) {
      sdOk_ = true;
      Serial.printf("[sd] mount ok @ %lu Hz (vfs %s)\n",
                    static_cast<unsigned long>(hz), kSdMount);
      break;
    }
    Serial.printf("[sd] mount fail @ %lu Hz\n", static_cast<unsigned long>(hz));
  }

  if (sdOk_) {
    std::strcpy(path_, "/");
    // Caller (App) lists once after begin — avoid double scan.
  } else {
    Serial.println("[sd] No card or unsupported FS (need FAT16/FAT32, not exFAT)");
  }
  return sdOk_;
}

bool SdBrowser::remount() {
  SD.end();
  sdOk_ = false;
  return begin();
}

bool SdBrowser::sdOk() const { return sdOk_; }

bool SdBrowser::listCurrent() {
  count_ = 0;
  truncated_ = false;
  cursor_ = 0;
  scroll_ = 0;
  if (!sdOk_) return false;

  const uint32_t t0 = millis();

  // Fast path: POSIX readdir — no per-entry File open/stat heap (unlike openNextFile).
  // FATFS already caches directory sectors; "batching" here means draining the
  // dir stream in one pass without opening each child inode.
  char vfsPath[cfg::kMaxPathLen + 8];
  if (!makeVfsPath(vfsPath, sizeof(vfsPath), path_)) {
    Serial.println("[sd] path too long");
    return false;
  }

  DIR* dir = ::opendir(vfsPath);
  if (!dir) {
    // Fallback: Arduino SD API (slower — opens every entry).
    Serial.printf("[sd] opendir(%s) failed — Arduino fallback\n", vfsPath);
    return listCurrentArduino();
  }

  // Scratch for rare DT_UNKNOWN stat.
  char childPath[cfg::kMaxPathLen + 16];

  while (true) {
    errno = 0;
    struct dirent* ent = ::readdir(dir);
    if (!ent) {
      if (errno != 0) {
        Serial.printf("[sd] readdir errno=%d\n", errno);
      }
      break;
    }

    const char* name = ent->d_name;
    // Skip hidden + "." / ".."
    if (!name || name[0] == '\0' || name[0] == '.') continue;

    bool isDir = false;
    bool known = true;
    // ESP-IDF dirent may only define DT_DIR / DT_REG / DT_UNKNOWN.
    if (ent->d_type == DT_DIR) {
      isDir = true;
    } else if (ent->d_type == DT_REG) {
      isDir = false;
    } else {
      known = false;  // DT_UNKNOWN or other — stat once
    }

    if (!known) {
      // One stat only when d_type is unreliable (some FAT builds).
      const int n = std::snprintf(childPath, sizeof(childPath), "%s/%s", vfsPath, name);
      if (n <= 0 || static_cast<size_t>(n) >= sizeof(childPath)) continue;
      struct stat st{};
      if (::stat(childPath, &st) != 0) continue;
      isDir = S_ISDIR(st.st_mode);
      if (!isDir && !S_ISREG(st.st_mode)) continue;
    }

    if (!isDir && !isAudioName(name)) continue;

    if (count_ >= cfg::kMaxDirEntries) {
      truncated_ = true;
      break;
    }

    std::strncpy(entries_[count_].name, name, cfg::kMaxNameLen - 1);
    entries_[count_].name[cfg::kMaxNameLen - 1] = '\0';
    entries_[count_].kind = isDir ? EntryKind::Dir : path::kindFromName(name);
    ++count_;
  }

  ::closedir(dir);
  sortEntries();

  const uint32_t dt = millis() - t0;
  Serial.printf("[sd] list %s → %u entries%s in %lums (readdir)\n", path_,
                static_cast<unsigned>(count_), truncated_ ? " (trunc)" : "",
                static_cast<unsigned long>(dt));
  return true;
}

bool SdBrowser::listCurrentArduino() {
  // Slow path kept as fallback if VFS opendir is unavailable.
  File dir = SD.open(path_);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  // Prefer getNextFileName(isDir) — readdir without opening each child File.
  while (true) {
    bool isDir = false;
    String full = dir.getNextFileName(&isDir);
    if (full.length() == 0) break;

    // full is absolute path under mount ("/sd/...") or relative; take basename.
    const char* p = full.c_str();
    const char* slash = std::strrchr(p, '/');
    const char* name = slash ? slash + 1 : p;
    if (!name || name[0] == '\0' || name[0] == '.') continue;
    if (!isDir && !isAudioName(name)) continue;

    if (count_ >= cfg::kMaxDirEntries) {
      truncated_ = true;
      break;
    }
    std::strncpy(entries_[count_].name, name, cfg::kMaxNameLen - 1);
    entries_[count_].name[cfg::kMaxNameLen - 1] = '\0';
    entries_[count_].kind = isDir ? EntryKind::Dir : path::kindFromName(name);
    ++count_;
  }

  dir.close();
  sortEntries();
  Serial.printf("[sd] list %s → %u entries%s (arduino)\n", path_,
                static_cast<unsigned>(count_), truncated_ ? " (trunc)" : "");
  return true;
}

bool SdBrowser::enter(const char* name) {
  if (!name) return false;
  char newPath[cfg::kMaxPathLen];
  if (!path::join(newPath, sizeof(newPath), path_, name)) return false;
  std::strncpy(path_, newPath, cfg::kMaxPathLen - 1);
  path_[cfg::kMaxPathLen - 1] = '\0';
  return listCurrent();
}

bool SdBrowser::up() {
  if (std::strcmp(path_, "/") == 0) return false;
  char newPath[cfg::kMaxPathLen];
  path::parent(newPath, sizeof(newPath), path_);
  std::strncpy(path_, newPath, cfg::kMaxPathLen - 1);
  path_[cfg::kMaxPathLen - 1] = '\0';
  return listCurrent();
}

bool SdBrowser::openPath(const char* absPath) {
  if (!absPath || absPath[0] != '/') return false;
  std::strncpy(path_, absPath, cfg::kMaxPathLen - 1);
  path_[cfg::kMaxPathLen - 1] = '\0';
  return listCurrent();
}

const char* SdBrowser::path() const { return path_; }
const DirEntry* SdBrowser::entries() const { return entries_; }
size_t SdBrowser::count() const { return count_; }
bool SdBrowser::truncated() const { return truncated_; }
size_t SdBrowser::cursor() const { return cursor_; }

void SdBrowser::setCursor(size_t i) {
  cursor_ = (count_ > 0 && i < count_) ? i : 0;
  ensureScroll();
}

void SdBrowser::moveCursor(int delta) {
  if (count_ == 0) return;
  int pos = static_cast<int>(cursor_) + delta;
  if (pos < 0) pos = 0;
  if (pos >= static_cast<int>(count_)) pos = static_cast<int>(count_) - 1;
  cursor_ = static_cast<size_t>(pos);
  ensureScroll();
}

size_t SdBrowser::scroll() const { return scroll_; }

bool SdBrowser::nextAudioAfter(const char* fileName, char* outPath, size_t outCap) {
  if (!fileName || !outPath || outCap == 0 || count_ == 0) return false;

  size_t startIdx = count_;
  for (size_t i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i].name, fileName) == 0) {
      startIdx = i;
      break;
    }
  }

  for (size_t i = startIdx + 1; i < count_; ++i) {
    if (entries_[i].kind == EntryKind::Mp3 || entries_[i].kind == EntryKind::Wav) {
      return path::join(outPath, outCap, path_, entries_[i].name);
    }
  }
  return false;
}

bool SdBrowser::revealPath(const char* absPath) {
  if (!absPath || absPath[0] != '/' || !sdOk_) return false;
  if (!SD.exists(absPath)) return false;

  char dir[cfg::kMaxPathLen];
  char name[cfg::kMaxNameLen];
  path::parent(dir, sizeof(dir), absPath);
  path::fileName(name, sizeof(name), absPath);
  if (name[0] == '\0') return false;

  if (!openPath(dir)) return false;
  for (size_t i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i].name, name) == 0) {
      setCursor(i);
      return true;
    }
  }
  return false;  // dir listed but file filtered out / gone
}

bool SdBrowser::prevAudioBefore(const char* fileName, char* outPath, size_t outCap) {
  if (!fileName || !outPath || outCap == 0 || count_ == 0) return false;

  size_t startIdx = count_;
  for (size_t i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i].name, fileName) == 0) {
      startIdx = i;
      break;
    }
  }
  if (startIdx == 0 || startIdx >= count_) return false;

  for (size_t i = startIdx; i-- > 0;) {
    if (entries_[i].kind == EntryKind::Mp3 || entries_[i].kind == EntryKind::Wav) {
      return path::join(outPath, outCap, path_, entries_[i].name);
    }
  }
  return false;
}

BrowseSnapshot SdBrowser::snapshot() const {
  BrowseSnapshot s{};
  std::strncpy(s.path, path_, cfg::kMaxPathLen - 1);
  s.path[cfg::kMaxPathLen - 1] = '\0';
  s.entries = entries_;
  s.count = count_;
  s.cursor = cursor_;
  s.scroll = scroll_;
  s.truncated = truncated_;
  s.sdOk = sdOk_;
  return s;
}

void SdBrowser::sortEntries() {
  // dirs first, then case-insensitive name. n≤256 — std::sort is fine.
  std::sort(entries_, entries_ + count_, [](const DirEntry& a, const DirEntry& b) {
    if (a.kind == EntryKind::Dir && b.kind != EntryKind::Dir) return true;
    if (a.kind != EntryKind::Dir && b.kind == EntryKind::Dir) return false;
    return cmpInsensitive(a.name, b.name) < 0;
  });
}

void SdBrowser::ensureScroll() {
  if (cursor_ < scroll_) {
    scroll_ = cursor_;
  } else if (cursor_ >= scroll_ + cfg::kMaxVisibleRows) {
    scroll_ = cursor_ - cfg::kMaxVisibleRows + 1;
  }
}
