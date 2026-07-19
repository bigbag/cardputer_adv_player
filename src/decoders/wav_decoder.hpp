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

WavInfo wavParseHeader(const uint8_t* data, size_t len);

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
