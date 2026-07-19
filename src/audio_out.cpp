#include "audio_out.hpp"
#include "config.hpp"

#include <M5Cardputer.h>
#include <driver/i2s.h>
#include <cmath>
#include <cstring>

static constexpr i2s_port_t kI2sPort = I2S_NUM_0;

// M5Unified Cardputer-ADV ES8311 enable. DAC 0xBF = ±0 dB.
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

// Wide diapason: soft = (UI/100)^exp * 100
// UI 15 → ~2%, 30 → 9%, 50 → 25%, 70 → 49%, 100 → 100% (before boost)
int AudioOut::softScaleFromUi() const {
  int v = volume_;
  if (v <= 0) return 0;
  if (v > 100) v = 100;

  // Integer power: v^exp / 100^(exp-1)
  int32_t p = v;
  for (int e = 1; e < cfg::kVolCurveExpNum; ++e) {
    p = (p * v) / 100;
  }
  if (p < 1) p = 1;
  if (p > 100) p = 100;
  return static_cast<int>(p);
}

void AudioOut::applyVolume() {
  softScale_ = softScaleFromUi();
  esWrite(0x32, 0xBF);  // fixed 0 dB; range is all digital

  const int mul = softScale_ * cfg::kVolPcmBoost;
  Serial.printf("[audio] ui=%d%% soft=%d%% pcmMul=%d (boost×%d)\n", volume_,
                softScale_, mul, cfg::kVolPcmBoost);
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
  Wire.begin(cfg::kI2cSda, cfg::kI2cScl, 100000);
  delay(10);

  if (!esInitRegisters()) {
    Serial.println("[audio] ES8311 init failed");
    return false;
  }
  if (!i2sStart(cfg::kDefaultSampleRate)) return false;

  volume_ = cfg::kDefaultVolumePercent;
  applyVolume();
  ready_ = true;
  Serial.printf("[audio] ready wide-range vol (exp=%d boost×%d)\n",
                cfg::kVolCurveExpNum, cfg::kVolPcmBoost);
  return true;
}

void AudioOut::end() {
  i2sStop();
  ready_ = false;
}

bool AudioOut::setSampleRate(uint32_t hz) {
  if (!ready_) return false;
  if (hz == rate_) return true;
  i2sStop();
  if (!i2sStart(hz)) return false;
  applyVolume();
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

  // mul = softScale (0..100) * boost  → 0 .. 100*boost
  const int mul = softScale_ * cfg::kVolPcmBoost;

  while (written < frames) {
    size_t n = frames - written;
    if (n > kChunkFrames) n = kChunkFrames;

    const int16_t* src = stereoFrames + written * 2;
    if (mul <= 0) {
      std::memset(buf, 0, n * 4);
    } else if (mul == 100) {
      std::memcpy(buf, src, n * 4);
    } else {
      for (size_t i = 0; i < n * 2; ++i) {
        int32_t s = (static_cast<int32_t>(src[i]) * mul) / 100;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        buf[i] = static_cast<int16_t>(s);
      }
    }

    size_t bytesWritten = 0;
    esp_err_t err = i2s_write(kI2sPort, buf, n * 4, &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK || bytesWritten == 0) break;
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
