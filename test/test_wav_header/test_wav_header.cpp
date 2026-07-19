#include <unity.h>
#include "decoders/wav_decoder.hpp"
#include <cstring>

static void buildWavHeader(uint8_t* buf, uint16_t channels, uint32_t sampleRate,
                           uint16_t bitsPerSample, uint32_t dataSize,
                           uint16_t formatTag = 1) {
  std::memset(buf, 0, 44);
  std::memcpy(buf, "RIFF", 4);
  uint32_t fileSize = 36 + dataSize;
  buf[4] = fileSize & 0xFF;
  buf[5] = (fileSize >> 8) & 0xFF;
  buf[6] = (fileSize >> 16) & 0xFF;
  buf[7] = (fileSize >> 24) & 0xFF;
  std::memcpy(buf + 8, "WAVE", 4);

  std::memcpy(buf + 12, "fmt ", 4);
  buf[16] = 16; buf[17] = 0; buf[18] = 0; buf[19] = 0;
  buf[20] = formatTag & 0xFF;
  buf[21] = (formatTag >> 8) & 0xFF;
  buf[22] = channels & 0xFF;
  buf[23] = (channels >> 8) & 0xFF;
  buf[24] = sampleRate & 0xFF;
  buf[25] = (sampleRate >> 8) & 0xFF;
  buf[26] = (sampleRate >> 16) & 0xFF;
  buf[27] = (sampleRate >> 24) & 0xFF;
  uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  buf[28] = byteRate & 0xFF;
  buf[29] = (byteRate >> 8) & 0xFF;
  buf[30] = (byteRate >> 16) & 0xFF;
  buf[31] = (byteRate >> 24) & 0xFF;
  uint16_t blockAlign = channels * (bitsPerSample / 8);
  buf[32] = blockAlign & 0xFF;
  buf[33] = (blockAlign >> 8) & 0xFF;
  buf[34] = bitsPerSample & 0xFF;
  buf[35] = (bitsPerSample >> 8) & 0xFF;

  std::memcpy(buf + 36, "data", 4);
  buf[40] = dataSize & 0xFF;
  buf[41] = (dataSize >> 8) & 0xFF;
  buf[42] = (dataSize >> 16) & 0xFF;
  buf[43] = (dataSize >> 24) & 0xFF;
}

void test_valid_stereo_44100() {
  uint8_t hdr[44];
  buildWavHeader(hdr, 2, 44100, 16, 0);
  WavInfo info = wavParseHeader(hdr, sizeof(hdr));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(44100, info.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(2, info.channels);
  TEST_ASSERT_EQUAL_UINT16(16, info.bitsPerSample);
  TEST_ASSERT_EQUAL_UINT32(44, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(0, info.dataSize);
  TEST_ASSERT_NULL(info.error);
}

void test_valid_mono_22050() {
  uint8_t hdr[44];
  buildWavHeader(hdr, 1, 22050, 16, 1024);
  WavInfo info = wavParseHeader(hdr, sizeof(hdr));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(22050, info.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(1, info.channels);
  TEST_ASSERT_EQUAL_UINT32(1024, info.dataSize);
}

void test_reject_ieee_float() {
  uint8_t hdr[44];
  buildWavHeader(hdr, 2, 44100, 16, 0, 3);
  WavInfo info = wavParseHeader(hdr, sizeof(hdr));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_EQUAL_STRING("not pcm", info.error);
}

void test_reject_8bit() {
  uint8_t hdr[44];
  buildWavHeader(hdr, 2, 44100, 8, 0);
  WavInfo info = wavParseHeader(hdr, sizeof(hdr));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_EQUAL_STRING("bad bps", info.error);
}

void test_reject_not_riff() {
  uint8_t hdr[44];
  buildWavHeader(hdr, 2, 44100, 16, 0);
  std::memcpy(hdr, "XXXX", 4);
  WavInfo info = wavParseHeader(hdr, sizeof(hdr));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_EQUAL_STRING("not riff", info.error);
}

void test_reject_not_wave() {
  uint8_t hdr[44];
  buildWavHeader(hdr, 2, 44100, 16, 0);
  std::memcpy(hdr + 8, "AVI ", 4);
  WavInfo info = wavParseHeader(hdr, sizeof(hdr));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_EQUAL_STRING("not wave", info.error);
}

void test_reject_too_short() {
  uint8_t hdr[8] = {};
  WavInfo info = wavParseHeader(hdr, sizeof(hdr));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_EQUAL_STRING("too short", info.error);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_stereo_44100);
  RUN_TEST(test_valid_mono_22050);
  RUN_TEST(test_reject_ieee_float);
  RUN_TEST(test_reject_8bit);
  RUN_TEST(test_reject_not_riff);
  RUN_TEST(test_reject_not_wave);
  RUN_TEST(test_reject_too_short);
  return UNITY_END();
}
