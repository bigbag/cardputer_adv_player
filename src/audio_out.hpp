#pragma once

#include <cstddef>
#include <cstdint>

class AudioOut {
 public:
  bool begin();
  void end();
  bool setSampleRate(uint32_t hz);
  void setVolumePercent(int percent);
  int volumePercent() const;
  size_t write(const int16_t* stereoFrames, size_t frames);
  void updateAmpFromHp();
  bool headphonesInserted() const;
  bool playTestBeep(uint32_t freqHz, uint32_t ms);

 private:
  bool esWrite(uint8_t reg, uint8_t val);
  bool esInitRegisters();
  bool i2sStart(uint32_t rate);
  void i2sStop();
  void applyVolume();
  // 0..100 UI → 0..100 effective after HP curve
  int effectiveVolumePercent() const;
  // ES8311 DAC reg 0x32 value for 0..100 percent
  static uint8_t esDacRegFromPercent(int percent);

  uint32_t rate_ = 0;
  int volume_ = 70;
  bool ready_ = false;
  bool hpInserted_ = false;
  int softScale_ = 100;  // residual software scale 0..100 after DAC set
};
