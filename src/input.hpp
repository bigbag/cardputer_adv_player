#pragma once
#include "actions.hpp"
#include "types.hpp"

class Input {
 public:
  void begin();
  // screen-aware: ;/. = Up/Down in Browse, Prev/Next track while Playing.
  Action poll(Screen screen);
};
