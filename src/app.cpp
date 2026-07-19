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

  audio_.begin();
  browser_.begin();
  browser_.listCurrent();
  player_.begin(&audio_, &browser_);
  ui_.begin();
  input_.begin();

  ui_.render(screen_, browser_.snapshot(), player_.snapshot(), millis());
}

void App::loop() {
  M5Cardputer.update();
  audio_.updateAmpFromHp();
  player_.service();

  Action a = input_.poll();
  if (a != Action::None) handle(a);

  char errBuf[48];
  if (player_.takeError(errBuf, sizeof(errBuf))) {
    ui_.showToast(errBuf, millis());
    if (player_.snapshot().state == PlayState::Error) {
      screen_ = Screen::Browse;
    }
  }

  ui_.render(screen_, browser_.snapshot(), player_.snapshot(), millis());
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
        browser_.remount();
        browser_.listCurrent();
        break;
      }
      if (browser_.count() == 0) break;
      const DirEntry& e = browser_.entries()[browser_.cursor()];
      if (e.kind == EntryKind::Dir) {
        browser_.enter(e.name);
        browser_.listCurrent();
      } else {
        playSelection();
      }
      break;
    }
    case Action::Space: {
      if (browser_.count() == 0) break;
      const DirEntry& e = browser_.entries()[browser_.cursor()];
      if (e.kind != EntryKind::Dir) {
        playSelection();
      }
      break;
    }
    case Action::Back:
      browser_.up();
      browser_.listCurrent();
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
  player_.open(absPath);
  screen_ = Screen::Playing;
}
