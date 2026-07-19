#pragma once

#include <cstddef>
#include <cstdint>

class AudioOut {
 public:
  bool begin();
  void end();
  bool setSampleRate(uint32_t hz);

  void setVolumePercent(int percent);
  int volumePercent() const { return volume_; }

  size_t write(const int16_t* stereoFrames, size_t frames);
  bool playTestBeep(uint32_t freqHz, uint32_t ms);

 private:
  bool esWrite(uint8_t reg, uint8_t val);
  bool esInitRegisters();
  bool i2sStart(uint32_t rate);
  void i2sStop();
  void applyVolume();
  // sample * mulNum_ / mulDen_  (high-res curve + boost)
  void recomputeMul();

  uint32_t rate_ = 0;
  int volume_ = 45;
  bool ready_ = false;
  int32_t mulNum_ = 0;
  int32_t mulDen_ = 1;
};
