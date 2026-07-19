#pragma once

#include <cstdint>
#include <cstddef>

// RGB565 palettes. Values based on classic CRT phosphors + common
// Cardputer/cyberdeck aesthetics (Bruce-style high contrast on small TFT).
//
// Phosphor refs (approx hex → RGB565):
//   P1 green  ~ #4AFF00 / #33FF33  (retrocomputing.SE / SuperUser)
//   P3 amber  ~ #FFB000 / #FFB700
//   Ice cyan  ~ common cyberdeck / “cool retro terminal”
//   Paper     ~ outdoor-readable cream (original brainstorm option C)
struct Theme {
  const char* name;
  uint16_t bg;
  uint16_t fg;
  uint16_t dim;
  uint16_t selectBg;
  uint16_t selectFg;
};

namespace themes {

// 0 Phosphor — P1 green on black (default “terminal”)
// 1 Amber    — P3 amber CRT
// 2 Cyan     — ice/cyan cyberdeck on deep blue-black
// 3 Paper    — dark ink on cream (easier outdoors)
constexpr size_t kCount = 4;

inline const Theme& get(size_t index) {
  // RGB565 helpers were computed from the hex notes above.
  static const Theme kAll[kCount] = {
      // Phosphor: bg #000000, fg #4AFF00, dim #1A8800, sel fg on green
      {"Phosphor", 0x0000, 0x4FE0, 0x1440, 0x4FE0, 0x0000},
      // Amber: bg #000000, fg #FFB700, dim #A37000
      {"Amber", 0x0000, 0xFDA0, 0xA380, 0xFDA0, 0x0000},
      // Cyan: bg #001018, fg #00E5FF, dim #0088AA
      {"Cyan", 0x0083, 0x073F, 0x0455, 0x073F, 0x0083},
      // Paper: bg #F4F1DE, fg #3D405B, dim #8A8A7A, select warm coral
      {"Paper", 0xF79B, 0x3A0B, 0x8C4F, 0xE3CB, 0xF79B},
  };
  if (index >= kCount) index = 0;
  return kAll[index];
}

inline const char* name(size_t index) { return get(index).name; }

}  // namespace themes
