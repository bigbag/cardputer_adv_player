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

void test_changed_path_keeps_previous_file_in_recent() {
  Settings settings;
  settings.load();
  settings.setLastPath("/a.mp3");
  settings.setLastPositionMs(100);
  settings.setLastPath("/b.mp3");
  settings.setLastPositionMs(200);

  TEST_ASSERT_EQUAL_UINT(2, settings.recentCount());
  TEST_ASSERT_EQUAL_STRING("/b.mp3", settings.recentEntry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(200, settings.recentEntry(0)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/a.mp3", settings.recentEntry(1)->path);
  TEST_ASSERT_EQUAL_UINT32(100, settings.recentEntry(1)->positionMs);
}

void test_recent_round_trip_persists_five_files() {
  Settings settings;
  settings.load();
  settings.setLastPath("/1.mp3");
  settings.setLastPositionMs(11);
  settings.setLastPath("/2.mp3");
  settings.setLastPositionMs(22);
  settings.setLastPath("/3.mp3");
  settings.setLastPositionMs(33);
  settings.setLastPath("/4.mp3");
  settings.setLastPositionMs(44);
  settings.setLastPath("/5.mp3");
  settings.setLastPositionMs(55);
  TEST_ASSERT_TRUE(settings.save());

  Settings restarted;
  restarted.load();
  TEST_ASSERT_EQUAL_STRING("/5.mp3", restarted.lastPath());
  TEST_ASSERT_EQUAL_UINT32(55, restarted.lastPositionMs());
  TEST_ASSERT_EQUAL_UINT(5, restarted.recentCount());
  TEST_ASSERT_EQUAL_STRING("/5.mp3", restarted.recentEntry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(55, restarted.recentEntry(0)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/4.mp3", restarted.recentEntry(1)->path);
  TEST_ASSERT_EQUAL_UINT32(44, restarted.recentEntry(1)->positionMs);
  TEST_ASSERT_EQUAL_STRING("/3.mp3", restarted.recentEntry(2)->path);
  TEST_ASSERT_EQUAL_STRING("/2.mp3", restarted.recentEntry(3)->path);
  TEST_ASSERT_EQUAL_STRING("/1.mp3", restarted.recentEntry(4)->path);
  TEST_ASSERT_EQUAL_UINT32(11, restarted.recentEntry(4)->positionMs);
}

void test_old_config_seeds_recent_from_last_path() {
  seed(Settings::kConfigPath, "last_path=/old/song.mp3\nlast_position_ms=9\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_UINT(1, settings.recentCount());
  TEST_ASSERT_EQUAL_STRING("/old/song.mp3", settings.recentEntry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(9, settings.recentEntry(0)->positionMs);
}

void test_sixth_recent_file_drops_oldest() {
  Settings settings;
  settings.load();
  settings.setLastPath("/1.mp3");
  settings.setLastPath("/2.mp3");
  settings.setLastPath("/3.mp3");
  settings.setLastPath("/4.mp3");
  settings.setLastPath("/5.mp3");
  settings.setLastPath("/6.mp3");
  TEST_ASSERT_EQUAL_UINT(5, settings.recentCount());
  TEST_ASSERT_EQUAL_STRING("/6.mp3", settings.recentEntry(0)->path);
  TEST_ASSERT_EQUAL_STRING("/2.mp3", settings.recentEntry(4)->path);
  TEST_ASSERT_NULL(settings.recentEntry(5));
}

void test_remove_recent_syncs_last_path() {
  Settings settings;
  settings.load();
  settings.setLastPath("/a.mp3");
  settings.setLastPositionMs(1);
  settings.setLastPath("/b.mp3");
  settings.setLastPositionMs(2);
  TEST_ASSERT_TRUE(settings.removeRecent(0));
  TEST_ASSERT_EQUAL_STRING("/a.mp3", settings.lastPath());
  TEST_ASSERT_EQUAL_UINT32(1, settings.lastPositionMs());
  TEST_ASSERT_EQUAL_UINT(1, settings.recentCount());
}

void test_recent_keys_load_when_last_path_is_absent() {
  seed(Settings::kConfigPath,
       "recent_0_path=/r0.mp3\nrecent_0_position_ms=10\n"
       "recent_1_path=/r1.mp3\nrecent_1_position_ms=20\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_STRING("/r0.mp3", settings.lastPath());
  TEST_ASSERT_EQUAL_UINT32(10, settings.lastPositionMs());
  TEST_ASSERT_EQUAL_UINT(2, settings.recentCount());
  TEST_ASSERT_EQUAL_STRING("/r1.mp3", settings.recentEntry(1)->path);
  TEST_ASSERT_EQUAL_UINT32(20, settings.recentEntry(1)->positionMs);
}

void test_last_path_wins_over_recent_slot_zero() {
  seed(Settings::kConfigPath,
       "recent_0_path=/old.mp3\nrecent_0_position_ms=1\n"
       "recent_1_path=/keep.mp3\nrecent_1_position_ms=2\n"
       "last_path=/now.mp3\nlast_position_ms=9\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_STRING("/now.mp3", settings.lastPath());
  TEST_ASSERT_EQUAL_UINT32(9, settings.lastPositionMs());
  TEST_ASSERT_EQUAL_STRING("/now.mp3", settings.recentEntry(0)->path);
  TEST_ASSERT_EQUAL_STRING("/old.mp3", settings.recentEntry(1)->path);
  TEST_ASSERT_EQUAL_STRING("/keep.mp3", settings.recentEntry(2)->path);
}

void test_recent_position_after_path_keeps_path() {
  seed(Settings::kConfigPath,
       "recent_0_path=/book.mp3\nrecent_0_position_ms=1234\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_UINT(1, settings.recentCount());
  TEST_ASSERT_EQUAL_STRING("/book.mp3", settings.recentEntry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(1234, settings.recentEntry(0)->positionMs);
}

void test_old_config_save_writes_recent_keys() {
  seed(Settings::kConfigPath, "last_path=/old/song.mp3\nlast_position_ms=9\n");
  Settings settings;
  settings.load();
  TEST_ASSERT_TRUE(settings.save());
  const std::string text = textOf(Settings::kConfigPath);
  TEST_ASSERT_TRUE(text.find("recent_0_path=/old/song.mp3") != std::string::npos);
  TEST_ASSERT_TRUE(text.find("recent_0_position_ms=9") != std::string::npos);
}

void test_set_last_path_rejects_overlong_without_truncating_recent() {
  Settings settings;
  settings.load();
  settings.setLastPath("/ok.mp3");
  settings.setLastPositionMs(42000);

  char tooLong[cfg::kMaxPathLen + 2];
  tooLong[0] = '/';
  for (size_t i = 1; i < cfg::kMaxPathLen; ++i) tooLong[i] = 'a';
  tooLong[cfg::kMaxPathLen] = '\0';
  TEST_ASSERT_EQUAL_UINT(cfg::kMaxPathLen, std::strlen(tooLong));
  settings.setLastPath(tooLong);

  char relative[cfg::kMaxPathLen + 2];
  for (size_t i = 0; i < cfg::kMaxPathLen; ++i) relative[i] = 'b';
  relative[cfg::kMaxPathLen] = '\0';
  TEST_ASSERT_EQUAL_UINT(cfg::kMaxPathLen, std::strlen(relative));
  TEST_ASSERT_TRUE(relative[0] != '/');
  settings.setLastPath(relative);

  TEST_ASSERT_EQUAL_STRING("/ok.mp3", settings.lastPath());
  TEST_ASSERT_EQUAL_UINT32(42000, settings.lastPositionMs());
  TEST_ASSERT_EQUAL_UINT(1, settings.recentCount());
  TEST_ASSERT_EQUAL_STRING("/ok.mp3", settings.recentEntry(0)->path);
  TEST_ASSERT_EQUAL_UINT32(42000, settings.recentEntry(0)->positionMs);
}



void test_overlong_recent_path_key_is_not_loaded_truncated() {
  std::string text = "recent_0_path=/";
  text.append(cfg::kMaxPathLen - 1, 'a');
  text += "\n";
  seed(Settings::kConfigPath, text.c_str());

  Settings settings;
  settings.load();
  TEST_ASSERT_EQUAL_UINT(0, settings.recentCount());
  TEST_ASSERT_EQUAL_STRING("", settings.lastPath());
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
  RUN_TEST(test_changed_path_keeps_previous_file_in_recent);
  RUN_TEST(test_recent_round_trip_persists_five_files);
  RUN_TEST(test_old_config_seeds_recent_from_last_path);
  RUN_TEST(test_sixth_recent_file_drops_oldest);
  RUN_TEST(test_remove_recent_syncs_last_path);
  RUN_TEST(test_recent_keys_load_when_last_path_is_absent);
  RUN_TEST(test_last_path_wins_over_recent_slot_zero);
  RUN_TEST(test_recent_position_after_path_keeps_path);
  RUN_TEST(test_old_config_save_writes_recent_keys);
  RUN_TEST(test_set_last_path_rejects_overlong_without_truncating_recent);
  RUN_TEST(test_overlong_recent_path_key_is_not_loaded_truncated);




  return UNITY_END();
}
