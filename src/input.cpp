#include "input.hpp"
#include <M5Cardputer.h>

void Input::begin() {}

Action Input::poll(Screen screen) {
  (void)screen;  // layout is global; no per-screen key conflicts now

  // M5Cardputer.update() must have been called (App::loop does this).
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return Action::None;
  }

  const Keyboard_Class::KeysState& st = M5Cardputer.Keyboard.keysState();

  // Special keys are NOT in st.word.
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

  // Cardputer diamond cluster (physical):
  //        ;
  //     ,  .  /
  //   left down right   (up = ;)
  for (char key : st.word) {
    switch (key) {
      case ';':
        return Action::Up;
      case '.':
        return Action::Down;
      case ',':
        return Action::VolDown;   // left  = decrease
      case '/':
        return Action::VolUp;     // right = increase
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
      // Optional extras still work
      case '=':
      case '+':
        return Action::VolUp;
      case '-':
      case '_':
        return Action::VolDown;
      default:
        break;
    }
  }

  return Action::None;
}
