#include "input.hpp"
#include <M5Cardputer.h>

void Input::begin() {}

Action Input::poll(Screen screen) {
  // The caller must call M5Cardputer.update() before poll. App::loop does this.
  const bool changed = M5Cardputer.Keyboard.isChange();
  const uint8_t pressed = M5Cardputer.Keyboard.isPressed();
  if (pressed == 0) {
    waitForRelease_ = false;
    return Action::None;
  }

  const Keyboard_Class::KeysState& st = M5Cardputer.Keyboard.keysState();
  if (waitForRelease_) return Action::None;
  if (pressed == 2 && st.fn && st.word.size() == 1 &&
      (st.word[0] == 'l' || st.word[0] == 'L')) {
    locked_ = !locked_;
    waitForRelease_ = true;
    return Action::ToggleLock;
  }
  if (locked_ || !changed) return Action::None;

  // Special keys are not part of st.word.
  if (st.enter) {
    return Action::Enter;
  }
  if (st.space) {
    return Action::Space;
  }
  if (st.del) {
    return Action::Back;
  }
  if (st.tab) {
    return Action::Settings;
  }

  // Physical diamond cluster on the Cardputer keyboard:
  //        ;
  //     ,  .  /
  // ; = up in Browse, previous track while Playing
  // . = down in Browse, next track while Playing
  // , = volume down, / = volume up
  const bool playing = (screen == Screen::Playing);
  for (char key : st.word) {
    switch (key) {
      case ';':
        return playing ? Action::PrevTrack : Action::Up;
      case '.':
        return playing ? Action::NextTrack : Action::Down;
      case ',':
        return Action::VolDown;
      case '/':
        return Action::VolUp;
      case '[':
        return Action::SeekBack;
      case ']':
        return Action::SeekFwd;
      case '`':
      case '~':
        return Action::Back;
      case ' ':
        return Action::Space;
      case 's':
      case 'S':
        return Action::Settings;
      case 'i':
      case 'I':
        return Action::System;
      // Extra alias keys.
      case '=':
      case '+':
        return Action::VolUp;
      case '-':
      case '_':
        return Action::VolDown;
      case 'n':
      case 'N':
        if (playing) return Action::NextTrack;
        break;
      case 'p':
      case 'P':
        // Request a switch between Browse and Playing.
        // Use ';' for the previous track.
        return Action::TogglePlayer;
      default:
        break;
    }
  }

  return Action::None;
}
