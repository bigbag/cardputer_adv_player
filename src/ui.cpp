#include "ui.hpp"
#include "config.hpp"
#include <M5Cardputer.h>
#include <esp_timer.h>
#include <cstdio>
#include <cstring>

static void drawFileText(char* text, int x, int y, int right = cfg::kScreenW - 2) {
  auto& d = M5Cardputer.Display;
  const auto* previousFont = d.getFont();
  d.setFont(&fonts::efontJA_10);
  const int width = right - x;
  if (d.textWidth(text) > width) {
    const int end = d.textLength(text, width - d.textWidth("...") + 1);
    std::strcpy(text + end, "...");
  }
  d.drawString(text, x, y);
  d.setFont(previousFont);
}

void Ui::begin() {
  applyTheme(themes::get(0));
  auto& d = M5Cardputer.Display;
  d.setRotation(1);
  d.setBrightness(cfg::kDisplayBrightness);
  d.fillScreen(theme_.bg);
  d.setTextSize(1);
  d.setAttribute(lgfx::attribute_t::utf8_switch, true);
  hasLastBrowse_ = false;
  hasLastPlayer_ = false;
  hasLastSettings_ = false;
  battery_ = {};
  displayOn_ = true;
  lastHint_[0] = '\0';
}

void Ui::applyTheme(const Theme& t) {
  theme_ = t;
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
    // App restores the brightness through setBrightness after wake.
    hasLastBrowse_ = false;
    hasLastPlayer_ = false;
    hasLastSettings_ = false;
    battery_ = {};
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
  if (p.durationEstimated != lastPlayer_.durationEstimated) return true;
  return false;
}

bool Ui::settingsChanged(const Settings& s) const {
  if (!hasLastSettings_) return true;
  SettingsSnapshot cur = s.snapshot();
  return cur.volumePercent != lastSettings_.volumePercent ||
         cur.brightness != lastSettings_.brightness ||
         cur.displayTimeoutMs != lastSettings_.displayTimeoutMs ||
         cur.idleTimeoutMs != lastSettings_.idleTimeoutMs ||
         cur.autoNext != lastSettings_.autoNext ||
         cur.onBoot != lastSettings_.onBoot ||
         cur.themeIndex != lastSettings_.themeIndex ||
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

  const int previousBatteryMv = battery_.millivolts();
  const int previousBatteryLevel = battery_.level();
  if (battery_.due(nowMs)) {
    battery_.addSample(nowMs, M5.Power.getBatteryVoltage());
  }
  const bool batteryChanged = battery_.level() != previousBatteryLevel;
  const bool batteryVoltageChanged = battery_.millivolts() != previousBatteryMv;

  // Read the selected theme from settings. Apply theme changes immediately.
  bool themeChanged = false;
  if (theme_.name != settings.theme().name) {
    applyTheme(settings.theme());
    themeChanged = true;
    hasLastBrowse_ = false;
    hasLastPlayer_ = false;
    hasLastSettings_ = false;
    lastHint_[0] = '\0';
  }

  const bool toastAppeared = (toast_.expiresAtMs != 0 && toast_.expiresAtMs != lastToastExp_);
  const bool toastExpired =
      (lastToastExp_ != 0 && (toast_.expiresAtMs == 0 || nowMs >= toast_.expiresAtMs));

  if (toast_.expiresAtMs != 0 && nowMs >= toast_.expiresAtMs) {
    toast_.expiresAtMs = 0;
  }

  const bool screenSwitch = (screen != lastScreen_);
  bool dirty = force || themeChanged || screenSwitch || toastAppeared || toastExpired;
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
    } else if (screen == Screen::System) {
      dirty = batteryVoltageChanged || nowMs - lastSystemMs_ >= 1000 ||
              browse.sdOk != lastSdOk_;
    }
  }
  if (!dirty) {
    if (batteryChanged) drawBattery();
    return batteryChanged;
  }

  auto& d = M5Cardputer.Display;

  if (screenSwitch || themeChanged) {
    d.fillScreen(theme_.bg);
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
  } else if (screen == Screen::Settings) {
    drawSettings(settings);
    rememberSettings(settings);
  } else if (screen == Screen::System) {
    drawSystem(browse, nowMs, force || screenSwitch || themeChanged || toastExpired);
    lastSdOk_ = browse.sdOk;
  }

  if (!progressOnly || batteryChanged) drawBattery();
  drawToastIfAny(nowMs);
  lastToastExp_ = toast_.expiresAtMs;
  return true;
}

void Ui::drawBrowse(const BrowseSnapshot& b, bool full) {
  auto& d = M5Cardputer.Display;

  if (!b.sdOk) {
    if (full || lastSdOk_) {
      d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, theme_.bg);
      d.setTextColor(theme_.fg, theme_.bg);
      d.drawString("No SD card", 4, cfg::kScreenH / 2 - 10);
      d.setTextColor(theme_.dim, theme_.bg);
      d.drawString("FAT32 only (not exFAT)", 4, cfg::kScreenH / 2 + 6);
    }
    drawHint("Ent retry  S set  I sys");
    return;
  }

  if (full || std::strcmp(b.path, lastPath_) != 0 || !lastSdOk_) {
    d.fillRect(0, 0, cfg::kScreenW, 11, theme_.bg);
    d.setTextColor(theme_.dim, theme_.bg);
    char pathBuf[cfg::kMaxPathLen + 4];
    snprintf(pathBuf, sizeof(pathBuf), "SD:%s", b.path);
    drawFileText(pathBuf, 2, 1, cfg::kScreenW - cfg::kBatteryWidgetW - 2);
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
      const uint16_t bg = selected ? theme_.selectBg : theme_.bg;
      const uint16_t fg = selected ? theme_.selectFg : theme_.fg;
      d.fillRect(0, y, cfg::kScreenW, cfg::kListRowH, bg);
      d.setTextColor(fg, bg);
      char rowBuf[cfg::kMaxNameLen + 2];
      if (b.entries[idx].kind == EntryKind::Dir) {
        snprintf(rowBuf, sizeof(rowBuf), "%s/", b.entries[idx].name);
      } else {
        snprintf(rowBuf, sizeof(rowBuf), "%s", b.entries[idx].name);
      }
      drawFileText(rowBuf, 4, y + 2);
    } else {
      d.fillRect(0, y, cfg::kScreenW, cfg::kListRowH, theme_.bg);
    }
  }

  if (b.count == 0 && (full || lastCount_ != 0)) {
    d.setTextColor(theme_.dim, theme_.bg);
    d.drawString("(empty)", 4, listY + 2);
  }

  if (b.truncated) {
    const int truncY = cfg::kScreenH - cfg::kHintBarH - 10;
    d.setTextColor(theme_.dim, theme_.bg);
    d.drawString("* truncated", 2, truncY);
  }

  drawHint(";. move  P play  S set  I sys");
}

void Ui::drawPlayingProgress(const PlayerSnapshot& p) {
  auto& d = M5Cardputer.Display;

  d.fillRect(0, 40, cfg::kScreenW, 12, theme_.bg);
  d.setTextColor(theme_.fg, theme_.bg);
  char timeBuf[24];
  if (p.durationMs == 0) {
    uint32_t posSec = p.positionMs / 1000;
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu / --:--",
             static_cast<unsigned long>(posSec / 60),
             static_cast<unsigned long>(posSec % 60));
  } else {
    uint32_t posSec = p.positionMs / 1000;
    uint32_t durSec = p.durationMs / 1000;
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu / %s%02lu:%02lu",
             static_cast<unsigned long>(posSec / 60),
             static_cast<unsigned long>(posSec % 60),
             p.durationEstimated ? "~" : "",
             static_cast<unsigned long>(durSec / 60),
             static_cast<unsigned long>(durSec % 60));
  }
  d.drawString(timeBuf, 4, 40);

  const int barY = 58;
  const int barW = 232;
  const int barX = (cfg::kScreenW - barW) / 2;
  const int barH = 6;
  d.fillRect(barX, barY, barW, barH, theme_.bg);
  d.drawRect(barX, barY, barW, barH, theme_.dim);
  if (p.durationMs > 0) {
    int fill = static_cast<int>((static_cast<uint64_t>(p.positionMs) * (barW - 2)) / p.durationMs);
    if (fill < 0) fill = 0;
    if (fill > barW - 2) fill = barW - 2;
    if (fill > 0) {
      d.fillRect(barX + 1, barY + 1, fill, barH - 2, theme_.fg);
    }
  }
}

void Ui::drawPlaying(const PlayerSnapshot& p, bool full) {
  auto& d = M5Cardputer.Display;

  if (full) {
    d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, theme_.bg);
  }

  d.setTextColor(theme_.fg, theme_.bg);

  if (full || p.state != lastPlayer_.state) {
    d.fillRect(0, 4, 80, 12, theme_.bg);
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
    d.fillRect(0, 20, cfg::kScreenW, 16, theme_.bg);
    char nameBuf[cfg::kMaxNameLen];
    snprintf(nameBuf, sizeof(nameBuf), "%s", p.fileName);
    drawFileText(nameBuf, 4, 20);
  }

  drawPlayingProgress(p);

  if (full || p.volumePercent != lastPlayer_.volumePercent) {
    d.fillRect(0, 74, 110, 12, theme_.bg);
    char volBuf[16];
    snprintf(volBuf, sizeof(volBuf), "Vol %d%%", p.volumePercent);
    d.drawString(volBuf, 4, 74);
  }

  drawHint(";. trk  P brws  [] seek  I sys");
}

void Ui::drawSettings(const Settings& s) {
  auto& d = M5Cardputer.Display;
  d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, theme_.bg);

  d.setTextColor(theme_.fg, theme_.bg);
  d.drawString("SETTINGS", 4, 2);

  const int rowH = cfg::kListRowH;
  const int startY = 18;
  for (size_t i = 0; i < Settings::kCount; ++i) {
    const int y = startY + static_cast<int>(i) * rowH;
    const bool sel = (i == s.cursor());
    const uint16_t bg = sel ? theme_.selectBg : theme_.bg;
    const uint16_t fg = sel ? theme_.selectFg : theme_.fg;
    d.fillRect(0, y, cfg::kScreenW, rowH, bg);
    d.setTextColor(fg, bg);

    char val[16];
    s.formatValue(i, val, sizeof(val));
    char line[40];
    snprintf(line, sizeof(line), "%s", s.label(i));
    d.drawString(line, 4, y + 3);

    const int vw = static_cast<int>(strlen(val)) * 6;
    d.drawString(val, cfg::kScreenW - 4 - vw, y + 3);
  }

  drawHint(";. move  ,/ adj  Bs save  I sys");
}

void Ui::drawBattery() {
  auto& d = M5Cardputer.Display;
  const int x = cfg::kScreenW - cfg::kBatteryWidgetW;
  d.fillRect(x, 0, cfg::kBatteryWidgetW, 12, theme_.bg);
  d.setTextColor(theme_.dim, theme_.bg);
  const int level = battery_.level();
  char text[8];
  if (level < 0) {
    snprintf(text, sizeof(text), "--%%");
  } else {
    snprintf(text, sizeof(text), "~%d%%", level);
  }
  d.drawString(text, x + 2, 2);
  const int iconX = cfg::kScreenW - 22;
  d.drawRect(iconX, 2, 18, 8, theme_.dim);
  d.fillRect(iconX + 18, 4, 2, 4, theme_.dim);
  if (level > 0) {
    const int fill = (level * 14 + 99) / 100;
    d.fillRect(iconX + 2, 4, fill, 4, theme_.dim);
  }
}

void Ui::drawSystem(const BrowseSnapshot& b, uint32_t nowMs, bool full) {
  auto& d = M5Cardputer.Display;
  char line[40];
  d.setTextColor(theme_.fg, theme_.bg);
  if (full) {
    d.fillRect(0, 0, cfg::kScreenW, cfg::kScreenH - cfg::kHintBarH, theme_.bg);
    d.drawString("SYSTEM", 4, 2);
    d.drawString("Device  Cardputer-ADV", 4, 18);
    snprintf(line, sizeof(line), "%s  %luMHz  %u cores", ESP.getChipModel(),
             static_cast<unsigned long>(ESP.getCpuFreqMHz()),
             static_cast<unsigned>(ESP.getChipCores()));
    d.drawString(line, 4, 32);
    snprintf(line, sizeof(line), "Flash   %lu MiB",
             static_cast<unsigned long>(ESP.getFlashChipSize() / (1024 * 1024)));
    d.drawString(line, 4, 46);
  }
  d.fillRect(0, 60, cfg::kScreenW, 4 * cfg::kListRowH, theme_.bg);
  snprintf(line, sizeof(line), "Heap    %lu KiB free",
           static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
  d.drawString(line, 4, 60);
  const uint64_t seconds = static_cast<uint64_t>(esp_timer_get_time()) / 1000000;
  snprintf(line, sizeof(line), "Uptime  %luh %02lum %02lus",
           static_cast<unsigned long>(seconds / 3600),
           static_cast<unsigned long>((seconds / 60) % 60),
           static_cast<unsigned long>(seconds % 60));
  d.drawString(line, 4, 74);
  d.drawString(b.sdOk ? "SD      Mounted" : "SD      Not mounted", 4, 88);
  const int mv = battery_.millivolts();
  if (mv > 0) {
    snprintf(line, sizeof(line), "Battery %d.%03d V", mv / 1000, mv % 1000);
  } else {
    snprintf(line, sizeof(line), "Battery -- V");
  }
  d.drawString(line, 4, 102);
  drawHint("I / Bs back");
  lastSystemMs_ = nowMs;
}

void Ui::drawHint(const char* text) {
  if (text && std::strcmp(text, lastHint_) == 0) {
    return;
  }
  auto& d = M5Cardputer.Display;
  int y = cfg::kScreenH - cfg::kHintBarH;
  d.fillRect(0, y, cfg::kScreenW, cfg::kHintBarH, theme_.bg);
  d.setTextColor(theme_.dim, theme_.bg);
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
  d.fillRect(tx, ty, tw, 16, theme_.fg);
  d.setTextColor(theme_.bg, theme_.fg);
  d.drawString(toast_.text, tx + 4, ty + 4);
}
