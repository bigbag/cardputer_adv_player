#include "ui.hpp"
#include "config.hpp"
#include <M5Cardputer.h>
#include <cstdio>
#include <cstring>

void Ui::begin() {
  auto& d = M5Cardputer.Display;
  d.setRotation(1);
  d.fillScreen(cfg::kColorBg);
  d.setTextSize(1);
  hasLastBrowse_ = false;
  hasLastPlayer_ = false;
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

bool Ui::playerChanged(const PlayerSnapshot& p) const {
  if (!hasLastPlayer_) return true;
  if (p.state != lastPlayer_.state) return true;
  if (p.volumePercent != lastPlayer_.volumePercent) return true;
  // Throttle time/bar updates to whole seconds to cut redraws.
  if ((p.positionMs / 1000) != (lastPlayer_.positionMs / 1000)) return true;
  if ((p.durationMs / 1000) != (lastPlayer_.durationMs / 1000)) return true;
  if (std::strcmp(p.fileName, lastPlayer_.fileName) != 0) return true;
  return false;
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

bool Ui::render(Screen screen,
                const BrowseSnapshot& browse,
                const PlayerSnapshot& player,
                uint32_t nowMs,
                bool force) {
  const bool toastActive = (toast_.expiresAtMs != 0 && nowMs < toast_.expiresAtMs);
  const bool toastExpired = (lastToastExp_ != 0 && toast_.expiresAtMs == 0);
  const bool toastChanged =
      (toast_.expiresAtMs != lastToastExp_) || toastActive || toastExpired;

  bool dirty = force || (screen != lastScreen_) || toastChanged;
  if (!dirty) {
    if (screen == Screen::Browse) {
      dirty = browseChanged(browse);
    } else {
      dirty = playerChanged(player);
    }
  }
  if (!dirty) {
    return false;
  }

  auto& d = M5Cardputer.Display;

  // Full clear only on screen switch or forced redraw — avoids constant blink.
  if (force || screen != lastScreen_) {
    d.fillScreen(cfg::kColorBg);
    hasLastBrowse_ = false;
    hasLastPlayer_ = false;
  }
  lastScreen_ = screen;

  if (screen == Screen::Browse) {
    drawBrowse(browse);
    rememberBrowse(browse);
  } else {
    drawPlaying(player);
    rememberPlayer(player);
  }

  drawToastIfAny(nowMs);
  lastToastExp_ = toast_.expiresAtMs;
  return true;
}

void Ui::drawBrowse(const BrowseSnapshot& b) {
  auto& d = M5Cardputer.Display;

  // Clear content area (leave nothing from previous player frame).
  d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, cfg::kColorBg);

  if (!b.sdOk) {
    d.setTextColor(cfg::kColorFg, cfg::kColorBg);
    d.drawString("No SD card", 4, cfg::kScreenH / 2 - 10);
    d.setTextColor(cfg::kColorDim, cfg::kColorBg);
    d.drawString("FAT32 only (not exFAT)", 4, cfg::kScreenH / 2 + 6);
    drawHint("Ent retry");
    return;
  }

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

  int listY = 12;
  size_t visible = (b.count > b.scroll) ? (b.count - b.scroll) : 0;
  if (visible > static_cast<size_t>(cfg::kMaxVisibleRows)) {
    visible = cfg::kMaxVisibleRows;
  }

  for (size_t i = 0; i < static_cast<size_t>(cfg::kMaxVisibleRows); ++i) {
    int y = listY + static_cast<int>(i) * cfg::kListRowH;
    if (i < visible) {
      size_t idx = b.scroll + i;
      bool selected = (idx == b.cursor);
      uint16_t bg = selected ? cfg::kColorSelectBg : cfg::kColorBg;
      uint16_t fg = selected ? cfg::kColorSelectFg : cfg::kColorFg;
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

  if (b.count == 0) {
    d.setTextColor(cfg::kColorDim, cfg::kColorBg);
    d.drawString("(empty)", 4, listY + 2);
  }

  if (b.truncated) {
    int truncY = cfg::kScreenH - cfg::kHintBarH - 10;
    d.setTextColor(cfg::kColorDim, cfg::kColorBg);
    d.drawString("* truncated", 2, truncY);
  }

  drawHint(";/ move  Ent open  Bs back");
}

void Ui::drawPlaying(const PlayerSnapshot& p) {
  auto& d = M5Cardputer.Display;
  // Paint solid background once per dirty frame (not every loop).
  d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, cfg::kColorBg);
  d.setTextColor(cfg::kColorFg, cfg::kColorBg);

  const char* status = "IDLE";
  switch (p.state) {
    case PlayState::Playing: status = "PLAY"; break;
    case PlayState::Paused:  status = "PAUSE"; break;
    case PlayState::Done:    status = "DONE"; break;
    case PlayState::Error:   status = "ERROR"; break;
    default: break;
  }
  d.drawString(status, 4, 4);
  d.drawString(p.fileName, 4, 20);

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
  d.drawRect(barX, barY, barW, barH, cfg::kColorDim);
  if (p.durationMs > 0) {
    int fill = static_cast<int>((static_cast<uint64_t>(p.positionMs) * (barW - 2)) / p.durationMs);
    if (fill < 0) fill = 0;
    if (fill > barW - 2) fill = barW - 2;
    if (fill > 0) {
      d.fillRect(barX + 1, barY + 1, fill, barH - 2, cfg::kColorFg);
    }
  }

  char volBuf[12];
  snprintf(volBuf, sizeof(volBuf), "Vol %d%%", p.volumePercent);
  d.drawString(volBuf, 4, 74);

  drawHint("Spc pause  [] seek  ,= vol  Bs list");
}

void Ui::drawHint(const char* text) {
  auto& d = M5Cardputer.Display;
  int y = cfg::kScreenH - cfg::kHintBarH;
  d.fillRect(0, y, cfg::kScreenW, cfg::kHintBarH, cfg::kColorBg);
  d.setTextColor(cfg::kColorDim, cfg::kColorBg);
  d.drawString(text, 2, y + 2);
}

void Ui::drawToastIfAny(uint32_t nowMs) {
  if (toast_.expiresAtMs == 0) {
    return;
  }
  if (nowMs >= toast_.expiresAtMs) {
    toast_.expiresAtMs = 0;
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
