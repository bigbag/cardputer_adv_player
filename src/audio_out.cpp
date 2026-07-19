#include "audio_out.hpp"
#include "config.hpp"

#include <M5Cardputer.h>
#include <driver/i2s.h>
#include <cmath>
#include <cstring>

static constexpr i2s_port_t kI2sPort = I2S_NUM_0;

// ES8311 bring-up. DAC volume (0x32) is set separately via applyVolume().
// 0xBF ≈ 0 dB (unity); we intentionally do not leave it maxed for HP path.
static const uint8_t kEs8311InitSeq[][2] = {
    {0x00, 0x80},  // reset
    {0x01, 0xB5},  // clock manager 1
    {0x02, 0x18},  // clock manager 2
    {0x0D, 0x01},  // system
    {0x12, 0x00},  // power
    {0x13, 0x10},  // DAC power-ish
    {0x37, 0x08},  // analog
};

bool AudioOut::esWrite(uint8_t reg, uint8_t val) {
  return M5.In_I2C.writeRegister(cfg::kEs8311Addr, reg, &val, 1,
                                 cfg::kEs8311I2cHz);
}

bool AudioOut::esInitRegisters() {
  for (auto& pair : kEs8311InitSeq) {
    if (!esWrite(pair[0], pair[1])) return false;
    delay(1);
  }
  return true;
}

// Map UI 0..100 onto ES8311 DAC digital volume register.
// Datasheet-style: 0xBF ≈ 0 dB; each step ~0.5 dB. Mute near 0.
// Matches common ESP-ADF mapping: reg = 0xBF - 2*(100-vol) for vol>0.
uint8_t AudioOut::esDacRegFromPercent(int percent) {
  if (percent <= 0) return 0x00;  // mute-ish
  if (percent > 100) percent = 100;
  int reg = 0xBF - 2 * (100 - percent);
  if (reg < 0) reg = 0;
  if (reg > 0xBF) reg = 0xBF;
  return static_cast<uint8_t>(reg);
}

int AudioOut::effectiveVolumePercent() const {
  // Headphones are much louder than the tiny speaker at the same digital level.
  // Cap HP path and compress the curve so "5–20%" is usable.
  int v = volume_;
  if (v < 0) v = 0;
  if (v > 100) v = 100;

  if (hpInserted_) {
    // Square-ish curve on a reduced ceiling (~45% of full scale max).
    // At UI 5%  → ~0.1% of full  (very quiet)
    // At UI 20% → ~1.8%
    // At UI 50% → ~11%
    // At UI 100%→ 45%
    const float t = v / 100.0f;
    const float shaped = t * t;  // more resolution at the bottom
    int eff = static_cast<int>(shaped * 45.0f + 0.5f);
    if (v > 0 && eff < 1) eff = 1;
    return eff;
  }

  // Speaker: gentler log-ish curve, full range available.
  // Keep some punch at high settings; soften bottom.
  if (v == 0) return 0;
  // Mix linear and squared: 0.35*lin + 0.65*quad
  const float t = v / 100.0f;
  const float shaped = 0.35f * t + 0.65f * (t * t);
  int eff = static_cast<int>(shaped * 100.0f + 0.5f);
  if (eff < 1) eff = 1;
  return eff;
}

void AudioOut::applyVolume() {
  const int eff = effectiveVolumePercent();

  // Primary attenuation: ES8311 DAC register (0..100 → 0x00..0xBF).
  const uint8_t dac = esDacRegFromPercent(eff);
  esWrite(0x32, dac);

  // Soft scale is a fine trim only (keep near 100 once DAC carries the load).
  // Extra -3 dB software pad on HP for safety (~0.707).
  if (hpInserted_) {
    softScale_ = 70;
  } else {
    softScale_ = 100;
  }

  Serial.printf("[audio] vol ui=%d%% eff=%d%% dac=0x%02X hp=%d soft=%d\n",
                volume_, eff, dac, hpInserted_ ? 1 : 0, softScale_);
}

bool AudioOut::i2sStart(uint32_t rate) {
  i2s_config_t i2sCfg = {};
  i2sCfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sCfg.sample_rate = (int)rate;
  i2sCfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sCfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sCfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sCfg.dma_buf_count = 6;
  i2sCfg.dma_buf_len = 256;
  i2sCfg.tx_desc_auto_clear = true;

  if (i2s_driver_install(kI2sPort, &i2sCfg, 0, nullptr) != ESP_OK)
    return false;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = cfg::kI2sBclk;
  pins.ws_io_num = cfg::kI2sLrck;
  pins.data_out_num = cfg::kI2sDout;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_set_pin(kI2sPort, &pins) != ESP_OK) {
    i2s_driver_uninstall(kI2sPort);
    return false;
  }

  rate_ = rate;
  return true;
}

void AudioOut::i2sStop() {
  if (rate_) {
    i2s_driver_uninstall(kI2sPort);
    rate_ = 0;
  }
}

bool AudioOut::begin() {
  pinMode(cfg::kHpDetectPin, INPUT_PULLUP);
  pinMode(cfg::kAmpEnablePin, OUTPUT);

  if (!esInitRegisters()) return false;
  if (!i2sStart(cfg::kDefaultSampleRate)) return false;

  hpInserted_ = headphonesInserted();
  updateAmpFromHp();
  volume_ = cfg::kDefaultVolumePercent;
  applyVolume();
  ready_ = true;
  return true;
}

void AudioOut::end() {
  i2sStop();
  digitalWrite(cfg::kAmpEnablePin, LOW);
  ready_ = false;
}

bool AudioOut::setSampleRate(uint32_t hz) {
  if (!ready_) return false;
  if (hz == rate_) return true;
  i2sStop();
  return i2sStart(hz);
}

void AudioOut::setVolumePercent(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  if (percent == volume_ && ready_) {
    // Still re-apply if HP state may matter — cheap.
  }
  volume_ = percent;
  if (ready_) applyVolume();
}

int AudioOut::volumePercent() const { return volume_; }

size_t AudioOut::write(const int16_t* stereoFrames, size_t frames) {
  if (!ready_ || frames == 0) return 0;

  constexpr size_t kChunkFrames = 128;
  int16_t buf[kChunkFrames * 2];
  size_t written = 0;
  const int scale = softScale_;

  while (written < frames) {
    size_t n = frames - written;
    if (n > kChunkFrames) n = kChunkFrames;

    const int16_t* src = stereoFrames + written * 2;
    if (scale >= 100) {
      // memcpy path
      std::memcpy(buf, src, n * 4);
    } else if (scale <= 0) {
      std::memset(buf, 0, n * 4);
    } else {
      for (size_t i = 0; i < n * 2; ++i) {
        buf[i] = static_cast<int16_t>((static_cast<int32_t>(src[i]) * scale) / 100);
      }
    }

    size_t bytesWritten = 0;
    esp_err_t err = i2s_write(kI2sPort, buf, n * 4, &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK || bytesWritten == 0) break;
    written += bytesWritten / 4;
  }

  return written;
}

void AudioOut::updateAmpFromHp() {
  const bool hp = headphonesInserted();
  digitalWrite(cfg::kAmpEnablePin, hp ? LOW : HIGH);
  if (hp != hpInserted_) {
    hpInserted_ = hp;
    Serial.printf("[audio] headphones %s\n", hp ? "in" : "out");
    if (ready_) applyVolume();
  }
}

bool AudioOut::headphonesInserted() const {
  return digitalRead(cfg::kHpDetectPin) == LOW;
}

bool AudioOut::playTestBeep(uint32_t freqHz, uint32_t ms) {
  if (!ready_) return false;

  uint32_t totalFrames = rate_ * ms / 1000;
  constexpr size_t kChunk = 128;
  int16_t buf[kChunk * 2];
  size_t pos = 0;

  while (pos < totalFrames) {
    size_t n = totalFrames - pos;
    if (n > kChunk) n = kChunk;

    for (size_t i = 0; i < n; ++i) {
      float t = (float)(pos + i) / (float)rate_;
      // Quiet test tone
      int16_t sample = (int16_t)(4000.0f * sinf(2.0f * M_PI * freqHz * t));
      buf[i * 2] = sample;
      buf[i * 2 + 1] = sample;
    }

    write(buf, n);
    pos += n;
  }

  return true;
}
