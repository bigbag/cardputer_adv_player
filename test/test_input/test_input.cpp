#include <unity.h>
#include <M5Cardputer.h>
#include "input.hpp"

static Action press(Input& input, const char* keys, bool fn = false,
                    Screen screen = Screen::Playing) {
  auto& keyboard = M5Cardputer.Keyboard;
  keyboard.state = {};
  keyboard.state.fn = fn;
  keyboard.count = fn ? 1 : 0;
  for (; *keys; ++keys) {
    ++keyboard.count;
    switch (*keys) {
      case '\n': keyboard.state.enter = true; break;
      case '\b': keyboard.state.del = true; break;
      case '\t': keyboard.state.tab = true; break;
      default:
        keyboard.state.word.push_back(*keys);
        if (*keys == ' ') keyboard.state.space = true;
        break;
    }
  }
  return input.poll(screen);
}

void test_lock_blocks_controls_on_every_screen() {
  Input input;
  TEST_ASSERT_EQUAL(Action::ToggleLock, press(input, "l", true));
  press(input, "");
  for (Screen screen : {Screen::Browse, Screen::Playing, Screen::Settings, Screen::System}) {
    for (char key : {';', '.', ',', '/', '[', ']', '`', ' ', 's', 'i', '=', '-', 'n', 'p', '\n', '\b', '\t'}) {
      const char keys[] = {key, '\0'};
      TEST_ASSERT_EQUAL(Action::None, press(input, keys, false, screen));
      press(input, "", false, screen);
    }
  }
  TEST_ASSERT_EQUAL(Action::ToggleLock, press(input, "L", true, Screen::System));
  press(input, "");
  TEST_ASSERT_EQUAL(Action::Space, press(input, " "));
}

void test_chord_consumes_keys_until_full_release() {
  Input input;
  TEST_ASSERT_EQUAL(Action::None, press(input, "", true));
  TEST_ASSERT_EQUAL(Action::ToggleLock, press(input, "l", true));
  TEST_ASSERT_EQUAL(Action::None, press(input, "l", true));
  TEST_ASSERT_EQUAL(Action::None, press(input, "l ", true));
  TEST_ASSERT_EQUAL(Action::None, press(input, "l", true));
  TEST_ASSERT_EQUAL(Action::None, press(input, "", true));
  TEST_ASSERT_EQUAL(Action::None, press(input, "l", true));
  press(input, "");
  TEST_ASSERT_EQUAL(Action::ToggleLock, press(input, "l", true));
  TEST_ASSERT_EQUAL(Action::None, press(input, "l ", true));
  TEST_ASSERT_EQUAL(Action::None, press(input, " "));
  press(input, "");
  TEST_ASSERT_EQUAL(Action::Space, press(input, " "));
}

void test_chord_is_detected_when_key_count_does_not_change() {
  Input input;
  press(input, "i", true);
  TEST_ASSERT_EQUAL(Action::ToggleLock, press(input, "l", true));
  press(input, "");
  TEST_ASSERT_EQUAL(Action::None, press(input, "l ", true));
  press(input, "");
  TEST_ASSERT_EQUAL(Action::ToggleLock, press(input, "l", true));
}

void setUp() { M5Cardputer.Keyboard = {}; }
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_lock_blocks_controls_on_every_screen);
  RUN_TEST(test_chord_consumes_keys_until_full_release);
  RUN_TEST(test_chord_is_detected_when_key_count_does_not_change);
  return UNITY_END();
}
