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
  NextTrack,  // play mode: next audio in folder
  PrevTrack,  // play mode: prev / restart current
  TogglePlayer,  // Browse ↔ Playing when a track is loaded
  RetrySd,
  Settings,  // open settings (Tab or 's')
};
