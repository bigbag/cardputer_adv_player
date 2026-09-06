#pragma once

#include "decoder.hpp"
#include <cstdint>

struct WavInfo {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  bool valid = false;
  const char* error = nullptr;
};

// Bounded random-access byte source. Returns the number of bytes read;
// reads shorter than the requested size mean truncated data or a card
// failure, never normal end of file.
using WavReadAt = size_t (*)(void* context, uint32_t offset,
                             uint8_t* destination, size_t size);

// Scans the RIFF chunk tree through readAt. Works on real files and
// in-memory buffers alike. Memory use is fixed: unknown chunks are
// skipped by offset, never buffered.
WavInfo wavParseStream(WavReadAt readAt, void* context, uint32_t fileSize);

// Both helpers consume a validated WavInfo and return zero otherwise.
uint32_t wavDurationMs(const WavInfo& info);
uint32_t wavSeekByteOffset(const WavInfo& info, uint32_t targetMs);

class WavDecoder : public IDecoder {
 public:
  bool open(const char* path) override;
  void close() override;
  AudioFormat format() const override;
  DecodeStatus decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) override;
  bool seekMs(uint32_t ms) override;
  uint32_t positionMs() const override;

 private:
  AudioFormat fmt_{};
  WavInfo info_{};
  uint32_t bytesRead_ = 0;
#ifndef UNIT_TEST
  void* file_ = nullptr;
#endif
};
