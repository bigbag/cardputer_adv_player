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
}

void Ui::showToast(const char* text, uint32_t nowMs) {
  strncpy(toast_.text, text, sizeof(toast_.text) - 1);
  toast_.text[sizeof(toast_.text) - 1] = '\0';
  toast_.expiresAtMs = nowMs + cfg::kToastMs;
}

void Ui::render(Screen screen,
                const BrowseSnapshot& browse,
                const PlayerSnapshot& player,
                uint32_t nowMs) {
  auto& d = M5Cardputer.Display;

  if (screen != last_) {
    d.fillScreen(cfg::kColorBg);
    last_ = screen;
  }

  if (screen == Screen::Browse) {
    drawBrowse(browse);
  } else {
    drawPlaying(player);
  }

  drawToastIfAny(nowMs);
}

void Ui::drawBrowse(const BrowseSnapshot& b) {
  auto& d = M5Cardputer.Display;

  if (!b.sdOk) {
    d.fillScreen(cfg::kColorBg);
    d.setTextColor(cfg::kColorFg, cfg::kColorBg);
    d.drawString("No SD card", cfg::kScreenW / 2 - 30, cfg::kScreenH / 2 - 10);
    drawHint("Ent retry");
    return;
  }

  // Path line at top (dim)
  d.fillRect(0, 0, cfg::kScreenW, 10, cfg::kColorBg);
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

  // List area
  int listY = 12;
  size_t visible = (b.count > b.scroll) ? b.count - b.scroll : 0;
  if (visible > cfg::kMaxVisibleRows) visible = cfg::kMaxVisibleRows;

  for (size_t i = 0; i < cfg::kMaxVisibleRows; ++i) {
    int y = listY + i * cfg::kListRowH;
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

  // Truncated indicator
  int hintY = cfg::kScreenH - cfg::kHintBarH;
  if (b.truncated) {
    int truncY = hintY - 10;
    d.setTextColor(cfg::kColorDim, cfg::kColorBg);
    d.drawString("* truncated", 2, truncY);
  }

  drawHint(";/ move  Ent open  Esc up");
}

void Ui::drawPlaying(const PlayerSnapshot& p) {
  auto& d = M5Cardputer.Display;
  d.fillScreen(cfg::kColorBg);
  d.setTextColor(cfg::kColorFg, cfg::kColorBg);

  // Status
  const char* status = "IDLE";
  switch (p.state) {
    case PlayState::Playing: status = "PLAY"; break;
    case PlayState::Paused:  status = "PAUSE"; break;
    case PlayState::Done:    status = "DONE"; break;
    case PlayState::Error:   status = "ERROR"; break;
    default: break;
  }
  d.drawString(status, 4, 4);

  // Filename
  d.drawString(p.fileName, 4, 20);

  // Time mm:ss / mm:ss
  char timeBuf[24];
  if (p.durationMs == 0) {
    snprintf(timeBuf, sizeof(timeBuf), "--:-- / --:--");
  } else {
    uint32_t posSec = p.positionMs / 1000;
    uint32_t durSec = p.durationMs / 1000;
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu / %02lu:%02lu",
             (unsigned long)(posSec / 60), (unsigned long)(posSec % 60),
             (unsigned long)(durSec / 60), (unsigned long)(durSec % 60));
  }
  d.drawString(timeBuf, 4, 40);

  // Progress bar
  int barY = 58;
  int barW = 232;
  int barX = (cfg::kScreenW - barW) / 2;
  int barH = 6;
  d.drawRect(barX, barY, barW, barH, cfg::kColorDim);
  if (p.durationMs > 0) {
    int fill = (int)((uint64_t)p.positionMs * (barW - 2) / p.durationMs);
    if (fill > barW - 2) fill = barW - 2;
    d.fillRect(barX + 1, barY + 1, fill, barH - 2, cfg::kColorFg);
  }

  // Volume
  char volBuf[12];
  snprintf(volBuf, sizeof(volBuf), "Vol %d%%", p.volumePercent);
  d.drawString(volBuf, 4, 74);

  drawHint("Spc pause  [] seek  ,. vol  Esc list");
}

void Ui::drawHint(const char* text) {
  auto& d = M5Cardputer.Display;
  int y = cfg::kScreenH - cfg::kHintBarH;
  d.fillRect(0, y, cfg::kScreenW, cfg::kHintBarH, cfg::kColorBg);
  d.setTextColor(cfg::kColorDim, cfg::kColorBg);
  d.drawString(text, 2, y + 2);
}

void Ui::drawToastIfAny(uint32_t nowMs) {
  if (toast_.expiresAtMs == 0 || nowMs >= toast_.expiresAtMs) {
    toast_.expiresAtMs = 0;
    return;
  }
  auto& d = M5Cardputer.Display;
  int tw = strlen(toast_.text) * 6 + 8;
  int tx = (cfg::kScreenW - tw) / 2;
  int ty = cfg::kScreenH / 2 - 8;
  d.fillRect(tx, ty, tw, 16, cfg::kColorFg);
  d.setTextColor(cfg::kColorBg, cfg::kColorFg);
  d.drawString(toast_.text, tx + 4, ty + 4);
}
