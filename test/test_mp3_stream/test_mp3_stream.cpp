// Native MP3 decoder stream tests. Every case drives the real minimp3
// decoder through Mp3Decoder over fixture bytes; expected PCM comes from an
// independent single-shot minimp3 reference pass over the same bytes.

#include <unity.h>

#include "decoders/mp3_decoder.hpp"
#include "mp3_test_file.hpp"
#include "minimp3.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// Locate the fixture directory without build-time configuration: env var,
// repo-relative path, then a path derived from __FILE__.
std::string fixturePath(const char* name) {
  if (const char* env = std::getenv("MP3_FIXTURE_DIR")) {
    return std::string(env) + "/" + name;
  }
  std::string rel = std::string("test/fixtures/mp3/") + name;
  if (std::FILE* f = std::fopen(rel.c_str(), "rb")) {
    std::fclose(f);
    return rel;
  }
  std::string base = __FILE__;
  base = base.substr(0, base.find_last_of("/\\"));
  base = base.substr(0, base.find_last_of("/\\"));
  return base + "/fixtures/mp3/" + name;
}

Mp3TestFile loadFixture(const char* name) {
  Mp3TestFile f;
  TEST_ASSERT_TRUE_MESSAGE(f.openFile(fixturePath(name).c_str()), name);
  return f;
}

// Independent reference: one minimp3 pass over the whole byte range, mono
// duplicated to stereo the way the decoder delivers it.
std::vector<int16_t> referencePcm(const std::vector<uint8_t>& bytes,
                                  uint32_t* outHz, int* outChannels) {
  mp3dec_t dec;
  mp3dec_init(&dec);
  std::vector<int16_t> out;
  mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  size_t off = 0;
  *outHz = 0;
  *outChannels = 0;
  while (off + 4 < bytes.size()) {
    mp3dec_frame_info_t info{};
    int samples = mp3dec_decode_frame(&dec, bytes.data() + off,
                                      static_cast<int>(bytes.size() - off),
                                      pcm, &info);
    if (info.frame_bytes == 0) break;
    off += static_cast<size_t>(info.frame_bytes);
    if (samples <= 0) continue;
    if (*outHz == 0) {
      *outHz = static_cast<uint32_t>(info.hz);
      *outChannels = info.channels;
    }
    if (info.channels == 1) {
      for (int i = 0; i < samples; i++) {
        out.push_back(pcm[i]);
        out.push_back(pcm[i]);
      }
    } else {
      out.insert(out.end(), pcm, pcm + samples * 2);
    }
  }
  return out;
}

struct DrainResult {
  std::vector<int16_t> pcm;
  DecodeStatus status = DecodeStatus::Ok;
  int calls = 0;      // total decode calls
  int idleCalls = 0;  // calls that returned NeedMore with no PCM (budget)
};

// Decode to completion with a bounded call count. Budget: one input window of
// compressed bytes per call, so calls stay well under fileSize/512 plus slop.
DrainResult drain(Mp3Decoder& dec, size_t capFrames = 512) {
  DrainResult r;
  std::vector<int16_t> buf(capFrames * 2);
  const int maxCalls = 4096;
  while (r.calls < maxCalls) {
    size_t got = 0;
    DecodeStatus st = dec.decode(buf.data(), capFrames, &got);
    r.calls++;
    r.pcm.insert(r.pcm.end(), buf.begin(), buf.begin() + static_cast<long>(got * 2));
    if (got == 0 && st == DecodeStatus::NeedMore) r.idleCalls++;
    if (st == DecodeStatus::Finished || st == DecodeStatus::Error) {
      r.status = st;
      return r;
    }
  }
  r.status = DecodeStatus::Error;
  TEST_FAIL_MESSAGE("decode call count exceeded the bound");
  return r;
}

struct AlignmentResult {
  int32_t diff = INT32_MAX;
  long offset = -1;  // best haystack frame; -1 when the window holds no needle
};

// Find the smallest maximum sample difference in [loFrame, hiFrame).
// MPEG1 frame boundaries add up to 1152 samples of position error.
// This is about 26 ms at 44.1 kHz.
// Byte mapping can add position error.
// Callers use wider search windows for these differences.
// Chirp fixtures give each region a distinct sample sequence.
AlignmentResult bestAlignment(const std::vector<int16_t>& needle,
                              const std::vector<int16_t>& haystack, long loFrame,
                              long hiFrame) {
  const size_t needleFrames = needle.size() / 2;
  AlignmentResult r;
  if (needleFrames == 0 || hiFrame <= loFrame) return r;
  for (long f = loFrame; f >= 0 && f + static_cast<long>(needleFrames) <= hiFrame; f++) {
    const int16_t* h = haystack.data() + f * 2;
    int32_t maxd = 0;
    for (size_t i = 0; i < needle.size(); i++) {
      int32_t diff = static_cast<int32_t>(needle[i]) - static_cast<int32_t>(h[i]);
      if (diff < 0) diff = -diff;
      if (diff > maxd) maxd = diff;
    }
    if (maxd < r.diff) {
      r.diff = maxd;
      r.offset = f;
    }
  }
  return r;
}

}  // namespace

void test_full_stream_matches_reference() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_EQUAL_UINT32(44100, fmt.sampleRate);
  TEST_ASSERT_EQUAL_UINT16(2, fmt.channels);
  TEST_ASSERT_EQUAL_UINT16(2, fmt.sourceChannels);

  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
  // Delivery is capped per call; the bound that matters is budget-idle calls.
  TEST_ASSERT_LESS_THAN_INT32(16, r.idleCalls);
}

void test_short_reads_produce_identical_pcm() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);

  Mp3Decoder dec;
  file.setReadLimit(37);  // short reads across frame headers (frame ~= 418 B)
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}

void test_short_reads_high_bitrate_identical_pcm() {
  Mp3TestFile file = loadFixture("cbr320_stereo_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);

  Mp3Decoder dec;
  file.setReadLimit(100);  // 320 kbit/s frames are ~1045 B
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}

void test_mono_and_other_rates_match_reference() {
  const char* files[] = {
      "cbr128_mono_44100.mp3", "cbr128_stereo_48000.mp3", "mpeg2_22050_mono.mp3",
  };
  for (const char* name : files) {
    Mp3TestFile file = loadFixture(name);
    std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
    uint32_t hz = 0;
    int channels = 0;
    std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
    TEST_ASSERT_TRUE(want.size() > 0);

    Mp3Decoder dec;
    TEST_ASSERT_TRUE(dec.openWithFile(file));
    AudioFormat fmt = dec.format();
    TEST_ASSERT_EQUAL_UINT32(hz, fmt.sampleRate);
    TEST_ASSERT_EQUAL_UINT16(2, fmt.channels);
    TEST_ASSERT_EQUAL_UINT16(static_cast<uint16_t>(channels), fmt.sourceChannels);

    DrainResult r = drain(dec);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                            static_cast<uint8_t>(r.status));
    TEST_ASSERT_TRUE(r.pcm == want);
  }
}

void test_vbr_stream_matches_reference() {
  Mp3TestFile file = loadFixture("vbr_stereo_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}

void test_id3_prefixed_stream_matches_reference() {
  for (const char* name : {"id3v23_cbr128_44100.mp3", "id3v24_cbr128_44100.mp3"}) {
    Mp3TestFile file = loadFixture(name);
    std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
    uint32_t hz = 0;
    int channels = 0;
    std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
    TEST_ASSERT_TRUE(want.size() > 0);

    Mp3Decoder dec;
    TEST_ASSERT_TRUE(dec.openWithFile(file));
    DrainResult r = drain(dec);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                            static_cast<uint8_t>(r.status));
    TEST_ASSERT_TRUE(r.pcm == want);
  }
}

void test_final_partial_frame_finishes_with_complete_prefix() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  file.truncateTo(file.byteSize() - 100);  // cut inside the last frame
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}

void test_junk_input_is_rejected_at_open() {
  // No 0xFF byte: no sync word, so no frame can ever be found.
  std::vector<uint8_t> junk(40 * 1024);
  uint32_t s = 12345;
  for (size_t i = 0; i < junk.size(); i++) {
    s = s * 1103515245u + 12345u;
    junk[i] = static_cast<uint8_t>((s >> 16) & 0x7F);
  }
  Mp3TestFile file = Mp3TestFile::fromBytes(junk.data(), junk.size());
  Mp3Decoder dec;
  TEST_ASSERT_FALSE(dec.openWithFile(file));

  junk.resize(1024);  // smaller than any window
  Mp3TestFile small = Mp3TestFile::fromBytes(junk.data(), junk.size());
  Mp3Decoder dec2;
  TEST_ASSERT_FALSE(dec2.openWithFile(small));
}

void test_junk_after_valid_prefix_is_controlled_error() {
  Mp3TestFile good = loadFixture("cbr128_stereo_44100.mp3");
  // Enough leading frames for minimp3's 10-header sync match, then garbage.
  std::vector<uint8_t> bytes(good.data(), good.data() + 16384);
  uint32_t s = 987654321u;
  for (size_t i = 0; i < 32 * 1024u; i++) {
    s = s * 1103515245u + 12345u;
    bytes.push_back(static_cast<uint8_t>((s >> 16) & 0x7F));
  }
  Mp3TestFile file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Error),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_LESS_THAN_INT32(128, r.calls);
}

void test_failed_read_before_eof_is_error_not_finished() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  file.failNextReads(1);  // I/O failure in the middle of the file

  DrainResult r = drain(dec);
  // Delivered PCM stays an exact prefix of the reference and the failure is
  // reported as Error, never as Finished.
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Error),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm.size() < want.size());
  TEST_ASSERT_TRUE(std::vector<int16_t>(want.begin(),
                                        want.begin() + static_cast<long>(r.pcm.size())) == r.pcm);
}

void test_failed_seek_at_open_fails() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  file.failNextSeeks(1);
  Mp3Decoder dec;
  TEST_ASSERT_FALSE(dec.openWithFile(file));
}

void test_zero_capacity_decode_still_makes_progress() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  size_t got = 99;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Ok),
                          static_cast<uint8_t>(dec.decode(nullptr, 0, &got)));
  TEST_ASSERT_EQUAL_UINT32(0, got);
  // A following normal call still returns the prefetched PCM.
  int16_t buf[512 * 2];
  TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
  TEST_ASSERT_TRUE(got > 0);
}

void test_position_zero_after_open_and_advances_by_delivered_frames() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  TEST_ASSERT_EQUAL_UINT32(0, dec.positionMs());

  int16_t buf[512 * 2];
  size_t got = 0;
  TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
  TEST_ASSERT_EQUAL_UINT32(512, got);
  TEST_ASSERT_EQUAL_UINT32((uint64_t)512 * 1000 / 44100, dec.positionMs());

  // A call returns only the frames it delivers; the counter advances by
  // exactly the cumulative delivered frames (512 + returned), regardless of
  // leftover refill inside the call.
  int16_t wide[1024 * 2];
  TEST_ASSERT_TRUE(dec.decode(wide, 1024, &got) == DecodeStatus::Ok);
  TEST_ASSERT_EQUAL_UINT32(1024, got);  // cap-limited: 640 leftover + next frame
  TEST_ASSERT_EQUAL_UINT32((uint64_t)(512 + 1024) * 1000 / 44100, dec.positionMs());

  // Zero-capacity decode advances nothing.
  size_t ignored = 0;
  TEST_ASSERT_TRUE(dec.decode(buf, 0, &ignored) == DecodeStatus::Ok);

  // Position never moves backward during continuous decode.
  uint32_t last = dec.positionMs();
  for (int i = 0; i < 64; i++) {
    size_t n = 0;
    DecodeStatus st = dec.decode(buf, 512, &n);
    uint32_t pos = dec.positionMs();
    TEST_ASSERT_TRUE(pos >= last);
    last = pos;
    if (st == DecodeStatus::Finished) break;
    TEST_ASSERT_TRUE(st == DecodeStatus::Ok || st == DecodeStatus::NeedMore);
  }
  TEST_ASSERT_TRUE(last > 0);
}

void test_estimate_path_keeps_first_audio_and_matches_reference() {
  Mp3TestFile file = loadFixture("noxing_cbr128_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  // The scan-based estimate is close to the encoded 2 seconds.
  TEST_ASSERT_TRUE(fmt.durationEstimated);
  TEST_ASSERT_TRUE(fmt.durationMs > 1600 && fmt.durationMs < 2400);
  TEST_ASSERT_EQUAL_UINT32(0, dec.positionMs());

  // First delivered PCM matches the reference start: the estimation scan
  // did not consume the opening audio.
  int16_t buf[512 * 2];
  size_t got = 0;
  TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
  TEST_ASSERT_EQUAL_UINT32(512, got);
  TEST_ASSERT_TRUE(std::vector<int16_t>(want.begin(), want.begin() + 1024) ==
                   std::vector<int16_t>(buf, buf + 1024));

  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  // r.pcm starts after the 512 frames delivered above.
  TEST_ASSERT_TRUE(r.pcm == std::vector<int16_t>(want.begin() + 1024, want.end()));
}

void test_failed_read_keeps_delivered_position() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  int16_t buf[512 * 2];
  size_t got = 0;
  TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
  uint32_t before = dec.positionMs();

  file.failNextReads(1);
  for (int i = 0; i < 4096; i++) {
    size_t n = 0;
    DecodeStatus st = dec.decode(buf, 512, &n);
    TEST_ASSERT_TRUE(dec.positionMs() >= before);
    if (st == DecodeStatus::Error) break;
  }
  TEST_ASSERT_TRUE(dec.positionMs() >= before);
}

void test_seek_delivers_reference_pcm_from_mapped_region() {
  // No-Xing fixture: the seek map is the documented linear fallback, so the
  // byte region is computable and the delivered PCM must match a reference
  // decode started at exactly that byte.
  Mp3TestFile file = loadFixture("noxing_cbr128_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationMs > 0);
  uint32_t rel = static_cast<uint32_t>(
      (uint64_t)file.byteSize() * 1000 / fmt.durationMs);

  TEST_ASSERT_TRUE(dec.seekMs(1000));
  TEST_ASSERT_EQUAL_UINT32(1000, dec.positionMs());

  std::vector<int16_t> out;
  int16_t buf[512 * 2];
  for (int i = 0; i < 3; i++) {
    size_t got = 0;
    TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
    TEST_ASSERT_EQUAL_UINT32(512, got);
    out.insert(out.end(), buf, buf + got * 2);
  }
  // Pre-roll delivers the first frame at/after the mapped byte; the output
  // must match the continuous reference decode at that alignment.
  const long totalFrames = static_cast<long>(want.size() / 2);
  const long expectFrames =
      static_cast<long>((uint64_t)rel * totalFrames / file.byteSize());
  AlignmentResult a = bestAlignment(out, want, expectFrames - 1500,
                                    expectFrames + 2500 + 1536);
  TEST_ASSERT_TRUE(a.diff <= 32);
  TEST_ASSERT_TRUE(a.offset >= 0);
  TEST_ASSERT_TRUE(a.offset + 1500 >= expectFrames);
  TEST_ASSERT_TRUE(a.offset <= expectFrames + 2500);
  TEST_ASSERT_TRUE(a.offset >= expectFrames - 32);

  // The chirp fixture makes regions unique: the output must NOT align with
  // the start of the reference track.
  AlignmentResult start = bestAlignment(out, want, 0, 64 + 1536);
  TEST_ASSERT_TRUE(start.diff > 8 * a.diff);
}

void test_seek_with_id3_uses_mpeg_start_base() {
  Mp3TestFile file = loadFixture("id3v23_cbr128_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationMs > 0);

  // Mirror the decoder: audio range starts after the ID3v2 tag.
  uint32_t id3Size = 10u + (((uint32_t)raw[6] & 0x7F) << 21) +
                     (((uint32_t)raw[7] & 0x7F) << 14) +
                     (((uint32_t)raw[8] & 0x7F) << 7) + (uint32_t)raw[9];
  uint32_t audioBytes = static_cast<uint32_t>(raw.size() - id3Size);
  uint32_t rel = (uint32_t)((uint64_t)audioBytes * 500 / fmt.durationMs);

  TEST_ASSERT_TRUE(dec.seekMs(500));

  std::vector<int16_t> out;
  int16_t buf[512 * 2];
  for (int i = 0; i < 3; i++) {
    size_t got = 0;
    TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
    TEST_ASSERT_EQUAL_UINT32(512, got);
    out.insert(out.end(), buf, buf + got * 2);
  }
  // Full continuous reference; alignment center is the mapped audio position
  // (500 ms worth of bytes after the ID3 prefix), proving the prefix was
  // excluded from the seek map.
  uint32_t hz2 = 0;
  int ch2 = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz2, &ch2);
  TEST_ASSERT_TRUE(want.size() > 0);
  const long totalFrames = static_cast<long>(want.size() / 2);
  const long expectFrames = static_cast<long>(
      (uint64_t)(id3Size + rel) * totalFrames / raw.size());
  AlignmentResult a = bestAlignment(out, want, expectFrames - 1500,
                                    expectFrames + 2500 + 1536);
  TEST_ASSERT_TRUE(a.diff <= 32);
  TEST_ASSERT_TRUE(a.offset >= 0);
  TEST_ASSERT_TRUE(a.offset + 1500 >= expectFrames);
  TEST_ASSERT_TRUE(a.offset <= expectFrames + 2500);

  AlignmentResult start = bestAlignment(out, want, 0, 64 + 1536);
  TEST_ASSERT_TRUE(start.diff > 8 * a.diff);
}

void test_seek_beyond_duration_finishes_without_output() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationMs > 0);

  TEST_ASSERT_TRUE(dec.seekMs(fmt.durationMs + 5000));
  int16_t buf[512 * 2];
  size_t got = 1;
  int calls = 0;
  DecodeStatus st = DecodeStatus::Ok;
  while (calls++ < 64) {
    st = dec.decode(buf, 512, &got);
    if (st == DecodeStatus::Finished || st == DecodeStatus::Error) break;
  }
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(st));
  TEST_ASSERT_EQUAL_UINT32(0, got);
}

void test_failed_seek_keeps_pcm_and_position() {
  Mp3TestFile file = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  int16_t buf[512 * 2];
  size_t got = 0;
  TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
  uint32_t posBefore = dec.positionMs();

  file.failNextSeeks(1);
  TEST_ASSERT_FALSE(dec.seekMs(1000));
  TEST_ASSERT_EQUAL_UINT32(posBefore, dec.positionMs());

  // Playback continues seamlessly: the next 512 frames are exactly the
  // reference continuation.
  TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
  TEST_ASSERT_EQUAL_UINT32(512, got);
  TEST_ASSERT_TRUE(std::vector<int16_t>(want.begin() + 1024, want.begin() + 2048) ==
                   std::vector<int16_t>(buf, buf + 1024));
}

void test_vbr_toc_seek_lands_near_requested_time() {
  Mp3TestFile file = loadFixture("vbr_stereo_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);
  const uint32_t totalMs = static_cast<uint32_t>(
      (uint64_t)want.size() / 2 * 1000 / hz);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationMs > 0);

  TEST_ASSERT_TRUE(dec.seekMs(3000));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  // Delivered audio plus the requested target should land within +/-1.5 s
  // of the full track length (TOC accuracy on a generated fixture).
  const uint32_t deliveredMs =
      static_cast<uint32_t>((uint64_t)r.pcm.size() / 2 * 1000 / hz);
  TEST_ASSERT_TRUE(deliveredMs + 3000 + 1500 >= totalMs);
  TEST_ASSERT_TRUE(deliveredMs + 3000 <= totalMs + 1500);
}

void test_seek_to_zero_replays_from_the_beginning() {
  Mp3TestFile file = loadFixture("noxing_cbr128_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  int16_t buf[512 * 2];
  size_t got = 0;
  TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);

  // Seek to zero is normal startup: no warm-up discard, original output.
  TEST_ASSERT_TRUE(dec.seekMs(0));
  TEST_ASSERT_EQUAL_UINT32(0, dec.positionMs());
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}

void test_midfile_seek_output_corresponds_to_target_region() {
  Mp3TestFile file = loadFixture("noxing_cbr128_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationMs > 0);
  TEST_ASSERT_TRUE(dec.seekMs(1000));  // ~44100 stereo frames in

  std::vector<int16_t> out;
  int16_t buf[512 * 2];
  for (int i = 0; i < 3; i++) {
    size_t got = 0;
    TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);
    TEST_ASSERT_EQUAL_UINT32(512, got);
    out.insert(out.end(), buf, buf + got * 2);
  }
  // Pre-roll output must match the continuous reference bit-near at the
  // alignment implied by the mapped target byte.
  const long totalFrames = static_cast<long>(want.size() / 2);
  const uint32_t audioBytes = static_cast<uint32_t>(file.byteSize());
  const uint32_t rel =
      (uint32_t)((uint64_t)audioBytes * 1000 / fmt.durationMs);
  const long expectFrames =
      static_cast<long>((uint64_t)rel * totalFrames / audioBytes);
  AlignmentResult a = bestAlignment(out, want, expectFrames - 1500,
                                    expectFrames + 2500 + 1536);
  TEST_ASSERT_TRUE(a.diff <= 32);
  TEST_ASSERT_TRUE(a.offset >= 0);
  TEST_ASSERT_TRUE(a.offset + 1500 >= expectFrames);
  TEST_ASSERT_TRUE(a.offset <= expectFrames + 2500);

  AlignmentResult start = bestAlignment(out, want, 0, 64 + 1536);
  TEST_ASSERT_TRUE(start.diff > 8 * a.diff);
}

void test_repeated_seek_discards_old_leftovers() {
  Mp3TestFile file = loadFixture("noxing_cbr128_44100.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationMs > 0);
  TEST_ASSERT_TRUE(dec.seekMs(1000));
  int16_t buf[512 * 2];
  size_t got = 0;
  TEST_ASSERT_TRUE(dec.decode(buf, 512, &got) == DecodeStatus::Ok);

  // A second seek must not leak PCM from the first target's leftovers.
  TEST_ASSERT_TRUE(dec.seekMs(500));
  std::vector<int16_t> out;
  for (int i = 0; i < 3; i++) {
    size_t n = 0;
    TEST_ASSERT_TRUE(dec.decode(buf, 512, &n) == DecodeStatus::Ok);
    TEST_ASSERT_EQUAL_UINT32(512, n);
    out.insert(out.end(), buf, buf + n * 2);
  }
  const long totalFrames = static_cast<long>(want.size() / 2);
  const uint32_t audioBytes = static_cast<uint32_t>(file.byteSize());
  const long expectFrames = static_cast<long>(
      (uint64_t)audioBytes * 500 / fmt.durationMs * totalFrames / audioBytes);
  AlignmentResult a = bestAlignment(out, want, expectFrames - 1500,
                                    expectFrames + 2500 + 1536);
  TEST_ASSERT_TRUE(a.diff <= 32);
  TEST_ASSERT_TRUE(a.offset >= 0);
  TEST_ASSERT_TRUE(a.offset + 1500 >= expectFrames);
  TEST_ASSERT_TRUE(a.offset <= expectFrames + 2500);

  AlignmentResult start = bestAlignment(out, want, 0, 64 + 1536);
  TEST_ASSERT_TRUE(start.diff > 8 * a.diff);
}

void test_seek_near_eof_stays_bounded() {
  Mp3TestFile file = loadFixture("mpeg2_22050_mono.mp3");
  std::vector<uint8_t> raw(file.data(), file.data() + file.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);
  const uint32_t totalMs = static_cast<uint32_t>(
      (uint64_t)want.size() / 2 * 1000 / hz);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationMs > 0);
  TEST_ASSERT_EQUAL_UINT16(1, fmt.sourceChannels);

  // Near-EOF target: pre-roll terminates as a bounded EOF, never a loop.
  TEST_ASSERT_TRUE(dec.seekMs(totalMs - 150));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.calls < 64);
}


void test_open_path_owns_adapter_and_decodes() {
  // open(path) must keep its adapter alive for the decoder lifetime (device
  // parity); decode the full fixture through it and match the reference.
  std::string path = fixturePath("noxing_cbr128_44100.mp3");
  Mp3TestFile probe;
  TEST_ASSERT_TRUE(probe.openFile(path.c_str()));
  std::vector<uint8_t> raw(probe.data(), probe.data() + probe.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.open(path.c_str()));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}


void test_id3v1_trailer_ends_finished_with_same_pcm() {
  Mp3TestFile base = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(base.data(), base.data() + base.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  // Real ID3v1 trailer: 'TAG' + 125 bytes of tag fields.
  std::vector<uint8_t> tagged = raw;
  tagged.push_back('T');
  tagged.push_back('A');
  tagged.push_back('G');
  for (int i = 0; i < 125; i++) {
    tagged.push_back(i < 30 ? static_cast<uint8_t>('a' + i % 26) : 0);
  }

  Mp3TestFile file = Mp3TestFile::fromBytes(tagged.data(), tagged.size());
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationMs > 1500 && fmt.durationMs < 2500);
  TEST_ASSERT_TRUE(!fmt.durationEstimated);

  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);  // trailer never reaches the decoder
  TEST_ASSERT_LESS_THAN_INT32(16, r.idleCalls);

  // End seek on the tagged file stays bounded and finishes.
  TEST_ASSERT_TRUE(dec.seekMs(fmt.durationMs));
  DrainResult end = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(end.status));
}

void test_short_zero_padding_tail_ends_finished() {
  Mp3TestFile base = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(base.data(), base.data() + base.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);

  std::vector<uint8_t> padded = raw;
  padded.insert(padded.end(), 64, 0x00);  // short all-zero padding

  Mp3TestFile file = Mp3TestFile::fromBytes(padded.data(), padded.size());
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}

void test_bogus_xing_metadata_falls_back_to_estimate() {
  Mp3TestFile base = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(base.data(), base.data() + base.byteSize());

  // Locate the Info/Xing tag inside the first frame (after the 20-byte ID3,
  // 4-byte header, 32-byte side info) and poison the frames and bytes
  // fields with implausible values.
  size_t tagPos = 20 + 4 + 32;
  TEST_ASSERT_TRUE(tagPos + 16 <= raw.size());
  auto poison = [&](size_t at, uint32_t v) {
    raw[at] = static_cast<uint8_t>(v >> 24);
    raw[at + 1] = static_cast<uint8_t>(v >> 16);
    raw[at + 2] = static_cast<uint8_t>(v >> 8);
    raw[at + 3] = static_cast<uint8_t>(v);
  };
  poison(tagPos + 8, 0xFFFFFFFFu);   // frames
  poison(tagPos + 12, 0xFFFFFFFFu);  // bytes

  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3TestFile file = Mp3TestFile::fromBytes(raw.data(), raw.size());
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  // Bad optional metadata must not become a 49-day duration: the decoder
  // falls back to the bounded estimate and keeps playing.
  TEST_ASSERT_TRUE(fmt.durationEstimated);
  TEST_ASSERT_TRUE(fmt.durationMs > 1500 && fmt.durationMs < 2500);

  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);

  // A poisoned bytes field alone must NOT discard a plausible frame count:
  // the frame-count duration stays exact, only the TOC is dropped.
  std::vector<uint8_t> raw2(base.data(), base.data() + base.byteSize());
  size_t tagPos2 = 20 + 4 + 32;
  raw2[tagPos2 + 12] = 0x7F;
  raw2[tagPos2 + 13] = 0xFF;
  raw2[tagPos2 + 14] = 0xFF;
  raw2[tagPos2 + 15] = 0xFF;  // bytes = 0x7FFFFFFF > file size
  std::vector<int16_t> want2 = referencePcm(raw2, &hz, &channels);
  Mp3TestFile file2 = Mp3TestFile::fromBytes(raw2.data(), raw2.size());
  Mp3Decoder dec2;
  TEST_ASSERT_TRUE(dec2.openWithFile(file2));
  AudioFormat fmt2 = dec2.format();
  TEST_ASSERT_TRUE(!fmt2.durationEstimated);  // frame count stays usable
  TEST_ASSERT_TRUE(fmt2.durationMs > 1500 && fmt2.durationMs < 2500);
  DrainResult r2 = drain(dec2);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r2.status));
  TEST_ASSERT_TRUE(r2.pcm == want2);
}

void test_estimate_scan_reads_are_bounded_across_junk_gap() {
  Mp3TestFile base = loadFixture("noxing_cbr128_44100.mp3");
  std::vector<uint8_t> bytes(base.data(), base.data() + 8192);  // ~19 frames
  uint32_t s = 424242u;
  for (size_t i = 0; i < 400 * 1024u; i++) {  // long junk gap, no sync byte
    s = s * 1103515245u + 12345u;
    bytes.push_back(static_cast<uint8_t>((s >> 16) & 0x7F));
  }
  bytes.insert(bytes.end(), base.data() + 16384, base.data() + base.byteSize());

  Mp3TestFile file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  AudioFormat fmt = dec.format();
  TEST_ASSERT_TRUE(fmt.durationEstimated);
  TEST_ASSERT_TRUE(fmt.durationMs > 0);
  // The open-time estimate scan must cap its read work at the documented
  // 256 KiB bound plus window slop, not sweep the whole 400 KiB gap.
  TEST_ASSERT_TRUE(file.totalBytesRead() < 320u * 1024u);
}


void test_id3v1_with_short_reads_never_becomes_error() {
  // The trailer probe itself must survive short platform reads, and the
  // tagged file must decode fully under aggressive short reads.
  Mp3TestFile base = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(base.data(), base.data() + base.byteSize());
  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(raw, &hz, &channels);

  std::vector<uint8_t> tagged = raw;
  tagged.push_back('T');
  tagged.push_back('A');
  tagged.push_back('G');
  for (int i = 0; i < 125; i++) tagged.push_back(0);

  Mp3TestFile file = Mp3TestFile::fromBytes(tagged.data(), tagged.size());
  file.setReadLimit(7);  // every read is a positive short read
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  TEST_ASSERT_TRUE(dec.format().durationMs > 1500 &&
                   dec.format().durationMs < 2500);
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}


void test_large_garbage_prefix_rejects_open_with_bounded_reads() {
  // A non-sync prefix longer than one window must be rejected at open with
  // bounded read work, never scanned whole-file.
  Mp3TestFile base = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> bytes(48 * 1024u);  // > 16 KiB non-sync prefix
  uint32_t s = 777u;
  for (size_t i = 0; i < bytes.size(); i++) {
    s = s * 1103515245u + 12345u;
    bytes[i] = static_cast<uint8_t>((s >> 16) & 0x7F);
  }
  bytes.insert(bytes.end(), base.data(), base.data() + base.byteSize());

  Mp3TestFile file = Mp3TestFile::fromBytes(bytes.data(), bytes.size());
  Mp3Decoder dec;
  TEST_ASSERT_FALSE(dec.openWithFile(file));
  TEST_ASSERT_TRUE(file.totalBytesRead() < 64u * 1024u);
}

void test_short_file_with_padding_still_opens_and_finishes() {
  // A few complete frames plus trailing zero padding: open must sync on
  // the capped single-frame decode, finish cleanly, and preserve the exact
  // PCM of the unpadded short stream.
  Mp3TestFile base = loadFixture("cbr128_stereo_44100.mp3");
  std::vector<uint8_t> raw(base.data(), base.data() + base.byteSize());
  std::vector<uint8_t> shortRaw(raw.begin(), raw.begin() + 1700);  // ~4 frames

  std::vector<uint8_t> padded = shortRaw;
  padded.insert(padded.end(), 64, 0x00);

  uint32_t hz = 0;
  int channels = 0;
  std::vector<int16_t> want = referencePcm(padded, &hz, &channels);
  TEST_ASSERT_TRUE(want.size() > 0);

  Mp3TestFile file = Mp3TestFile::fromBytes(padded.data(), padded.size());
  Mp3Decoder dec;
  TEST_ASSERT_TRUE(dec.openWithFile(file));
  DrainResult r = drain(dec);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(DecodeStatus::Finished),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.pcm == want);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_full_stream_matches_reference);
  RUN_TEST(test_short_reads_produce_identical_pcm);
  RUN_TEST(test_short_reads_high_bitrate_identical_pcm);
  RUN_TEST(test_mono_and_other_rates_match_reference);
  RUN_TEST(test_vbr_stream_matches_reference);
  RUN_TEST(test_id3_prefixed_stream_matches_reference);
  RUN_TEST(test_final_partial_frame_finishes_with_complete_prefix);
  RUN_TEST(test_junk_input_is_rejected_at_open);
  RUN_TEST(test_junk_after_valid_prefix_is_controlled_error);
  RUN_TEST(test_failed_read_before_eof_is_error_not_finished);
  RUN_TEST(test_failed_seek_at_open_fails);
  RUN_TEST(test_zero_capacity_decode_still_makes_progress);
  RUN_TEST(test_position_zero_after_open_and_advances_by_delivered_frames);
  RUN_TEST(test_failed_read_keeps_delivered_position);
  RUN_TEST(test_estimate_path_keeps_first_audio_and_matches_reference);
  RUN_TEST(test_seek_delivers_reference_pcm_from_mapped_region);
  RUN_TEST(test_seek_with_id3_uses_mpeg_start_base);
  RUN_TEST(test_seek_beyond_duration_finishes_without_output);
  RUN_TEST(test_failed_seek_keeps_pcm_and_position);
  RUN_TEST(test_vbr_toc_seek_lands_near_requested_time);
  RUN_TEST(test_seek_to_zero_replays_from_the_beginning);
  RUN_TEST(test_midfile_seek_output_corresponds_to_target_region);
  RUN_TEST(test_repeated_seek_discards_old_leftovers);
  RUN_TEST(test_seek_near_eof_stays_bounded);
  RUN_TEST(test_open_path_owns_adapter_and_decodes);
  RUN_TEST(test_id3v1_trailer_ends_finished_with_same_pcm);
  RUN_TEST(test_short_zero_padding_tail_ends_finished);
  RUN_TEST(test_bogus_xing_metadata_falls_back_to_estimate);
  RUN_TEST(test_estimate_scan_reads_are_bounded_across_junk_gap);
  RUN_TEST(test_id3v1_with_short_reads_never_becomes_error);
  RUN_TEST(test_large_garbage_prefix_rejects_open_with_bounded_reads);
  RUN_TEST(test_short_file_with_padding_still_opens_and_finishes);
  return UNITY_END();
}
