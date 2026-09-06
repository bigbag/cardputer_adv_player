#include "audio_out.hpp"
#include "audio_dsp.hpp"
#include "config.hpp"

#include <M5Cardputer.h>
#include <driver/i2s.h>
#include <cmath>
#include <cstring>

static constexpr i2s_port_t kI2sPort = I2S_NUM_0;

static const uint8_t kEs8311InitSeq[][2] = {
    {0x00, 0x80},
    {0x01, 0xB5},
    {0x02, 0x18},
    {0x0D, 0x01},
    {0x12, 0x00},
    {0x13, 0x10},
    {0x32, 0xBF},
    {0x37, 0x08},
};

bool AudioOut::esWrite(uint8_t reg, uint8_t val) {
  return M5.In_I2C.writeRegister(cfg::kEs8311Addr, reg, &val, 1,
                                 cfg::kEs8311I2cHz);
}

#if AUDIO_DIAG
bool AudioOut::esRead(uint8_t reg, uint8_t& value) {
  uint8_t v = 0;
  if (!M5.In_I2C.readRegister(cfg::kEs8311Addr, reg, &v, 1,
                              cfg::kEs8311I2cHz)) {
    return false;
  }
  value = v;
  return true;
}
#endif

bool AudioOut::esInitRegisters() {
  for (auto& pair : kEs8311InitSeq) {
    if (!esWrite(pair[0], pair[1])) {
      Serial.printf("[audio] ES8311 write fail reg 0x%02X\n", pair[0]);
      return false;
    }
    delay(2);
  }
  return true;
}

// Software volume changes PCM gain, not the codec's fixed DAC gain.
void AudioOut::applyVolume() {
  gainQ15_ = audio_dsp::volumeToGainQ15(volume_);
#if AUDIO_DIAG
  Serial.printf("[audio] ui=%d%% gain q15=%ld\n", volume_,
                static_cast<long>(gainQ15_));
#endif
}

bool AudioOut::i2sStart(uint32_t rate) {
  i2s_config_t i2sCfg = {};
  i2sCfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  i2sCfg.sample_rate = (int)rate;
  i2sCfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  i2sCfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  i2sCfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  i2sCfg.dma_buf_count = 8;
  i2sCfg.dma_buf_len = 256;
  i2sCfg.tx_desc_auto_clear = true;
  i2sCfg.use_apll = false;

  if (i2s_driver_install(kI2sPort, &i2sCfg, 0, nullptr) != ESP_OK) {
    Serial.println("[audio] i2s_driver_install failed");
    return false;
  }

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = cfg::kI2sBclk;
  pins.ws_io_num = cfg::kI2sLrck;
  pins.data_out_num = cfg::kI2sDout;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  if (i2s_set_pin(kI2sPort, &pins) != ESP_OK) {
    Serial.println("[audio] i2s_set_pin failed");
    i2s_driver_uninstall(kI2sPort);
    return false;
  }
  installed_ = true;
  rate_ = rate;
#if AUDIO_DIAG
  Serial.printf("[audio] i2s rate req=%lu cfg=%lu\n",
                static_cast<unsigned long>(rate),
                static_cast<unsigned long>(i2s_get_clk(kI2sPort)));
#endif
  return true;
}

void AudioOut::i2sStop() {
  if (installed_) {
    i2s_driver_uninstall(kI2sPort);
    installed_ = false;
  }
  rate_ = 0;
}

bool AudioOut::begin() {
  Wire.begin(cfg::kI2cSda, cfg::kI2cScl, 100000);
  delay(10);

  if (!esInitRegisters()) {
    Serial.println("[audio] ES8311 init failed");
    return false;
  }
#if AUDIO_DIAG
  // Report failed register reads as unavailable, not as zero.
  for (uint8_t r : {0x00, 0x01, 0x02, 0x09, 0x12, 0x13, 0x32, 0x37}) {
    uint8_t v = 0;
    if (esRead(r, v)) {
      Serial.printf("[audio] es8311 reg %02X = %02X\n", r, v);
    } else {
      Serial.printf("[audio] es8311 reg %02X unavailable\n", r);
    }
  }
#endif
  if (!i2sStart(cfg::kDefaultSampleRate)) return false;

  volume_ = cfg::kDefaultVolumePercent;
  applyVolume();
  ready_ = true;
  Serial.printf("[audio] ready q15 gain=%ld\n", static_cast<long>(gainQ15_));
  return true;
}

void AudioOut::end() {
  if (!esWrite(0x32, 0x00)) {
    Serial.println("[audio] DAC mute failed");
  }
  i2sStop();
  ready_ = false;
}

bool AudioOut::setSampleRate(uint32_t hz) {
  if (!ready_) return false;
  if (hz == 0) return false;
  // Fast path only when the driver is actually installed.
  if (hz == rate_ && installed_) return true;

  // Clock change only: no gain recalculation and no codec register writes.
  const esp_err_t err =
      i2s_set_clk(kI2sPort, hz, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
  if (err == ESP_OK) {
    rate_ = hz;
#if AUDIO_DIAG
    // i2s_get_clk reports the configured clock, not a measurement.
    Serial.printf("[audio] rate req=%lu cfg=%lu\n",
                  static_cast<unsigned long>(hz),
                  static_cast<unsigned long>(i2s_get_clk(kI2sPort)));
#endif
    return true;
  }

  // One uninstall/install cycle restores a complete driver at the new rate.
  Serial.printf("[audio] i2s_set_clk err=%d; reinstall at %lu\n",
                static_cast<int>(err), static_cast<unsigned long>(hz));
  i2sStop();
  if (!i2sStart(hz)) {
    // Never leave ready_ true without a driver.
    ready_ = false;
    Serial.println("[audio] output unavailable");
    return false;
  }
  return true;
}

bool AudioOut::resetStream() {
  if (!ready_) return false;
  // ponytail: reinstall the driver on each seek to clear its queue and DMA.
  // This uses public IDF calls but requires an allocation cycle.
  // Use a measured public-API reset if seek latency becomes a problem.
  const uint32_t rate = rate_;
  i2sStop();
  if (!i2sStart(rate)) {
    ready_ = false;
    Serial.println("[audio] output unavailable");
    return false;
  }
  return true;
}

void AudioOut::setVolumePercent(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  volume_ = percent;
  if (ready_) applyVolume();
}

size_t AudioOut::write(const int16_t* stereoFrames, size_t frames) {
  if (!ready_ || frames == 0) return 0;

  constexpr size_t kChunkFrames = 128;
  int16_t buf[kChunkFrames * 2];
  size_t written = 0;
  const int32_t gain = gainQ15_;

  while (written < frames) {
    size_t n = frames - written;
    if (n > kChunkFrames) n = kChunkFrames;

    const int16_t* src = stereoFrames + written * 2;
    if (gain <= 0) {
      std::memset(buf, 0, n * 4);
    } else {
      // The mono DAC needs a downmix even at unity gain.
      for (size_t i = 0; i < n; ++i) {
        const int16_t mono = audio_dsp::stereoToMono(src[i * 2], src[i * 2 + 1]);
        const int16_t s = audio_dsp::applyGainQ15(mono, gain);
        buf[i * 2] = s;
        buf[i * 2 + 1] = s;
      }
    }

    size_t bytesWritten = 0;
    // Finite wait per 128-frame chunk bounds the audio task's output wait.
    esp_err_t err = i2s_write(kI2sPort, buf, n * 4, &bytesWritten,
                              pdMS_TO_TICKS(100));
    if (err != ESP_OK || bytesWritten < n * 4) {
#if AUDIO_DIAG
      Serial.printf("[audio] i2s_write err=%d req=%u got=%u\n", static_cast<int>(err),
                    static_cast<unsigned>(n * 4), static_cast<unsigned>(bytesWritten));
#endif
      break;
    }
    written += bytesWritten / 4;
  }

  return written;
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
      int16_t sample = (int16_t)(5000.0f * sinf(2.0f * M_PI * freqHz * t));
      buf[i * 2] = sample;
      buf[i * 2 + 1] = sample;
    }

    write(buf, n);
    pos += n;
  }

  return true;
}
