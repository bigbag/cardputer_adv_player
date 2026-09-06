// Unit tests for the bounded MP3 metadata parsers: ID3v2 data-start
// computation and Xing/Info extraction from the first MPEG frame.

#include <unity.h>

#include "decoders/mp3_info.hpp"

#include <cstring>
#include <vector>

namespace {

// MPEG1 Layer III, 44100 Hz, 128 kbit/s, no CRC.
const uint8_t kM1Stereo[4] = {0xFF, 0xFB, 0x90, 0x00};  // side info 32
const uint8_t kM1Mono[4] = {0xFF, 0xFB, 0x90, 0xC0};    // side info 17
// MPEG1 Layer III with CRC (protection bit 0).
const uint8_t kM1MonoCrc[4] = {0xFF, 0xFA, 0x90, 0xC0};  // 2 + side info 17
// MPEG2 Layer III, 22050 Hz, 128 kbit/s, mono -> side info 9, 576 samples.
const uint8_t kM2Mono[4] = {0xFF, 0xF3, 0xD0, 0xC0};
// MPEG2.5 Layer III, 8000 Hz, 128 kbit/s, mono -> side info 9, 576 samples.
const uint8_t kM25Mono[4] = {0xFF, 0xE3, 0xDA, 0xC0};

std::vector<uint8_t> frameWith(const uint8_t hdr[4], bool crc, size_t sideBytes) {
  std::vector<uint8_t> f(hdr, hdr + 4);
  if (crc) {
    f.push_back(0x12);
    f.push_back(0x34);
  }
  f.resize(f.size() + sideBytes, 0);
  return f;
}

void appendTag(std::vector<uint8_t>& f, const char* tag, uint32_t flags,
               uint32_t frames = 0, uint32_t audioBytes = 0,
               const uint8_t* toc = nullptr, uint32_t quality = 0) {
  f.insert(f.end(), tag, tag + 4);
  const uint8_t fl[4] = {uint8_t(flags >> 24), uint8_t(flags >> 16),
                         uint8_t(flags >> 8), uint8_t(flags)};
  f.insert(f.end(), fl, fl + 4);
  auto push32 = [&](uint32_t v) {
    const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8),
                          uint8_t(v)};
    f.insert(f.end(), b, b + 4);
  };
  if (flags & 0x1) push32(frames);
  if (flags & 0x2) push32(audioBytes);
  if (flags & 0x4 && toc) f.insert(f.end(), toc, toc + 100);
  if (flags & 0x8) push32(quality);
}

std::vector<uint8_t> id3Tag(uint8_t version, uint8_t flags, uint32_t size) {
  std::vector<uint8_t> t = {'I', 'D', '3', version, 0x00, flags};
  t.push_back(uint8_t(size >> 21 & 0x7F));
  t.push_back(uint8_t(size >> 14 & 0x7F));
  t.push_back(uint8_t(size >> 7 & 0x7F));
  t.push_back(uint8_t(size & 0x7F));
  t.resize(t.size() + size, 0);
  return t;
}

}  // namespace

void test_id3_absent_returns_zero_offset() {
  const uint8_t raw[10] = {0xFF, 0xFB, 0x90, 0x00, 0, 0, 0, 0, 0, 0};
  uint32_t start = 99;
  TEST_ASSERT_TRUE(mp3Id3DataStart(raw, sizeof(raw), 1000, start));
  TEST_ASSERT_EQUAL_UINT32(0, start);
}

void test_id3_v23_valid() {
  std::vector<uint8_t> t = id3Tag(3, 0x00, 500);
  uint32_t start = 0;
  TEST_ASSERT_TRUE(mp3Id3DataStart(t.data(), t.size(), 100000, start));
  TEST_ASSERT_EQUAL_UINT32(510, start);
}

void test_id3_v24_footer_adds_ten_bytes() {
  std::vector<uint8_t> t = id3Tag(4, 0x10, 500);
  // Header declares only the body size; the parser must add the footer.
  uint32_t start = 0;
  TEST_ASSERT_TRUE(mp3Id3DataStart(t.data(), t.size(), 100000, start));
  TEST_ASSERT_EQUAL_UINT32(520, start);
}

void test_id3_v2_footer_flag_ignored() {
  std::vector<uint8_t> t = id3Tag(3, 0x10, 500);
  uint32_t start = 0;
  TEST_ASSERT_TRUE(mp3Id3DataStart(t.data(), t.size(), 100000, start));
  TEST_ASSERT_EQUAL_UINT32(510, start);  // v2.3 has no footer
}

void test_id3_unsupported_version_rejected() {
  std::vector<uint8_t> t = id3Tag(5, 0x00, 500);
  uint32_t start = 7;
  TEST_ASSERT_FALSE(mp3Id3DataStart(t.data(), t.size(), 100000, start));
}

void test_id3_truncated_header_rejected() {
  const uint8_t t[] = {'I', 'D', '3', 3};
  uint32_t start = 7;
  TEST_ASSERT_FALSE(mp3Id3DataStart(t, sizeof(t), 100000, start));
}

void test_id3_bad_synchsafe_size_rejected() {
  std::vector<uint8_t> t = id3Tag(3, 0x00, 0);
  t[6] = 0x81;  // continuation bit set: not synchsafe
  uint32_t start = 7;
  TEST_ASSERT_FALSE(mp3Id3DataStart(t.data(), t.size(), 100000, start));
}

void test_id3_tag_past_file_size_rejected() {
  std::vector<uint8_t> t = id3Tag(3, 0x00, 500);
  uint32_t start = 0;
  TEST_ASSERT_FALSE(mp3Id3DataStart(t.data(), t.size(), 100, start));
}

void test_xing_full_tag_parses_all_fields() {
  uint8_t toc[100];
  for (int i = 0; i < 100; i++) toc[i] = uint8_t(i * 2 + 1);
  std::vector<uint8_t> f = frameWith(kM1Stereo, false, 32);
  appendTag(f, "Xing", 0xF, 1000, 123456, toc, 42);

  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_TRUE(info.hasSeekToc);
  TEST_ASSERT_EQUAL_UINT32(1000, info.totalFrames);
  TEST_ASSERT_EQUAL_UINT32(123456, info.audioBytes);
  TEST_ASSERT_EQUAL_UINT32(26122, info.durationMs);  // 1000*1152*1000/44100
  TEST_ASSERT_EQUAL_UINT8(1, info.toc[0]);
  TEST_ASSERT_EQUAL_UINT8(199, info.toc[99]);
}

void test_info_tag_is_recognized() {
  std::vector<uint8_t> f = frameWith(kM1Stereo, false, 32);
  appendTag(f, "Info", 0x3, 9000, 1000000);
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_FALSE(info.hasSeekToc);  // no TOC field
  TEST_ASSERT_EQUAL_UINT32(9000, info.totalFrames);
  TEST_ASSERT_EQUAL_UINT32(1000000, info.audioBytes);
  TEST_ASSERT_EQUAL_UINT32(235102, info.durationMs);  // 9000*1152000/44100
}

void test_count_only_metadata_gives_duration_without_bytes() {
  std::vector<uint8_t> f = frameWith(kM1Mono, false, 17);
  appendTag(f, "Xing", 0x1, 100, 0, nullptr, 0);
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_EQUAL_UINT32(100, info.totalFrames);
  TEST_ASSERT_EQUAL_UINT32(0, info.audioBytes);
  TEST_ASSERT_FALSE(info.hasSeekToc);
  TEST_ASSERT_EQUAL_UINT32(2612, info.durationMs);
}

void test_missing_frames_field_gives_no_duration() {
  std::vector<uint8_t> f = frameWith(kM1Stereo, false, 32);
  appendTag(f, "Xing", 0x2 | 0x4, 0, 500000, nullptr, 0);
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_EQUAL_UINT32(0, info.totalFrames);
  TEST_ASSERT_EQUAL_UINT32(0, info.durationMs);
}

void test_truncated_toc_does_not_block_audio_fields() {
  std::vector<uint8_t> f = frameWith(kM1Stereo, false, 32);
  appendTag(f, "Xing", 0x3, 1000, 123456, nullptr, 0);  // frames + bytes
  // Claim a TOC in the flags, but end the span halfway through the table.
  f[4 + 32 + 4 + 3] |= 0x4;
  for (int i = 0; i < 50; i++) f.push_back(uint8_t(i * 2 + 1));
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_EQUAL_UINT32(1000, info.totalFrames);
  TEST_ASSERT_EQUAL_UINT32(123456, info.audioBytes);
  TEST_ASSERT_FALSE(info.hasSeekToc);
  TEST_ASSERT_EQUAL_UINT32(26122, info.durationMs);
}

void test_truncated_frames_field_is_reported_absent() {
  std::vector<uint8_t> f = frameWith(kM1Mono, false, 17);
  // Room for tag+flags only (span ends inside the frames field).
  f.resize(f.size() + 8, 0);
  std::memcpy(f.data() + 4 + 17, "Xing", 4);
  f[4 + 17 + 4] = 0;
  f[4 + 17 + 5] = 0;
  f[4 + 17 + 6] = 0;
  f[4 + 17 + 7] = 0x1;
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_EQUAL_UINT32(0, info.totalFrames);
  TEST_ASSERT_EQUAL_UINT32(0, info.durationMs);
}

void test_descending_toc_is_not_seekable() {
  uint8_t toc[100];
  for (int i = 0; i < 100; i++) toc[i] = uint8_t(200 - i);
  std::vector<uint8_t> f = frameWith(kM1Stereo, false, 32);
  appendTag(f, "Xing", 0x7, 1000, 123456, toc, 0);
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_FALSE(info.hasSeekToc);
}

void test_repeated_toc_entries_stay_seekable() {
  uint8_t toc[100];
  for (int i = 0; i < 100; i++) toc[i] = uint8_t(i / 10);  // plateaus, nondecreasing
  std::vector<uint8_t> f = frameWith(kM1Stereo, false, 32);
  appendTag(f, "Xing", 0x7, 1000, 123456, toc, 0);
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasSeekToc);
}

void test_crc_frame_shifts_tag_offset() {
  std::vector<uint8_t> f = frameWith(kM1MonoCrc, true, 17);  // off = 4+2+17
  appendTag(f, "Xing", 0x1, 500, 0, nullptr, 0);
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_EQUAL_UINT32(500, info.totalFrames);
}

void test_mono_and_stereo_side_info_offsets_differ() {
  std::vector<uint8_t> stereo = frameWith(kM1Stereo, false, 32);
  appendTag(stereo, "Xing", 0x1, 10, 0, nullptr, 0);
  Mp3StreamInfo s = mp3ParseInfo(stereo.data(), stereo.size());
  TEST_ASSERT_TRUE(s.hasVbrInfo);

  // A stereo frame must reject a complete tag at the mono offset (4+17).
  // Keep both spans equal in size to test the offset, not truncation.
  std::vector<uint8_t> misplaced = frameWith(kM1Stereo, false, 17);
  appendTag(misplaced, "Xing", 0x1, 10, 0, nullptr, 0);
  misplaced.resize(stereo.size(), 0);
  Mp3StreamInfo m = mp3ParseInfo(misplaced.data(), misplaced.size());
  TEST_ASSERT_FALSE(m.hasVbrInfo);
}

void test_mpeg2_uses_576_samples_per_frame() {
  std::vector<uint8_t> f = frameWith(kM2Mono, false, 9);
  appendTag(f, "Xing", 0x1, 2000, 0, nullptr, 0);
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_EQUAL_UINT32(52244, info.durationMs);  // 2000*576*1000/22050
}

void test_mpeg25_low_rate_duration() {
  std::vector<uint8_t> f = frameWith(kM25Mono, false, 9);
  appendTag(f, "Xing", 0x1, 2000, 0, nullptr, 0);
  Mp3StreamInfo info = mp3ParseInfo(f.data(), f.size());
  TEST_ASSERT_TRUE(info.hasVbrInfo);
  TEST_ASSERT_EQUAL_UINT32(144000, info.durationMs);  // 2000*576*1000/8000
}

void test_invalid_headers_return_no_info() {
  // Reserved version.
  const uint8_t badVersion[4] = {0xFF, 0xF7, 0x90, 0x00};
  std::vector<uint8_t> f(badVersion, badVersion + 4);
  f.resize(60, 0);
  TEST_ASSERT_FALSE(mp3ParseInfo(f.data(), f.size()).hasVbrInfo);

  // Reserved sample-rate index.
  const uint8_t badRate[4] = {0xFF, 0xFB, 0x8C, 0x00};
  std::vector<uint8_t> f2(badRate, badRate + 4);
  f2.resize(60, 0);
  TEST_ASSERT_FALSE(mp3ParseInfo(f2.data(), f2.size()).hasVbrInfo);

  // No tag bytes at all.
  std::vector<uint8_t> f3 = frameWith(kM1Stereo, false, 32);
  f3.resize(f3.size() + 64, 0);
  TEST_ASSERT_FALSE(mp3ParseInfo(f3.data(), f3.size()).hasVbrInfo);

  // Span shorter than a header.
  TEST_ASSERT_FALSE(mp3ParseInfo(f3.data(), 3).hasVbrInfo);
}


namespace {

Mp3StreamInfo tocInfo(const uint8_t* toc, uint32_t audioBytes, bool usable) {
  Mp3StreamInfo info;
  if (toc) std::memcpy(info.toc, toc, 100);
  info.hasVbrInfo = true;
  info.hasSeekToc = usable;
  info.audioBytes = audioBytes;
  return info;
}

}  // namespace

void test_seek_offset_boundaries() {
  Mp3StreamInfo info = tocInfo(nullptr, 0, false);
  TEST_ASSERT_EQUAL_UINT32(0, mp3SeekByteOffset(info, 100000, 10000, 0));
  TEST_ASSERT_EQUAL_UINT32(0, mp3SeekByteOffset(info, 100000, 0, 5000));
  TEST_ASSERT_EQUAL_UINT32(0, mp3SeekByteOffset(info, 0, 10000, 5000));
  TEST_ASSERT_EQUAL_UINT32(100000, mp3SeekByteOffset(info, 100000, 10000, 10000));
  TEST_ASSERT_EQUAL_UINT32(100000, mp3SeekByteOffset(info, 100000, 10000, 12000));
}

void test_seek_offset_linear_fallback() {
  Mp3StreamInfo info = tocInfo(nullptr, 0, false);  // no usable TOC
  TEST_ASSERT_EQUAL_UINT32(25000, mp3SeekByteOffset(info, 100000, 10000, 2500));
  TEST_ASSERT_EQUAL_UINT32(50000, mp3SeekByteOffset(info, 100000, 10000, 5000));
  // TOC present but the bytes field missing: interpolation has no base.
  uint8_t toc[100] = {};
  Mp3StreamInfo noBytes = tocInfo(toc, 0, false);
  TEST_ASSERT_EQUAL_UINT32(50000,
                           mp3SeekByteOffset(noBytes, 100000, 10000, 5000));
}

void test_seek_offset_toc_interpolation() {
  uint8_t toc[100];
  for (int i = 0; i < 100; i++) toc[i] = uint8_t(i * 64 / 25);  // linear map
  Mp3StreamInfo info = tocInfo(toc, 100000, true);
  // Midpoint hits entry 50 exactly.
  TEST_ASSERT_EQUAL_UINT32(50000, mp3SeekByteOffset(info, 100000, 10000, 5000));
  // Interpolated inside entry 25..26: hand-computed expectation.
  TEST_ASSERT_EQUAL_UINT32(25305, mp3SeekByteOffset(info, 100000, 10000, 2560));
  // Final interval uses entry 99, not an out-of-range index.
  TEST_ASSERT_EQUAL_UINT32(98626, mp3SeekByteOffset(info, 100000, 10000, 9900));
}

void test_seek_offset_nonlinear_toc_and_descending_fallback() {
  uint8_t flat[100];
  std::memset(flat, 0, sizeof(flat));  // first half silent per the TOC
  flat[50] = 128;
  for (int i = 51; i < 100; i++) flat[i] = 255;
  Mp3StreamInfo info = tocInfo(flat, 100000, true);
  TEST_ASSERT_EQUAL_UINT32(0, mp3SeekByteOffset(info, 100000, 10000, 2500));
  TEST_ASSERT_EQUAL_UINT32(50000, mp3SeekByteOffset(info, 100000, 10000, 5000));

  uint8_t down[100];
  for (int i = 0; i < 100; i++) down[i] = uint8_t(200 - i);  // malformed
  Mp3StreamInfo bad = tocInfo(down, 100000, false);  // parser marks unusable
  TEST_ASSERT_EQUAL_UINT32(50000,
                           mp3SeekByteOffset(bad, 100000, 10000, 5000));  // linear
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_id3_absent_returns_zero_offset);
  RUN_TEST(test_id3_v23_valid);
  RUN_TEST(test_id3_v24_footer_adds_ten_bytes);
  RUN_TEST(test_id3_v2_footer_flag_ignored);
  RUN_TEST(test_id3_unsupported_version_rejected);
  RUN_TEST(test_id3_truncated_header_rejected);
  RUN_TEST(test_id3_bad_synchsafe_size_rejected);
  RUN_TEST(test_id3_tag_past_file_size_rejected);
  RUN_TEST(test_xing_full_tag_parses_all_fields);
  RUN_TEST(test_info_tag_is_recognized);
  RUN_TEST(test_count_only_metadata_gives_duration_without_bytes);
  RUN_TEST(test_missing_frames_field_gives_no_duration);
  RUN_TEST(test_truncated_toc_does_not_block_audio_fields);
  RUN_TEST(test_truncated_frames_field_is_reported_absent);
  RUN_TEST(test_descending_toc_is_not_seekable);
  RUN_TEST(test_repeated_toc_entries_stay_seekable);
  RUN_TEST(test_crc_frame_shifts_tag_offset);
  RUN_TEST(test_mono_and_stereo_side_info_offsets_differ);
  RUN_TEST(test_mpeg2_uses_576_samples_per_frame);
  RUN_TEST(test_mpeg25_low_rate_duration);
  RUN_TEST(test_invalid_headers_return_no_info);
  RUN_TEST(test_seek_offset_boundaries);
  RUN_TEST(test_seek_offset_linear_fallback);
  RUN_TEST(test_seek_offset_toc_interpolation);
  RUN_TEST(test_seek_offset_nonlinear_toc_and_descending_fallback);
  return UNITY_END();
}
