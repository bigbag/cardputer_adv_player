#include "input.hpp"
#include <M5Cardputer.h>

void Input::begin() {}

Action Input::poll(Screen screen) {
  // The caller must call M5Cardputer.update() before poll. App::loop does this.
  const uint8_t pressed = M5Cardputer.Keyboard.isPressed();
  if (pressed == 0) {
    waitForRelease_ = false;
    activeKey_ = '\0';
    return Action::None;
  }

  const Keyboard_Class::KeysState& st = M5Cardputer.Keyboard.keysState();
  if (waitForRelease_) return Action::None;
  if (pressed == 2 && st.fn && st.word.size() == 1 &&
      (st.word[0] == 'l' || st.word[0] == 'L')) {
    locked_ = !locked_;
    waitForRelease_ = true;
    activeKey_ = '\0';
    return Action::ToggleLock;
  }
  if (locked_) return Action::None;

  Action action = Action::None;
  char source = '\0';

  // Special keys are not part of st.word.
  if (st.enter) {
    source = '\n';
    action = Action::Enter;
  } else if (st.space) {
    source = ' ';
    action = Action::Space;
  } else if (st.del) {
    source = '\b';
    action = Action::Back;
  } else if (st.tab) {
    source = '\t';
    action = Action::Settings;
  }

  // Physical diamond cluster on the Cardputer keyboard:
  //        ;
  //     ,  .  /
  // ; = up in Browse, previous track while Playing
  // . = down in Browse, next track while Playing
  // , = volume down, / = volume up
  const bool playing = (screen == Screen::Playing);
  if (action == Action::None) {
    for (char rawKey : st.word) {
      const char key = rawKey >= 'A' && rawKey <= 'Z'
                           ? static_cast<char>(rawKey - 'A' + 'a')
                           : rawKey;
      switch (key) {
        case ';':
          source = ';';
          action = playing ? Action::PrevTrack : Action::Up;
          break;
        case '.':
          source = '.';
          action = playing ? Action::NextTrack : Action::Down;
          break;
        case ',':
          source = ',';
          action = Action::VolDown;
          break;
        case '/':
          source = '/';
          action = Action::VolUp;
          break;
        case '[':
          source = '[';
          action = Action::SeekBack;
          break;
        case ']':
          source = ']';
          action = Action::SeekFwd;
          break;
        case '`':
        case '~':
          source = '`';
          action = Action::Back;
          break;
        case ' ':
          source = ' ';
          action = Action::Space;
          break;
        case 's':
          source = 's';
          action = Action::Settings;
          break;
        case 'i':
          source = 'i';
          action = Action::System;
          break;
        // Extra alias keys.
        case '=':
        case '+':
          source = '=';
          action = Action::VolUp;
          break;
        case '-':
        case '_':
          source = '-';
          action = Action::VolDown;
          break;
        case 'n':
          if (playing) {
            source = 'n';
            action = Action::NextTrack;
          }
          break;
        case 'p':
          // Request a switch between Browse and Playing.
          // Use ';' for the previous track.
          source = 'p';
          action = Action::TogglePlayer;
          break;
        case 'r':
          source = 'r';
          action = Action::Recent;
          break;

        default:
          break;
      }
      if (action != Action::None) break;
    }
  }

  if (source == activeKey_) return Action::None;
  activeKey_ = source;
  return action;
}
