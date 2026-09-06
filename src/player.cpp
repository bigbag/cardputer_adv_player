#include "player.hpp"
#include "path_utils.hpp"
#include "config.hpp"
#include <cstring>
#include <cstdio>

bool Player::begin(AudioOut* out, SdBrowser* browser) {
  if (!out || !browser) return false;
  out_ = out;
  browser_ = browser;
  state_.store(PlayState::Idle);
  desiredVolume_.store(cfg::kDefaultVolumePercent);
  appliedVolume_.store(cfg::kDefaultVolumePercent);
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
  } else if (path::hasExtInsensitive(path, ".flac")) {
    if (!flacDec_.open(path)) return false;
    decoder_ = &flacDec_;
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

void Player::publishError(PlayerError e) {
  pendingError_.store(e);
}

void Player::resetTrackPublication() {
  publishedPositionMs_.store(0);
  publishedDurationMs_.store(0);
  publishedSampleRate_.store(0);
  publishedDurationEstimated_.store(false);
}

#ifndef UNIT_TEST

#if AUDIO_DIAG
#include <Arduino.h>

static const char* audioDiagCodec(const char* path) {
  if (path::hasExtInsensitive(path, ".mp3")) return "MP3";
  if (path::hasExtInsensitive(path, ".flac")) return "FLAC";
  return "WAV";
}

static void audioDiagHeapStack() {
  Serial.printf("[audio] heap free=%u min=%u maxAlloc=%u stkHWM=%u\n",
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(ESP.getMinFreeHeap()),
                static_cast<unsigned>(ESP.getMaxAllocHeap()),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

static void audioDiagOpen(const char* path, const AudioFormat& fmt) {
  Serial.printf("[audio] open codec=%s path=%s ch=%u srcCh=%u rate=%lu dur=%lums\n",
                audioDiagCodec(path), path, fmt.channels, fmt.sourceChannels,
                static_cast<unsigned long>(fmt.sampleRate),
                static_cast<unsigned long>(fmt.durationMs));
  audioDiagHeapStack();
}

// fmt is null when the decoder fails to open.
static void audioDiagExit(const char* path, const AudioFormat* fmt,
                          uint32_t maxDecodeUs, const char* why) {
  if (fmt) {
    Serial.printf(
        "[audio] exit why=%s codec=%s ch=%u srcCh=%u rate=%lu dur=%lums maxDecode=%luus\n",
        why, audioDiagCodec(path), fmt->channels, fmt->sourceChannels,
        static_cast<unsigned long>(fmt->sampleRate),
        static_cast<unsigned long>(fmt->durationMs),
        static_cast<unsigned long>(maxDecodeUs));
  } else {
    Serial.printf("[audio] exit why=%s path=%s maxDecode=0us\n", why, path);
  }
  audioDiagHeapStack();
}
#endif

// Wait for the audio task to release AudioOut, decoder resources, and SD.
// Do not add a timeout escape or force-delete the task.
// Finite I2S writes, decoder close, and EOF drain bound resource cleanup.
// The flag confirms resource release, not stack reclamation.
// The FreeRTOS idle task reclaims the stack later.
void Player::waitTaskGone() {
  while (audioTaskRunning_.load(std::memory_order_acquire)) {
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

bool Player::open(const char* absPath) {
  if (!out_ || !absPath || absPath[0] == '\0' ||
      std::strlen(absPath) >= sizeof(currentPath_)) return false;

  // Stop the previous track on the audio task. Do not open decoders on loopTask.
  stop();
  // The previous task released ownership. Names are UI-owned and safe to replace.
  std::strncpy(currentPath_, absPath, cfg::kMaxPathLen - 1);
  currentPath_[cfg::kMaxPathLen - 1] = '\0';
  path::fileName(currentName_, cfg::kMaxNameLen, absPath);

  // Clear the prior auto-next event and all requests for the new track.
  stopReq_.store(false);
  paused_.store(false);
  seekDeltaMs_.store(0);
  autoNextPending_.store(false);
  resetTrackPublication();  // a new name must not display the old track's duration
  state_.store(PlayState::Playing);  // The UI shows playback while the decoder opens.

  // Mark ownership before creation. The stack size is passed in bytes.
  audioTaskRunning_.store(true, std::memory_order_release);
  const BaseType_t ok = xTaskCreatePinnedToCore(
      audioTaskThunk, "audio", cfg::kAudioTaskStack,
      this, cfg::kAudioTaskPrio, nullptr,
      0  // Reserve core 1 for the application loop.
  );
  if (ok != pdPASS) {
    audioTaskRunning_.store(false, std::memory_order_release);
    state_.store(PlayState::Error);
    publishError(PlayerError::TaskCreateError);
    return false;
  }
  return true;
}

void Player::stop() {
  if (audioTaskRunning_.load(std::memory_order_acquire)) {
    stopReq_.store(true);
  }
  // Always wait for the resource barrier. After it the task owns nothing,
  // so no shutdown path can close SD or AudioOut under a live task.
  waitTaskGone();
  // Requests and the auto-next event reset only after the barrier.
  autoNextPending_.store(false);
  stopReq_.store(false);
  paused_.store(false);
  seekDeltaMs_.store(0);
  if (state_.load() != PlayState::Error) {
    state_.store(PlayState::Idle);
  }
}

void Player::audioTaskThunk(void* arg) {
  static_cast<Player*>(arg)->audioTaskMain();
}

// Natural EOF: the DMA queue still holds the track's tail. Push enough
// silence through the finite write path to advance the full DMA capacity
// (8 x 256 frames), so a rate-changing auto-next cannot truncate the tail.
// A failed drain propagates as AudioOutputError.
void Player::drainOutput() {
  constexpr size_t kDmaFrames = 8 * 256;  // dma_buf_count x dma_buf_len
  constexpr size_t kChunkFrames = 256;
  int16_t silence[kChunkFrames * 2] = {};
  size_t drained = 0;
  while (drained < kDmaFrames) {
    if (out_->write(silence, kChunkFrames) < kChunkFrames) {
      // A failed EOF drain is an error exit: no auto-next may follow.
      autoNextPending_.store(false);
      state_.store(PlayState::Error);
      publishError(PlayerError::AudioOutputError);
      return;
    }
    drained += kChunkFrames;
  }
}

// Single cleanup path for every audio-task exit, on core 0. Order: decoder
// resources, output handling, then the ownership release. After the release
// store the task touches nothing; the release pairs with stop()'s wait.
void Player::finishAudioTask() {
  closeDecoder();
  if (state_.load() == PlayState::Done) {
    // Natural EOF: let the queued tail play out before any rate-changing
    // auto-next reconfigures the hardware.
    drainOutput();
  } else {
    // Explicit stop, manual replacement, and errors discard stale PCM.
    out_->resetStream();
  }
  audioTaskRunning_.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}

void Player::audioTaskMain() {
  // Open and run decoders on the audio task.
  if (!openDecoder(currentPath_)) {
    state_.store(PlayState::Error);
    publishError(PlayerError::CannotDecode);
#if AUDIO_DIAG
    audioDiagExit(currentPath_, nullptr, 0, "open-failed");
#endif
    finishAudioTask();
    return;
  }

  // The format stays fixed within a track. Publish it after a successful open.
  const AudioFormat fmt = decoder_->format();
  if (fmt.sampleRate == 0) {
    state_.store(PlayState::Error);
    publishError(PlayerError::BadFormat);
#if AUDIO_DIAG
    audioDiagExit(currentPath_, &fmt, 0, "bad-format");
#endif
    finishAudioTask();
    return;
  }
  publishedSampleRate_.store(fmt.sampleRate);
  publishedDurationMs_.store(fmt.durationMs);
  publishedDurationEstimated_.store(fmt.durationEstimated);

  // All AudioOut control, rate, reset, and write calls stay on this task.
  if (!out_->setSampleRate(fmt.sampleRate)) {
    state_.store(PlayState::Error);
    publishError(PlayerError::AudioOutputError);
#if AUDIO_DIAG
    audioDiagExit(currentPath_, &fmt, 0, "rate-failed");
#endif
    finishAudioTask();
    return;
  }

  int lastAppliedVolume = desiredVolume_.load();
  out_->setVolumePercent(lastAppliedVolume);
  appliedVolume_.store(lastAppliedVolume);

#if AUDIO_DIAG
  audioDiagOpen(currentPath_, fmt);
  uint32_t maxDecodeUs = 0;
#endif

  // Keep UI pause requests that arrive while the decoder opens.
  if (!paused_.load()) {
    state_.store(PlayState::Playing);
  }

  constexpr size_t kBufFrames = 512;

  while (!stopReq_.load(std::memory_order_acquire)) {
    // Apply volume and seek requests before the paused wait.
    const int requested = desiredVolume_.load();
    if (requested != lastAppliedVolume) {
      out_->setVolumePercent(requested);
      lastAppliedVolume = requested;
      appliedVolume_.store(requested);
    }

    int32_t seekDelta = seekDeltaMs_.exchange(0);
    if (seekDelta != 0 && decoder_) {
      uint32_t cur = decoder_->positionMs();
      int64_t target = static_cast<int64_t>(cur) + seekDelta;
      if (target < 0) target = 0;
      // Reset before moving the decoder so old DMA cannot play through a
      // slow seek. No task PCM is produced during this sequence. A failed
      // reset terminates with AudioOutputError.
      if (!out_->resetStream()) {
        state_.store(PlayState::Error);
        publishError(PlayerError::AudioOutputError);
        break;
      }
#if AUDIO_DIAG
      const uint32_t seekStartUs = micros();
#endif
      const bool seekOk = decoder_->seekMs(static_cast<uint32_t>(target));
#if AUDIO_DIAG
      const uint32_t seekUs = micros() - seekStartUs;
      Serial.printf("[audio] seek old=%lu req=%llu res=%lu ok=%d us=%lu\n",
                    static_cast<unsigned long>(cur),
                    static_cast<unsigned long long>(target),
                    static_cast<unsigned long>(decoder_->positionMs()),
                    seekOk, static_cast<unsigned long>(seekUs));
      audioDiagHeapStack();
#endif
      if (!seekOk) {
        state_.store(PlayState::Error);
        publishError(PlayerError::SeekError);
        break;
      }
      // Publish the position after a successful seek. A paused seek stays
      // paused; the wait below follows immediately.
      publishedPositionMs_.store(decoder_->positionMs());
    }

    if (paused_.load()) {
      // Core-0-only state writes while running; the loop publishes Paused
      // for the pause request and Playing again when production resumes.
      state_.store(PlayState::Paused);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    state_.store(PlayState::Playing);

    size_t got = 0;
#if AUDIO_DIAG
    uint32_t decodeStartUs = micros();
#endif
    DecodeStatus st = decoder_->decode(pcmBuf_, kBufFrames, &got);
#if AUDIO_DIAG
    // Unsigned subtraction stays correct across the micros() rollover.
    uint32_t decodeUs = micros() - decodeStartUs;
    if (decodeUs > maxDecodeUs) maxDecodeUs = decodeUs;
#endif

    if (got > 0) {
      const size_t writtenFrames = out_->write(pcmBuf_, got);
      if (writtenFrames < got) {
        // A short write is an output failure, not permission to drop PCM
        // and decode the next block.
        state_.store(PlayState::Error);
        publishError(PlayerError::AudioOutputError);
        break;
      }
      publishedPositionMs_.store(decoder_->positionMs());
    }

    if (st == DecodeStatus::Finished) {
      state_.store(PlayState::Done);
      if (autoNextEnabled_.load()) {
        autoNextPending_.store(true);
      }
      break;
    }
    if (st == DecodeStatus::Error) {
      state_.store(PlayState::Error);
      publishError(PlayerError::DecodeError);
      break;
    }
    if (got == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }

  // The final published position and duration survive the decoder close.
#if AUDIO_DIAG
  // Single exit line for every loop exit. The early returns above log
  // their own exit lines with maxDecodeUs = 0.
  const char* why = "stopped";
  if (state_.load() == PlayState::Done) why = "finished";
  if (state_.load() == PlayState::Error) why = "decode-error";
  audioDiagExit(currentPath_, &fmt, maxDecodeUs, why);
#endif
  finishAudioTask();
}

void Player::togglePause() {
  // The UI changes only the pause request during playback.
  // The audio task owns Playing/Paused updates to prevent terminal-state races.
  // Ignore requests during EOF drain, error cleanup, or after resource release.
  // Two rapid presses cancel each other.
  // The audio loop preserves requests that arrive while the decoder opens.
  if (!audioTaskRunning_.load(std::memory_order_acquire)) return;
  const PlayState st = state_.load();
  if (st == PlayState::Done || st == PlayState::Error) return;
  paused_.store(!paused_.load());
}

void Player::seekRelative(int deltaSeconds) {
  if (!audioTaskRunning_.load(std::memory_order_acquire)) return;
  seekDeltaMs_.fetch_add(static_cast<int32_t>(deltaSeconds) * 1000);
}

#else

bool Player::open(const char*) { return false; }
void Player::stop() {}
void Player::togglePause() {}
void Player::seekRelative(int) {}

#endif

void Player::setVolumePercent(int p) {
  // The audio task applies the requested volume.
  // Settings read the new value immediately through volumePercent().
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  desiredVolume_.store(p);
}

void Player::adjustVolume(int deltaPercent) {
  setVolumePercent(desiredVolume_.load() + deltaPercent);
}

void Player::service() {
  // Consume auto-next once, only after the previous task released its
  // resources. A pending event from a finished track never races a new one.
  if (audioTaskRunning_.load(std::memory_order_acquire)) return;
  if (!autoNextPending_.exchange(false)) return;
  enqueueAutoNext();
}

bool Player::enqueueAutoNext() {
  return nextTrack();
}

uint32_t Player::positionMs() const {
  return publishedPositionMs_.load();
}

bool Player::adjacentTrack(bool next, char* outPath, size_t outCap) {
  if (!browser_ || currentPath_[0] != '/' || !outPath || outCap == 0) return false;
  const BrowserLocation visibleLocation = browser_->location();
  bool found = false;
  if (browser_->revealPath(currentPath_)) {
    found = next ? browser_->nextAudioAfter(currentName_, outPath, outCap)
                 : browser_->prevAudioBefore(currentName_, outPath, outCap);
  }
  browser_->restoreLocationPreservingHistory(visibleLocation);
  return found;
}

bool Player::nextTrack() {
  char nextPath[cfg::kMaxPathLen];
  if (adjacentTrack(true, nextPath, sizeof(nextPath))) {
    return open(nextPath);
  }
  return false;
}

bool Player::prevTrack() {
  // Restart after kPrevRestartMs. Otherwise, select the previous track.
  if (positionMs() > cfg::kPrevRestartMs) {
    if (currentPath_[0] != '\0') {
      return open(currentPath_);
    }
    return false;
  }
  char prevPath[cfg::kMaxPathLen];
  if (adjacentTrack(false, prevPath, sizeof(prevPath))) {
    return open(prevPath);
  }
  // Restart the current track if no previous track exists.
  if (currentPath_[0] != '\0') {
    return open(currentPath_);
  }
  return false;
}

PlayerSnapshot Player::snapshot() const {
  PlayerSnapshot s{};
  s.state = state_.load();
  std::strncpy(s.fileName, currentName_, cfg::kMaxNameLen - 1);
  s.fileName[cfg::kMaxNameLen - 1] = '\0';
  s.volumePercent = desiredVolume_.load();
  s.positionMs = publishedPositionMs_.load();
  s.durationMs = publishedDurationMs_.load();
  s.sampleRate = publishedSampleRate_.load();
  s.durationEstimated = publishedDurationEstimated_.load();
  return s;
}

bool Player::takeError(char* buf, size_t cap) {
  if (buf == nullptr || cap == 0) return false;
  const PlayerError e = pendingError_.exchange(PlayerError::None);
  if (e == PlayerError::None) return false;
  const char* msg = "Error";
  switch (e) {
    case PlayerError::CannotDecode: msg = "Can't decode"; break;
    case PlayerError::BadFormat: msg = "Bad format"; break;
    case PlayerError::DecodeError: msg = "Decode error"; break;
    case PlayerError::AudioOutputError: msg = "Audio output error"; break;
    case PlayerError::TaskCreateError: msg = "No task mem"; break;
    case PlayerError::SeekError: msg = "Seek failed"; break;
    case PlayerError::None: break;
  }
  std::snprintf(buf, cap, "%s", msg);
  return true;
}
