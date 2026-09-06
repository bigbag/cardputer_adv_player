#include <unity.h>
#include "path_utils.hpp"

void test_join_root() {
  char buf[64];
  TEST_ASSERT_TRUE(path::join(buf, sizeof(buf), "/", "music"));
  TEST_ASSERT_EQUAL_STRING("/music", buf);
}

void test_join_nested() {
  char buf[64];
  TEST_ASSERT_TRUE(path::join(buf, sizeof(buf), "/music", "a.mp3"));
  TEST_ASSERT_EQUAL_STRING("/music/a.mp3", buf);
}

void test_parent_nested() {
  char buf[64];
  TEST_ASSERT_TRUE(path::parent(buf, sizeof(buf), "/music/rock"));
  TEST_ASSERT_EQUAL_STRING("/music", buf);
}

void test_parent_root() {
  char buf[64];
  TEST_ASSERT_TRUE(path::parent(buf, sizeof(buf), "/"));
  TEST_ASSERT_EQUAL_STRING("/", buf);
}

void test_ext_case() {
  TEST_ASSERT_TRUE(path::hasExtInsensitive("Song.MP3", ".mp3"));
  TEST_ASSERT_TRUE(path::hasExtInsensitive("x.Wav", ".wav"));
  TEST_ASSERT_FALSE(path::hasExtInsensitive("x.txt", ".mp3"));
}

void test_flac_is_a_playable_file() {
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(EntryKind::Dir),
                        static_cast<int>(path::kindFromName("track.FLAC")));
  TEST_ASSERT_EQUAL(static_cast<int>(EntryKind::Dir),
                    static_cast<int>(path::kindFromName("track.flac.bak")));
}

void test_utf8_track_path() {
  const char* name = u8"Очень длинное русское название песни — 日本語.MP3";
  char full[cfg::kMaxPathLen];
  char base[cfg::kMaxNameLen];
  char dir[cfg::kMaxPathLen];
  TEST_ASSERT_TRUE(path::join(full, sizeof(full), u8"/Музыка", name));
  path::fileName(base, sizeof(base), full);
  TEST_ASSERT_EQUAL_STRING(name, base);
  TEST_ASSERT_TRUE(path::hasExtInsensitive(base, ".mp3"));
  TEST_ASSERT_TRUE(path::parent(dir, sizeof(dir), full));
  TEST_ASSERT_EQUAL_STRING(u8"/Музыка", dir);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_join_root);
  RUN_TEST(test_join_nested);
  RUN_TEST(test_parent_nested);
  RUN_TEST(test_parent_root);
  RUN_TEST(test_ext_case);
  RUN_TEST(test_flac_is_a_playable_file);
  RUN_TEST(test_utf8_track_path);
  return UNITY_END();
}
