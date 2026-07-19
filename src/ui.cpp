#include "ui.hpp"
#include "config.hpp"
#include <M5Cardputer.h>
#include <cstdio>
#include <cstring>

void Ui::begin() {
  auto& d = M5Cardputer.Display;
  d.setRotation(1);
  d.setBrightness(cfg::kDisplayBrightness);
  d.fillScreen(cfg::kColorBg);
  d.setTextSize(1);
  hasLastBrowse_ = false;
  hasLastPlayer_ = false;
  hasLastSettings_ = false;
  displayOn_ = true;
  lastHint_[0] = '\0';
}

void Ui::setBrightness(uint8_t b) {
  if (displayOn_) {
    M5Cardputer.Display.setBrightness(b);
  }
}

void Ui::setDisplayOn(bool on) {
  if (on == displayOn_) return;
  displayOn_ = on;
  auto& d = M5Cardputer.Display;
  if (on) {
    // brightness restored by App via setBrightness after wake
    hasLastBrowse_ = false;
    hasLastPlayer_ = false;
    hasLastSettings_ = false;
  } else {
    d.setBrightness(0);
  }
}

void Ui::showToast(const char* text, uint32_t nowMs) {
  strncpy(toast_.text, text, sizeof(toast_.text) - 1);
  toast_.text[sizeof(toast_.text) - 1] = '\0';
  toast_.expiresAtMs = nowMs + cfg::kToastMs;
}

bool Ui::browseChanged(const BrowseSnapshot& b) const {
  if (!hasLastBrowse_) return true;
  if (b.sdOk != lastSdOk_) return true;
  if (b.count != lastCount_) return true;
  if (b.cursor != lastCursor_) return true;
  if (b.scroll != lastScroll_) return true;
  if (b.truncated != lastTruncated_) return true;
  if (std::strcmp(b.path, lastPath_) != 0) return true;
  if (b.entries) {
    for (size_t i = 0; i < b.count; ++i) {
      if (b.entries[i].kind != lastEntries_[i].kind) return true;
      if (std::strcmp(b.entries[i].name, lastEntries_[i].name) != 0) return true;
    }
  }
  return false;
}

bool Ui::playerChromeChanged(const PlayerSnapshot& p) const {
  if (!hasLastPlayer_) return true;
  if (p.state != lastPlayer_.state) return true;
  if (p.volumePercent != lastPlayer_.volumePercent) return true;
  if (std::strcmp(p.fileName, lastPlayer_.fileName) != 0) return true;
  return false;
}

bool Ui::playerChanged(const PlayerSnapshot& p) const {
  if (playerChromeChanged(p)) return true;
  if ((p.positionMs / 1000) != (lastPlayer_.positionMs / 1000)) return true;
  if ((p.durationMs / 1000) != (lastPlayer_.durationMs / 1000)) return true;
  return false;
}

bool Ui::settingsChanged(const Settings& s) const {
  if (!hasLastSettings_) return true;
  SettingsSnapshot cur = s.snapshot();
  return cur.volumePercent != lastSettings_.volumePercent ||
         cur.brightness != lastSettings_.brightness ||
         cur.displayTimeoutMs != lastSettings_.displayTimeoutMs ||
         cur.autoNext != lastSettings_.autoNext ||
         cur.cursor != lastSettings_.cursor;
}

void Ui::rememberBrowse(const BrowseSnapshot& b) {
  lastSdOk_ = b.sdOk;
  lastCount_ = b.count;
  lastCursor_ = b.cursor;
  lastScroll_ = b.scroll;
  lastTruncated_ = b.truncated;
  std::strncpy(lastPath_, b.path, cfg::kMaxPathLen - 1);
  lastPath_[cfg::kMaxPathLen - 1] = '\0';
  if (b.entries) {
    for (size_t i = 0; i < b.count && i < cfg::kMaxDirEntries; ++i) {
      lastEntries_[i] = b.entries[i];
    }
  }
  hasLastBrowse_ = true;
}

void Ui::rememberPlayer(const PlayerSnapshot& p) {
  lastPlayer_ = p;
  hasLastPlayer_ = true;
}

void Ui::rememberSettings(const Settings& s) {
  lastSettings_ = s.snapshot();
  hasLastSettings_ = true;
}

bool Ui::render(Screen screen,
                const BrowseSnapshot& browse,
                const PlayerSnapshot& player,
                const Settings& settings,
                uint32_t nowMs,
                bool force) {
  if (!displayOn_) {
    if (toast_.expiresAtMs != 0 && nowMs >= toast_.expiresAtMs) {
      toast_.expiresAtMs = 0;
      lastToastExp_ = 0;
    }
    return false;
  }

  const bool toastAppeared = (toast_.expiresAtMs != 0 && toast_.expiresAtMs != lastToastExp_);
  const bool toastExpired =
      (lastToastExp_ != 0 && (toast_.expiresAtMs == 0 || nowMs >= toast_.expiresAtMs));

  if (toast_.expiresAtMs != 0 && nowMs >= toast_.expiresAtMs) {
    toast_.expiresAtMs = 0;
  }

  const bool screenSwitch = (screen != lastScreen_);
  bool dirty = force || screenSwitch || toastAppeared || toastExpired;
  bool progressOnly = false;

  if (!dirty) {
    if (screen == Screen::Browse) {
      dirty = browseChanged(browse);
    } else if (screen == Screen::Playing) {
      if (playerChromeChanged(player)) {
        dirty = true;
      } else if (playerChanged(player)) {
        dirty = true;
        progressOnly = hasLastPlayer_;
      }
    } else if (screen == Screen::Settings) {
      dirty = settingsChanged(settings);
    }
  }
  if (!dirty) {
    return false;
  }

  auto& d = M5Cardputer.Display;

  if (screenSwitch) {
    d.fillScreen(cfg::kColorBg);
    hasLastBrowse_ = false;
    hasLastPlayer_ = false;
    hasLastSettings_ = false;
    lastHint_[0] = '\0';
    progressOnly = false;
  }
  lastScreen_ = screen;

  if (screen == Screen::Browse) {
    const bool full = !hasLastBrowse_ || force || screenSwitch || toastExpired;
    drawBrowse(browse, full);
    rememberBrowse(browse);
  } else if (screen == Screen::Playing) {
    if (progressOnly) {
      drawPlayingProgress(player);
      rememberPlayer(player);
    } else {
      const bool full = !hasLastPlayer_ || force || screenSwitch || toastExpired;
      drawPlaying(player, full);
      rememberPlayer(player);
    }
  } else {
    drawSettings(settings);
    rememberSettings(settings);
  }

  drawToastIfAny(nowMs);
  lastToastExp_ = toast_.expiresAtMs;
  return true;
}

void Ui::drawBrowse(const BrowseSnapshot& b, bool full) {
  auto& d = M5Cardputer.Display;

  if (!b.sdOk) {
    if (full || lastSdOk_) {
      d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, cfg::kColorBg);
      d.setTextColor(cfg::kColorFg, cfg::kColorBg);
      d.drawString("No SD card", 4, cfg::kScreenH / 2 - 10);
      d.setTextColor(cfg::kColorDim, cfg::kColorBg);
      d.drawString("FAT32 only (not exFAT)", 4, cfg::kScreenH / 2 + 6);
    }
    drawHint("Ent retry  S set");
    return;
  }

  if (full || std::strcmp(b.path, lastPath_) != 0 || !lastSdOk_) {
    d.fillRect(0, 0, cfg::kScreenW, 11, cfg::kColorBg);
    d.setTextColor(cfg::kColorDim, cfg::kColorBg);
    char pathBuf[cfg::kMaxPathLen + 4];
    snprintf(pathBuf, sizeof(pathBuf), "SD:%s", b.path);
    if (strlen(pathBuf) > 38) {
      pathBuf[35] = '.';
      pathBuf[36] = '.';
      pathBuf[37] = '.';
      pathBuf[38] = '\0';
    }
    d.drawString(pathBuf, 2, 1);
  }

  const int listY = 12;
  size_t visible = (b.count > b.scroll) ? (b.count - b.scroll) : 0;
  if (visible > static_cast<size_t>(cfg::kMaxVisibleRows)) {
    visible = cfg::kMaxVisibleRows;
  }

  const bool listIdentityChanged =
      full || !hasLastBrowse_ || b.scroll != lastScroll_ || b.count != lastCount_ ||
      std::strcmp(b.path, lastPath_) != 0 || !lastSdOk_;

  for (size_t i = 0; i < static_cast<size_t>(cfg::kMaxVisibleRows); ++i) {
    const int y = listY + static_cast<int>(i) * cfg::kListRowH;
    const bool rowVisible = (i < visible);
    const size_t idx = b.scroll + i;

    bool needRow = listIdentityChanged;
    if (!needRow && rowVisible && hasLastBrowse_) {
      const size_t oldIdx = lastScroll_ + i;
      const size_t oldVis =
          (lastCount_ > lastScroll_) ? (lastCount_ - lastScroll_) : 0;
      const bool wasVisible =
          (i < oldVis && i < static_cast<size_t>(cfg::kMaxVisibleRows));
      const bool selNow = rowVisible && (idx == b.cursor);
      const bool selWas = wasVisible && (oldIdx == lastCursor_);
      if (selNow != selWas) needRow = true;
      if (!needRow && wasVisible && b.entries) {
        if (b.entries[idx].kind != lastEntries_[oldIdx].kind ||
            std::strcmp(b.entries[idx].name, lastEntries_[oldIdx].name) != 0) {
          needRow = true;
        }
      }
      if (!wasVisible) needRow = true;
    } else if (!needRow && !rowVisible && hasLastBrowse_) {
      const size_t oldVis =
          (lastCount_ > lastScroll_) ? (lastCount_ - lastScroll_) : 0;
      if (i < oldVis) needRow = true;
    }

    if (!needRow) continue;

    if (rowVisible && b.entries) {
      const bool selected = (idx == b.cursor);
      const uint16_t bg = selected ? cfg::kColorSelectBg : cfg::kColorBg;
      const uint16_t fg = selected ? cfg::kColorSelectFg : cfg::kColorFg;
      d.fillRect(0, y, cfg::kScreenW, cfg::kListRowH, bg);
      d.setTextColor(fg, bg);
      char rowBuf[cfg::kMaxNameLen + 2];
      if (b.entries[idx].kind == EntryKind::Dir) {
        snprintf(rowBuf, sizeof(rowBuf), "%s/", b.entries[idx].name);
      } else {
        snprintf(rowBuf, sizeof(rowBuf), "%s", b.entries[idx].name);
      }
      d.drawString(rowBuf, 4, y + 2);
    } else {
      d.fillRect(0, y, cfg::kScreenW, cfg::kListRowH, cfg::kColorBg);
    }
  }

  if (b.count == 0 && (full || lastCount_ != 0)) {
    d.setTextColor(cfg::kColorDim, cfg::kColorBg);
    d.drawString("(empty)", 4, listY + 2);
  }

  if (b.truncated) {
    const int truncY = cfg::kScreenH - cfg::kHintBarH - 10;
    d.setTextColor(cfg::kColorDim, cfg::kColorBg);
    d.drawString("* truncated", 2, truncY);
  }

  drawHint(";. move  ,/ val  S set");
}

void Ui::drawPlayingProgress(const PlayerSnapshot& p) {
  auto& d = M5Cardputer.Display;

  d.fillRect(0, 40, cfg::kScreenW, 12, cfg::kColorBg);
  d.setTextColor(cfg::kColorFg, cfg::kColorBg);
  char timeBuf[24];
  if (p.durationMs == 0) {
    uint32_t posSec = p.positionMs / 1000;
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu / --:--",
             static_cast<unsigned long>(posSec / 60),
             static_cast<unsigned long>(posSec % 60));
  } else {
    uint32_t posSec = p.positionMs / 1000;
    uint32_t durSec = p.durationMs / 1000;
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu / %02lu:%02lu",
             static_cast<unsigned long>(posSec / 60),
             static_cast<unsigned long>(posSec % 60),
             static_cast<unsigned long>(durSec / 60),
             static_cast<unsigned long>(durSec % 60));
  }
  d.drawString(timeBuf, 4, 40);

  const int barY = 58;
  const int barW = 232;
  const int barX = (cfg::kScreenW - barW) / 2;
  const int barH = 6;
  d.fillRect(barX, barY, barW, barH, cfg::kColorBg);
  d.drawRect(barX, barY, barW, barH, cfg::kColorDim);
  if (p.durationMs > 0) {
    int fill = static_cast<int>((static_cast<uint64_t>(p.positionMs) * (barW - 2)) / p.durationMs);
    if (fill < 0) fill = 0;
    if (fill > barW - 2) fill = barW - 2;
    if (fill > 0) {
      d.fillRect(barX + 1, barY + 1, fill, barH - 2, cfg::kColorFg);
    }
  }
}

void Ui::drawPlaying(const PlayerSnapshot& p, bool full) {
  auto& d = M5Cardputer.Display;

  if (full) {
    d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, cfg::kColorBg);
  }

  d.setTextColor(cfg::kColorFg, cfg::kColorBg);

  if (full || p.state != lastPlayer_.state) {
    d.fillRect(0, 4, 80, 12, cfg::kColorBg);
    const char* status = "IDLE";
    switch (p.state) {
      case PlayState::Playing: status = "PLAY"; break;
      case PlayState::Paused:  status = "PAUSE"; break;
      case PlayState::Done:    status = "DONE"; break;
      case PlayState::Error:   status = "ERROR"; break;
      default: break;
    }
    d.drawString(status, 4, 4);
  }

  if (full || std::strcmp(p.fileName, lastPlayer_.fileName) != 0) {
    d.fillRect(0, 20, cfg::kScreenW, 16, cfg::kColorBg);
    d.drawString(p.fileName, 4, 20);
  }

  drawPlayingProgress(p);

  if (full || p.volumePercent != lastPlayer_.volumePercent) {
    d.fillRect(0, 74, 80, 12, cfg::kColorBg);
    char volBuf[12];
    snprintf(volBuf, sizeof(volBuf), "Vol %d%%", p.volumePercent);
    d.drawString(volBuf, 4, 74);
  }

  drawHint("Spc pause  ,/ vol  S set");
}

void Ui::drawSettings(const Settings& s) {
  auto& d = M5Cardputer.Display;
  d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, cfg::kColorBg);

  d.setTextColor(cfg::kColorFg, cfg::kColorBg);
  d.drawString("SETTINGS", 4, 2);

  const int rowH = 16;
  const int startY = 18;
  for (size_t i = 0; i < Settings::kCount; ++i) {
    const int y = startY + static_cast<int>(i) * rowH;
    const bool sel = (i == s.cursor());
    const uint16_t bg = sel ? cfg::kColorSelectBg : cfg::kColorBg;
    const uint16_t fg = sel ? cfg::kColorSelectFg : cfg::kColorFg;
    d.fillRect(0, y, cfg::kScreenW, rowH, bg);
    d.setTextColor(fg, bg);

    char val[16];
    s.formatValue(i, val, sizeof(val));
    char line[40];
    snprintf(line, sizeof(line), "%s", s.label(i));
    d.drawString(line, 4, y + 3);

    // value right-ish
    const int vw = static_cast<int>(strlen(val)) * 6;
    d.drawString(val, cfg::kScreenW - 4 - vw, y + 3);
  }

  drawHint(";. move  ,/ adj  Bs save");
}

void Ui::drawHint(const char* text) {
  if (text && std::strcmp(text, lastHint_) == 0) {
    return;
  }
  auto& d = M5Cardputer.Display;
  int y = cfg::kScreenH - cfg::kHintBarH;
  d.fillRect(0, y, cfg::kScreenW, cfg::kHintBarH, cfg::kColorBg);
  d.setTextColor(cfg::kColorDim, cfg::kColorBg);
  d.drawString(text, 2, y + 2);
  if (text) {
    std::strncpy(lastHint_, text, sizeof(lastHint_) - 1);
    lastHint_[sizeof(lastHint_) - 1] = '\0';
  }
}

void Ui::drawToastIfAny(uint32_t nowMs) {
  if (toast_.expiresAtMs == 0 || nowMs >= toast_.expiresAtMs) {
    return;
  }
  auto& d = M5Cardputer.Display;
  int tw = static_cast<int>(strlen(toast_.text)) * 6 + 8;
  if (tw > cfg::kScreenW - 4) tw = cfg::kScreenW - 4;
  int tx = (cfg::kScreenW - tw) / 2;
  int ty = cfg::kScreenH / 2 - 8;
  d.fillRect(tx, ty, tw, 16, cfg::kColorFg);
  d.setTextColor(cfg::kColorBg, cfg::kColorFg);
  d.drawString(toast_.text, tx + 4, ty + 4);
}
