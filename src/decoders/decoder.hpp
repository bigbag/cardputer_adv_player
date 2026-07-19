#pragma once

#include <cstddef>
#include <cstdint>

struct AudioFormat {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 16;
  uint32_t durationMs = 0;
};

enum class DecodeStatus : uint8_t {
  Ok,
  NeedMore,
  Finished,
  Error
};

class IDecoder {
 public:
  virtual ~IDecoder() = default;
  virtual bool open(const char* path) = 0;
  virtual void close() = 0;
  virtual AudioFormat format() const = 0;
  virtual DecodeStatus decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) = 0;
  virtual bool seekMs(uint32_t ms) = 0;
  virtual uint32_t positionMs() const = 0;
};
