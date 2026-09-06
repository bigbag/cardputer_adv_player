#include <unity.h>
#include "audio_dsp.hpp"

void test_mono_equal_channels() {
  TEST_ASSERT_EQUAL_INT16(10000, audio_dsp::stereoToMono(10000, 10000));
}

void test_mono_left_only() {
  TEST_ASSERT_EQUAL_INT16(5000, audio_dsp::stereoToMono(10000, 0));
}

void test_mono_right_only() {
  TEST_ASSERT_EQUAL_INT16(5000, audio_dsp::stereoToMono(0, 10000));
}

// Anti-phase cancellation is the expected mono result, not a defect.
void test_mono_antiphase_cancels() {
  TEST_ASSERT_EQUAL_INT16(0, audio_dsp::stereoToMono(10000, -10000));
}

// Identical channels must not double in level.
void test_mono_full_scale_positive() {
  TEST_ASSERT_EQUAL_INT16(32767, audio_dsp::stereoToMono(32767, 32767));
}

void test_mono_full_scale_negative() {
  TEST_ASSERT_EQUAL_INT16(-32768, audio_dsp::stereoToMono(-32768, -32768));
}

// Inexact means truncate toward zero, not floor.
void test_mono_inexact_mean_truncates_toward_zero() {
  TEST_ASSERT_EQUAL_INT16(2, audio_dsp::stereoToMono(3, 2));
  TEST_ASSERT_EQUAL_INT16(-2, audio_dsp::stereoToMono(-3, -2));
}

void test_gain_clamps_below_zero() {
  TEST_ASSERT_EQUAL_INT32(audio_dsp::volumeToGainQ15(0),
                          audio_dsp::volumeToGainQ15(-5));
}

void test_gain_clamps_above_hundred() {
  TEST_ASSERT_EQUAL_INT32(audio_dsp::volumeToGainQ15(100),
                          audio_dsp::volumeToGainQ15(105));
}

void test_gain_mute() {
  TEST_ASSERT_EQUAL_INT32(0, audio_dsp::volumeToGainQ15(0));
}

void test_gain_unity() {
  TEST_ASSERT_EQUAL_INT32(32768, audio_dsp::volumeToGainQ15(100));
}

void test_gain_midpoint_matches_previous_quarter_volume() {
  const int32_t gain = audio_dsp::volumeToGainQ15(50);
  TEST_ASSERT_EQUAL_INT16(625, audio_dsp::applyGainQ15(10000, gain));
}

void test_gain_low_levels_follow_q15_precision() {
  TEST_ASSERT_EQUAL_INT32(0, audio_dsp::volumeToGainQ15(7));
  TEST_ASSERT_EQUAL_INT32(1, audio_dsp::volumeToGainQ15(8));
}

void test_gain_monotonic_across_ui_settings() {
  int32_t prev = audio_dsp::volumeToGainQ15(0);
  for (int p = 1; p <= 100; ++p) {
    const int32_t g = audio_dsp::volumeToGainQ15(p);
    TEST_ASSERT_TRUE(g >= prev);
    TEST_ASSERT_TRUE(g >= 0);
    TEST_ASSERT_TRUE(g <= 32768);
    prev = g;
  }
}

void test_apply_gain_preserves_endpoints_at_unity() {
  TEST_ASSERT_EQUAL_INT16(32767, audio_dsp::applyGainQ15(32767, 32768));
  TEST_ASSERT_EQUAL_INT16(-32768, audio_dsp::applyGainQ15(-32768, 32768));
}

void test_apply_gain_full_scale_positive() {
  TEST_ASSERT_EQUAL_INT16(0, audio_dsp::applyGainQ15(32767, 0));
  for (int p = 1; p <= 100; ++p) {
    const int16_t out =
        audio_dsp::applyGainQ15(32767, audio_dsp::volumeToGainQ15(p));
    TEST_ASSERT_TRUE(out >= 0);
  }
}

void test_apply_gain_full_scale_negative() {
  TEST_ASSERT_EQUAL_INT16(0, audio_dsp::applyGainQ15(-32768, 0));
  for (int p = 1; p <= 100; ++p) {
    const int16_t out =
        audio_dsp::applyGainQ15(-32768, audio_dsp::volumeToGainQ15(p));
    TEST_ASSERT_TRUE(out <= 0);
  }
}

// Inexact negative products truncate toward zero, not floor.
void test_apply_gain_inexact_truncates_toward_zero() {
  TEST_ASSERT_EQUAL_INT16(-16383, audio_dsp::applyGainQ15(-32767, 16384));
}

void test_apply_gain_mute_is_silence() {
  TEST_ASSERT_EQUAL_INT16(0, audio_dsp::applyGainQ15(123, 0));
  TEST_ASSERT_EQUAL_INT16(0, audio_dsp::applyGainQ15(-123, 0));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mono_equal_channels);
  RUN_TEST(test_mono_left_only);
  RUN_TEST(test_mono_right_only);
  RUN_TEST(test_mono_antiphase_cancels);
  RUN_TEST(test_mono_full_scale_positive);
  RUN_TEST(test_mono_full_scale_negative);
  RUN_TEST(test_mono_inexact_mean_truncates_toward_zero);
  RUN_TEST(test_gain_clamps_below_zero);
  RUN_TEST(test_gain_clamps_above_hundred);
  RUN_TEST(test_gain_mute);
  RUN_TEST(test_gain_unity);
  RUN_TEST(test_gain_midpoint_matches_previous_quarter_volume);
  RUN_TEST(test_gain_low_levels_follow_q15_precision);
  RUN_TEST(test_gain_monotonic_across_ui_settings);
  RUN_TEST(test_apply_gain_preserves_endpoints_at_unity);
  RUN_TEST(test_apply_gain_full_scale_positive);
  RUN_TEST(test_apply_gain_full_scale_negative);
  RUN_TEST(test_apply_gain_inexact_truncates_toward_zero);
  RUN_TEST(test_apply_gain_mute_is_silence);
  return UNITY_END();
}
