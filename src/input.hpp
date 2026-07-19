#pragma once
#include "types.hpp"

class Input {
 public:
  void begin();
  Action poll();
};
