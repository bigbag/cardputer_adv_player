#include <unity.h>
#include "recent_list.hpp"
#include <cstring>
#include <cstdio>


void test_touch_puts_file_at_front_with_position() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/books/a.mp3", 12000));
  TEST_ASSERT_EQUAL_UINT(1, recent.size());
  const RecentEntry* e = recent.entry(0);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_STRING("/books/a.mp3", e->path);
  TEST_ASSERT_EQUAL_UINT32(12000, e->positionMs);
}

void test_touch_same_path_updates_position_without_growing() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/a.mp3", 100));
  TEST_ASSERT_TRUE(recent.touch("/a.mp3", 250));
  TEST_ASSERT_EQUAL_UINT(1, recent.size());
  TEST_ASSERT_EQUAL_UINT32(250, recent.entry(0)->positionMs);
}

void test_touch_existing_middle_moves_to_front() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/a.mp3", 1));
  TEST_ASSERT_TRUE(recent.touch("/b.mp3", 2));
  TEST_ASSERT_TRUE(recent.touch("/c.mp3", 3));
  TEST_ASSERT_TRUE(recent.touch("/a.mp3", 99));

  TEST_ASSERT_EQUAL_UINT(3, recent.size());
  TEST_ASSERT_EQUAL_STRING("/a.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(99, recent.entry(0)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/c.mp3", recent.entry(1)->path);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", recent.entry(2)->path);
}

void test_touch_drops_oldest_when_full() {
  RecentList recent;
  for (size_t i = 0; i < RecentList::kCapacity; ++i) {
    char path[16];
    std::snprintf(path, sizeof(path), "/%u.mp3", static_cast<unsigned>(i));
    TEST_ASSERT_TRUE(recent.touch(path, static_cast<uint32_t>(i * 10)));
  }
  TEST_ASSERT_TRUE(recent.touch("/new.mp3", 500));
  TEST_ASSERT_EQUAL_UINT(RecentList::kCapacity, recent.size());
  TEST_ASSERT_EQUAL_STRING("/new.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(500, recent.entry(0)->positionMs);

  char oldest[16];
  std::snprintf(oldest, sizeof(oldest), "/0.mp3");
  for (size_t i = 0; i < recent.size(); ++i) {
    TEST_ASSERT_TRUE(std::strcmp(recent.entry(i)->path, oldest) != 0);
  }
}

void test_touch_same_directory_keeps_each_file_path() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/dir1/a.mp3", 10));
  TEST_ASSERT_TRUE(recent.touch("/dir2/b.mp3", 20));
  TEST_ASSERT_TRUE(recent.touch("/dir1/c.mp3", 30));

  TEST_ASSERT_EQUAL_UINT(3, recent.size());
  TEST_ASSERT_EQUAL_STRING("/dir1/c.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(30, recent.entry(0)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/dir2/b.mp3", recent.entry(1)->path);
  TEST_ASSERT_EQUAL_UINT32(20, recent.entry(1)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/dir1/a.mp3", recent.entry(2)->path);
  TEST_ASSERT_EQUAL_UINT32(10, recent.entry(2)->positionMs);
}

void test_touch_second_file_in_same_directory_keeps_both() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/dir1/a.mp3", 10));
  TEST_ASSERT_TRUE(recent.touch("/dir1/b.mp3", 20));

  TEST_ASSERT_EQUAL_UINT(2, recent.size());
  TEST_ASSERT_EQUAL_STRING("/dir1/b.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(20, recent.entry(0)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/dir1/a.mp3", recent.entry(1)->path);
  TEST_ASSERT_EQUAL_UINT32(10, recent.entry(1)->positionMs);
}

void test_touch_with_own_entry_path_updates_position_only() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/dir1/a.mp3", 10));
  TEST_ASSERT_TRUE(recent.touch(recent.entry(0)->path, 25));
  TEST_ASSERT_EQUAL_UINT(1, recent.size());
  TEST_ASSERT_EQUAL_STRING("/dir1/a.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(25, recent.entry(0)->positionMs);
}

void test_touch_root_files_get_separate_entries() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/a.mp3", 1));
  TEST_ASSERT_TRUE(recent.touch("/b.mp3", 2));
  TEST_ASSERT_EQUAL_UINT(2, recent.size());
}

void test_touch_distinguishes_nested_directories() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/dir/a.mp3", 1));
  TEST_ASSERT_TRUE(recent.touch("/dir/sub/b.mp3", 2));
  TEST_ASSERT_EQUAL_UINT(2, recent.size());
}

void test_touch_new_file_in_full_list_keeps_other_file_from_same_directory() {
  RecentList recent;
  for (size_t i = 0; i < RecentList::kCapacity; ++i) {
    char path[20];
    std::snprintf(path, sizeof(path), "/dir%u/a.mp3", static_cast<unsigned>(i));
    TEST_ASSERT_TRUE(recent.touch(path, static_cast<uint32_t>(i)));
  }

  TEST_ASSERT_TRUE(recent.touch("/dir4/b.mp3", 99));
  TEST_ASSERT_EQUAL_UINT(RecentList::kCapacity, recent.size());
  TEST_ASSERT_EQUAL_STRING("/dir4/b.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(99, recent.entry(0)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/dir4/a.mp3", recent.entry(1)->path);
  TEST_ASSERT_EQUAL_STRING("/dir3/a.mp3", recent.entry(2)->path);
  TEST_ASSERT_EQUAL_STRING("/dir2/a.mp3", recent.entry(3)->path);
  TEST_ASSERT_EQUAL_STRING("/dir1/a.mp3", recent.entry(4)->path);
}

void test_touch_rejects_invalid_path() {
  RecentList recent;
  TEST_ASSERT_FALSE(recent.touch(nullptr, 1));
  TEST_ASSERT_FALSE(recent.touch("", 1));
  TEST_ASSERT_FALSE(recent.touch("rel.mp3", 1));
  TEST_ASSERT_EQUAL_UINT(0, recent.size());
  TEST_ASSERT_NULL(recent.entry(0));
}

void test_touch_rejects_overlong_path() {
  RecentList recent;
  char tooLong[cfg::kMaxPathLen + 2];
  tooLong[0] = '/';
  for (size_t i = 1; i < cfg::kMaxPathLen; ++i) tooLong[i] = 'a';
  tooLong[cfg::kMaxPathLen] = '\0';
  TEST_ASSERT_FALSE(recent.touch(tooLong, 1));
  TEST_ASSERT_EQUAL_UINT(0, recent.size());
  recent.setSlot(0, tooLong, 9);
  TEST_ASSERT_EQUAL_UINT(0, recent.size());

  char maxOk[cfg::kMaxPathLen];
  maxOk[0] = '/';
  for (size_t i = 1; i < cfg::kMaxPathLen - 1; ++i) maxOk[i] = 'b';
  maxOk[cfg::kMaxPathLen - 1] = '\0';
  TEST_ASSERT_TRUE(recent.touch(maxOk, 2));
  TEST_ASSERT_EQUAL_STRING(maxOk, recent.entry(0)->path);
}


void test_remove_at_compacts_list() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/a.mp3", 1));
  TEST_ASSERT_TRUE(recent.touch("/b.mp3", 2));
  TEST_ASSERT_TRUE(recent.touch("/c.mp3", 3));
  TEST_ASSERT_TRUE(recent.removeAt(1));
  TEST_ASSERT_EQUAL_UINT(2, recent.size());
  TEST_ASSERT_EQUAL_STRING("/c.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_STRING("/a.mp3", recent.entry(1)->path);
  TEST_ASSERT_FALSE(recent.removeAt(2));
  TEST_ASSERT_NULL(recent.entry(2));
}

void test_set_slot_then_compact_skips_holes() {
  RecentList recent;
  recent.setSlot(0, "", 0);
  recent.setSlot(2, "/keep.mp3", 40);
  recent.setSlot(1, "/also.mp3", 20);
  recent.compact();
  TEST_ASSERT_EQUAL_UINT(2, recent.size());
  TEST_ASSERT_EQUAL_STRING("/also.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(20, recent.entry(0)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/keep.mp3", recent.entry(1)->path);
  TEST_ASSERT_EQUAL_UINT32(40, recent.entry(1)->positionMs);
}

void test_compact_keeps_different_files_in_one_directory() {
  RecentList recent;
  recent.setSlot(0, "/dir1/a.mp3", 10);
  recent.setSlot(1, "/dir2/b.mp3", 20);
  recent.setSlot(2, "/dir1/c.mp3", 30);
  recent.compact();
  TEST_ASSERT_EQUAL_UINT(3, recent.size());
  TEST_ASSERT_EQUAL_STRING("/dir1/a.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(10, recent.entry(0)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/dir2/b.mp3", recent.entry(1)->path);
  TEST_ASSERT_EQUAL_STRING("/dir1/c.mp3", recent.entry(2)->path);
  TEST_ASSERT_EQUAL_UINT32(30, recent.entry(2)->positionMs);
}

void test_compact_keeps_distinct_root_files() {
  RecentList recent;
  recent.setSlot(0, "/a.mp3", 10);
  recent.setSlot(1, "/b.mp3", 20);
  recent.compact();
  TEST_ASSERT_EQUAL_UINT(2, recent.size());
  TEST_ASSERT_EQUAL_STRING("/a.mp3", recent.entry(0)->path);
  TEST_ASSERT_EQUAL_STRING("/b.mp3", recent.entry(1)->path);
}

void test_set_position_updates_one_slot() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/a.mp3", 1));
  TEST_ASSERT_TRUE(recent.setPosition(0, 777));
  TEST_ASSERT_EQUAL_UINT32(777, recent.entry(0)->positionMs);
  TEST_ASSERT_FALSE(recent.setPosition(1, 1));
}

void test_times_differ_when_only_position_changes() {
  RecentList recent;
  TEST_ASSERT_TRUE(recent.touch("/a.mp3", 1000));
  TEST_ASSERT_TRUE(recent.touch("/b.mp3", 2000));
  uint32_t last[RecentList::kCapacity] = {2000, 1000};
  TEST_ASSERT_FALSE(recent.timesDiffer(last, 2));
  last[0] = 2500;
  TEST_ASSERT_FALSE(recent.timesDiffer(last, 2));
  last[0] = 3000;
  TEST_ASSERT_TRUE(recent.timesDiffer(last, 2));
  TEST_ASSERT_TRUE(recent.timesDiffer(last, 1));
}



void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_touch_puts_file_at_front_with_position);
  RUN_TEST(test_touch_same_path_updates_position_without_growing);
  RUN_TEST(test_touch_existing_middle_moves_to_front);
  RUN_TEST(test_touch_drops_oldest_when_full);
  RUN_TEST(test_touch_same_directory_keeps_each_file_path);
  RUN_TEST(test_touch_second_file_in_same_directory_keeps_both);
  RUN_TEST(test_touch_root_files_get_separate_entries);
  RUN_TEST(test_touch_distinguishes_nested_directories);
  RUN_TEST(test_touch_with_own_entry_path_updates_position_only);
  RUN_TEST(test_touch_new_file_in_full_list_keeps_other_file_from_same_directory);
  RUN_TEST(test_touch_rejects_invalid_path);
  RUN_TEST(test_touch_rejects_overlong_path);

  RUN_TEST(test_remove_at_compacts_list);
  RUN_TEST(test_set_slot_then_compact_skips_holes);
  RUN_TEST(test_set_position_updates_one_slot);
  RUN_TEST(test_compact_keeps_different_files_in_one_directory);
  RUN_TEST(test_compact_keeps_distinct_root_files);
  RUN_TEST(test_times_differ_when_only_position_changes);

  return UNITY_END();
}
