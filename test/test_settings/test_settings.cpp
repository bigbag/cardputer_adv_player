#include <unity.h>
#include "settings.hpp"
#include "SD.h"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Seed one file in the fake SD card.
static void seed(const char* path, const char* text) {
  SD.files[path] = std::vector<uint8_t>(text, text + std::strlen(text));
}

// Return the full content of one file, or "" when it is absent.
static std::string textOf(const char* path) {
  auto it = SD.files.find(path);
  if (it == SD.files.end()) return std::string();
  return std::string(it->second.begin(), it->second.end());
}

void test_round_trip_persists_the_pair() {
  Settings settings;
  settings.load();
  settings.setLastPath("/Music/book.mp3");
  settings.setLastPositionMs(42000);
  TEST_ASSERT_TRUE(settings.save());
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigTmpPath));

  Settings restarted;
  restarted.load();
  TEST_ASSERT_EQUAL_STRING("/Music/book.mp3", restarted.lastPath());
  TEST_ASSERT_EQUAL_UINT32(42000, restarted.lastPositionMs());
  TEST_ASSERT_TRUE(textOf(Settings::kConfigPath).find("last_position_ms=42000") !=
                   std::string::npos);
}

void test_changed_path_resets_position_same_path_keeps_it() {
  Settings settings;
  settings.load();
  settings.setLastPath("/Music/a.mp3");
  settings.setLastPositionMs(100);
  TEST_ASSERT_EQUAL_UINT32(100, settings.lastPositionMs());

  settings.setLastPath("/Music/a.mp3");
  TEST_ASSERT_EQUAL_UINT32(100, settings.lastPositionMs());

  settings.setLastPath("/Music/b.mp3");
  TEST_ASSERT_EQUAL_UINT32(0, settings.lastPositionMs());
}

void test_pair_loads_in_any_line_order() {
  seed(Settings::kConfigPath,
       "last_position_ms=2500\nlast_path=/m/f.mp3\n");
  Settings first;
  first.load();
  TEST_ASSERT_EQUAL_STRING("/m/f.mp3", first.lastPath());
  TEST_ASSERT_EQUAL_UINT32(2500, first.lastPositionMs());

  seed(Settings::kConfigPath,
       "last_path=/m/f.mp3\nlast_position_ms=2500\n");
  Settings second;
  second.load();
  TEST_ASSERT_EQUAL_STRING("/m/f.mp3", second.lastPath());
  TEST_ASSERT_EQUAL_UINT32(2500, second.lastPositionMs());
}

void test_old_config_without_position_loads_zero() {
  seed(Settings::kConfigPath, "volume=50\nlast_path=/old/song.mp3\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_STRING("/old/song.mp3", settings.lastPath());
  TEST_ASSERT_EQUAL_UINT32(0, settings.lastPositionMs());
  TEST_ASSERT_EQUAL_INT(50, settings.volumePercent());
}

void test_invalid_position_values_are_rejected() {
  seed(Settings::kConfigPath,
       "last_position_ms=-5\nlast_position_ms=abc\nlast_position_ms=12x\n"
       "last_position_ms=4294967296\nlast_path=/x.mp3\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_UINT32(0, settings.lastPositionMs());

  // A value at the uint32 limit is valid. An invalid value never clobbers it.
  seed(Settings::kConfigPath,
       "last_position_ms=4294967295\nlast_position_ms=1x\n");
  Settings boundary;
  boundary.load();
  TEST_ASSERT_EQUAL_UINT32(4294967295u, boundary.lastPositionMs());
}

void test_short_write_keeps_the_previous_config() {
  Settings settings;
  settings.load();
  settings.setLastPath("/m/one.mp3");
  settings.setLastPositionMs(1000);
  TEST_ASSERT_TRUE(settings.save());

  settings.setLastPositionMs(2000);
  SD.shortWrites = 1;
  TEST_ASSERT_FALSE(settings.save());
  SD.shortWrites = 0;

  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigTmpPath));
  TEST_ASSERT_TRUE(textOf(Settings::kConfigPath).find("last_position_ms=1000") !=
                   std::string::npos);

  Settings restarted;
  restarted.load();
  TEST_ASSERT_EQUAL_UINT32(1000, restarted.lastPositionMs());
}

void test_backup_rename_failure_keeps_the_current_config() {
  Settings settings;
  settings.load();
  settings.setLastPath("/m/one.mp3");
  settings.setLastPositionMs(1000);
  TEST_ASSERT_TRUE(settings.save());

  settings.setLastPositionMs(2000);
  SD.failRenameSrc.insert(Settings::kConfigPath);
  TEST_ASSERT_FALSE(settings.save());
  SD.failRenameSrc.clear();

  TEST_ASSERT_TRUE(textOf(Settings::kConfigPath).find("last_position_ms=1000") !=
                   std::string::npos);
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigTmpPath));
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigBackupPath));
}

void test_promotion_failure_restores_the_previous_config() {
  Settings settings;
  settings.load();
  settings.setLastPath("/m/one.mp3");
  settings.setLastPositionMs(1000);
  TEST_ASSERT_TRUE(settings.save());

  settings.setLastPositionMs(2000);
  SD.failRenameSrc.insert(Settings::kConfigTmpPath);
  TEST_ASSERT_FALSE(settings.save());
  SD.failRenameSrc.clear();

  TEST_ASSERT_TRUE(textOf(Settings::kConfigPath).find("last_position_ms=1000") !=
                   std::string::npos);
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigTmpPath));
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigBackupPath));

  Settings restarted;
  restarted.load();
  TEST_ASSERT_EQUAL_UINT32(1000, restarted.lastPositionMs());
}

void test_restart_with_only_a_backup_recovers_and_heals() {
  Settings settings;
  settings.load();
  settings.setLastPath("/m/one.mp3");
  settings.setLastPositionMs(1000);
  TEST_ASSERT_TRUE(settings.save());

  // The promotion and the restore both fail. The backup holds the last
  // complete config.
  settings.setLastPositionMs(2000);
  SD.failRenameSrc.insert(Settings::kConfigTmpPath);
  SD.failRenameSrc.insert(Settings::kConfigBackupPath);
  TEST_ASSERT_FALSE(settings.save());
  SD.failRenameSrc.clear();

  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigPath));
  TEST_ASSERT_TRUE(SD.exists(Settings::kConfigBackupPath));
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigTmpPath));

  Settings restarted;
  restarted.load();
  TEST_ASSERT_EQUAL_STRING("/m/one.mp3", restarted.lastPath());
  TEST_ASSERT_EQUAL_UINT32(1000, restarted.lastPositionMs());

  // The next successful save rebuilds the canonical config.
  TEST_ASSERT_TRUE(restarted.save());
  Settings healed;
  healed.load();
  TEST_ASSERT_EQUAL_UINT32(1000, healed.lastPositionMs());
  TEST_ASSERT_TRUE(SD.exists(Settings::kConfigPath));
}

void test_incomplete_tmp_file_is_never_loaded() {
  seed(Settings::kConfigTmpPath, "volume=1\nlast_path=/tmp/song.mp3\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_INT(cfg::kDefaultVolumePercent, settings.volumePercent());
  TEST_ASSERT_EQUAL_STRING("", settings.lastPath());
  TEST_ASSERT_EQUAL_UINT32(0, settings.lastPositionMs());
}

void test_legacy_migration_writes_config_and_removes_legacy() {
  seed(Settings::kConfigLegacyPath,
       "volume=40\nlast_path=/legacy/song.mp3\nlast_position_ms=7000\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_STRING("/legacy/song.mp3", settings.lastPath());
  TEST_ASSERT_EQUAL_UINT32(7000, settings.lastPositionMs());
  TEST_ASSERT_TRUE(SD.exists(Settings::kConfigPath));
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigLegacyPath));
}

void test_failed_migration_keeps_the_legacy_file() {
  seed(Settings::kConfigLegacyPath,
       "volume=40\nlast_path=/legacy/song.mp3\n");
  SD.shortWrites = 1;
  Settings settings;
  settings.load();
  SD.shortWrites = 0;

  TEST_ASSERT_EQUAL_INT(40, settings.volumePercent());
  TEST_ASSERT_TRUE(SD.exists(Settings::kConfigLegacyPath));
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigPath));
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigTmpPath));
}

void test_failed_file_operations_report_failure() {
  Settings settings;
  settings.load();
  settings.setLastPath("/m/x.mp3");
  settings.setLastPositionMs(5);

  SD.failOpenWrite = true;
  TEST_ASSERT_FALSE(settings.save());
  SD.failOpenWrite = false;
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigPath));

  SD.reset();
  SD.failMkdir = true;
  TEST_ASSERT_FALSE(settings.save());
  SD.failMkdir = false;
  TEST_ASSERT_FALSE(SD.exists(Settings::kConfigPath));
}

void test_stale_backup_removal_failure_keeps_the_current_bookmark() {
  Settings settings;
  settings.load();
  settings.setLastPath("/m/one.mp3");
  settings.setLastPositionMs(1000);
  TEST_ASSERT_TRUE(settings.save());
  seed(Settings::kConfigBackupPath, "last_path=/old.mp3\nlast_position_ms=10\n");
  SD.failRemove.insert(Settings::kConfigBackupPath);
  settings.setLastPositionMs(2000);
  TEST_ASSERT_FALSE(settings.save());

  Settings restarted;
  restarted.load();
  TEST_ASSERT_EQUAL_STRING("/m/one.mp3", restarted.lastPath());
  TEST_ASSERT_EQUAL_UINT32(1000, restarted.lastPositionMs());
  SD.failRemove.clear();
  TEST_ASSERT_TRUE(settings.save());
  restarted.load();
  TEST_ASSERT_EQUAL_UINT32(2000, restarted.lastPositionMs());
}

void test_stale_temporary_file_can_be_truncated_when_remove_fails() {
  seed(Settings::kConfigTmpPath, "last_path=/old.mp3\nlast_position_ms=1000\n");
  SD.failRemove.insert(Settings::kConfigTmpPath);
  Settings settings;
  settings.load();
  settings.setLastPath("/new.mp3");
  settings.setLastPositionMs(2000);
  TEST_ASSERT_TRUE(settings.save());

  Settings restarted;
  restarted.load();
  TEST_ASSERT_EQUAL_STRING("/new.mp3", restarted.lastPath());
  TEST_ASSERT_EQUAL_UINT32(2000, restarted.lastPositionMs());
}

void setUp() { SD.reset(); }
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_persists_the_pair);
  RUN_TEST(test_changed_path_resets_position_same_path_keeps_it);
  RUN_TEST(test_pair_loads_in_any_line_order);
  RUN_TEST(test_old_config_without_position_loads_zero);
  RUN_TEST(test_invalid_position_values_are_rejected);
  RUN_TEST(test_short_write_keeps_the_previous_config);
  RUN_TEST(test_backup_rename_failure_keeps_the_current_config);
  RUN_TEST(test_promotion_failure_restores_the_previous_config);
  RUN_TEST(test_restart_with_only_a_backup_recovers_and_heals);
  RUN_TEST(test_incomplete_tmp_file_is_never_loaded);
  RUN_TEST(test_legacy_migration_writes_config_and_removes_legacy);
  RUN_TEST(test_failed_migration_keeps_the_legacy_file);
  RUN_TEST(test_failed_file_operations_report_failure);
  RUN_TEST(test_stale_backup_removal_failure_keeps_the_current_bookmark);
  RUN_TEST(test_stale_temporary_file_can_be_truncated_when_remove_fails);
  return UNITY_END();
}
