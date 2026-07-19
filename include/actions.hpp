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
  RetrySd,
  Settings,  // open settings (Tab or 's')
};
