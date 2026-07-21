#pragma once
#include "types.hpp"
#include "browser_history.hpp"

class SdBrowser {
 public:
  bool begin();
  bool remount();
  bool sdOk() const;
  bool listCurrent();
  bool enter(const char* name);
  bool up();
  bool openPath(const char* absPath);
  const char* path() const;
  const DirEntry* entries() const;
  size_t count() const;
  bool truncated() const;
  size_t cursor() const;
  void setCursor(size_t i);
  void moveCursor(int delta);
  size_t scroll() const;
  bool nextAudioAfter(const char* fileName, char* outPath, size_t outCap);
  bool prevAudioBefore(const char* fileName, char* outPath, size_t outCap);
  // List parent of absPath and put cursor on the file. false if missing.
  bool revealPath(const char* absPath);
  // Current folder + selected visible entry (not cursor index / scroll).
  BrowserLocation location() const;
  // Restore folder and selected entry. Missing folder/item resets to root.
  bool restoreLocation(const BrowserLocation& location);
  // Restore a temporary lookup’s visible list without clearing session history.
  bool restoreLocationPreservingHistory(const BrowserLocation& location);
  BrowseSnapshot snapshot() const;

 private:
  bool openPathInternal(const char* absPath, bool clearHistory);
  bool restoreLocationInternal(const BrowserLocation& location, bool clearHistory);
  void restoreListPosition(size_t cursor, size_t scroll);
  bool sdOk_ = false;
  char path_[cfg::kMaxPathLen] = "/";
  DirEntry entries_[cfg::kMaxDirEntries];
  size_t count_ = 0;
  bool truncated_ = false;
  size_t cursor_ = 0;
  size_t scroll_ = 0;
  BrowserHistory history_;
  void sortEntries();
  void ensureScroll();
  bool listCurrentArduino();  // slow fallback
};
