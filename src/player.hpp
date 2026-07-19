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
  void seekRelative(int deltaSeconds);
  void service();
  PlayerSnapshot snapshot() const;
  bool takeError(char* buf, size_t cap);

 private:
#ifndef UNIT_TEST
  static void audioTaskThunk(void* arg);
  void audioTaskMain();
#endif
  bool openDecoder(const char* path);
  void closeDecoder();
  bool enqueueAutoNext();

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
#endif

  volatile PlayState state_ = PlayState::Idle;
#ifdef UNIT_TEST
  bool autoNextPending_ = false;
#endif
  char currentPath_[cfg::kMaxPathLen]{};
  char currentName_[cfg::kMaxNameLen]{};
  char lastError_[48]{};
  int volume_ = cfg::kDefaultVolumePercent;
};
