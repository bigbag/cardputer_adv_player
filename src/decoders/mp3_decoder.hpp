#pragma once

#include "decoder.hpp"
#include <cstdint>

class Mp3Decoder : public IDecoder {
 public:
  bool open(const char* path) override;
  void close() override;
  AudioFormat format() const override;
  DecodeStatus decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) override;
  bool seekMs(uint32_t ms) override;
  uint32_t positionMs() const override;

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};
