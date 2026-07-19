#pragma once

#include "types.hpp"
#include <cstddef>
#include <cstdint>

class AudioOut {
 public:
  bool begin();
  void end();
  bool setSampleRate(uint32_t hz);

  // Active route volume (depends on setRoute).
  void setVolumePercent(int percent);
  int volumePercent() const;

  void setSpeakerVolume(int percent);
  void setHpVolume(int percent);
  int speakerVolume() const { return speakerVol_; }
  int hpVolume() const { return hpVol_; }

  // Software output profile — jack mute is hardware; we only switch gain.
  void setRoute(OutputRoute r);
  OutputRoute route() const { return route_; }
  void toggleRoute();

  size_t write(const int16_t* stereoFrames, size_t frames);
  bool playTestBeep(uint32_t freqHz, uint32_t ms);

 private:
  bool esWrite(uint8_t reg, uint8_t val);
  bool esInitRegisters();
  bool i2sStart(uint32_t rate);
  void i2sStop();
  void applyVolume();
  int effectiveVolumePercent() const;
  static uint8_t esDacRegFromPercent(int percent);
  int activeVolume() const {
    return route_ == OutputRoute::Headphone ? hpVol_ : speakerVol_;
  }

  uint32_t rate_ = 0;
  int speakerVol_ = 70;
  int hpVol_ = 40;
  OutputRoute route_ = OutputRoute::Speaker;
  bool ready_ = false;
  int softScale_ = 100;  // 0..100 amplitude after vol^2 curve
};
