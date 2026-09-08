#pragma once
#include "actions.hpp"
#include "types.hpp"

class Input {
 public:
  void begin();
  // Key meaning depends on the screen: ; and . move the cursor in Browse and
  // change tracks while Playing.
  Action poll(Screen screen);
  bool locked() const { return locked_; }

 private:
  bool locked_ = false;
  bool waitForRelease_ = false;
};
