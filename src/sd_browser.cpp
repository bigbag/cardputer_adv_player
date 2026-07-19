#include "sd_browser.hpp"
#include "path_utils.hpp"
#include "config.hpp"

#include <SD.h>
#include <SPI.h>
#include <cstring>
#include <algorithm>

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

bool SdBrowser::begin() {
  SPI.begin(cfg::kSdSck, cfg::kSdMiso, cfg::kSdMosi, cfg::kSdCs);
  sdOk_ = SD.begin(cfg::kSdCs, SPI, cfg::kSdSpiHz);
  if (sdOk_) {
    std::strcpy(path_, "/");
    listCurrent();
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
  if (!sdOk_) return false;

  File dir = SD.open(path_);
  if (!dir || !dir.isDirectory()) return false;

  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;

    const char* name = entry.name();
    if (!name || name[0] == '.') { entry.close(); continue; }

    bool isDir = entry.isDirectory();
    entry.close();

    if (!isDir) {
      if (!path::hasExtInsensitive(name, ".mp3") &&
          !path::hasExtInsensitive(name, ".wav")) {
        continue;
      }
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
  cursor_ = 0;
  scroll_ = 0;
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
