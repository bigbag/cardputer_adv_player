#include "app.hpp"
#include "path_utils.hpp"
#include "config.hpp"
#include <M5Cardputer.h>
#include <SD.h>
#include <cstring>

void App::begin() {
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setFont(&fonts::Font0);

  Serial.printf("[app] board=%d\n", static_cast<int>(M5.getBoard()));

  if (!audio_.begin()) {
    Serial.println("[app] audio begin FAILED");
  }
  // Mount the SD card before loading /.asvmp3/config.cfg.
  browser_.begin();
  browser_.listCurrent();
  settings_.load();
  Serial.printf("[app] cfg vol=%d bright=%u timeout=%lu theme=%u autonext=%d\n",
                settings_.volumePercent(), settings_.brightness(),
                static_cast<unsigned long>(settings_.displayTimeoutMs()),
                static_cast<unsigned>(settings_.themeIndex()),
                settings_.autoNext() ? 1 : 0);

  player_.begin(&audio_, &browser_);
  ui_.begin();
  input_.begin();

  applySettings();
  // Create the config file if the card does not contain one.
  if (browser_.sdOk() && !SD.exists(Settings::kConfigPath)) {
    settings_.save();
  }

  // The browser location is independent of the last played track.
  restoreBrowserLocation();
  resumeLastTrack();

  lastActivityMs_ = millis();
  idleTimeout_.reset(lastActivityMs_);
  ui_.render(screen_, browser_.snapshot(), player_.snapshot(), settings_, lastActivityMs_, true);
}

void App::applySettings() {
  player_.setVolumePercent(settings_.volumePercent());
  player_.setAutoNext(settings_.autoNext());
  ui_.setBrightness(settings_.brightness());
  if (ui_.displayOn()) {
    M5Cardputer.Display.setBrightness(settings_.brightness());
  }
}

void App::openSettings() {
  settingsReturn_ = (screen_ == Screen::Settings) ? Screen::Browse : screen_;
  screen_ = Screen::Settings;
}

void App::closeSettings() {
  // Settings changes request a save before this method runs.
  applySettings();
  screen_ = settingsReturn_;
}

void App::persistSettings() {
  applySettings();
  if (settings_.save()) {
    browserLocationDirty_ = false;
  } else {
    ui_.showToast("Save fail (SD?)", millis());
  }
}

void App::rememberLastPath(const char* absPath) {
  if (!absPath || absPath[0] != '/') return;
  const char* prev = settings_.lastPath();
  if (prev && std::strcmp(prev, absPath) == 0) return;
  settings_.setLastPath(absPath);
  if (settings_.save()) {
    browserLocationDirty_ = false;
  }
}

void App::rememberBrowserLocation() {
  settings_.setBrowserLocation(browser_.location());
  browserLocationDirty_ = true;
  browserLocationChangedAtMs_ = millis();
}

void App::flushBrowserLocation(bool showError) {
  if (!browserLocationDirty_) return;
  if (settings_.save()) {
    browserLocationDirty_ = false;
  } else {
    // Retry after another debounce interval, not on every loop iteration.
    browserLocationChangedAtMs_ = millis();
    if (showError) {
      ui_.showToast("Save fail (SD?)", millis());
    }
  }
}

void App::restoreBrowserLocation() {
  if (!browser_.restoreLocation(settings_.browserLocation())) {
    Serial.println("[app] browser location unavailable — root");
  }
}

void App::resumeLastTrack() {
  if (!browser_.sdOk() || settings_.onBoot() != OnBootMode::Play) return;

  const char* last = settings_.lastPath();
  if (!last || last[0] != '/' || !SD.exists(last)) {
    if (last && last[0] == '/') {
      Serial.printf("[app] last track missing: %s\n", last);
    }
    return;
  }

  Serial.printf("[app] resume last: %s\n", last);
  if (player_.open(last)) {
    screen_ = Screen::Playing;
  }
}

void App::noteActivity(uint32_t nowMs) {
  lastActivityMs_ = nowMs;
  idleTimeout_.reset(nowMs);
  if (!ui_.displayOn()) {
    ui_.setDisplayOn(true);
    M5Cardputer.Display.setBrightness(settings_.brightness());
    ui_.render(screen_, browser_.snapshot(), player_.snapshot(), settings_, nowMs, true);
  }
}

void App::updateDisplayPower(uint32_t nowMs) {
  if (!ui_.displayOn()) return;
  const uint32_t timeout = settings_.displayTimeoutMs();
  if (timeout == 0) return;  // 0 disables the timeout
  if ((nowMs - lastActivityMs_) >= timeout) {
    Serial.println("[app] display off (timeout)");
    ui_.setDisplayOn(false);
  }
}

void App::updateIdlePower(uint32_t nowMs, PlayState state) {
  if (!idleTimeout_.expired(nowMs, settings_.idleTimeoutMs(),
                            state == PlayState::Playing)) return;

  if (!settings_.save()) {
    noteActivity(millis());
    ui_.showToast("Save fail (SD?)", millis());
    return;
  }
  browserLocationDirty_ = false;
  player_.stop();
  audio_.end();
  SD.end();
  Serial.println("[app] idle off (deep sleep)");
  Serial.flush();
  M5.Power.powerOff();
}

void App::loop() {
  const uint32_t now = millis();
  M5Cardputer.update();
  player_.service();
  // Save the last track when its path changes.
  // Keep this active while the display shows the Browser screen.
  const char* currentPath = player_.currentPath();
  if (currentPath && currentPath[0] == '/') rememberLastPath(currentPath);

  Action a = input_.poll(screen_);
  bool forceUi = false;
  if (a != Action::None) {
    noteActivity(now);
    handle(a);
    forceUi = true;
  }

  char errBuf[48];
  if (player_.takeError(errBuf, sizeof(errBuf))) {
    noteActivity(now);
    ui_.showToast(errBuf, now);
    if (player_.snapshot().state == PlayState::Error) {
      if (screen_ == Screen::Browse || screen_ == Screen::Playing) screen_ = Screen::Browse;
    }
    forceUi = true;
  }

  if (browserLocationDirty_ &&
      millis() - browserLocationChangedAtMs_ >= cfg::kBrowserLocationSaveDelayMs) {
    flushBrowserLocation(false);
  }

  const PlayerSnapshot player = player_.snapshot();
  ui_.render(screen_, browser_.snapshot(), player, settings_, now, forceUi);
  updateDisplayPower(now);
  updateIdlePower(millis(), player.state);

  delay(10);
}

void App::handle(Action a) {
  if (a == Action::System) {
    if (screen_ == Screen::System) {
      screen_ = systemReturn_;
    } else {
      if (screen_ == Screen::Browse) {
        rememberBrowserLocation();
        flushBrowserLocation(true);
      }
      systemReturn_ = screen_;
      screen_ = Screen::System;
    }
    return;
  }
  if (screen_ == Screen::System) {
    if (a == Action::Back) screen_ = systemReturn_;
    return;
  }

  // The Settings key works on the Browse and Playing screens.
  if (a == Action::Settings && screen_ != Screen::Settings) {
    if (screen_ == Screen::Browse) {
      rememberBrowserLocation();
      flushBrowserLocation(true);
    }
    openSettings();
    return;
  }

  // P switches between Browse and Playing if the player has a track.
  if (a == Action::TogglePlayer && screen_ != Screen::Settings) {
    const char* path = player_.currentPath();
    const bool hasTrack = path && path[0] == '/';
    if (screen_ == Screen::Playing) {
      screen_ = Screen::Browse;
    } else if (hasTrack) {
      rememberBrowserLocation();
      flushBrowserLocation(true);
      screen_ = Screen::Playing;
    } else {
      ui_.showToast("Nothing playing", millis());
    }
    return;
  }

  switch (screen_) {
    case Screen::Browse:
      handleBrowse(a);
      break;
    case Screen::Playing:
      handlePlaying(a);
      break;
    case Screen::Settings:
      handleSettings(a);
      break;
    case Screen::System:
      break;
  }
}

void App::handleBrowse(Action a) {
  switch (a) {
    case Action::Up:
      browser_.moveCursor(-1);
      break;
    case Action::Down:
      browser_.moveCursor(1);
      break;
    case Action::Enter: {
      if (!browser_.sdOk()) {
        Serial.println("[app] SD retry");
        if (browser_.remount()) {
          settings_.load();
          applySettings();
          restoreBrowserLocation();
        }
        break;
      }
      if (browser_.count() == 0) break;
      const DirEntry& e = browser_.entries()[browser_.cursor()];
      if (e.kind == EntryKind::Dir) {
        browser_.enter(e.name);
      } else {
        playSelection();
      }
      break;
    }
    case Action::Space: {
      if (!browser_.sdOk() || browser_.count() == 0) break;
      const DirEntry& e = browser_.entries()[browser_.cursor()];
      if (e.kind != EntryKind::Dir) {
        playSelection();
      }
      break;
    }
    case Action::Back:
      browser_.up();
      break;
    default:
      break;
  }

  // A successful file selection switches to the Playing screen.
  // playSelection() records the browser location before it starts playback.
  if (screen_ == Screen::Browse &&
      (a == Action::Up || a == Action::Down || a == Action::Enter ||
       a == Action::Space || a == Action::Back)) {
    rememberBrowserLocation();
  }
}

void App::handlePlaying(Action a) {
  switch (a) {
    case Action::Space:
      player_.togglePause();
      break;
    case Action::VolUp:
      player_.adjustVolume(cfg::kVolumeStepPercent);
      settings_.setVolumePercent(player_.volumePercent());
      persistSettings();
      break;
    case Action::VolDown:
      player_.adjustVolume(-cfg::kVolumeStepPercent);
      settings_.setVolumePercent(player_.volumePercent());
      persistSettings();
      break;
    case Action::SeekFwd:
      player_.seekRelative(cfg::kSeekStepSeconds);
      break;
    case Action::SeekBack:
      player_.seekRelative(-cfg::kSeekStepSeconds);
      break;
    case Action::NextTrack:
      if (!player_.nextTrack()) {
        ui_.showToast("Last track", millis());
      } else {
        rememberLastPath(player_.currentPath());
      }
      break;
    case Action::PrevTrack:
      if (player_.prevTrack()) {
        rememberLastPath(player_.currentPath());
      }
      break;
    case Action::Back:
      screen_ = Screen::Browse;
      break;
    default:
      break;
  }
}

void App::handleSettings(Action a) {
  bool changed = false;
  switch (a) {
    case Action::Up:
      settings_.moveCursor(-1);
      break;
    case Action::Down:
      settings_.moveCursor(1);
      break;
    case Action::VolUp:
    case Action::SeekFwd:
      switch (settings_.cursor()) {
        case 0: settings_.cycleTheme(+1); changed = true; break;
        case 1:
          settings_.adjustVolume(+cfg::kVolumeStepPercent);
          player_.setVolumePercent(settings_.volumePercent());
          changed = true;
          break;
        case 2:
          settings_.adjustBrightness(+15);
          ui_.setBrightness(settings_.brightness());
          M5Cardputer.Display.setBrightness(settings_.brightness());
          changed = true;
          break;
        case 3: settings_.cycleDisplayTimeout(); changed = true; break;
        case 4:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          changed = true;
          break;
        case 5:
          settings_.cycleOnBoot(+1);
          changed = true;
          break;
        case 6: settings_.cycleIdleTimeout(+1); changed = true; break;
        default: break;
      }
      break;
    case Action::VolDown:
    case Action::SeekBack:
      switch (settings_.cursor()) {
        case 0: settings_.cycleTheme(-1); changed = true; break;
        case 1:
          settings_.adjustVolume(-cfg::kVolumeStepPercent);
          player_.setVolumePercent(settings_.volumePercent());
          changed = true;
          break;
        case 2:
          settings_.adjustBrightness(-15);
          ui_.setBrightness(settings_.brightness());
          M5Cardputer.Display.setBrightness(settings_.brightness());
          changed = true;
          break;
        case 3:
          // Four forward steps equal one backward step in the cycle.
          settings_.cycleDisplayTimeout();
          settings_.cycleDisplayTimeout();
          settings_.cycleDisplayTimeout();
          settings_.cycleDisplayTimeout();
          changed = true;
          break;
        case 4:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          changed = true;
          break;
        case 5:
          settings_.cycleOnBoot(-1);
          changed = true;
          break;
        case 6: settings_.cycleIdleTimeout(-1); changed = true; break;
        default: break;
      }
      break;
    case Action::Enter:
    case Action::Space:
      switch (settings_.cursor()) {
        case 0: settings_.cycleTheme(+1); changed = true; break;
        case 3: settings_.cycleDisplayTimeout(); changed = true; break;
        case 4:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          changed = true;
          break;
        case 5:
          settings_.cycleOnBoot(+1);
          changed = true;
          break;
        case 6: settings_.cycleIdleTimeout(+1); changed = true; break;
        default: break;
      }
      break;
    case Action::Back:
    case Action::Settings:
      closeSettings();
      break;
    default:
      break;
  }
  if (changed) {
    persistSettings();
  }
}

void App::playSelection() {
  char absPath[cfg::kMaxPathLen];
  const DirEntry& e = browser_.entries()[browser_.cursor()];
  if (!path::join(absPath, sizeof(absPath), browser_.path(), e.name)) return;
  rememberBrowserLocation();
  Serial.printf("[app] play %s\n", absPath);
  if (player_.open(absPath)) {
    // rememberLastPath() combines both saves when the track changes.
    // Flush the browser location even if the user selects the same track again.
    rememberLastPath(absPath);
    flushBrowserLocation(true);
    screen_ = Screen::Playing;
  }
}
