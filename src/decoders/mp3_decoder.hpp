#pragma once

#include "decoder.hpp"
#include "decoder_input.hpp"
#include <cstdint>

class Mp3Decoder : public IDecoder {
 public:
  bool open(const char* path) override;
  void close() override;
  ~Mp3Decoder() { close(); }  // a dropped decoder must release its buffers
  AudioFormat format() const override;
  DecodeStatus decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) override;
  bool seekMs(uint32_t ms) override;
  uint32_t positionMs() const override;

#ifdef UNIT_TEST
  // Test seam: run the shared loop over caller-owned fixture bytes. The
  // decoder stops using the input at close(); the caller keeps ownership.
  bool openWithFile(DecoderInput& input);
#endif

 private:
  struct Impl;
  static void refillWindow(Impl& d);
  static bool frameStep(Impl& d, size_t& consumed, DecodeStatus& failStatus,
                        bool stagePcm, int* samplesOut);
  static DecodeStatus pump(Impl& d, int16_t* outStereo, size_t maxFrames,
                           size_t* gotFrames);
  bool openInput(DecoderInput& in);
  static void estimateDuration(Impl& d);
  static bool restoreToStart(Impl& d);
  Impl* impl_ = nullptr;
};
