#pragma once

#include <cstdint>

class IdleTimeout {
 public:
  void reset(uint32_t nowMs) { lastActivityMs_ = nowMs; }

  bool expired(uint32_t nowMs, uint32_t timeoutMs, bool playing) {
    if (playing || wasPlaying_) reset(nowMs);
    wasPlaying_ = playing;
    return !playing && timeoutMs != 0 &&
           (nowMs - lastActivityMs_) >= timeoutMs;
  }

 private:
  uint32_t lastActivityMs_ = 0;
  bool wasPlaying_ = false;
};
