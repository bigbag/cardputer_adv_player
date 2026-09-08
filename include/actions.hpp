#pragma once
#include <cstdint>

enum class Action : uint8_t {
  None = 0,
  Up,
  Down,
  Enter,
  Back,
  Space,
  VolUp,
  VolDown,
  SeekFwd,
  SeekBack,
  NextTrack,  // Select the next audio file in the playing track's folder.
  PrevTrack,  // Select the previous track or restart the current track.
  TogglePlayer,  // Switch between Browse and Playing if the player has a track.
  RetrySd,
  Settings,  // Open Settings with Tab or S.
  System,
  ToggleLock,
};
