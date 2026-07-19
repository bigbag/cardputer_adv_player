#include "player.hpp"
#include "path_utils.hpp"
#include "config.hpp"
#include <cstring>
#include <cstdio>

bool Player::begin(AudioOut* out, SdBrowser* browser) {
  if (!out || !browser) return false;
  out_ = out;
  browser_ = browser;
  state_ = PlayState::Idle;
  volume_ = cfg::kDefaultVolumePercent;
  return true;
}

void Player::end() {
  stop();
  out_ = nullptr;
  browser_ = nullptr;
}

bool Player::openDecoder(const char* path) {
  closeDecoder();
  if (path::hasExtInsensitive(path, ".wav")) {
    if (!wavDec_.open(path)) return false;
    decoder_ = &wavDec_;
  } else if (path::hasExtInsensitive(path, ".mp3")) {
    if (!mp3Dec_.open(path)) return false;
    decoder_ = &mp3Dec_;
  } else {
    return false;
  }
  return true;
}

void Player::closeDecoder() {
  if (decoder_) {
    decoder_->close();
    decoder_ = nullptr;
  }
}

#ifndef UNIT_TEST

void Player::waitTaskGone() {
  // Audio task clears taskHandle_ then self-deletes.
  while (taskHandle_ != nullptr) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

bool Player::open(const char* absPath) {
  if (!out_ || !absPath || absPath[0] == '\0') return false;

  // Stop previous track on the audio task — do NOT open decoders on loopTask.
  stop();

  std::strncpy(currentPath_, absPath, cfg::kMaxPathLen - 1);
  currentPath_[cfg::kMaxPathLen - 1] = '\0';
  path::fileName(currentName_, cfg::kMaxNameLen, absPath);

  stopReq_.store(false);
  paused_.store(false);
  seekDeltaMs_.store(0);
  autoNextPending_.store(false);
  state_ = PlayState::Playing;  // "opening" treated as playing for UI

  // Generous stack: minimp3 frame decode + SD read + path copies.
  const uint32_t stackWords = cfg::kAudioTaskStack;  // bytes in config; FreeRTOS wants words
  BaseType_t ok = xTaskCreatePinnedToCore(
      audioTaskThunk, "audio",
      stackWords / sizeof(StackType_t),
      this, cfg::kAudioTaskPrio, &taskHandle_,
      0  // core 0 — leave core 1 for loop/UI
  );
  if (ok != pdPASS) {
    taskHandle_ = nullptr;
    state_ = PlayState::Error;
    snprintf(lastError_, sizeof(lastError_), "No task mem");
    return false;
  }
  return true;
}

void Player::stop() {
  if (taskHandle_ != nullptr) {
    stopReq_.store(true);
    waitTaskGone();
  }
  closeDecoder();
  autoNextPending_.store(false);
  if (state_ != PlayState::Error) {
    state_ = PlayState::Idle;
  }
}

void Player::audioTaskThunk(void* arg) {
  static_cast<Player*>(arg)->audioTaskMain();
}

void Player::audioTaskMain() {
  // Open decoder HERE (not on loopTask) — minimp3 needs stack headroom.
  if (!openDecoder(currentPath_)) {
    snprintf(lastError_, sizeof(lastError_), "Can't decode");
    state_ = PlayState::Error;
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  AudioFormat fmt = decoder_->format();
  if (fmt.sampleRate == 0) {
    snprintf(lastError_, sizeof(lastError_), "Bad format");
    state_ = PlayState::Error;
    closeDecoder();
    taskHandle_ = nullptr;
    vTaskDelete(nullptr);
    return;
  }

  out_->setSampleRate(fmt.sampleRate);
  out_->setVolumePercent(volume_);
  state_ = PlayState::Playing;

  constexpr size_t kBufFrames = 512;

  while (!stopReq_.load()) {
    if (paused_.load()) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    int32_t seekDelta = seekDeltaMs_.exchange(0);
    if (seekDelta != 0 && decoder_) {
      uint32_t cur = decoder_->positionMs();
      int64_t target = static_cast<int64_t>(cur) + seekDelta;
      if (target < 0) target = 0;
      decoder_->seekMs(static_cast<uint32_t>(target));
    }

    size_t got = 0;
    DecodeStatus st = decoder_->decode(pcmBuf_, kBufFrames, &got);

    if (got > 0) {
      out_->write(pcmBuf_, got);
    }

    if (st == DecodeStatus::Finished) {
      state_ = PlayState::Done;
      if (autoNextEnabled_) {
        autoNextPending_.store(true);
      }
      break;
    }
    if (st == DecodeStatus::Error) {
      snprintf(lastError_, sizeof(lastError_), "Decode error");
      state_ = PlayState::Error;
      break;
    }
    // NeedMore with no samples: yield briefly
    if (got == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  closeDecoder();
  taskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void Player::togglePause() {
  if (state_ == PlayState::Playing) {
    paused_.store(true);
    state_ = PlayState::Paused;
  } else if (state_ == PlayState::Paused) {
    paused_.store(false);
    state_ = PlayState::Playing;
  }
}

void Player::seekRelative(int deltaSeconds) {
  if (state_ == PlayState::Playing || state_ == PlayState::Paused) {
    seekDeltaMs_.fetch_add(deltaSeconds * 1000);
  }
}

#else

bool Player::open(const char*) { return false; }
void Player::stop() {}
void Player::togglePause() {}
void Player::seekRelative(int) {}

#endif

void Player::setVolumePercent(int p) {
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  volume_ = p;
  if (out_) out_->setVolumePercent(volume_);
}

void Player::adjustVolume(int deltaPercent) {
  setVolumePercent(volume_ + deltaPercent);
}

void Player::service() {
#ifndef UNIT_TEST
  if (!autoNextPending_.load()) return;
  autoNextPending_.store(false);
#else
  if (!autoNextPending_) return;
  autoNextPending_ = false;
#endif
  enqueueAutoNext();
}

bool Player::enqueueAutoNext() {
  if (!browser_) return false;
  char nextPath[cfg::kMaxPathLen];
  if (browser_->nextAudioAfter(currentName_, nextPath, sizeof(nextPath))) {
    return open(nextPath);
  }
  return false;
}

PlayerSnapshot Player::snapshot() const {
  PlayerSnapshot s{};
  s.state = state_;
  std::strncpy(s.fileName, currentName_, cfg::kMaxNameLen - 1);
  s.fileName[cfg::kMaxNameLen - 1] = '\0';
  s.volumePercent = volume_;
  if (decoder_) {
    s.positionMs = decoder_->positionMs();
    AudioFormat fmt = decoder_->format();
    s.durationMs = fmt.durationMs;
    s.sampleRate = fmt.sampleRate;
  }
  return s;
}

bool Player::takeError(char* buf, size_t cap) {
  if (lastError_[0] == '\0') return false;
  std::strncpy(buf, lastError_, cap - 1);
  buf[cap - 1] = '\0';
  lastError_[0] = '\0';
  return true;
}
