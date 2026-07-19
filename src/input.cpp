#include "input.hpp"
#include <M5Cardputer.h>

void Input::begin() {}

Action Input::poll(Screen screen) {
  // M5Cardputer.update() must have been called (App::loop does this).
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return Action::None;
  }

  const Keyboard_Class::KeysState& st = M5Cardputer.Keyboard.keysState();

  // Enter / Space / Backspace are NOT in st.word — check flags first.
  if (st.enter) {
    return Action::Enter;
  }
  if (st.space) {
    return Action::Space;
  }
  // No Esc key on Cardputer; backspace acts as Back (parent / leave player).
  if (st.del) {
    return Action::Back;
  }

  if (st.tab) {
    return Action::Settings;
  }

  for (char key : st.word) {
    switch (key) {
      case ';':
        return Action::Up;
      case '/':
        return Action::Down;
      case '.':
        // Lists (Browse/Settings): down. Playing: volume up.
        return (screen == Screen::Playing) ? Action::VolUp : Action::Down;
      case ',':
        return (screen == Screen::Playing) ? Action::VolDown : Action::None;
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
