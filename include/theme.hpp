#pragma once

#include <cstdint>
#include <cstddef>

// RGB565 palettes for the 240×135 TFT. Classic CRT phosphors + a few
// high-contrast cyberdeck / DOS looks that stay readable when dimmed.
//
// Hex notes → RGB565 (rrrrrggggggbbbbb):
//   P1 green   ~ #4AFF00
//   P3 amber   ~ #FFB000 / #FFB700
//   Ice cyan   ~ #00E5FF on deep blue-black
//   VGA text   ~ #AAAAAA / #FFFFFF on #000000 (CGA light gray)
//   Matrix     ~ #00FF41 / #008F11 on black
//   Hot        ~ #FF2BD6 / #AA0088 on black
//   Blood      ~ #FF1A1A / #880000 on #0A0000
struct Theme {
  const char* name;
  uint16_t bg;
  uint16_t fg;
  uint16_t dim;
  uint16_t selectBg;
  uint16_t selectFg;
};

namespace themes {

// 0 Phosphor — P1 green on black (default)
// 1 Amber    — P3 amber CRT
// 2 Cyan     — ice/cyan cyberdeck
// 3 VGA      — DOS/CGA light gray
// 4 Matrix   — bright green trail
// 5 Hot      — magenta cyber
// 6 Blood    — crimson on near-black
constexpr size_t kCount = 7;

inline const Theme& get(size_t index) {
  static const Theme kAll[kCount] = {
      // Phosphor: bg #000000, fg #4AFF00, dim #1A8800
      {"Phosphor", 0x0000, 0x4FE0, 0x1440, 0x4FE0, 0x0000},
      // Amber: bg #000000, fg #FFB700, dim #A37000
      {"Amber", 0x0000, 0xFDA0, 0xA380, 0xFDA0, 0x0000},
      // Cyan: bg #001018, fg #00E5FF, dim #0088AA
      {"Cyan", 0x0083, 0x073F, 0x0455, 0x073F, 0x0083},
      // VGA: bg #000000, fg #AAAAAA, dim #555555, sel white on gray
      // #AAAAAA → 0xAD55, #555555 → 0x52AA, #FFFFFF → 0xFFFF
      {"VGA", 0x0000, 0xAD55, 0x52AA, 0xAD55, 0x0000},
      // Matrix: bg #000000, fg #00FF41, dim #008F11
      // #00FF41 → 0x07E8, #008F11 → 0x0462
      {"Matrix", 0x0000, 0x07E8, 0x0462, 0x07E8, 0x0000},
      // Hot: bg #000000, fg #FF2BD6, dim #AA0088
      {"Hot", 0x0000, 0xF95A, 0xA811, 0xF95A, 0x0000},
      // Blood: bg #0A0000, fg #FF1A1A, dim #880000
      {"Blood", 0x0800, 0xF8C3, 0x8800, 0xF8C3, 0x0800},
  };
  if (index >= kCount) index = 0;
  return kAll[index];
}

inline const char* name(size_t index) { return get(index).name; }

}  // namespace themes
