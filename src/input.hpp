#pragma once
#include "actions.hpp"
#include "types.hpp"

class Input {
 public:
  void begin();
  // screen-aware: '.' is Down in Browse, VolUp while Playing (Cardputer convention).
  Action poll(Screen screen);
};
