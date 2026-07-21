#include <unity.h>
#include "browser_history.hpp"
#include <cstring>

void test_pop_returns_most_recent_frame() {
  BrowserHistory history;
  TEST_ASSERT_TRUE(history.push("/", 5, 1));
  TEST_ASSERT_TRUE(history.push("/Music", 9, 3));

  BrowserHistoryFrame frame{};
  TEST_ASSERT_TRUE(history.peek(&frame));
  TEST_ASSERT_EQUAL_STRING("/Music", frame.path);
  TEST_ASSERT_EQUAL_UINT(9, frame.cursor);
  TEST_ASSERT_EQUAL_UINT(3, frame.scroll);
  history.discardTop();

  TEST_ASSERT_TRUE(history.peek(&frame));
  TEST_ASSERT_EQUAL_STRING("/", frame.path);
  TEST_ASSERT_EQUAL_UINT(5, frame.cursor);
  TEST_ASSERT_EQUAL_UINT(1, frame.scroll);
}

void test_pop_then_enter_replaces_abandoned_branch() {
  BrowserHistory history;
  TEST_ASSERT_TRUE(history.push("/", 2, 0));       // enter A
  TEST_ASSERT_TRUE(history.push("/A", 6, 2));      // enter B
  history.discardTop();                             // Back from B to A
  TEST_ASSERT_TRUE(history.push("/A", 4, 1));      // enter C

  BrowserHistoryFrame frame{};
  TEST_ASSERT_TRUE(history.peek(&frame));
  TEST_ASSERT_EQUAL_STRING("/A", frame.path);
  TEST_ASSERT_EQUAL_UINT(4, frame.cursor);
  history.discardTop();
  TEST_ASSERT_TRUE(history.peek(&frame));
  TEST_ASSERT_EQUAL_STRING("/", frame.path);
}

void test_clear_and_capacity_are_safe() {
  BrowserHistory history;
  for (size_t i = 0; i < BrowserHistory::kCapacity; ++i) {
    TEST_ASSERT_TRUE(history.push("/A", i, i));
  }
  TEST_ASSERT_FALSE(history.push("/overflow", 0, 0));
  BrowserHistoryFrame frame{};
  TEST_ASSERT_FALSE(history.peek(&frame));
  TEST_ASSERT_EQUAL_UINT(0, history.size());

  TEST_ASSERT_TRUE(history.push("/A", 0, 0));
  history.clear();
  TEST_ASSERT_FALSE(history.peek(&frame));
  TEST_ASSERT_EQUAL_UINT(0, history.size());
}

void test_clamp_position_keeps_selected_row_visible() {
  size_t cursor = 99;
  size_t scroll = 99;
  BrowserHistory::clampPosition(10, 7, &cursor, &scroll);
  TEST_ASSERT_EQUAL_UINT(9, cursor);
  TEST_ASSERT_EQUAL_UINT(3, scroll);

  cursor = 2;
  scroll = 99;
  BrowserHistory::clampPosition(10, 7, &cursor, &scroll);
  TEST_ASSERT_EQUAL_UINT(2, cursor);
  TEST_ASSERT_EQUAL_UINT(2, scroll);

  cursor = 4;
  scroll = 3;
  BrowserHistory::clampPosition(0, 7, &cursor, &scroll);
  TEST_ASSERT_EQUAL_UINT(0, cursor);
  TEST_ASSERT_EQUAL_UINT(0, scroll);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pop_returns_most_recent_frame);
  RUN_TEST(test_pop_then_enter_replaces_abandoned_branch);
  RUN_TEST(test_clear_and_capacity_are_safe);
  RUN_TEST(test_clamp_position_keeps_selected_row_visible);
  return UNITY_END();
}
