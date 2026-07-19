#pragma once
#include "types.hpp"
#include <cstdint>

class Ui {
 public:
  void begin();
  void showToast(const char* text, uint32_t nowMs);
  bool render(Screen screen,
              const BrowseSnapshot& browse,
              const PlayerSnapshot& player,
              uint32_t nowMs,
              bool force = false);

  void setDisplayOn(bool on);
  bool displayOn() const { return displayOn_; }

 private:
  void drawBrowse(const BrowseSnapshot& b, bool full);
  void drawPlaying(const PlayerSnapshot& p, bool full);
  void drawPlayingProgress(const PlayerSnapshot& p);
  void drawHint(const char* text);
  void drawToastIfAny(uint32_t nowMs);
  bool browseChanged(const BrowseSnapshot& b) const;
  bool playerChanged(const PlayerSnapshot& p) const;
  bool playerChromeChanged(const PlayerSnapshot& p) const;
  void rememberBrowse(const BrowseSnapshot& b);
  void rememberPlayer(const PlayerSnapshot& p);

  Toast toast_{};
  Screen lastScreen_{Screen::Browse};
  bool hasLastBrowse_ = false;
  bool hasLastPlayer_ = false;
  bool displayOn_ = true;

  char lastPath_[cfg::kMaxPathLen]{};
  DirEntry lastEntries_[cfg::kMaxDirEntries]{};
  size_t lastCount_ = 0;
  size_t lastCursor_ = 0;
  size_t lastScroll_ = 0;
  bool lastTruncated_ = false;
  bool lastSdOk_ = false;

  PlayerSnapshot lastPlayer_{};
  uint32_t lastToastExp_ = 0;
  char lastHint_[40]{};
};
