#pragma once

#include <cstdint>

namespace audio_dsp {

// Mean of the two channels with truncation toward zero.
int16_t stereoToMono(int16_t left, int16_t right);
// Map the fourth-power percent curve to Q15 in [0, 32768].
// Clamp percent to [0, 100].
// Q15 precision rounds settings 1-7 to zero.
int32_t volumeToGainQ15(int percent);

// sample * gain / 32768 with truncation toward zero.
// gain must be in [0, 32768].
int16_t applyGainQ15(int16_t sample, int32_t gain);

}  // namespace audio_dsp
