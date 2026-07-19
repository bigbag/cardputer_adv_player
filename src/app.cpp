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
  M5Cardputer.Display.setBrightness(cfg::kDisplayBrightness);

  Serial.printf("[app] board=%d\n", static_cast<int>(M5.getBoard()));

  audio_.begin();
  browser_.begin();
  browser_.listCurrent();
  player_.begin(&audio_, &browser_);
  ui_.begin();
  input_.begin();

  lastActivityMs_ = millis();
  ui_.render(screen_, browser_.snapshot(), player_.snapshot(), lastActivityMs_, true);
}

void App::noteActivity(uint32_t nowMs) {
  lastActivityMs_ = nowMs;
  if (!ui_.displayOn()) {
    ui_.setDisplayOn(true);
    // Immediate full paint after wake.
    ui_.render(screen_, browser_.snapshot(), player_.snapshot(), nowMs, true);
  }
}

void App::updateDisplayPower(uint32_t nowMs) {
  if (!ui_.displayOn()) return;
  if ((nowMs - lastActivityMs_) >= cfg::kDisplayTimeoutMs) {
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
    // Any key while screen was off only wakes; still process the key.
    handle(a);
    forceUi = true;
  }

  char errBuf[48];
  if (player_.takeError(errBuf, sizeof(errBuf))) {
    noteActivity(now);
    ui_.showToast(errBuf, now);
    if (player_.snapshot().state == PlayState::Error) {
      screen_ = Screen::Browse;
    }
    forceUi = true;
  }

  // Progress ticks while playing should not reset idle timeout.
  ui_.render(screen_, browser_.snapshot(), player_.snapshot(), now, forceUi);
  updateDisplayPower(now);

  delay(10);
}

void App::handle(Action a) {
  if (screen_ == Screen::Browse) {
    handleBrowse(a);
  } else {
    handlePlaying(a);
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
      break;
    case Action::VolDown:
      player_.adjustVolume(-cfg::kVolumeStepPercent);
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

void App::playSelection() {
  char absPath[cfg::kMaxPathLen];
  const DirEntry& e = browser_.entries()[browser_.cursor()];
  if (!path::join(absPath, sizeof(absPath), browser_.path(), e.name)) return;
  Serial.printf("[app] play %s\n", absPath);
  player_.open(absPath);
  screen_ = Screen::Playing;
}
