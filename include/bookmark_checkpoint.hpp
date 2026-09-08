#pragma once

#include "types.hpp"

class BookmarkCheckpoint {
 public:
  bool update(uint32_t nowMs, PlayState state, uint32_t positionMs, bool trackChanged) {
    const bool changed = trackChanged || positionMs != positionMs_;
    const bool urgent = trackChanged || positionMs < positionMs_ ||
                        (state == PlayState::Paused && (state != state_ || changed)) ||
                        ((state == PlayState::Done || state == PlayState::Error) && state != state_);
    dirty_ = dirty_ || changed;
    state_ = state;
    positionMs_ = positionMs;
    return dirty_ && (urgent || due(nowMs));
  }

  bool due(uint32_t nowMs) const {
    return dirty_ && nowMs - lastAttemptMs_ >= cfg::kBookmarkSaveIntervalMs;
  }

  void attempted(uint32_t nowMs, bool saved) {
    lastAttemptMs_ = nowMs;
    dirty_ = !saved;
  }

 private:
  PlayState state_ = PlayState::Idle;
  uint32_t positionMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
  bool dirty_ = false;
};
