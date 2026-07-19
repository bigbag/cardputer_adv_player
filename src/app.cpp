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
  // SD must be mounted before loading /.asvmp3/config.cfg
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
  // Create config file only if missing (so first boot has something on disk).
  if (browser_.sdOk() && !SD.exists(Settings::kConfigPath)) {
    settings_.save();
  }

  // Restore last played track (folder cursor + auto-play if file still exists).
  resumeLastTrack();

  lastActivityMs_ = millis();
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
  // Values already auto-saved on each change.
  applySettings();
  screen_ = settingsReturn_;
}

void App::persistSettings() {
  applySettings();
  if (!settings_.save()) {
    ui_.showToast("Save fail (SD?)", millis());
  }
}

void App::rememberLastPath(const char* absPath) {
  if (!absPath || absPath[0] != '/') return;
  const char* prev = settings_.lastPath();
  if (prev && std::strcmp(prev, absPath) == 0) return;
  settings_.setLastPath(absPath);
  settings_.save();  // silent; volume path already handles toast on fail
}

void App::resumeLastTrack() {
  if (!browser_.sdOk()) return;
  if (settings_.onBoot() == OnBootMode::Off) {
    Serial.println("[app] on_boot=off — skip last track");
    return;
  }

  const char* last = settings_.lastPath();
  if (!last || last[0] != '/') return;

  if (!browser_.revealPath(last)) {
    Serial.printf("[app] last track missing: %s\n", last);
    return;
  }

  if (settings_.onBoot() == OnBootMode::Browse) {
    Serial.printf("[app] on_boot=browse — revealed %s\n", last);
    return;  // stay Screen::Browse
  }

  Serial.printf("[app] resume last: %s\n", last);
  if (player_.open(last)) {
    screen_ = Screen::Playing;
  }
}

void App::noteActivity(uint32_t nowMs) {
  lastActivityMs_ = nowMs;
  if (!ui_.displayOn()) {
    ui_.setDisplayOn(true);
    M5Cardputer.Display.setBrightness(settings_.brightness());
    ui_.render(screen_, browser_.snapshot(), player_.snapshot(), settings_, nowMs, true);
  }
}

void App::updateDisplayPower(uint32_t nowMs) {
  if (!ui_.displayOn()) return;
  const uint32_t timeout = settings_.displayTimeoutMs();
  if (timeout == 0) return;  // never
  if ((nowMs - lastActivityMs_) >= timeout) {
    Serial.println("[app] display off (timeout)");
    ui_.setDisplayOn(false);
  }
}

void App::loop() {
  const uint32_t now = millis();
  M5Cardputer.update();
  player_.service();
  // Auto-next (and any other path change) → persist last track.
  if (screen_ == Screen::Playing) {
    const char* p = player_.currentPath();
    if (p && p[0] == '/') rememberLastPath(p);
  }

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
      if (screen_ != Screen::Settings) screen_ = Screen::Browse;
    }
    forceUi = true;
  }

  ui_.render(screen_, browser_.snapshot(), player_.snapshot(), settings_, now, forceUi);
  updateDisplayPower(now);

  delay(10);
}

void App::handle(Action a) {
  // Global: Settings key from Browse/Playing
  if (a == Action::Settings && screen_ != Screen::Settings) {
    openSettings();
    return;
  }

  // Global: P toggles Browse ↔ Playing when a track is loaded.
  if (a == Action::TogglePlayer && screen_ != Screen::Settings) {
    const char* path = player_.currentPath();
    const bool hasTrack = path && path[0] == '/';
    if (screen_ == Screen::Playing) {
      screen_ = Screen::Browse;
    } else if (hasTrack) {
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
          // step backward through timeout cycle
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
  Serial.printf("[app] play %s\n", absPath);
  if (player_.open(absPath)) {
    rememberLastPath(absPath);
    screen_ = Screen::Playing;
  }
}
