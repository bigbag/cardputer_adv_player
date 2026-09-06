#pragma once

#include "config.hpp"
#include <algorithm>
#include <cstdint>

class BatteryReading {
 public:
  bool due(uint32_t nowMs) const {
    if (!started_) return true;
    return samples_ == 0 ? nowMs - batchStartMs_ >= cfg::kBatteryUpdateMs
                         : nowMs - lastSampleMs_ >= cfg::kBatterySampleMs;
  }

  void addSample(uint32_t nowMs, int millivolts) {
    if (samples_ == 0) batchStartMs_ = nowMs;
    started_ = true;
    lastSampleMs_ = nowMs;
    if (millivolts < 2000 || millivolts > 4500) {
      valid_ = false;
    } else {
      sum_ += millivolts;
    }
    if (++samples_ < cfg::kBatterySamples) return;
    millivolts_ = valid_ ? (sum_ + samples_ / 2) / samples_ : -1;
    sum_ = 0;
    samples_ = 0;
    valid_ = true;
  }

  int millivolts() const { return millivolts_; }

  int level() const {
    if (millivolts_ < 0) return -1;
    // Keep the M5Unified voltage scale. This is not a capacity measurement.
    return std::max(0, std::min(100, (millivolts_ - 3300) * 100 / 800));
  }

 private:
  uint32_t batchStartMs_ = 0;
  uint32_t lastSampleMs_ = 0;
  int sum_ = 0;
  int millivolts_ = -1;
  uint8_t samples_ = 0;
  bool started_ = false;
  bool valid_ = true;
};
