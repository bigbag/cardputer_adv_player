#include "app.hpp"
#include "path_utils.hpp"
#include "config.hpp"
#include <M5Cardputer.h>
#include <cstring>

void App::begin() {
  Serial.begin(115200);
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setFont(&fonts::Font0);

  settings_.load();
  Serial.printf("[app] board=%d vol=%d bright=%u timeout=%lu autonext=%d\n",
                static_cast<int>(M5.getBoard()), settings_.volumePercent(),
                settings_.brightness(),
                static_cast<unsigned long>(settings_.displayTimeoutMs()),
                settings_.autoNext() ? 1 : 0);

  audio_.begin();
  browser_.begin();
  browser_.listCurrent();
  player_.begin(&audio_, &browser_);
  ui_.begin();
  input_.begin();

  applySettings();

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
  settings_.save();
  applySettings();
  screen_ = settingsReturn_;
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
  audio_.updateAmpFromHp();
  player_.service();

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
        browser_.remount();
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
      settings_.setVolumePercent(player_.snapshot().volumePercent);
      break;
    case Action::VolDown:
      player_.adjustVolume(-cfg::kVolumeStepPercent);
      settings_.setVolumePercent(player_.snapshot().volumePercent);
      break;
    case Action::SeekFwd:
      player_.seekRelative(cfg::kSeekStepSeconds);
      break;
    case Action::SeekBack:
      player_.seekRelative(-cfg::kSeekStepSeconds);
      break;
    case Action::Back:
      screen_ = Screen::Browse;
      break;
    default:
      break;
  }
}

void App::handleSettings(Action a) {
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
        case 0:
          settings_.setVolumePercent(settings_.volumePercent() + cfg::kVolumeStepPercent);
          player_.setVolumePercent(settings_.volumePercent());
          break;
        case 1:
          settings_.adjustBrightness(+15);
          ui_.setBrightness(settings_.brightness());
          M5Cardputer.Display.setBrightness(settings_.brightness());
          break;
        case 2:
          settings_.cycleDisplayTimeout();
          break;
        case 3:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          break;
        case 4:
          settings_.cycleTheme(+1);
          break;
        default:
          break;
      }
      break;
    case Action::VolDown:
    case Action::SeekBack:
      switch (settings_.cursor()) {
        case 0:
          settings_.setVolumePercent(settings_.volumePercent() - cfg::kVolumeStepPercent);
          player_.setVolumePercent(settings_.volumePercent());
          break;
        case 1:
          settings_.adjustBrightness(-15);
          ui_.setBrightness(settings_.brightness());
          M5Cardputer.Display.setBrightness(settings_.brightness());
          break;
        case 2:
          settings_.cycleDisplayTimeout();
          settings_.cycleDisplayTimeout();
          settings_.cycleDisplayTimeout();
          settings_.cycleDisplayTimeout();
          break;
        case 3:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          break;
        case 4:
          settings_.cycleTheme(-1);
          break;
        default:
          break;
      }
      break;
    case Action::Enter:
    case Action::Space:
      switch (settings_.cursor()) {
        case 2:
          settings_.cycleDisplayTimeout();
          break;
        case 3:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          break;
        case 4:
          settings_.cycleTheme(+1);
          break;
        default:
          break;
      }
      break;
    case Action::Back:
    case Action::Settings:
      closeSettings();
      break;
    default:
      break;
  }
}

void App::playSelection() {
  char absPath[cfg::kMaxPathLen];
  const DirEntry& e = browser_.entries()[browser_.cursor()];
  if (!path::join(absPath, sizeof(absPath), browser_.path(), e.name)) return;
  Serial.printf("[app] play %s\n", absPath);
  player_.open(absPath);
  screen_ = Screen::Playing;
}
