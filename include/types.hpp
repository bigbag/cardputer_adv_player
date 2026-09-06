#pragma once
#include "config.hpp"
#include <cstdint>

enum class Screen : uint8_t { Browse, Playing, Settings, System };
enum class EntryKind : uint8_t { Dir, Mp3, Wav };
enum class PlayState : uint8_t { Idle, Playing, Paused, Done, Error };
enum class OnBootMode : uint8_t { Play, Browse, Off };

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

struct BrowserLocation {
  char path[cfg::kMaxPathLen];
  char item[cfg::kMaxNameLen];  // selected visible entry; empty means no selection
};

struct PlayerSnapshot {
  PlayState state;
  char fileName[cfg::kMaxNameLen];
  uint32_t positionMs;
  uint32_t durationMs;
  int volumePercent;
  uint32_t sampleRate;
};

struct Toast {
  char text[48];
  uint32_t expiresAtMs;
};

struct SettingsSnapshot {
  int volumePercent;
  uint8_t brightness;
  uint32_t displayTimeoutMs;  // 0 disables the timeout
  uint32_t idleTimeoutMs;  // 0 disables the timeout
  bool autoNext;
  OnBootMode onBoot;
  size_t themeIndex;
  size_t cursor;
};
