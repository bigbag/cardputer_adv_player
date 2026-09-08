#pragma once

#include "types.hpp"
#include "audio_out.hpp"
#include "sd_browser.hpp"
#include "decoders/decoder.hpp"
#include "decoders/wav_decoder.hpp"
#include "decoders/mp3_decoder.hpp"

#include <atomic>
#include <cstddef>

#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

class Player {
 public:
  bool begin(AudioOut* out, SdBrowser* browser);
  void end();
  bool open(const char* absPath, uint32_t startPositionMs = 0);  // startPositionMs resumes before the first output
  void stop();
  void togglePause();
  void setVolumePercent(int p);
  void adjustVolume(int deltaPercent);
  int volumePercent() const { return desiredVolume_.load(); }
  void seekRelative(int deltaSeconds);
  bool nextTrack();  // next audio file in the current folder; returns false at the end
  bool prevTrack();  // restarts the track after kPrevRestartMs; otherwise moves to the previous track
  const char* currentPath() const { return currentPath_; }
  void service();
  void setAutoNext(bool on) { autoNextEnabled_.store(on); }
  bool autoNext() const { return autoNextEnabled_.load(); }
  PlayerSnapshot snapshot() const;
  bool takeError(char* buf, size_t cap);

 private:
#ifndef UNIT_TEST
  static void audioTaskThunk(void* arg);
  void audioTaskMain();
  void finishAudioTask();
  void drainOutput();
  void waitTaskGone();
#endif
  bool openDecoder(const char* path);
  void closeDecoder();
  bool adjacentTrack(bool next, char* outPath, size_t outCap);
  bool enqueueAutoNext();
  uint32_t positionMs() const;
  void resetTrackPublication();
  void publishError(PlayerError e);

  AudioOut* out_ = nullptr;
  SdBrowser* browser_ = nullptr;
  IDecoder* decoder_ = nullptr;
  WavDecoder wavDec_;
  Mp3Decoder mp3Dec_;

  std::atomic<bool> audioTaskRunning_{false};  // resource-ownership barrier
  int16_t pcmBuf_[512 * 2]{};

  // The UI sets requests. The audio task consumes them.
  std::atomic<bool> stopReq_{false};
  std::atomic<bool> paused_{false};
  std::atomic<int32_t> seekDeltaMs_{0};
  std::atomic<uint32_t> startSeekMs_{0};  // UI writes it in open(); the audio task consumes it once
  std::atomic<bool> autoNextEnabled_{true};
  std::atomic<int> desiredVolume_{cfg::kDefaultVolumePercent};

  // The audio task publishes these values during playback.
  // The UI can reset them before task creation or after resource release.
  std::atomic<PlayState> state_{PlayState::Idle};
  std::atomic<uint32_t> publishedPositionMs_{0};
  std::atomic<uint32_t> publishedDurationMs_{0};
  std::atomic<uint32_t> publishedSampleRate_{0};
  std::atomic<bool> publishedDurationEstimated_{false};
  std::atomic<int> appliedVolume_{cfg::kDefaultVolumePercent};

  // The UI consumes completion only after the audio task releases resources.
  std::atomic<bool> autoNextPending_{false};

  // Either task can report an error. The UI consumes it.
  std::atomic<PlayerError> pendingError_{PlayerError::None};

  // UI-owned; replaced only while no audio task owns the decoder.
  char currentPath_[cfg::kMaxPathLen]{};
  char currentName_[cfg::kMaxNameLen]{};
};
