#pragma once
#include "config.hpp"
#include <cstdint>

enum class Screen : uint8_t { Browse, Playing, Settings };
enum class EntryKind : uint8_t { Dir, Mp3, Wav };
enum class PlayState : uint8_t { Idle, Playing, Paused, Done, Error };
// Software output profile (jack mute is hardware; MCU cannot sense the plug).
enum class OutputRoute : uint8_t { Speaker = 0, Headphone = 1 };

struct DirEntry {
  char name[cfg::kMaxNameLen];
  EntryKind kind;
};

struct BrowseSnapshot {
  char path[cfg::kMaxPathLen];
  const DirEntry* entries;
  size_t count;
  size_t cursor;
  size_t scroll;
  bool truncated;
  bool sdOk;
};

struct PlayerSnapshot {
  PlayState state;
  char fileName[cfg::kMaxNameLen];
  uint32_t positionMs;
  uint32_t durationMs;
  int volumePercent;
  uint32_t sampleRate;
  OutputRoute route;
};

struct Toast {
  char text[48];
  uint32_t expiresAtMs;
};

struct SettingsSnapshot {
  int volumePercent;
  OutputRoute route;  // Spk vs HP gain profile (manual; no MCU jack sense)
  uint8_t brightness;
  uint32_t displayTimeoutMs;  // 0 = never
  bool autoNext;
  size_t themeIndex;
  size_t cursor;
};
