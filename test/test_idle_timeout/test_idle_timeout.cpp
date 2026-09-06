#include <unity.h>
#include "idle_timeout.hpp"

void test_commands_restart_the_full_interval() {
  IdleTimeout timer;
  timer.reset(100);
  TEST_ASSERT_FALSE(timer.expired(300099, 300000, false));
  timer.reset(300099);
  TEST_ASSERT_FALSE(timer.expired(600098, 300000, false));
  TEST_ASSERT_TRUE(timer.expired(600099, 300000, false));
  TEST_ASSERT_FALSE(timer.expired(3600000, 0, false));
}

void test_playback_and_its_end_prevent_early_shutdown() {
  IdleTimeout timer;
  TEST_ASSERT_FALSE(timer.expired(3600000, 300000, true));
  TEST_ASSERT_FALSE(timer.expired(7200000, 300000, true));
  TEST_ASSERT_FALSE(timer.expired(7200010, 300000, false));
  TEST_ASSERT_FALSE(timer.expired(7500009, 300000, false));
  TEST_ASSERT_TRUE(timer.expired(7500010, 300000, false));
}

void test_clock_wrap_preserves_the_interval() {
  IdleTimeout timer;
  timer.reset(0xfffffff0u);
  TEST_ASSERT_FALSE(timer.expired(299983, 300000, false));
  TEST_ASSERT_TRUE(timer.expired(299984, 300000, false));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_commands_restart_the_full_interval);
  RUN_TEST(test_playback_and_its_end_prevent_early_shutdown);
  RUN_TEST(test_clock_wrap_preserves_the_interval);
  return UNITY_END();
}
