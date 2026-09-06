#pragma once

#include "decoder.hpp"
#include "decoder_input.hpp"
#include <cstdint>

class FlacDecoder : public IDecoder {
 public:
  bool open(const char* path) override;
  void close() override;
  ~FlacDecoder() override { close(); }
  AudioFormat format() const override;
  DecodeStatus decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) override;
  bool seekMs(uint32_t ms) override;
  uint32_t positionMs() const override;

#ifdef UNIT_TEST
  // Test seam: open over a caller-owned fixture. The decoder stops using the
  // input at close(); the caller keeps ownership.
  bool openWithFile(DecoderInput& input);
#endif

 private:
  struct Impl;
  bool openInput(DecoderInput& in);
  Impl* impl_ = nullptr;
};
