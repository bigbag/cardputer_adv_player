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
constexpr int kI2sBclk = 41;
constexpr int kI2sLrck = 43;
constexpr int kI2sDout = 42;
constexpr int kHpDetectPin = 17;   // LOW = headphones inserted
constexpr int kAmpEnablePin = 46;  // HIGH = speaker amp on
constexpr uint8_t kEs8311Addr = 0x18;
constexpr uint32_t kEs8311I2cHz = 400000;

// microSD SPI
constexpr int kSdCs = 12;
constexpr int kSdMosi = 14;
constexpr int kSdSck = 40;
constexpr int kSdMiso = 39;
// Start conservative; many microSD cards fail mount at 15–25 MHz on this bus.
constexpr uint32_t kSdSpiHz = 4000000;

// UI
constexpr uint16_t kColorBg = 0x0000;      // black
constexpr uint16_t kColorFg = 0x07E0;      // green
constexpr uint16_t kColorDim = 0x03E0;     // dim green
constexpr uint16_t kColorSelectBg = 0x07E0;
constexpr uint16_t kColorSelectFg = 0x0000;
constexpr int kHintBarH = 12;
constexpr int kListRowH = 14;
constexpr int kMaxVisibleRows = 7;
constexpr uint8_t kDisplayBrightness = 128;
// Dim/off backlight after this many ms with no key activity (audio keeps playing).
constexpr uint32_t kDisplayTimeoutMs = 10000;

// Browser / player
constexpr size_t kMaxDirEntries = 256;
constexpr size_t kMaxPathLen = 256;
constexpr size_t kMaxNameLen = 64;
constexpr int kVolumeStepPercent = 5;
constexpr int kSeekStepSeconds = 5;
// UI default; HP path is further attenuated in AudioOut.
constexpr int kDefaultVolumePercent = 40;
constexpr uint32_t kToastMs = 1500;

// Audio task / buffers
constexpr uint32_t kDefaultSampleRate = 44100;
// Bytes for xTaskCreate stack (minimp3 + SD + path work on this task).
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
