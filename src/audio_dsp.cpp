#include "audio_dsp.hpp"
#include <algorithm>

namespace audio_dsp {

int16_t stereoToMono(int16_t left, int16_t right) {
  const int32_t sum = static_cast<int32_t>(left) + static_cast<int32_t>(right);
  return static_cast<int16_t>(sum / 2);
}

int32_t volumeToGainQ15(int percent) {
  const int64_t v = std::clamp(percent, 0, 100);
  return static_cast<int32_t>((v * v * v * v * 32768) / 100000000);
}

int16_t applyGainQ15(int16_t sample, int32_t gain) {
  return static_cast<int16_t>((static_cast<int32_t>(sample) * gain) / 32768);
}
}  // namespace audio_dsp
