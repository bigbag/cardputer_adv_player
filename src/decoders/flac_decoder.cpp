#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#include "dr_flac.h"

#include "flac_decoder.hpp"
#include "config.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

#if AUDIO_DIAG
#include <Arduino.h>
#define FLAC_DIAG(...) Serial.printf(__VA_ARGS__)
#else
#define FLAC_DIAG(...)
#endif

#ifdef UNIT_TEST
#include "mp3_test_file.hpp"
#else
#include <SD.h>
#endif

struct FlacDecoder::Impl {
  struct alignas(std::max_align_t) Allocation { size_t bytes; };

  DecoderInput* in = nullptr;
  drflac* flac = nullptr;
  bool ownsInput = false;
  bool failed = false;
  AudioFormat fmt{};
  uint16_t sourceBits = 0;
  uint32_t fileSize = 0;
  uint64_t totalFrames = 0;
  uint64_t samplesDelivered = 0;
  size_t heapUsed = 0;
  size_t heapPeak = 0;
  size_t readWork = 0;
  size_t readBudget = cfg::kFlacDecodeReadBudget;

  static void* allocate(size_t bytes, void* context) {
    auto& d = *static_cast<Impl*>(context);
    if (bytes > cfg::kFlacHeapBudget - d.heapUsed) return nullptr;
    auto* allocation = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + bytes));
    if (!allocation) return nullptr;
    allocation->bytes = bytes;
    d.heapUsed += bytes;
    d.heapPeak = std::max(d.heapPeak, d.heapUsed);
    return allocation + 1;
  }

  static void release(void* data, void* context) {
    if (!data) return;
    auto& d = *static_cast<Impl*>(context);
    auto* allocation = static_cast<Allocation*>(data) - 1;
    d.heapUsed -= allocation->bytes;
    std::free(allocation);
  }

  static size_t read(void* context, void* buffer, size_t bytes) {
    auto& d = *static_cast<Impl*>(context);
    if (d.failed || bytes == 0) return 0;
    if (bytes > d.readBudget - d.readWork) {
      d.failed = true;
      return 0;
    }
    size_t done = 0;
    auto* out = static_cast<uint8_t*>(buffer);
    while (done < bytes) {
      const uint32_t position = d.in->position();
      if (position == d.fileSize) break;
      if (position > d.fileSize) {
        d.failed = true;
        break;
      }
      const size_t want = std::min(bytes - done, static_cast<size_t>(d.fileSize - position));
      const size_t got = d.in->read(out + done, want);
      if (got == 0 || got > want) {
        d.failed = true;
        break;
      }
      done += got;
      d.readWork += got;
    }
    return done;
  }

  static drflac_bool32 seek(void* context, int offset, drflac_seek_origin origin) {
    auto& d = *static_cast<Impl*>(context);
    if (d.failed) return DRFLAC_FALSE;
    int64_t base = 0;
    if (origin == DRFLAC_SEEK_CUR) base = d.in->position();
    else if (origin == DRFLAC_SEEK_END) base = d.fileSize;
    else if (origin != DRFLAC_SEEK_SET) return DRFLAC_FALSE;
    const int64_t target = base + offset;
    // Binary search can probe beyond the file. This is not an SD failure.
    if (target < 0 || static_cast<uint64_t>(target) > d.fileSize) return DRFLAC_FALSE;
    if (!d.in->seek(static_cast<uint32_t>(target))) {
      d.failed = true;
      return DRFLAC_FALSE;
    }
    return DRFLAC_TRUE;
  }

  static drflac_bool32 tell(void* context, drflac_int64* position) {
    auto& d = *static_cast<Impl*>(context);
    if (d.failed) return DRFLAC_FALSE;
    *position = d.in->position();
    return DRFLAC_TRUE;
  }

  bool preflight() {
    uint8_t header[42];
    if (read(this, header, sizeof(header)) != sizeof(header)) return false;
    if (std::memcmp(header, "fLaC", 4) != 0 || (header[4] & 0x7f) != 0 ||
        header[5] != 0 || header[6] != 0 || header[7] != 34) return false;
    const uint16_t minBlock = (uint16_t(header[8]) << 8) | header[9];
    const uint16_t maxBlock = (uint16_t(header[10]) << 8) | header[11];
    if (minBlock < 16 || minBlock > maxBlock || maxBlock > cfg::kFlacMaxBlockFrames) return false;
    uint64_t packed = 0;
    for (size_t i = 18; i < 26; ++i) packed = (packed << 8) | header[i];
    fmt.sampleRate = static_cast<uint32_t>(packed >> 44);
    fmt.sourceChannels = static_cast<uint16_t>(((packed >> 41) & 7) + 1);
    sourceBits = static_cast<uint16_t>(((packed >> 36) & 31) + 1);
    totalFrames = packed & ((uint64_t(1) << 36) - 1);
    if (fmt.sampleRate < 8000 || fmt.sampleRate > 48000 || fmt.sourceChannels > 2 ||
        (sourceBits != 16 && sourceBits != 24) || totalFrames == 0) return false;
    const uint64_t duration = totalFrames * 1000 / fmt.sampleRate;
    if (duration > UINT32_MAX) return false;
    fmt.channels = 2;
    fmt.bitsPerSample = 16;
    fmt.durationMs = static_cast<uint32_t>(duration);
    return true;
  }

  bool frameMatches(uint64_t next) const {
    const auto& frame = flac->currentFLACFrame;
    const auto& header = frame.header;
    if (header.blockSizeInPCMFrames == 0) return next == 0;
    if ((header.sampleRate != 0 && header.sampleRate != fmt.sampleRate) ||
        header.bitsPerSample != sourceBits ||
        frame.pcmFramesRemaining > header.blockSizeInPCMFrames) return false;
    const uint64_t first = header.pcmFrameNumber != 0 ? header.pcmFrameNumber :
        uint64_t(header.flacFrameNumber) * flac->maxBlockSizeInPCMFrames;
    const uint64_t end = first + header.blockSizeInPCMFrames;
    // CRC recovery must not shift audio past a missing frame.
    return end <= totalFrames && end - frame.pcmFramesRemaining == next;
  }
};

bool FlacDecoder::openInput(DecoderInput& input) {
  close();
  if (!input.valid() || !input.seek(0)) return false;
  impl_ = new (std::nothrow) Impl();
  if (!impl_) return false;
  auto& d = *impl_;
  d.in = &input;
  d.fileSize = input.size();
  if (!d.preflight() || !Impl::seek(&d, 0, DRFLAC_SEEK_SET)) {
    close();
    return false;
  }
  const drflac_allocation_callbacks allocation{&d, Impl::allocate, nullptr, Impl::release};
  d.flac = drflac_open(Impl::read, Impl::seek, Impl::tell, &d, &allocation);
  if (!d.flac || d.failed || d.flac->sampleRate != d.fmt.sampleRate ||
      d.flac->channels != d.fmt.sourceChannels || d.flac->bitsPerSample != d.sourceBits ||
      d.flac->totalPCMFrameCount != d.totalFrames ||
      d.flac->maxBlockSizeInPCMFrames > cfg::kFlacMaxBlockFrames) {
    close();
    return false;
  }
  FLAC_DIAG("[audio] flac open bits=%u block=%u heapPeak=%u\n", d.sourceBits,
            d.flac->maxBlockSizeInPCMFrames, static_cast<unsigned>(d.heapPeak));
  return true;
}

#ifdef UNIT_TEST
bool FlacDecoder::openWithFile(DecoderInput& input) { return openInput(input); }
#endif

bool FlacDecoder::open(const char* path) {
  close();
  if (!path) return false;
#ifdef UNIT_TEST
  auto* input = new (std::nothrow) Mp3TestFile();
  if (!input || !input->openFile(path)) { delete input; return false; }
#else
  auto* input = new (std::nothrow) SdInput();
  if (!input) return false;
  input->file = SD.open(path, FILE_READ);
  if (!input->valid()) { delete input; return false; }
#endif
  if (!openInput(*input)) { input->close(); delete input; return false; }
  impl_->ownsInput = true;
  return true;
}

void FlacDecoder::close() {
  if (!impl_) return;
  if (impl_->flac) drflac_close(impl_->flac);
  if (impl_->ownsInput) {
    impl_->in->close();
    delete impl_->in;
  }
  delete impl_;
  impl_ = nullptr;
}

AudioFormat FlacDecoder::format() const {
  return impl_ ? impl_->fmt : AudioFormat{};
}

DecodeStatus FlacDecoder::decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) {
  if (!gotFrames) return DecodeStatus::Error;
  *gotFrames = 0;
  if (!impl_ || impl_->failed || (!outStereo && maxFrames)) return DecodeStatus::Error;
  auto& d = *impl_;
  if (d.samplesDelivered == d.totalFrames) return DecodeStatus::Finished;
  if (maxFrames == 0) return DecodeStatus::NeedMore;
  d.readWork = 0;
  d.readBudget = cfg::kFlacDecodeReadBudget;
  const size_t capacity = std::min<size_t>(maxFrames, 512);
  while (*gotFrames < capacity && d.samplesDelivered < d.totalFrames) {
    const size_t remaining = d.flac->currentFLACFrame.pcmFramesRemaining;
    // Read one sample to load a new frame, then check its header before delivery.
    const size_t want = remaining ? std::min(capacity - *gotFrames, remaining) : 1;
    int16_t* output = outStereo + *gotFrames * 2;
    const size_t got = static_cast<size_t>(drflac_read_pcm_frames_s16(d.flac, want, output));
    if (d.failed || got != want || !d.frameMatches(d.samplesDelivered + got)) {
      d.failed = true;
      FLAC_DIAG("[audio] flac decode failed sample=%llu read=%u\n",
                static_cast<unsigned long long>(d.samplesDelivered),
                static_cast<unsigned>(d.readWork));
      return DecodeStatus::Error;
    }
    if (d.fmt.sourceChannels == 1) {
      for (size_t i = got; i-- > 0;) output[i * 2] = output[i * 2 + 1] = output[i];
    }
    *gotFrames += got;
    d.samplesDelivered += got;
  }
  return d.samplesDelivered == d.totalFrames ? DecodeStatus::Finished : DecodeStatus::Ok;
}

bool FlacDecoder::seekMs(uint32_t ms) {
  if (!impl_ || impl_->failed) return false;
  auto& d = *impl_;
  const uint64_t target = ms > 0 && ms >= d.fmt.durationMs ? d.totalFrames :
      std::min(d.totalFrames, uint64_t(ms) * d.fmt.sampleRate / 1000);
  if (target == d.totalFrames) {
    d.samplesDelivered = target;
    return true;
  }
  d.readWork = 0;
  d.readBudget = cfg::kFlacSeekReadBudget;
  const uint64_t current = d.flac->currentPCMFrame;
  const uint64_t distance = target > current ? target - current : current - target;
  // dr_flac narrows same-frame seek distances to 32 bits.
  if (distance > UINT32_MAX && !drflac_seek_to_pcm_frame(d.flac, 0)) {
    d.failed = true;
    return false;
  }
  const bool ok = drflac_seek_to_pcm_frame(d.flac, target) != 0;
  if (!ok || d.failed || d.flac->currentPCMFrame != target || !d.frameMatches(target)) {
    d.failed = true;
    return false;
  }
  d.samplesDelivered = target;
  return true;
}

uint32_t FlacDecoder::positionMs() const {
  return impl_ ? static_cast<uint32_t>(impl_->samplesDelivered * 1000 / impl_->fmt.sampleRate) : 0;
}
