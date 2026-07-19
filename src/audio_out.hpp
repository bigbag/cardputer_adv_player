#pragma once

#include <cstddef>
#include <cstdint>

class AudioOut {
 public:
  bool begin();
  void end();
  bool setSampleRate(uint32_t hz);

  // Volume for the currently active route (speaker or HP).
  void setVolumePercent(int percent);
  int volumePercent() const;

  // Per-route volumes (persisted by Settings/App).
  void setSpeakerVolume(int percent);
  void setHpVolume(int percent);
  int speakerVolume() const { return speakerVol_; }
  int hpVolume() const { return hpVol_; }

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
  int effectiveVolumePercent() const;
  static uint8_t esDacRegFromPercent(int percent);
  int activeVolume() const { return hpInserted_ ? hpVol_ : speakerVol_; }

  uint32_t rate_ = 0;
  int speakerVol_ = 70;
  int hpVol_ = 40;
  bool ready_ = false;
  bool hpInserted_ = false;
  int softScale_ = 100;  // 0..100 amplitude after curve
};
