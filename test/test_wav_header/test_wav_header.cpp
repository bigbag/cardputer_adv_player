#include <unity.h>
#include "decoders/wav_decoder.hpp"
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace {

// Sparse in-memory file: real fixture bytes placed at absolute offsets,
// zeros everywhere else up to an explicit logical size. Supports failed
// and short reads.
struct MemWav {
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> segs;
  uint32_t logicalSize = 0;
  bool failReads = false;
  bool shortReads = false;
  uint32_t shortReadAt = 0;
};

size_t memReadAt(void* context, uint32_t offset, uint8_t* destination, size_t size) {
  auto* f = static_cast<MemWav*>(context);
  if (f->failReads) return 0;
  if (offset >= f->logicalSize) return 0;
  const uint64_t avail = uint64_t(f->logicalSize) - offset;
  size_t n = size < avail ? size : static_cast<size_t>(avail);
  std::memset(destination, 0, n);
  for (const auto& seg : f->segs) {
    const uint64_t segEnd = uint64_t(seg.first) + seg.second.size();
    const uint64_t start = std::max(uint64_t(offset), uint64_t(seg.first));
    const uint64_t end = std::min(uint64_t(offset) + n, segEnd);
    if (start < end) {
      std::memcpy(destination + (start - offset), seg.second.data() + (start - seg.first),
                  static_cast<size_t>(end - start));
    }
  }
  if (f->shortReads && offset == f->shortReadAt && n > 0) n -= 1;
  return n;
}

void putU16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back((x >> 8) & 0xFF);
}

void putU32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; i++) v.push_back(static_cast<uint8_t>((x >> (8 * i)) & 0xFF));
}

void putTag(std::vector<uint8_t>& v, const char* tag) {
  v.insert(v.end(), tag, tag + 4);
}

struct ChunkSpec {
  const char* id;
  uint32_t declared = 0;
  std::vector<uint8_t> bytes;
  bool pad = true;
};

ChunkSpec plainChunk(const char* id, uint32_t declared) {
  ChunkSpec c;
  c.id = id;
  c.declared = declared;
  return c;
}

ChunkSpec fmtChunk(uint16_t channels, uint32_t rate, uint16_t bits,
                   uint16_t tag = 1, int declaredOverride = -1) {
  ChunkSpec c;
  c.id = "fmt ";
  putU16(c.bytes, tag);
  putU16(c.bytes, channels);
  putU32(c.bytes, rate);
  putU32(c.bytes, rate * channels * (bits / 8));
  putU16(c.bytes, channels * (bits / 8));
  putU16(c.bytes, bits);
  if (declaredOverride > 16) {
    c.bytes.resize(static_cast<size_t>(declaredOverride), 0);
    c.declared = static_cast<uint32_t>(declaredOverride);
  } else if (declaredOverride >= 0) {
    c.declared = static_cast<uint32_t>(declaredOverride);
  } else {
    c.declared = static_cast<uint32_t>(c.bytes.size());
  }
  return c;
}

// Sparse WAV file: bytes live in segments at absolute offsets; regions
// between segments read back as zeros. Declared payload bytes count
// toward the RIFF size and logical size without being stored.
struct WavFile {
  std::vector<std::pair<uint32_t, std::vector<uint8_t>>> segs;
  uint32_t logicalSize = 0;
};

WavFile buildWav(std::vector<ChunkSpec> chunks, long riffOverride = -1) {
  uint64_t bodySize = 0;
  for (const auto& c : chunks) {
    bodySize += 8ull + c.declared + (((c.declared & 1) && c.pad) ? 1 : 0);
  }
  const uint32_t riffSize = riffOverride >= 0 ? static_cast<uint32_t>(riffOverride)
                                              : static_cast<uint32_t>(4 + bodySize);
  WavFile wf;
  std::vector<uint8_t> riff;
  putTag(riff, "RIFF");
  putU32(riff, riffSize);
  putTag(riff, "WAVE");
  wf.segs.push_back({0, riff});
  uint64_t off = 12;
  for (const auto& c : chunks) {
    std::vector<uint8_t> hdr;
    putTag(hdr, c.id);
    putU32(hdr, c.declared);
    if (off + hdr.size() <= 0xFFFFFFFFull) wf.segs.push_back({static_cast<uint32_t>(off), hdr});
    off += hdr.size();
    const size_t embedded = c.bytes.size() < c.declared ? c.bytes.size() : c.declared;
    if (embedded > 0 && off + embedded <= 0xFFFFFFFFull) {
      wf.segs.push_back({static_cast<uint32_t>(off),
                         std::vector<uint8_t>(c.bytes.begin(), c.bytes.begin() + embedded)});
    }
    off += c.declared;
    if ((c.declared & 1) && c.pad) {
      if (off + 1 <= 0xFFFFFFFFull) wf.segs.push_back({static_cast<uint32_t>(off), {0}});
      off += 1;
    }
  }
  if (riffOverride >= 0) {
    wf.logicalSize = off > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(off);
  } else {
    const uint64_t riffEnd = 8ull + riffSize;
    wf.logicalSize = riffEnd > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(riffEnd);
  }
  return wf;
}

// Canonical 16-byte fmt at offset 12, data header at offset 36.
// The data payload stays sparse: headers are the only real bytes.
WavFile simpleWav(uint16_t channels, uint32_t rate, uint32_t dataBytes,
                  uint16_t bits = 16, uint16_t tag = 1) {
  return buildWav({fmtChunk(channels, rate, bits, tag), plainChunk("data", dataBytes)});
}

uint8_t* byteAt(WavFile& wf, uint32_t at) {
  for (auto& seg : wf.segs) {
    if (at >= seg.first && at < seg.first + seg.second.size()) {
      return seg.second.data() + (at - seg.first);
    }
  }
  return nullptr;
}

void patchU16(WavFile& wf, uint32_t at, uint16_t x) {
  uint8_t* p = byteAt(wf, at);
  p[0] = x & 0xFF;
  p[1] = (x >> 8) & 0xFF;
}

void patchU32(WavFile& wf, uint32_t at, uint32_t x) {
  for (int i = 0; i < 4; i++) byteAt(wf, at + i)[0] = static_cast<uint8_t>((x >> (8 * i)) & 0xFF);
}

WavInfo parseWav(const WavFile& wf, bool failReads = false, bool shortReads = false) {
  MemWav m;
  m.segs = wf.segs;
  m.logicalSize = wf.logicalSize;
  m.failReads = failReads;
  m.shortReads = shortReads;
  return wavParseStream(memReadAt, &m, m.logicalSize);
}

WavInfo validHelperInfo() {
  WavInfo info;
  info.valid = true;
  info.sampleRate = 44100;
  info.channels = 2;
  info.bitsPerSample = 16;
  info.dataOffset = 44;
  info.dataSize = 10584000;
  return info;
}

}  // namespace

void test_valid_stereo_44100() {
  WavInfo info = parseWav(simpleWav(2, 44100, 4));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(44100, info.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(2, info.channels);
  TEST_ASSERT_EQUAL_UINT16(16, info.bitsPerSample);
  TEST_ASSERT_EQUAL_UINT32(44, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(4, info.dataSize);
  TEST_ASSERT_NULL(info.error);
  TEST_ASSERT_EQUAL_UINT32(0, wavDurationMs(info));
}

void test_valid_mono_22050() {
  WavInfo info = parseWav(simpleWav(1, 22050, 1024));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(22050, info.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(1, info.channels);
  TEST_ASSERT_EQUAL_UINT32(44, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(1024, info.dataSize);
  TEST_ASSERT_EQUAL_UINT32(23, wavDurationMs(info));
}

void test_fmt_with_extension_accepted() {
  WavInfo info = parseWav(buildWav({fmtChunk(2, 44100, 16, 1, 18), plainChunk("data", 4)}));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(46, info.dataOffset);
}

void test_reject_ieee_float() {
  WavInfo info = parseWav(simpleWav(2, 44100, 4, 16, 3));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_extensible() {
  WavInfo info = parseWav(buildWav({fmtChunk(2, 44100, 16, 0xFFFE, 40), plainChunk("data", 4)}));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_8bit() {
  WavInfo info = parseWav(simpleWav(2, 44100, 4, 8));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_24bit() {
  WavInfo info = parseWav(simpleWav(2, 44100, 6, 24));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_rf64() {
  WavFile wf = simpleWav(2, 44100, 4);
  patchU32(wf, 0, 0x34364652);  // "RF64" little-endian dword
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_not_riff() {
  WavFile wf = simpleWav(2, 44100, 4);
  patchU32(wf, 0, 0x58585858);  // "XXXX"
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_not_wave() {
  WavFile wf = simpleWav(2, 44100, 4);
  patchU32(wf, 8, 0x20495641);  // "AVI "
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_too_short() {
  MemWav m;
  m.logicalSize = 8;
  WavInfo info = wavParseStream(memReadAt, &m, m.logicalSize);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_fmt_below_16() {
  WavInfo info = parseWav(buildWav({fmtChunk(2, 44100, 16, 1, 12), plainChunk("data", 4)}));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_truncated_file() {
  WavFile wf = simpleWav(2, 44100, 4);
  wf.logicalSize = 20;
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_riff_size_exceeds_file() {
  WavInfo info = parseWav(buildWav({fmtChunk(2, 44100, 16), plainChunk("data", 4)}, 100000));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_truncated_fmt_payload() {
  WavInfo info = parseWav(buildWav({fmtChunk(2, 44100, 16)}, 20));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_truncated_data_payload() {
  WavFile wf = buildWav({fmtChunk(2, 44100, 16), plainChunk("data", 1024)}, 40);
  wf.logicalSize = 48;  // the declared RIFF end exists on disk, the data payload does not
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_huge_chunk_size() {
  std::vector<ChunkSpec> chunks = {plainChunk("JUNK", 0xFFFFFFFFu),
                                   fmtChunk(2, 44100, 16),
                                   plainChunk("data", 4)};
  WavFile wf = buildWav(chunks, 0xFFFFFF00L);
  wf.logicalSize = 0xFFFFFFFFu;  // file claims the full RIFF; 64-bit math must reject the chunk
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_ignores_bytes_outside_riff() {
  WavFile wf = simpleWav(2, 44100, 4);
  wf.logicalSize += 100;  // trailing bytes past the declared RIFF end
  WavInfo info = parseWav(wf);
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(4, info.dataSize);
}

void test_junk_before_fmt() {
  WavInfo info = parseWav(buildWav({plainChunk("JUNK", 9), fmtChunk(2, 44100, 16),
                                    plainChunk("data", 4)}));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(62, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(4, info.dataSize);
}

void test_list_before_data() {
  WavInfo info = parseWav(buildWav({fmtChunk(1, 22050, 16), plainChunk("LIST", 7),
                                    plainChunk("data", 4)}));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(60, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(4, info.dataSize);
}

void test_data_beyond_512() {
  WavInfo info = parseWav(buildWav({plainChunk("JUNK", 600), fmtChunk(2, 44100, 16),
                                    plainChunk("data", 4)}));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(652, info.dataOffset);
}

void test_odd_padding_between_chunks() {
  WavInfo info = parseWav(buildWav({plainChunk("JUNK", 3), fmtChunk(2, 44100, 16),
                                    plainChunk("data", 4)}));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(56, info.dataOffset);
}

void test_reject_data_before_fmt_odd_fmt_missing_pad() {
  ChunkSpec fmtOdd = fmtChunk(2, 44100, 16, 1, 17);
  fmtOdd.pad = false;  // final odd fmt chunk without the pad byte the RIFF counts
  WavInfo info = parseWav(buildWav({plainChunk("data", 4), fmtOdd}));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_data_before_fmt_odd_fmt_padded() {
  WavInfo info =
      parseWav(buildWav({plainChunk("data", 4), fmtChunk(2, 44100, 16, 1, 17)}));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(20, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(4, info.dataSize);
}

void test_data_before_fmt() {
  WavInfo info = parseWav(buildWav({plainChunk("data", 4), fmtChunk(2, 44100, 16)}));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(20, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(4, info.dataSize);
}

void test_first_data_region_wins() {
  WavInfo info = parseWav(buildWav({plainChunk("data", 4), fmtChunk(2, 44100, 16),
                                    plainChunk("data", 8)}));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(20, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(4, info.dataSize);
}

void test_reject_zero_rate() {
  WavFile wf = simpleWav(2, 44100, 4);
  patchU32(wf, 24, 0);
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_block_align_mismatch() {
  WavFile wf = simpleWav(2, 44100, 4);
  patchU16(wf, 32, 8);
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_byte_rate_mismatch() {
  WavFile wf = simpleWav(2, 44100, 4);
  patchU32(wf, 28, 44100u * 2u * 2u + 1);
  WavInfo info = parseWav(wf);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_partial_frame() {
  TEST_ASSERT_FALSE(parseWav(simpleWav(2, 44100, 6)).valid);   // 1.5 stereo frames
  TEST_ASSERT_FALSE(parseWav(simpleWav(1, 22050, 3)).valid);   // 1.5 mono frames
}

void test_reject_missing_data() {
  WavInfo info = parseWav(buildWav({fmtChunk(2, 44100, 16)}));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_reject_missing_fmt() {
  WavInfo info = parseWav(buildWav({plainChunk("data", 4)}));
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_adapter_read_failure_rejected() {
  WavInfo info = parseWav(simpleWav(2, 44100, 4), true, false);
  TEST_ASSERT_FALSE(info.valid);
  TEST_ASSERT_NOT_NULL(info.error);
}

void test_adapter_short_read_rejected() {
  const auto wf = simpleWav(2, 44100, 4);
  for (uint32_t offset : {0u, 12u, 20u}) {
    MemWav file{wf.segs, wf.logicalSize, false, true, offset};
    const WavInfo info = wavParseStream(memReadAt, &file, file.logicalSize);
    TEST_ASSERT_FALSE(info.valid);
  }
}

void test_sixty_second_logical_wav() {
  // 44 real header bytes; the 10584000-byte payload exists only in the
  // logical size served by the sparse reader.
  WavInfo info = parseWav(simpleWav(2, 44100, 10584000));
  TEST_ASSERT_TRUE(info.valid);
  TEST_ASSERT_EQUAL_UINT32(44, info.dataOffset);
  TEST_ASSERT_EQUAL_UINT32(10584000, info.dataSize);
  TEST_ASSERT_EQUAL_UINT32(60000, wavDurationMs(info));
}

void test_duration_and_seek_helpers() {
  const WavInfo info = validHelperInfo();
  TEST_ASSERT_EQUAL_UINT32(60000, wavDurationMs(info));
  TEST_ASSERT_EQUAL_UINT32(882000, wavSeekByteOffset(info, 5000));
  TEST_ASSERT_EQUAL_UINT32(info.dataSize, wavSeekByteOffset(info, UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT32(176, wavSeekByteOffset(info, 1));  // frame-aligned

  WavInfo bad;
  TEST_ASSERT_EQUAL_UINT32(0, wavDurationMs(bad));
  TEST_ASSERT_EQUAL_UINT32(0, wavSeekByteOffset(bad, 1000));

  WavInfo tiny = validHelperInfo();
  tiny.channels = 1;
  tiny.sampleRate = 22050;
  tiny.dataSize = 2;
  // Clamped in 64 bits before narrowing: 2 is the data end, frame-aligned.
  TEST_ASSERT_EQUAL_UINT32(2, wavSeekByteOffset(tiny, UINT32_MAX));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_stereo_44100);
  RUN_TEST(test_valid_mono_22050);
  RUN_TEST(test_fmt_with_extension_accepted);
  RUN_TEST(test_reject_ieee_float);
  RUN_TEST(test_reject_extensible);
  RUN_TEST(test_reject_8bit);
  RUN_TEST(test_reject_24bit);
  RUN_TEST(test_reject_rf64);
  RUN_TEST(test_reject_not_riff);
  RUN_TEST(test_reject_not_wave);
  RUN_TEST(test_reject_too_short);
  RUN_TEST(test_reject_fmt_below_16);
  RUN_TEST(test_reject_truncated_file);
  RUN_TEST(test_reject_riff_size_exceeds_file);
  RUN_TEST(test_reject_truncated_fmt_payload);
  RUN_TEST(test_reject_truncated_data_payload);
  RUN_TEST(test_reject_huge_chunk_size);
  RUN_TEST(test_ignores_bytes_outside_riff);
  RUN_TEST(test_junk_before_fmt);
  RUN_TEST(test_list_before_data);
  RUN_TEST(test_data_beyond_512);
  RUN_TEST(test_odd_padding_between_chunks);
  RUN_TEST(test_reject_data_before_fmt_odd_fmt_missing_pad);
  RUN_TEST(test_data_before_fmt_odd_fmt_padded);
  RUN_TEST(test_data_before_fmt);
  RUN_TEST(test_first_data_region_wins);
  RUN_TEST(test_reject_zero_rate);
  RUN_TEST(test_reject_block_align_mismatch);
  RUN_TEST(test_reject_byte_rate_mismatch);
  RUN_TEST(test_reject_partial_frame);
  RUN_TEST(test_reject_missing_data);
  RUN_TEST(test_reject_missing_fmt);
  RUN_TEST(test_adapter_read_failure_rejected);
  RUN_TEST(test_adapter_short_read_rejected);
  RUN_TEST(test_sixty_second_logical_wav);
  RUN_TEST(test_duration_and_seek_helpers);
  return UNITY_END();
}
