#include "audio_out.hpp"
#include "config.hpp"

#include <M5Cardputer.h>
#include <driver/i2s.h>
#include <cmath>

static constexpr i2s_port_t kI2sPort = I2S_NUM_0;

static const uint8_t kEs8311InitSeq[][2] = {
    {0x00, 0x80},  // reset
    {0x01, 0xB5},  // clock manager 1
    {0x02, 0x18},  // clock manager 2
    {0x0D, 0x01},  // ADC/DAC config
    {0x12, 0x00},  // system control
    {0x13, 0x10},  // system power
    {0x32, 0xBF},  // DAC volume
    {0x37, 0x08},  // analog config
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

  updateAmpFromHp();
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
  volume_ = percent;
}

int AudioOut::volumePercent() const { return volume_; }

size_t AudioOut::write(const int16_t* stereoFrames, size_t frames) {
  if (!ready_ || frames == 0) return 0;

  // Software volume scaling into a stack buffer, write in chunks
  constexpr size_t kChunkFrames = 128;
  int16_t buf[kChunkFrames * 2];
  size_t written = 0;

  while (written < frames) {
    size_t n = frames - written;
    if (n > kChunkFrames) n = kChunkFrames;

    const int16_t* src = stereoFrames + written * 2;
    for (size_t i = 0; i < n * 2; ++i) {
      buf[i] = (int16_t)((int32_t)src[i] * volume_ / 100);
    }

    size_t bytesWritten = 0;
    esp_err_t err = i2s_write(kI2sPort, buf, n * 4, &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK || bytesWritten == 0) break;
    written += bytesWritten / 4;
  }

  return written;
}

void AudioOut::updateAmpFromHp() {
  bool hp = headphonesInserted();
  digitalWrite(cfg::kAmpEnablePin, hp ? LOW : HIGH);
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
      int16_t sample = (int16_t)(16000.0f * sinf(2.0f * M_PI * freqHz * t));
      buf[i * 2] = sample;
      buf[i * 2 + 1] = sample;
    }

    write(buf, n);
    pos += n;
  }

  return true;
}
