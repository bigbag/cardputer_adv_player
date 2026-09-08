#include <unity.h>
#include "bookmark_checkpoint.hpp"

void test_playback_checkpoints_do_not_write_each_loop() {
  BookmarkCheckpoint checkpoint;
  TEST_ASSERT_TRUE(checkpoint.update(100, PlayState::Playing, 0, true));
  checkpoint.attempted(100, true);
  TEST_ASSERT_FALSE(checkpoint.update(10099, PlayState::Playing, 9999, false));
  TEST_ASSERT_TRUE(checkpoint.update(10100, PlayState::Playing, 10000, false));
  checkpoint.attempted(10100, true);
  TEST_ASSERT_FALSE(checkpoint.update(10110, PlayState::Playing, 10010, false));
}

void test_pause_seek_restart_and_completion_save_without_delay() {
  BookmarkCheckpoint checkpoint;
  checkpoint.update(100, PlayState::Playing, 12000, true);
  checkpoint.attempted(100, true);
  TEST_ASSERT_TRUE(checkpoint.update(200, PlayState::Paused, 12100, false));
  checkpoint.attempted(200, true);
  TEST_ASSERT_FALSE(checkpoint.update(30000, PlayState::Paused, 12100, false));
  TEST_ASSERT_TRUE(checkpoint.update(30100, PlayState::Paused, 17100, false));
  checkpoint.attempted(30100, true);
  TEST_ASSERT_TRUE(checkpoint.update(30200, PlayState::Playing, 0, false));
  checkpoint.attempted(30200, true);
  checkpoint.update(30300, PlayState::Playing, 100, false);
  TEST_ASSERT_TRUE(checkpoint.update(30400, PlayState::Done, 0, false));
  checkpoint.attempted(30400, true);
  TEST_ASSERT_FALSE(checkpoint.update(50000, PlayState::Done, 0, false));
}

void test_new_file_replaces_bookmark_even_at_same_position() {
  BookmarkCheckpoint checkpoint;
  checkpoint.update(100, PlayState::Playing, 0, true);
  checkpoint.attempted(100, true);
  TEST_ASSERT_TRUE(checkpoint.update(200, PlayState::Playing, 0, true));
}

void test_failed_pause_save_retries_without_a_busy_loop() {
  BookmarkCheckpoint checkpoint;
  checkpoint.update(100, PlayState::Playing, 2000, true);
  checkpoint.attempted(100, true);
  TEST_ASSERT_TRUE(checkpoint.update(200, PlayState::Paused, 2100, false));
  checkpoint.attempted(200, false);
  TEST_ASSERT_FALSE(checkpoint.update(210, PlayState::Paused, 2100, false));
  TEST_ASSERT_FALSE(checkpoint.update(10199, PlayState::Paused, 2100, false));
  TEST_ASSERT_TRUE(checkpoint.update(10200, PlayState::Paused, 2100, false));
  checkpoint.attempted(10200, true);
  TEST_ASSERT_FALSE(checkpoint.update(20200, PlayState::Paused, 2100, false));
}

void test_checkpoint_and_failed_settings_save_survive_clock_wrap() {
  BookmarkCheckpoint checkpoint;
  checkpoint.attempted(0xfffffff0u, false);
  TEST_ASSERT_FALSE(checkpoint.due(9983));
  TEST_ASSERT_TRUE(checkpoint.due(9984));
  checkpoint.attempted(9984, true);
  TEST_ASSERT_FALSE(checkpoint.due(19984));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_playback_checkpoints_do_not_write_each_loop);
  RUN_TEST(test_pause_seek_restart_and_completion_save_without_delay);
  RUN_TEST(test_new_file_replaces_bookmark_even_at_same_position);
  RUN_TEST(test_failed_pause_save_retries_without_a_busy_loop);
  RUN_TEST(test_checkpoint_and_failed_settings_save_survive_clock_wrap);
  return UNITY_END();
}
