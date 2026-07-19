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

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_join_root);
  RUN_TEST(test_join_nested);
  RUN_TEST(test_parent_nested);
  RUN_TEST(test_parent_root);
  RUN_TEST(test_ext_case);
  return UNITY_END();
}
