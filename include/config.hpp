#pragma once

#include <cstddef>
#include <cstdint>
#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#endif

namespace cfg {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;

// I2C
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 9;

// ES8311 / I2S (Cardputer-ADV)
// G41=BCLK, G43=LRCK, G42=DSDIN. Jack mutes speaker in hardware (no MCU detect).
constexpr int kI2sBclk = 41;
constexpr int kI2sLrck = 43;
constexpr int kI2sDout = 42;
constexpr uint8_t kEs8311Addr = 0x18;
constexpr uint32_t kEs8311I2cHz = 400000;

// microSD SPI
constexpr int kSdCs = 12;
constexpr int kSdMosi = 14;
constexpr int kSdSck = 40;
constexpr int kSdMiso = 39;
constexpr uint32_t kSdSpiHz = 4000000;

// UI layout
constexpr int kHintBarH = 12;
constexpr int kListRowH = 14;
constexpr int kMaxVisibleRows = 7;
constexpr uint8_t kDisplayBrightness = 128;
constexpr uint32_t kDisplayTimeoutMs = 10000;

// Browser / player
constexpr size_t kMaxDirEntries = 256;
constexpr size_t kMaxPathLen = 256;
constexpr size_t kMaxNameLen = 64;
constexpr int kVolumeStepPercent = 5;
constexpr int kSeekStepSeconds = 5;
// Start mid-low: ok-ish on jack; raise for speaker.
constexpr int kDefaultVolumePercent = 45;
constexpr uint32_t kToastMs = 1500;

// Single wide volume range (no Spk/HP split — no jack detect on MCU).
// soft% = (UI/100)^kVolCurveExp * 100, then PCM * kVolPcmBoost with clip.
// Low UI → very quiet (headphones). High UI → boosted (tiny speaker).
// Exp 2.0: 20→4%, 40→16%, 60→36%, 80→64%, 100→100% before boost.
constexpr int kVolCurveExpNum = 2;     // integer exponent
constexpr int kVolPcmBoost = 3;        // ×3 at UI 100% (speaker usable; turn down for jack)

// Audio task
constexpr uint32_t kDefaultSampleRate = 44100;
constexpr int kAudioTaskStack = 24576;
#ifndef UNIT_TEST
constexpr UBaseType_t kAudioTaskPrio = 5;
#else
constexpr unsigned kAudioTaskPrio = 5;
#endif
constexpr size_t kPcmRingFrames = 4096;
constexpr size_t kCompressedBuf = 4096;
constexpr bool kBootBeep = false;

}  // namespace cfg
