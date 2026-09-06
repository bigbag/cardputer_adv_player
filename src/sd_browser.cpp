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

// The ESP32 Arduino SD library mounts the card at this VFS path.
// This is the SD.begin default.
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

static bool isAbsolutePath(const char* path) {
  return path && path[0] == '/';
}

// Build the absolute VFS path from "/sd" and relPath. relPath must start with "/".
static bool makeVfsPath(char* out, size_t cap, const char* relPath) {
  if (!out || cap < 4 || !relPath) return false;
  if (relPath[0] == '/' && relPath[1] == '\0') {
    std::snprintf(out, cap, "%s", kSdMount);
    return true;
  }
  int n = std::snprintf(out, cap, "%s%s", kSdMount, relPath);
  return n > 0 && static_cast<size_t>(n) < cap;
}

bool SdBrowser::begin() {
  pinMode(cfg::kSdCs, OUTPUT);
  digitalWrite(cfg::kSdCs, HIGH);
  SPI.begin(cfg::kSdSck, cfg::kSdMiso, cfg::kSdMosi, cfg::kSdCs);

  // Start at cfg::kSdSpiHz. Try the next speed only if the mount fails.
  static const uint32_t kSpeeds[] = {
      cfg::kSdSpiHz,  // 4 MHz by default
      10000000u,
      15000000u,
  };

  sdOk_ = false;
  history_.clear();
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
    // App lists the folder after begin. Do not scan the same folder here.
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

  // Use POSIX readdir to avoid opening a File object for each entry.
  // FATFS caches directory sectors. Read entries through one directory stream.
  char vfsPath[cfg::kMaxPathLen + 8];
  if (!makeVfsPath(vfsPath, sizeof(vfsPath), path_)) {
    Serial.println("[sd] path too long");
    return false;
  }

  DIR* dir = ::opendir(vfsPath);
  if (!dir) {
    // Use the Arduino SD API if VFS opendir fails.
    Serial.printf("[sd] opendir(%s) failed — Arduino fallback\n", vfsPath);
    return listCurrentArduino();
  }

  // Reserve a path buffer for entries that need a stat call.
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
    // Skip dot entries: hidden files and the "." and ".." entries.
    if (!name || name[0] == '\0' || name[0] == '.') continue;

    bool isDir = false;
    bool known = true;
    // The ESP-IDF dirent may define only DT_DIR, DT_REG, and DT_UNKNOWN.
    if (ent->d_type == DT_DIR) {
      isDir = true;
    } else if (ent->d_type == DT_REG) {
      isDir = false;
    } else {
      known = false;  // Check the entry type with stat.
    }

    if (!known) {
      // Call stat only when d_type does not identify the entry.
      const int n = std::snprintf(childPath, sizeof(childPath), "%s/%s", vfsPath, name);
      if (n <= 0 || static_cast<size_t>(n) >= sizeof(childPath)) continue;
      struct stat st{};
      if (::stat(childPath, &st) != 0) continue;
      isDir = S_ISDIR(st.st_mode);
      if (!isDir && !S_ISREG(st.st_mode)) continue;
    }

    if (!isDir && !isAudioName(name)) continue;
    if (std::strlen(name) >= cfg::kMaxNameLen) {
      truncated_ = true;
      continue;
    }

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
  File dir = SD.open(path_);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return false;
  }

  // getNextFileName(isDir) reads names without opening a File per child.
  while (true) {
    bool isDir = false;
    String full = dir.getNextFileName(&isDir);
    if (full.length() == 0) break;

    // full is an absolute path under the mount or a relative path. Take the basename.
    const char* p = full.c_str();
    const char* slash = std::strrchr(p, '/');
    const char* name = slash ? slash + 1 : p;
    if (!name || name[0] == '\0' || name[0] == '.') continue;
    if (!isDir && !isAudioName(name)) continue;
    if (std::strlen(name) >= cfg::kMaxNameLen) {
      truncated_ = true;
      continue;
    }

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

  char childPath[cfg::kMaxPathLen];
  if (!path::join(childPath, sizeof(childPath), path_, name)) return false;

  char parentPath[cfg::kMaxPathLen];
  std::strncpy(parentPath, path_, sizeof(parentPath) - 1);
  parentPath[sizeof(parentPath) - 1] = '\0';
  const size_t parentCursor = cursor_;
  const size_t parentScroll = scroll_;

  if (!openPathInternal(childPath, false)) return false;
  history_.push(parentPath, parentCursor, parentScroll);
  return true;
}

bool SdBrowser::up() {
  BrowserHistoryFrame frame{};
  if (history_.peek(&frame)) {
    if (!openPathInternal(frame.path, false)) return false;
    restoreListPosition(frame.cursor, frame.scroll);
    history_.discardTop();
    return true;
  }

  if (std::strcmp(path_, "/") == 0) return false;
  char parentPath[cfg::kMaxPathLen];
  path::parent(parentPath, sizeof(parentPath), path_);
  return openPathInternal(parentPath, false);
}

bool SdBrowser::openPathInternal(const char* absPath, bool clearHistory) {
  if (!isAbsolutePath(absPath) || std::strlen(absPath) >= sizeof(path_)) return false;

  char oldPath[cfg::kMaxPathLen];
  std::strncpy(oldPath, path_, sizeof(oldPath) - 1);
  oldPath[sizeof(oldPath) - 1] = '\0';
  const size_t oldCursor = cursor_;
  const size_t oldScroll = scroll_;

  std::strncpy(path_, absPath, sizeof(path_) - 1);
  path_[sizeof(path_) - 1] = '\0';
  if (!listCurrent()) {
    std::strncpy(path_, oldPath, sizeof(path_) - 1);
    path_[sizeof(path_) - 1] = '\0';
    if (listCurrent()) restoreListPosition(oldCursor, oldScroll);
    return false;
  }
  if (clearHistory) history_.clear();
  return true;
}

bool SdBrowser::openPath(const char* absPath) {
  return openPathInternal(absPath, true);
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

  if (!openPathInternal(dir, false)) return false;
  for (size_t i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i].name, name) == 0) {
      setCursor(i);
      return true;
    }
  }
  return false;  // No listed entry matches the file.
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

void SdBrowser::restoreListPosition(size_t cursor, size_t scroll) {
  BrowserHistory::clampPosition(count_, cfg::kMaxVisibleRows, &cursor, &scroll);
  cursor_ = cursor;
  scroll_ = scroll;
}

BrowserLocation SdBrowser::location() const {
  BrowserLocation out{};
  std::strncpy(out.path, path_, sizeof(out.path) - 1);
  if (count_ > 0 && cursor_ < count_) {
    std::strncpy(out.item, entries_[cursor_].name, sizeof(out.item) - 1);
  }
  return out;
}

bool SdBrowser::restoreLocationInternal(const BrowserLocation& location,
                                        bool clearHistory) {
  auto restoreRoot = [&]() {
    openPathInternal("/", clearHistory);
    return false;
  };

  // An empty path is the legacy default with no saved location. It restores root.
  if (location.path[0] == '\0') {
    return openPathInternal("/", clearHistory);
  }
  if (!isAbsolutePath(location.path) ||
      !openPathInternal(location.path, clearHistory)) {
    return restoreRoot();
  }

  // An empty selection is valid only for an empty folder. In a nonempty
  // folder, the saved item is missing and the browser resets to root.
  if (location.item[0] == '\0') {
    return count_ == 0 ? true : restoreRoot();
  }
  for (size_t i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i].name, location.item) == 0) {
      setCursor(i);
      return true;
    }
  }
  return restoreRoot();
}

bool SdBrowser::restoreLocation(const BrowserLocation& location) {
  return restoreLocationInternal(location, true);
}

bool SdBrowser::restoreLocationPreservingHistory(const BrowserLocation& location) {
  const bool restored = restoreLocationInternal(location, false);
  // A failed temporary restore falls back to root and makes the saved
  // history frames stale.
  if (!restored) history_.clear();
  return restored;
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
  // Sort directories first, then names case-insensitively. The list has at
  // most 256 entries, so std::sort is fast enough.
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
