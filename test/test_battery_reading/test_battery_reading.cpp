#include <unity.h>
#include <initializer_list>
#include "battery_reading.hpp"

void sampleBatch(BatteryReading& battery, uint32_t start, int millivolts) {
  for (uint32_t i = 0; i < cfg::kBatterySamples; ++i) {
    const uint32_t now = start + i * cfg::kBatterySampleMs;
    TEST_ASSERT_TRUE(battery.due(now));
    battery.addSample(now, millivolts);
  }
}

void test_average_rejects_single_sample_jumps_and_holds_complete_reading() {
  BatteryReading battery;
  battery.addSample(0, 3684);
  for (uint32_t i = 1; i < cfg::kBatterySamples; ++i) {
    TEST_ASSERT_EQUAL_INT(-1, battery.millivolts());
    battery.addSample(i * cfg::kBatterySampleMs, 3600);
  }
  TEST_ASSERT_EQUAL_INT(3605, battery.millivolts());
  TEST_ASSERT_EQUAL_INT(38, battery.level());

  const uint32_t next = cfg::kBatteryUpdateMs;
  for (uint32_t i = 0; i < cfg::kBatterySamples; ++i) {
    TEST_ASSERT_EQUAL_INT(3605, battery.millivolts());
    battery.addSample(next + i * cfg::kBatterySampleMs, 3800);
  }
  TEST_ASSERT_EQUAL_INT(3800, battery.millivolts());
  TEST_ASSERT_EQUAL_INT(62, battery.level());
}

void test_invalid_batch_does_not_become_empty_or_full_battery() {
  BatteryReading battery;
  sampleBatch(battery, 0, 3600);
  uint32_t start = cfg::kBatteryUpdateMs;
  for (int invalid : {0, 5000}) {
    battery.addSample(start, invalid);
    for (uint32_t i = 1; i < cfg::kBatterySamples; ++i) {
      battery.addSample(start + i * cfg::kBatterySampleMs, 3600);
    }
    TEST_ASSERT_EQUAL_INT(-1, battery.millivolts());
    TEST_ASSERT_EQUAL_INT(-1, battery.level());
    start += cfg::kBatteryUpdateMs;
    sampleBatch(battery, start, 3590);
    TEST_ASSERT_EQUAL_INT(3590, battery.millivolts());
    TEST_ASSERT_EQUAL_INT(36, battery.level());
    start += cfg::kBatteryUpdateMs;
  }
  sampleBatch(battery, start, 3300);
  TEST_ASSERT_EQUAL_INT(0, battery.level());
  sampleBatch(battery, start + cfg::kBatteryUpdateMs, 4200);
  TEST_ASSERT_EQUAL_INT(100, battery.level());
}

void test_sampling_clock_wrap_and_wake_discard_partial_batch() {
  BatteryReading battery;
  const uint32_t start = 0xfffffff0u;
  TEST_ASSERT_TRUE(battery.due(start));
  battery.addSample(start, 4400);
  TEST_ASSERT_FALSE(battery.due(start + cfg::kBatterySampleMs - 1));
  TEST_ASSERT_TRUE(battery.due(start + cfg::kBatterySampleMs));
  for (uint32_t i = 1; i < cfg::kBatterySamples; ++i) {
    battery.addSample(start + i * cfg::kBatterySampleMs, 4400);
  }
  TEST_ASSERT_FALSE(battery.due(start + cfg::kBatteryUpdateMs - 1));
  TEST_ASSERT_TRUE(battery.due(start + cfg::kBatteryUpdateMs));
  battery.addSample(start + cfg::kBatteryUpdateMs, 4400);
  battery = {};
  TEST_ASSERT_EQUAL_INT(-1, battery.level());
  sampleBatch(battery, 10000, 3600);
  TEST_ASSERT_EQUAL_INT(3600, battery.millivolts());
  TEST_ASSERT_EQUAL_INT(37, battery.level());
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_average_rejects_single_sample_jumps_and_holds_complete_reading);
  RUN_TEST(test_invalid_batch_does_not_become_empty_or_full_battery);
  RUN_TEST(test_sampling_clock_wrap_and_wake_discard_partial_batch);
  return UNITY_END();
}
