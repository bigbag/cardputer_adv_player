#pragma once

#include <cstdint>
#include <vector>

class Keyboard_Class {
 public:
  struct KeysState {
    bool tab = false;
    bool fn = false;
    bool shift = false;
    bool ctrl = false;
    bool opt = false;
    bool alt = false;
    bool del = false;
    bool enter = false;
    bool space = false;
    uint8_t modifiers = 0;
    std::vector<char> word;
    std::vector<uint8_t> hid_keys;
    std::vector<uint8_t> modifier_keys;
  };

  bool isChange() {
    const bool changed = count != lastCount_;
    lastCount_ = count;
    return changed;
  }
  uint8_t isPressed() const { return count; }
  KeysState& keysState() { return state; }

  KeysState state;
  uint8_t count = 0;

 private:
  uint8_t lastCount_ = 0;
};

struct NativeCardputer {
  Keyboard_Class Keyboard;
};

inline NativeCardputer M5Cardputer;
