#pragma once
#include "types.hpp"
#include "settings.hpp"
#include "theme.hpp"
#include <cstdint>

class Ui {
 public:
  void begin();
  void showToast(const char* text, uint32_t nowMs);
  bool render(Screen screen,
              const BrowseSnapshot& browse,
              const PlayerSnapshot& player,
              const Settings& settings,
              uint32_t nowMs,
              bool force = false);

  void setDisplayOn(bool on);
  bool displayOn() const { return displayOn_; }
  void setBrightness(uint8_t b);

 private:
  void drawBrowse(const BrowseSnapshot& b, bool full);
  void drawPlaying(const PlayerSnapshot& p, bool full);
  void drawPlayingProgress(const PlayerSnapshot& p);
  void drawSettings(const Settings& s);
  void drawHint(const char* text);
  void drawToastIfAny(uint32_t nowMs);
  bool browseChanged(const BrowseSnapshot& b) const;
  bool playerChanged(const PlayerSnapshot& p) const;
  bool playerChromeChanged(const PlayerSnapshot& p) const;
  bool settingsChanged(const Settings& s) const;
  void rememberBrowse(const BrowseSnapshot& b);
  void rememberPlayer(const PlayerSnapshot& p);
  void rememberSettings(const Settings& s);
  void applyTheme(const Theme& t);

  Theme theme_{};
  Toast toast_{};
  Screen lastScreen_{Screen::Browse};
  bool hasLastBrowse_ = false;
  bool hasLastPlayer_ = false;
  bool hasLastSettings_ = false;
  bool displayOn_ = true;

  char lastPath_[cfg::kMaxPathLen]{};
  DirEntry lastEntries_[cfg::kMaxDirEntries]{};
  size_t lastCount_ = 0;
  size_t lastCursor_ = 0;
  size_t lastScroll_ = 0;
  bool lastTruncated_ = false;
  bool lastSdOk_ = false;

  PlayerSnapshot lastPlayer_{};
  SettingsSnapshot lastSettings_{};
  uint32_t lastToastExp_ = 0;
  char lastHint_[40]{};
};
