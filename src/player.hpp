#pragma once

#include "types.hpp"
#include "audio_out.hpp"
#include "sd_browser.hpp"
#include "decoders/decoder.hpp"
#include "decoders/wav_decoder.hpp"
#include "decoders/mp3_decoder.hpp"

#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#endif

class Player {
 public:
  bool begin(AudioOut* out, SdBrowser* browser);
  void end();
  bool open(const char* absPath);
  void stop();
  void togglePause();
  void setVolumePercent(int p);
  void adjustVolume(int deltaPercent);
  int volumePercent() const { return volume_; }
  void seekRelative(int deltaSeconds);
  bool nextTrack();  // next audio in current folder; false at end
  bool prevTrack();  // restart if >kPrevRestartMs, else previous track
  const char* currentPath() const { return currentPath_; }
  void service();
  void setAutoNext(bool on) { autoNextEnabled_ = on; }
  bool autoNext() const { return autoNextEnabled_; }
  PlayerSnapshot snapshot() const;
  bool takeError(char* buf, size_t cap);

 private:
#ifndef UNIT_TEST
  static void audioTaskThunk(void* arg);
  void audioTaskMain();
  void waitTaskGone();
#endif
  bool openDecoder(const char* path);
  void closeDecoder();
  bool adjacentTrack(bool next, char* outPath, size_t outCap);
  bool enqueueAutoNext();
  uint32_t positionMs() const;

  AudioOut* out_ = nullptr;
  SdBrowser* browser_ = nullptr;
  IDecoder* decoder_ = nullptr;
  WavDecoder wavDec_;
  Mp3Decoder mp3Dec_;

#ifndef UNIT_TEST
  TaskHandle_t taskHandle_ = nullptr;
  std::atomic<bool> stopReq_{false};
  std::atomic<bool> paused_{false};
  std::atomic<int32_t> seekDeltaMs_{0};
  std::atomic<bool> autoNextPending_{false};
  int16_t pcmBuf_[512 * 2]{};
#endif

  volatile PlayState state_ = PlayState::Idle;
#ifdef UNIT_TEST
  bool autoNextPending_ = false;
#endif
  char currentPath_[cfg::kMaxPathLen]{};
  char currentName_[cfg::kMaxNameLen]{};
  char lastError_[48]{};
  int volume_ = cfg::kDefaultVolumePercent;
  bool autoNextEnabled_ = true;
};
