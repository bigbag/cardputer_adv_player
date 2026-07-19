#include "input.hpp"
#include <M5Cardputer.h>

void Input::begin() {}

Action Input::poll() {
  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed())
    return Action::None;

  auto state = M5Cardputer.Keyboard.keysState();

  for (auto key : state.word) {
    if (key == 0) continue;
    switch (key) {
      case ';':  return Action::Up;
      case '/':  return Action::Down;
      case '\r': return Action::Enter;     // Enter key
      case 0x1B: return Action::Back;      // Esc
      case '`':  return Action::Back;
      case ' ':  return Action::Space;
      case ',':  return Action::VolDown;
      case '.':  return Action::VolUp;
      case '[':  return Action::SeekBack;
      case ']':  return Action::SeekFwd;
      default:   break;
    }
  }
  return Action::None;
}
