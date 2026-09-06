// Generate expected PCM from the fixture waveform, not from another decoder.

#include <unity.h>

#include "config.hpp"
#include "decoders/flac_decoder.hpp"
#include "mp3_test_file.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// Locate the fixture directory without build-time configuration: env var,
// repo-relative path, then a path derived from __FILE__.
std::string fixturePath(const char* name) {
  if (const char* env = std::getenv("FLAC_FIXTURE_DIR")) {
    return std::string(env) + "/" + name;
  }
  std::string rel = std::string("test/fixtures/flac/") + name;
  if (std::FILE* f = std::fopen(rel.c_str(), "rb")) {
    std::fclose(f);
    return rel;
  }
  std::string base = __FILE__;
  base = base.substr(0, base.find_last_of("/\\"));
  base = base.substr(0, base.find_last_of("/\\"));
  return base + "/fixtures/flac/" + name;
}

Mp3TestFile loadFixture(const char* name) {
  Mp3TestFile f;
  TEST_ASSERT_TRUE_MESSAGE(f.openFile(fixturePath(name).c_str()), name);
  return f;
}

// Shared with test/fixtures/flac/generate.py -- keep in sync.
constexpr int16_t kSinTable[256] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
    0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804,
};

struct WaveSpec {
  uint16_t channels;   // source channels
  uint32_t frames;     // source frames
  uint64_t dphase[2];  // initial per-sample phase delta
  uint64_t dinc[2];    // per-sample delta increment (sweep)
};

constexpr WaveSpec kStereo16{
    2, 44100,
    {334634813128517943ULL, 167317406564258971ULL},
    {22764273001939ULL, 11382136500969ULL}};
constexpr WaveSpec kMono24{
    1, 32000,
    {345876451382054092ULL, 0ULL},
    {32425917317067ULL, 0ULL}};

// The fixture adds only discarded low bits to its 24-bit samples.
std::vector<int16_t> expectedPcm(const WaveSpec& spec) {
  std::vector<int16_t> out;
  out.reserve(static_cast<size_t>(spec.frames) * 2);
  uint64_t p[2] = {spec.dphase[0], spec.dphase[1]};
  uint64_t d[2] = {spec.dphase[0], spec.dphase[1]};
  for (uint32_t n = 0; n < spec.frames; n++) {
    for (uint16_t ch = 0; ch < spec.channels; ch++) {
      p[ch] += d[ch];
      d[ch] += spec.dinc[ch];
      const int16_t s = kSinTable[static_cast<size_t>(p[ch] >> 56)];
      out.push_back(s);
      if (spec.channels == 1) out.push_back(s);
    }
  }
  return out;
}

struct DrainResult {
  std::vector<int16_t> pcm;
  DecodeStatus status = DecodeStatus::Ok;
};

DrainResult drain(FlacDecoder& dec, size_t capacity = 512, uint64_t start = 0) {
  DrainResult result;
  std::vector<int16_t> buffer(capacity * 2);
  for (size_t calls = 0; calls < 4096; ++calls) {
    size_t got = 0;
    result.status = dec.decode(buffer.data(), capacity, &got);
    TEST_ASSERT_TRUE(got <= capacity);
    result.pcm.insert(result.pcm.end(), buffer.begin(), buffer.begin() + got * 2);
    TEST_ASSERT_EQUAL_UINT32((start + result.pcm.size() / 2) * 1000 / dec.format().sampleRate,
                            dec.positionMs());
    if (result.status == DecodeStatus::Finished || result.status == DecodeStatus::Error) return result;
  }
  TEST_FAIL_MESSAGE("decode does not reach a terminal state");
  return result;
}

std::vector<int16_t> collectFrames(FlacDecoder& dec, size_t frames) {
  std::vector<int16_t> output(frames * 2);
  size_t done = 0;
  while (done < frames) {
    size_t got = 0;
    const auto status = dec.decode(output.data() + done * 2, frames - done, &got);
    TEST_ASSERT_TRUE(status == DecodeStatus::Ok || status == DecodeStatus::Finished);
    TEST_ASSERT_TRUE(got > 0 && got <= frames - done);
    done += got;
  }
  return output;
}

size_t firstFrame(const std::vector<uint8_t>& bytes) {
  size_t offset = 4;
  while (offset + 4 <= bytes.size()) {
    const bool last = (bytes[offset] & 0x80) != 0;
    const size_t length = (size_t(bytes[offset + 1]) << 16) |
                         (size_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
    offset += 4 + length;
    TEST_ASSERT_TRUE(offset <= bytes.size());
    if (last) return offset;
  }
  TEST_FAIL_MESSAGE("fixture metadata is incomplete");
  return 0;
}

void packedBits(std::vector<uint8_t>& bytes, uint64_t value, uint64_t mask) {
  uint64_t packed = 0;
  for (size_t i = 18; i < 26; ++i) packed = (packed << 8) | bytes[i];
  packed = (packed & ~mask) | (value & mask);
  for (size_t i = 26; i-- > 18;) { bytes[i] = static_cast<uint8_t>(packed); packed >>= 8; }
}

const char* kStereoName = "chirp16_stereo_44100.flac";
const char* kMonoName = "chirp24_mono_32000.flac";

// Generate a complete six-day FLAC stream on demand instead of storing 14 MiB.
class LongFlacInput : public DecoderInput {
 public:
  LongFlacInput() {
    std::memcpy(header_.data(), "fLaC", 4);
    header_[7] = 34;
    put(header_.data() + 8, 4096, 2);
    put(header_.data() + 10, 4096, 2);
    put(header_.data() + 18, (uint64_t(8000) << 44) | (uint64_t(15) << 36) |
                            (uint64_t((1u << 20) + 2) * 4096), 8);
    header_[42] = 0x83;
    header_[45] = 36;
    put(header_.data() + 62, 4096, 2);
    put(header_.data() + 64, uint64_t(1) << 32, 8);
    put(header_.data() + 72, audioBytes() - 28, 8);
    put(header_.data() + 80, 4096, 2);
  }
  size_t read(uint8_t* dst, size_t bytes) override {
    size_t done = 0;
    while (done < bytes && pos_ < size()) {
      if (pos_ < header_.size()) {
        const size_t n = std::min(bytes - done, header_.size() - pos_);
        std::memcpy(dst + done, header_.data() + pos_, n);
        done += n; pos_ += n;
        continue;
      }
      uint32_t offset = pos_ - header_.size(), index = 0, length = 11;
      for (uint32_t count : counts_) {
        if (offset < count * length) break;
        offset -= count * length; index += count; ++length;
      }
      index += offset / length;
      offset %= length;
      uint8_t frame[14] = {0xff, 0xf8, 0xc4, 0x08};
      const size_t utf = length - 10;
      uint32_t value = index;
      for (size_t i = utf; i-- > 1;) { frame[4 + i] = 0x80 | (value & 63); value >>= 6; }
      frame[4] = utf == 1 ? value : uint8_t(0xff << (8 - utf)) | value;
      frame[4 + utf] = static_cast<uint8_t>(crc(frame, 4 + utf, 8, 0x07));
      put(frame + length - 2, crc(frame, length - 2, 16, 0x8005), 2);
      const size_t n = std::min(bytes - done, size_t(length - offset));
      std::memcpy(dst + done, frame + offset, n);
      done += n; pos_ += n;
    }
    return done;
  }
  bool seek(uint32_t position) override { if (position > size()) return false; pos_ = position; return true; }
  uint32_t position() override { return pos_; }
  uint32_t size() override { return header_.size() + audioBytes(); }
  bool valid() override { return true; }
  void close() override {}
 private:
  static void put(uint8_t* out, uint64_t value, size_t bytes) {
    for (size_t i = bytes; i-- > 0;) { out[i] = value & 255; value >>= 8; }
  }
  static uint16_t crc(const uint8_t* data, size_t bytes, unsigned width, uint16_t polynomial) {
    uint16_t result = 0;
    for (size_t i = 0; i < bytes; ++i) {
      result ^= uint16_t(data[i]) << (width - 8);
      for (unsigned bit = 0; bit < 8; ++bit)
        result = (result << 1) ^ ((result & (1u << (width - 1))) ? polynomial : 0);
      if (width == 8) result &= 255;
    }
    return result;
  }
  static uint32_t audioBytes() {
    uint32_t bytes = 0, length = 11;
    for (uint32_t count : counts_) bytes += count * length++;
    return bytes;
  }
  static constexpr uint32_t counts_[] = {128, 2048 - 128, 65536 - 2048, (1u << 20) + 2 - 65536};
  std::array<uint8_t, 82> header_{};
  uint32_t pos_ = 0;
};

void test_stereo16_short_reads_and_small_output_match_waveform() {
  auto file = loadFixture(kStereoName);
  file.setReadLimit(13);
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  TEST_ASSERT_EQUAL_UINT32(44100, dec.format().sampleRate);
  TEST_ASSERT_EQUAL_UINT32(1000, dec.format().durationMs);
  auto result = drain(dec, 17);
  TEST_ASSERT_EQUAL(DecodeStatus::Finished, result.status);
  TEST_ASSERT_TRUE(result.pcm == expectedPcm(kStereo16));
}

void test_mono24_owned_input_matches_stereo16_waveform() {
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.open(fixturePath(kMonoName).c_str()));
  auto result = drain(dec);
  TEST_ASSERT_EQUAL(DecodeStatus::Finished, result.status);
  TEST_ASSERT_TRUE(result.pcm == expectedPcm(kMono24));
}

void test_forward_backward_and_zero_seek_match_requested_samples() {
  auto file = loadFixture(kStereoName);
  auto expected = expectedPcm(kStereo16);
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  collectFrames(dec, 13230);
  for (uint32_t target : {700u, 200u, 0u}) {
    file.setReadLimit(37);
    TEST_ASSERT_TRUE(dec.seekMs(target));
    const auto got = collectFrames(dec, 4410);
    TEST_ASSERT_TRUE(std::equal(got.begin(), got.end(), expected.begin() + target * 44100 / 1000 * 2));
    TEST_ASSERT_EQUAL_UINT32(target + 100, dec.positionMs());
  }
}

void test_end_seek_and_seek_back() {
  auto file = loadFixture(kStereoName);
  auto expected = expectedPcm(kStereo16);
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  for (uint32_t target : {1000u, UINT32_MAX}) {
    TEST_ASSERT_TRUE(dec.seekMs(target));
    size_t got = 99;
    int16_t buffer[2];
    TEST_ASSERT_EQUAL(DecodeStatus::Finished, dec.decode(buffer, 1, &got));
    TEST_ASSERT_EQUAL_UINT32(0, got);
    TEST_ASSERT_EQUAL_UINT32(1000, dec.positionMs());
    TEST_ASSERT_TRUE(dec.seekMs(500));
    const auto pcm = collectFrames(dec, 4410);
    TEST_ASSERT_TRUE(std::equal(pcm.begin(), pcm.end(), expected.begin() + 22050 * 2));
  }
}

void test_seek_across_32_bit_sample_distance() {
  LongFlacInput file;
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  collectFrames(dec, 16);
  TEST_ASSERT_TRUE(dec.seekMs(536870914));
  TEST_ASSERT_EQUAL_UINT32(536870914, dec.positionMs());
  const auto pcm = collectFrames(dec, 16);
  TEST_ASSERT_TRUE(std::all_of(pcm.begin(), pcm.end(), [](int16_t sample) { return sample == 0; }));
  TEST_ASSERT_TRUE(dec.seekMs(4));
  TEST_ASSERT_EQUAL_UINT32(4, dec.positionMs());
  collectFrames(dec, 16);
  TEST_ASSERT_EQUAL_UINT32(6, dec.positionMs());
}

void test_trailing_data_does_not_extend_audio() {
  auto base = loadFixture(kStereoName);
  std::vector<uint8_t> bytes(base.data(), base.data() + base.byteSize());
  bytes.insert(bytes.end(), 64, 0x5a);
  auto file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  const auto result = drain(dec);
  TEST_ASSERT_EQUAL(DecodeStatus::Finished, result.status);
  TEST_ASSERT_TRUE(result.pcm == expectedPcm(kStereo16));
}

void test_truncated_stream_reports_only_valid_prefix_then_error() {
  auto file = loadFixture(kStereoName);
  const auto expected = expectedPcm(kStereo16);
  file.truncateTo(file.byteSize() * 3 / 4);
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  const auto result = drain(dec);
  TEST_ASSERT_EQUAL(DecodeStatus::Error, result.status);
  TEST_ASSERT_TRUE(result.pcm.size() < expected.size());
  TEST_ASSERT_TRUE(std::equal(result.pcm.begin(), result.pcm.end(), expected.begin()));
}

void test_crc_error_does_not_skip_into_later_audio() {
  auto base = loadFixture(kStereoName);
  std::vector<uint8_t> bytes(base.data(), base.data() + base.byteSize());
  bytes[firstFrame(bytes) + 1024] ^= 0xff;
  auto file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  const auto result = drain(dec);
  TEST_ASSERT_EQUAL(DecodeStatus::Error, result.status);
  TEST_ASSERT_TRUE(result.pcm.empty());
}

void test_failed_read_is_terminal_and_keeps_delivered_position() {
  auto file = loadFixture(kStereoName);
  const auto expected = expectedPcm(kStereo16);
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  collectFrames(dec, 8820);
  file.failNextReads(1);
  const auto result = drain(dec, 512, 8820);
  TEST_ASSERT_EQUAL(DecodeStatus::Error, result.status);
  TEST_ASSERT_TRUE(result.pcm.size() <= expected.size() - 8820 * 2);
  TEST_ASSERT_TRUE(std::equal(result.pcm.begin(), result.pcm.end(), expected.begin() + 8820 * 2));
  size_t got = 99;
  int16_t buffer[2];
  TEST_ASSERT_EQUAL(DecodeStatus::Error, dec.decode(buffer, 1, &got));
  TEST_ASSERT_EQUAL_UINT32(0, got);
}

void test_failed_seek_stays_failed_until_reopen() {
  auto file = loadFixture(kStereoName);
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  collectFrames(dec, 882);
  file.failNextSeeks(1);
  TEST_ASSERT_FALSE(dec.seekMs(500));
  TEST_ASSERT_EQUAL_UINT32(20, dec.positionMs());
  size_t got = 99;
  int16_t buffer[2];
  TEST_ASSERT_EQUAL(DecodeStatus::Error, dec.decode(buffer, 1, &got));
  TEST_ASSERT_EQUAL_UINT32(0, got);
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  const auto result = drain(dec);
  TEST_ASSERT_EQUAL(DecodeStatus::Finished, result.status);
  TEST_ASSERT_TRUE(result.pcm == expectedPcm(kStereo16));
}

void test_invalid_replacement_does_not_keep_old_audio() {
  auto file = loadFixture(kStereoName);
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  TEST_ASSERT_FALSE(dec.open(nullptr));
  size_t got = 99;
  int16_t buffer[2];
  TEST_ASSERT_EQUAL(DecodeStatus::Error, dec.decode(buffer, 1, &got));
  TEST_ASSERT_EQUAL_UINT32(0, got);
}

void test_unsupported_streaminfo_is_rejected() {
  const auto base = loadFixture(kStereoName);
  const std::vector<uint8_t> original(base.data(), base.data() + base.byteSize());
  const uint64_t masks[] = {(1ULL << 36) - 1, 0xfffffULL << 44, 7ULL << 41, 31ULL << 36};
  const uint64_t values[] = {0, 96000ULL << 44, 3ULL << 41, 31ULL << 36};
  for (size_t i = 0; i < 4; ++i) {
    auto bytes = original;
    packedBits(bytes, values[i], masks[i]);
    auto file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
    FlacDecoder dec;
    TEST_ASSERT_FALSE(dec.openWithFile(file));
  }
  auto bytes = original;
  bytes[10] = bytes[11] = 0xff;
  auto file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
  FlacDecoder dec;
  TEST_ASSERT_FALSE(dec.openWithFile(file));
}

void test_seek_table_allocation_is_bounded() {
  const auto base = loadFixture(kStereoName);
  const std::vector<uint8_t> original(base.data(), base.data() + base.byteSize());
  std::vector<uint8_t> bytes(original.begin(), original.begin() + 42);
  bytes[4] &= 0x7f;
  bytes.insert(bytes.end(), {0x83, 0x00, 0xd2, 0xf0});
  bytes.resize(bytes.size() + 3000 * 18, 0xff);
  bytes.insert(bytes.end(), original.begin() + firstFrame(original), original.end());
  auto file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
  FlacDecoder dec;
  TEST_ASSERT_FALSE(dec.openWithFile(file));
  auto good = loadFixture(kStereoName);
  TEST_ASSERT_TRUE(dec.openWithFile(good));
  TEST_ASSERT_TRUE(drain(dec).pcm == expectedPcm(kStereo16));
}

void test_decode_read_work_is_bounded_on_missing_frames() {
  const auto base = loadFixture(kStereoName);
  const std::vector<uint8_t> original(base.data(), base.data() + base.byteSize());
  std::vector<uint8_t> bytes(original.begin(), original.begin() + firstFrame(original));
  bytes.resize(bytes.size() + cfg::kFlacDecodeReadBudget * 2, 0);
  auto file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
  FlacDecoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  const uint64_t before = file.totalBytesRead();
  const auto result = drain(dec);
  TEST_ASSERT_EQUAL(DecodeStatus::Error, result.status);
  TEST_ASSERT_TRUE(result.pcm.empty());
  TEST_ASSERT_TRUE(file.totalBytesRead() - before <= cfg::kFlacDecodeReadBudget);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stereo16_short_reads_and_small_output_match_waveform);
  RUN_TEST(test_mono24_owned_input_matches_stereo16_waveform);
  RUN_TEST(test_forward_backward_and_zero_seek_match_requested_samples);
  RUN_TEST(test_end_seek_and_seek_back);
  RUN_TEST(test_seek_across_32_bit_sample_distance);
  RUN_TEST(test_trailing_data_does_not_extend_audio);
  RUN_TEST(test_truncated_stream_reports_only_valid_prefix_then_error);
  RUN_TEST(test_crc_error_does_not_skip_into_later_audio);
  RUN_TEST(test_failed_read_is_terminal_and_keeps_delivered_position);
  RUN_TEST(test_failed_seek_stays_failed_until_reopen);
  RUN_TEST(test_invalid_replacement_does_not_keep_old_audio);
  RUN_TEST(test_unsupported_streaminfo_is_rejected);
  RUN_TEST(test_seek_table_allocation_is_bounded);
  RUN_TEST(test_decode_read_work_is_bounded_on_missing_frames);
  return UNITY_END();
}
