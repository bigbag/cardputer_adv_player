# Cardputer-ADV Minimal MP3 Player Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a minimal SD-card MP3/WAV player for M5Stack Cardputer-ADV with a terminal-green file browser, now-playing screen, and custom ES8311 audio path.

**Architecture:** PlatformIO + Arduino app on Stamp-S3A. M5Cardputer owns display/keyboard/board bring-up. App state machine switches Browse ↔ Playing. SdBrowser lists FAT dirs. Player runs a FreeRTOS audio task that decodes (minimp3 or WAV) into a PCM ring and feeds I2S via AudioOut (ES8311 + amp/HP GPIO).

**Tech Stack:** PlatformIO, Arduino-ESP32, M5Cardputer ^1.0.3, minimp3 (vendored), ESP-IDF I2S driver (via Arduino), FreeRTOS.

**Spec:** `docs/superpowers/specs/2026-07-19-cardputer-adv-mp3-player-design.md`

---

## File map

| Path | Responsibility |
|------|----------------|
| `platformio.ini` | Device env + native test env |
| `include/config.hpp` | Pins, limits, colors, task sizes |
| `include/actions.hpp` | `Action` enum from keys |
| `include/types.hpp` | `DirEntry`, snapshots, screen enum |
| `include/path_utils.hpp` | Path join/parent/ext helpers (header-only or .cpp) |
| `src/path_utils.cpp` | Path helpers implementation |
| `src/audio_out.hpp/.cpp` | ES8311 + I2S + amp/HP |
| `src/decoders/decoder.hpp` | Common decoder interface |
| `src/decoders/wav_decoder.hpp/.cpp` | 16-bit PCM WAV |
| `src/decoders/mp3_decoder.hpp/.cpp` | minimp3 wrapper |
| `lib/minimp3/minimp3.h` | Vendored single-header decoder |
| `src/sd_browser.hpp/.cpp` | Mount, list, navigate, next-in-folder |
| `src/player.hpp/.cpp` | Open/pause/seek/volume/audio task |
| `src/ui.hpp/.cpp` | Browse + now-playing + toast render |
| `src/input.hpp/.cpp` | Keyboard → Action |
| `src/app.hpp/.cpp` | State machine |
| `src/main.cpp` | setup/loop wiring |
| `test/test_path_utils/test_path_utils.cpp` | Native tests |
| `test/test_wav_header/test_wav_header.cpp` | Native tests |
| `README.md` | Build, flash, on-device checklist |

---

### Task 1: PlatformIO scaffold + shared headers

**Files:**
- Create: `platformio.ini`
- Create: `include/config.hpp`
- Create: `include/actions.hpp`
- Create: `include/types.hpp`
- Create: `src/main.cpp` (stub)
- Create: `README.md` (stub)

- [ ] **Step 1: Create `platformio.ini`**

```ini
[env:cardputer-adv]
platform = espressif32@6.7.0
board = m5stack-stamps3
framework = arduino
upload_speed = 1500000
monitor_speed = 115200
lib_ldf_mode = deep
build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
    -DBOARD_HAS_PSRAM=0
    -std=gnu++17
lib_deps =
    m5stack/M5Cardputer@^1.0.3

[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -DUNIT_TEST
```

- [ ] **Step 2: Create `include/config.hpp`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace cfg {

constexpr int kScreenW = 240;
constexpr int kScreenH = 135;

// I2C
constexpr int kI2cSda = 8;
constexpr int kI2cScl = 9;

// ES8311 / I2S (Cardputer-ADV)
constexpr int kI2sBclk = 41;
constexpr int kI2sLrck = 43;
constexpr int kI2sDout = 42;
constexpr int kHpDetectPin = 17;   // LOW = headphones inserted
constexpr int kAmpEnablePin = 46;  // HIGH = speaker amp on
constexpr uint8_t kEs8311Addr = 0x18;
constexpr uint32_t kEs8311I2cHz = 400000;

// microSD SPI
constexpr int kSdCs = 12;
constexpr int kSdMosi = 14;
constexpr int kSdSck = 40;
constexpr int kSdMiso = 39;
constexpr uint32_t kSdSpiHz = 15000000;

// UI
constexpr uint16_t kColorBg = 0x0000;      // black
constexpr uint16_t kColorFg = 0x07E0;      // green
constexpr uint16_t kColorDim = 0x03E0;     // dim green
constexpr uint16_t kColorSelectBg = 0x07E0;
constexpr uint16_t kColorSelectFg = 0x0000;
constexpr int kHintBarH = 12;
constexpr int kListRowH = 14;
constexpr int kMaxVisibleRows = 7;

// Browser / player
constexpr size_t kMaxDirEntries = 256;
constexpr size_t kMaxPathLen = 256;
constexpr size_t kMaxNameLen = 64;
constexpr int kVolumeStepPercent = 5;
constexpr int kSeekStepSeconds = 5;
constexpr int kDefaultVolumePercent = 70;
constexpr uint32_t kToastMs = 1500;

// Audio task / buffers
constexpr uint32_t kDefaultSampleRate = 44100;
constexpr int kAudioTaskStack = 8192;
constexpr UBaseType_t kAudioTaskPrio = 5;
constexpr size_t kPcmRingFrames = 4096;   // stereo frames capacity
constexpr size_t kCompressedBuf = 4096;
constexpr bool kBootBeep = false;

}  // namespace cfg
```

Note: If `UBaseType_t` is unavailable in native tests, wrap the audio-task constants in `#ifndef UNIT_TEST` or include a tiny stub. Prefer:

```cpp
#ifndef UNIT_TEST
#include <freertos/FreeRTOS.h>
constexpr UBaseType_t kAudioTaskPrio = 5;
#else
constexpr unsigned kAudioTaskPrio = 5;
#endif
constexpr int kAudioTaskStack = 8192;
```

- [ ] **Step 3: Create `include/actions.hpp`**

```cpp
#pragma once

enum class Action : uint8_t {
  None = 0,
  Up,
  Down,
  Enter,
  Back,       // Esc or `
  Space,
  VolUp,      // .
  VolDown,    // ,
  SeekFwd,    // ]
  SeekBack,   // [
  RetrySd,    // optional: same as Enter on empty SD screen
};
```

- [ ] **Step 4: Create `include/types.hpp`**

```cpp
#pragma once

#include "config.hpp"
#include <cstdint>

enum class Screen : uint8_t { Browse, Playing };

enum class EntryKind : uint8_t { Dir, Mp3, Wav };

enum class PlayState : uint8_t { Idle, Playing, Paused, Done, Error };

struct DirEntry {
  char name[cfg::kMaxNameLen];
  EntryKind kind;
};

struct BrowseSnapshot {
  char path[cfg::kMaxPathLen];  // e.g. "/" or "/music"
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
};

struct Toast {
  char text[48];
  uint32_t expiresAtMs;  // 0 = inactive
};
```

- [ ] **Step 5: Create stub `src/main.cpp`**

```cpp
#include <M5Cardputer.h>

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg, true);
  Serial.begin(115200);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setCursor(4, 4);
  M5Cardputer.Display.println("cardputer_asv_mp3");
  M5Cardputer.Display.println("scaffold ok");
}

void loop() {
  M5Cardputer.update();
  delay(50);
}
```

- [ ] **Step 6: Create stub `README.md`**

```markdown
# cardputer_asv_mp3

Minimal MP3/WAV player for M5Stack Cardputer-ADV.

## Build

```bash
pio run -e cardputer-adv
pio run -e cardputer-adv -t upload
pio device monitor -b 115200
```

## Tests (host)

```bash
pio test -e native
```

## Spec

See `docs/superpowers/specs/2026-07-19-cardputer-adv-mp3-player-design.md`.
```

- [ ] **Step 7: Build device env (compile check)**

Run: `pio run -e cardputer-adv`  
Expected: SUCCESS (may download platform/libs first)

- [ ] **Step 8: Commit**

```bash
git add platformio.ini include/config.hpp include/actions.hpp include/types.hpp src/main.cpp README.md
git commit -m "chore: scaffold PlatformIO project and shared headers"
```

---

### Task 2: Path utilities + native tests

**Files:**
- Create: `include/path_utils.hpp`
- Create: `src/path_utils.cpp`
- Create: `test/test_path_utils/test_path_utils.cpp`
- Modify: `platformio.ini` (ensure native test can see `include/` and `src/path_utils.cpp`)

- [ ] **Step 1: Implement path helpers**

`include/path_utils.hpp`:

```cpp
#pragma once

#include "types.hpp"
#include <cstddef>

namespace path {

// dest capacity must be >= cfg::kMaxPathLen
bool join(char* dest, size_t destCap, const char* dir, const char* name);
bool parent(char* dest, size_t destCap, const char* path);  // "/" parent stays "/"
void fileName(char* dest, size_t destCap, const char* path);
bool hasExtInsensitive(const char* name, const char* extWithDot);  // ".mp3"
EntryKind kindFromName(const char* name);  // Dir if no known ext — caller handles dirs
void toLowerAscii(char* s);

}  // namespace path
```

`src/path_utils.cpp` — implement with `snprintf`, no Arduino `String`:

```cpp
#include "path_utils.hpp"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace path {

static int tolower_ascii(int c) {
  return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

void toLowerAscii(char* s) {
  if (!s) return;
  for (; *s; ++s) *s = static_cast<char>(tolower_ascii(static_cast<unsigned char>(*s)));
}

bool join(char* dest, size_t destCap, const char* dir, const char* name) {
  if (!dest || destCap == 0 || !dir || !name) return false;
  if (std::strcmp(dir, "/") == 0) {
    return std::snprintf(dest, destCap, "/%s", name) < static_cast<int>(destCap);
  }
  return std::snprintf(dest, destCap, "%s/%s", dir, name) < static_cast<int>(destCap);
}

bool parent(char* dest, size_t destCap, const char* path) {
  if (!dest || destCap == 0 || !path) return false;
  if (path[0] == '\0' || std::strcmp(path, "/") == 0) {
    std::snprintf(dest, destCap, "/");
    return true;
  }
  std::snprintf(dest, destCap, "%s", path);
  size_t n = std::strlen(dest);
  while (n > 1 && dest[n - 1] == '/') {
    dest[--n] = '\0';
  }
  char* slash = std::strrchr(dest, '/');
  if (!slash || slash == dest) {
    std::snprintf(dest, destCap, "/");
    return true;
  }
  *slash = '\0';
  if (dest[0] == '\0') std::snprintf(dest, destCap, "/");
  return true;
}

void fileName(char* dest, size_t destCap, const char* path) {
  if (!dest || destCap == 0) return;
  if (!path) { dest[0] = '\0'; return; }
  const char* base = std::strrchr(path, '/');
  base = base ? base + 1 : path;
  std::snprintf(dest, destCap, "%s", base);
}

bool hasExtInsensitive(const char* name, const char* extWithDot) {
  if (!name || !extWithDot) return false;
  size_t nlen = std::strlen(name);
  size_t elen = std::strlen(extWithDot);
  if (nlen < elen) return false;
  const char* tail = name + (nlen - elen);
  for (size_t i = 0; i < elen; ++i) {
    if (tolower_ascii(static_cast<unsigned char>(tail[i])) !=
        tolower_ascii(static_cast<unsigned char>(extWithDot[i]))) {
      return false;
    }
  }
  return true;
}

EntryKind kindFromName(const char* name) {
  if (hasExtInsensitive(name, ".mp3")) return EntryKind::Mp3;
  if (hasExtInsensitive(name, ".wav")) return EntryKind::Wav;
  return EntryKind::Dir;
}

}  // namespace path
```

- [ ] **Step 2: Native test**

`test/test_path_utils/test_path_utils.cpp`:

```cpp
#include <unity.h>
#include "path_utils.hpp"

void test_join_root() {
  char buf[64];
  TEST_ASSERT_TRUE(path::join(buf, sizeof(buf), "/", "music"));
  TEST_ASSERT_EQUAL_STRING("/music", buf);
}

void test_join_nested() {
  char buf[64];
  TEST_ASSERT_TRUE(path::join(buf, sizeof(buf), "/music", "a.mp3"));
  TEST_ASSERT_EQUAL_STRING("/music/a.mp3", buf);
}

void test_parent_nested() {
  char buf[64];
  TEST_ASSERT_TRUE(path::parent(buf, sizeof(buf), "/music/rock"));
  TEST_ASSERT_EQUAL_STRING("/music", buf);
}

void test_parent_root() {
  char buf[64];
  TEST_ASSERT_TRUE(path::parent(buf, sizeof(buf), "/"));
  TEST_ASSERT_EQUAL_STRING("/", buf);
}

void test_ext_case() {
  TEST_ASSERT_TRUE(path::hasExtInsensitive("Song.MP3", ".mp3"));
  TEST_ASSERT_TRUE(path::hasExtInsensitive("x.Wav", ".wav"));
  TEST_ASSERT_FALSE(path::hasExtInsensitive("x.txt", ".mp3"));
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_join_root);
  RUN_TEST(test_join_nested);
  RUN_TEST(test_parent_nested);
  RUN_TEST(test_parent_root);
  RUN_TEST(test_ext_case);
  return UNITY_END();
}
```

Update `platformio.ini` `[env:native]`:

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -DUNIT_TEST
    -Iinclude
build_src_filter =
    +<path_utils.cpp>
    +<../test/**/*>  ; not needed if using pio test defaults
```

PlatformIO native tests automatically compile `test/test_*/` and need access to sources. Prefer:

```ini
[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -DUNIT_TEST
    -Iinclude
build_src_filter = +<path_utils.cpp>
```

And put path_utils under `src/` (already). For `pio test -e native`, add:

```ini
test_build_src = true
```

so `src/path_utils.cpp` links into tests.

- [ ] **Step 3: Run tests**

Run: `pio test -e native -f test_path_utils`  
Expected: all PASS

- [ ] **Step 4: Commit**

```bash
git add include/path_utils.hpp src/path_utils.cpp test/test_path_utils platformio.ini
git commit -m "feat: add path utilities with native unit tests"
```

---

### Task 3: WAV header parser + decoder interface

**Files:**
- Create: `src/decoders/decoder.hpp`
- Create: `src/decoders/wav_decoder.hpp`
- Create: `src/decoders/wav_decoder.cpp`
- Create: `test/test_wav_header/test_wav_header.cpp`
- Modify: `platformio.ini` native `build_src_filter` to include `decoders/wav_decoder.cpp` **or** split pure parse function into testable unit

- [ ] **Step 1: Decoder interface**

`src/decoders/decoder.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

struct AudioFormat {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;     // 1 or 2
  uint16_t bitsPerSample = 16;
  uint32_t durationMs = 0;   // 0 if unknown
};

enum class DecodeStatus : uint8_t {
  Ok,          // produced samples
  NeedMore,    // need more input
  Finished,    // end of stream
  Error
};

class IDecoder {
 public:
  virtual ~IDecoder() = default;
  virtual bool open(const char* path) = 0;
  virtual void close() = 0;
  virtual AudioFormat format() const = 0;
  // Write up to maxFrames stereo int16 interleaved frames into outStereo.
  // Mono sources must be upmixed to stereo here.
  virtual DecodeStatus decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) = 0;
  // Best-effort seek; return false if unsupported/failure
  virtual bool seekMs(uint32_t ms) = 0;
  virtual uint32_t positionMs() const = 0;
};
```

- [ ] **Step 2: WAV parse structures + pure function**

In `wav_decoder.hpp` expose for tests:

```cpp
#pragma once

#include "decoder.hpp"
#include <cstdint>

struct WavInfo {
  uint32_t sampleRate = 0;
  uint16_t channels = 0;
  uint16_t bitsPerSample = 0;
  uint32_t dataOffset = 0;
  uint32_t dataSize = 0;
  bool valid = false;
  const char* error = nullptr;
};

// Parse RIFF/WAVE from a memory buffer (entire header region, at least first 44+ bytes).
// Does not require Arduino.
WavInfo wavParseHeader(const uint8_t* data, size_t len);

class WavDecoder : public IDecoder {
 public:
  bool open(const char* path) override;
  void close() override;
  AudioFormat format() const override;
  DecodeStatus decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) override;
  bool seekMs(uint32_t ms) override;
  uint32_t positionMs() const override;

 private:
  // FILE* or fs::File — on device use fs::File; gate with #ifndef UNIT_TEST for file ops
  AudioFormat fmt_{};
  WavInfo info_{};
  uint32_t bytesRead_ = 0;
#ifndef UNIT_TEST
  // hold SD file handle
  void* file_ = nullptr;  // implement with fs::File in .cpp
#endif
};
```

Implement `wavParseHeader` to:
- Require `RIFF`….`WAVE`
- Walk chunks until `fmt ` and `data`
- Accept PCM format tag `1` only, 16-bit, 1 or 2 channels
- Set `error` string on failure (`"not riff"`, `"not pcm"`, `"bad bps"`, …)

- [ ] **Step 3: Native tests with synthetic headers**

Build a minimal 44-byte PCM WAV header in the test (16-bit stereo 44100, data size 0) and assert parse success; also assert rejection of format tag 3 (IEEE float).

- [ ] **Step 4: Run tests**

Run: `pio test -e native -f test_wav_header`  
Expected: PASS

- [ ] **Step 5: Implement device `WavDecoder::open/decode/seek` using `fs::File` / `SD.open`**

- Read first 512 bytes, `wavParseHeader`
- Seek to `dataOffset`
- `decode`: read PCM, upmix mono→stereo, update `bytesRead_` / position
- `seekMs`: byte offset = ms * rate * channels * 2 / 1000, clamp to dataSize
- `durationMs` from dataSize

- [ ] **Step 6: Commit**

```bash
git add src/decoders include/ test/test_wav_header platformio.ini
git commit -m "feat: add WAV header parser and WavDecoder"
```

---

### Task 4: Vendor minimp3 + Mp3Decoder

**Files:**
- Create: `lib/minimp3/minimp3.h` (download from https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h)
- Create: `src/decoders/mp3_decoder.hpp`
- Create: `src/decoders/mp3_decoder.cpp`

- [ ] **Step 1: Vendor header**

```bash
mkdir -p lib/minimp3
curl -L -o lib/minimp3/minimp3.h \
  https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h
```

Add `lib_extra_dirs` or rely on PlatformIO `lib/` auto-discover. Prefer `#include "minimp3.h"` with:

```ini
build_flags =
    ...
    -Ilib/minimp3
```

in `[env:cardputer-adv]`.

- [ ] **Step 2: Implement `Mp3Decoder`**

```cpp
// mp3_decoder.hpp
#pragma once
#include "decoder.hpp"

class Mp3Decoder : public IDecoder {
 public:
  bool open(const char* path) override;
  void close() override;
  AudioFormat format() const override;
  DecodeStatus decode(int16_t* outStereo, size_t maxFrames, size_t* gotFrames) override;
  bool seekMs(uint32_t ms) override;
  uint32_t positionMs() const override;
 private:
  // minimp3_t, file handle, input buffer[cfg::kCompressedBuf], pcm leftovers, frame counters
};
```

Implementation notes:
- `#define MINIMP3_IMPLEMENTATION` in **one** `.cpp` only
- Prefer `MINIMP3_ONLY_MP3` if defined by library to save flash
- On `open`: open file, `mp3dec_init`, decode until first frame to learn rate/channels
- `durationMs`: 0 for v1 if VBR unknown (UI shows elapsed only or `?:??`); optional: file size heuristic later
- `seekMs`: best-effort — seek file byte ≈ `(ms/duration)*fileSize` then `mp3dec_init` resync (acceptable drift)
- Upmix mono to stereo in `decode`
- Clamp samples to int16

- [ ] **Step 3: Device compile check**

Run: `pio run -e cardputer-adv`  
Expected: SUCCESS

- [ ] **Step 4: Commit**

```bash
git add lib/minimp3 src/decoders/mp3_decoder.* platformio.ini
git commit -m "feat: vendor minimp3 and add Mp3Decoder"
```

---

### Task 5: AudioOut (ES8311 + I2S + amp/HP)

**Files:**
- Create: `src/audio_out.hpp`
- Create: `src/audio_out.cpp`
- Modify: `src/main.cpp` — temporary boot path to init AudioOut and optional beep

- [ ] **Step 1: API**

```cpp
#pragma once
#include <cstdint>
#include <cstddef>

class AudioOut {
 public:
  bool begin();  // I2C ES8311 + GPIO amp/HP + I2S install at default rate
  void end();
  bool setSampleRate(uint32_t hz);
  void setVolumePercent(int percent);  // 0..100
  int volumePercent() const;
  // Blocking-ish write of interleaved stereo int16 frames
  size_t write(const int16_t* stereoFrames, size_t frames);
  void updateAmpFromHp();  // read HP pin, drive amp enable
  bool headphonesInserted() const;
  bool playTestBeep(uint32_t freqHz, uint32_t ms);  // if cfg::kBootBeep
 private:
  bool esWrite(uint8_t reg, uint8_t val);
  bool esInitRegisters();
  bool i2sStart(uint32_t rate);
  void i2sStop();
  uint32_t rate_ = 0;
  int volume_ = 70;
  bool ready_ = false;
};
```

- [ ] **Step 2: Implement ES8311 init**

Use sequence from spec:

```cpp
static const uint8_t kSeq[][2] = {
  {0x00, 0x80}, {0x01, 0xB5}, {0x02, 0x18}, {0x0D, 0x01},
  {0x12, 0x00}, {0x13, 0x10}, {0x32, 0xBF}, {0x37, 0x08},
};
```

Write via `Wire` or `M5.In_I2C.writeRegister(cfg::kEs8311Addr, reg, &val, 1, cfg::kEs8311I2cHz)`.

- [ ] **Step 3: Implement I2S**

Use legacy driver API consistent with Arduino-ESP32 2.x (platform 6.7.0):

```cpp
i2s_config_t cfg = {};
cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
cfg.sample_rate = (int)rate;
cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
cfg.dma_buf_count = 6;
cfg.dma_buf_len = 256;
cfg.tx_desc_auto_clear = true;
// pins: bck=41, ws=43, dout=42
```

- [ ] **Step 4: Amp / HP**

```cpp
pinMode(cfg::kHpDetectPin, INPUT_PULLUP);
pinMode(cfg::kAmpEnablePin, OUTPUT);
// amp HIGH only if HP not inserted
```

Call `updateAmpFromHp()` from `begin()` and periodically from `App`/`loop`.

- [ ] **Step 5: Volume**

Map 0–100% onto ES8311 DAC volume register (0x32 scale) and/or software multiply in `write()` as backup. Keep it simple: software scale in `write()` first if register curve unknown; still poke a mid DAC level at init.

- [ ] **Step 6: Optional beep smoke test**

In `main` temporarily:

```cpp
AudioOut out;
out.begin();
if (cfg::kBootBeep) out.playTestBeep(440, 300);
```

Flash and listen. Then leave API ready; gate beep with `cfg::kBootBeep = false` by default.

- [ ] **Step 7: Commit**

```bash
git add src/audio_out.hpp src/audio_out.cpp src/main.cpp
git commit -m "feat: add ES8311 AudioOut with I2S and amp/HP control"
```

---

### Task 6: SdBrowser

**Files:**
- Create: `src/sd_browser.hpp`
- Create: `src/sd_browser.cpp`
- Modify: `src/main.cpp` only if needed for manual check

- [ ] **Step 1: API**

```cpp
#pragma once
#include "types.hpp"

class SdBrowser {
 public:
  bool begin();                 // SPI + SD.begin(cs)
  bool remount();
  bool sdOk() const;
  bool listCurrent();           // fill entries_ from path_
  bool enter(const char* name); // name is directory name
  bool up();
  bool openPath(const char* absPath);
  const char* path() const;
  const DirEntry* entries() const;
  size_t count() const;
  bool truncated() const;
  size_t cursor() const;
  void setCursor(size_t i);
  void moveCursor(int delta);   // clamp 0..count-1
  size_t scroll() const;        // maintain window for UI
  // Next audio file after current base name in current listing; false if none
  bool nextAudioAfter(const char* fileName, char* outPath, size_t outCap);
  BrowseSnapshot snapshot() const;
 private:
  bool sdOk_ = false;
  char path_[cfg::kMaxPathLen] = "/";
  DirEntry entries_[cfg::kMaxDirEntries];
  size_t count_ = 0;
  bool truncated_ = false;
  size_t cursor_ = 0;
  size_t scroll_ = 0;
  void sortEntries();
  void ensureScroll();
};
```

- [ ] **Step 2: Implement listing rules**

- Skip `.` and `..` and names starting with `.`
- Directories: `EntryKind::Dir`
- Files: only `.mp3`/`.wav` via `path::hasExtInsensitive`
- Sort: dirs alphabetically (case-insensitive), then files alphabetically
- Cap at `kMaxDirEntries`; set `truncated_`
- `nextAudioAfter`: find index of `fileName` in entries_; scan forward for Mp3/Wav; build abs path with `path::join`

SD begin:

```cpp
SPI.begin(cfg::kSdSck, cfg::kSdMiso, cfg::kSdMosi, cfg::kSdCs);
sdOk_ = SD.begin(cfg::kSdCs, SPI, cfg::kSdSpiHz);
```

- [ ] **Step 3: Manual device check (optional serial)**

Log path + entries on boot if SD present.

- [ ] **Step 4: Commit**

```bash
git add src/sd_browser.hpp src/sd_browser.cpp
git commit -m "feat: add SD browser with sorted dir listing"
```

---

### Task 7: Player + audio task

**Files:**
- Create: `src/player.hpp`
- Create: `src/player.cpp`

- [ ] **Step 1: API**

```cpp
#pragma once
#include "types.hpp"
#include "audio_out.hpp"
#include "sd_browser.hpp"

class Player {
 public:
  bool begin(AudioOut* out, SdBrowser* browser);
  void end();
  bool open(const char* absPath);  // stops previous
  void stop();
  void togglePause();
  void setVolumePercent(int p);
  void adjustVolume(int deltaPercent);
  void seekRelative(int deltaSeconds);
  void service();  // call from loop: handle track-end → auto-next flag
  PlayerSnapshot snapshot() const;
  bool takeError(char* buf, size_t cap);  // pop last error for toast
 private:
  static void audioTaskThunk(void* arg);
  void audioTaskMain();
  bool openDecoder(const char* path);
  void closeDecoder();
  bool enqueueAutoNext();

  AudioOut* out_ = nullptr;
  SdBrowser* browser_ = nullptr;
  IDecoder* decoder_ = nullptr;
  // storage for concrete decoders (union or two members + pointer)
  // FreeRTOS: task handle, queue or ring buffer, atomics for pause/stop/seek
  volatile PlayState state_ = PlayState::Idle;
  char currentPath_[cfg::kMaxPathLen]{};
  char currentName_[cfg::kMaxNameLen]{};
  char lastError_[48]{};
  int volume_ = cfg::kDefaultVolumePercent;
};
```

- [ ] **Step 2: Ring buffer**

Simple SPSC ring of `int16_t` stereo frames (`cfg::kPcmRingFrames`):
- Audio task: decode → push frames (block/skip if full)
- Same task can also `out_->write` pulling from ring **or** decode directly to I2S in one task (simpler v1)

**v1 simplification (recommended):** single audio task that loops:

```
while running:
  if stop requested: break
  if paused: vTaskDelay(10); continue
  decode → temporary stereo buffer (e.g. 512 frames)
  out_->write(buf, n)
  update position
if finished: set state Done; set autoNext flag
```

No separate consumer task unless underruns force it.

- [ ] **Step 3: open()**

```
stop previous task cooperatively
detect ext → construct WavDecoder or Mp3Decoder
decoder->open
out_->setSampleRate(fmt.sampleRate)
out_->setVolumePercent(volume_)
start audio task
state = Playing
```

- [ ] **Step 4: Auto-next**

When decode returns `Finished`:
- `state = Done`
- `service()` in loop calls `browser_->nextAudioAfter(currentName_, …)`  
  - if found: `open(next)`  
  - else: leave `Done`

- [ ] **Step 5: Commit**

```bash
git add src/player.hpp src/player.cpp
git commit -m "feat: add Player with FreeRTOS audio task and auto-next"
```

---

### Task 8: UI (terminal green)

**Files:**
- Create: `src/ui.hpp`
- Create: `src/ui.cpp`

- [ ] **Step 1: API**

```cpp
#pragma once
#include "types.hpp"

class Ui {
 public:
  void begin();
  void showToast(const char* text, uint32_t nowMs);
  void render(Screen screen,
              const BrowseSnapshot& browse,
              const PlayerSnapshot& player,
              uint32_t nowMs);
 private:
  void drawBrowse(const BrowseSnapshot& b);
  void drawPlaying(const PlayerSnapshot& p);
  void drawHint(const char* text);
  void drawToastIfAny(uint32_t nowMs);
  Toast toast_{};
  Screen last_{Screen::Browse};
  // dirty tracking optional
};
```

- [ ] **Step 2: Layout**

**Browse**
- y=0 path line (dim), truncated with `SD:` prefix display `SD:` + path
- list rows from y=12; height `kListRowH`; max `kMaxVisibleRows`
- selected row: fill `kColorSelectBg`, text `kColorSelectFg`
- dirs show trailing `/`
- if !sdOk: centered `No SD card` + hint `Ent retry`
- if truncated: dim `* truncated` above hint
- hint: `;/ move  Ent open  Esc up`  (keys locked in Task 9: `;` up, `/` down)

**Playing**
- status PLAY/PAUSE/DONE
- filename
- `mm:ss / mm:ss` (if duration 0 show `--:--`)
- progress bar width 232 at mid
- `Vol nn%`
- hint: `Spc pause  [] seek  ,. vol  Esc list`

Use `M5Cardputer.Display` (`M5GFX`): `fillScreen`, `setTextColor`, `drawString`, `fillRect`.

- [ ] **Step 3: Commit**

```bash
git add src/ui.hpp src/ui.cpp
git commit -m "feat: add terminal-green Browse and Now Playing UI"
```

---

### Task 9: Input mapping

**Files:**
- Create: `src/input.hpp`
- Create: `src/input.cpp`

- [ ] **Step 1: Implement**

```cpp
#pragma once
#include "actions.hpp"

class Input {
 public:
  void begin();
  Action poll();  // M5Cardputer.update is called by App/main before poll
};
```

Map from `M5Cardputer.Keyboard` keys when `isChange`/`isPressed`:
- `;` or UP arrow key if available → `Up` (Cardputer often uses `;`/`.` for arrows in landscape — **also** map raw key codes)
- `.` used for VolUp per spec — conflict with Cardputer default “down” in some apps

**Spec bindings (authoritative):**
| Key | Action |
|-----|--------|
| `;` | Up (Cardputer common) |
| `'` or `Down` key if any | Down — use `/` or `.` carefully |

Cardputer-ADV physical labels: use M5 keys:
- `';` and `.` are often up/down on Cardputer firmware  
- Spec wants `,` `.` for volume and `[` `]` seek  

**Resolve conflict explicitly in code comments:**

```cpp
// Navigation (Browse): ';' = up, '.' = down  — BUT spec assigns '.' to VolUp on Playing.
// Therefore: map by screen in App, OR use:
//   Browse: ';' up, '.' down
//   Playing: ',' vol down, '.' vol up
// App passes Screen to poll OR Input returns raw keys.

// Prefer raw key → Action independent of screen for dedicated keys,
// and map ';' → Up, '/' → Down (slash is near) if '.' must be volume.
```

**Final mapping (lock this in implementation):**

| Key | Action |
|-----|--------|
| `;` | Up |
| `/` | Down |
| `Enter` | Enter |
| `Esc` or `` ` `` | Back |
| `Space` | Space |
| `,` | VolDown |
| `.` | VolUp |
| `[` | SeekBack |
| `]` | SeekFwd |

Document on hint bar as `;/ nav` instead of `^v` if arrows unavailable:

Hint Browse: `;/ move  Ent open  Esc up`  
Hint Playing: unchanged

Update UI hints accordingly in Task 8 if not already.

- [ ] **Step 2: Commit**

```bash
git add src/input.hpp src/input.cpp src/ui.cpp
git commit -m "feat: map Cardputer keys to player actions"
```

---

### Task 10: App state machine + main wiring

**Files:**
- Create: `src/app.hpp`
- Create: `src/app.cpp`
- Modify: `src/main.cpp`
- Modify: `README.md` (full checklist)

- [ ] **Step 1: App API**

```cpp
#pragma once
#include "sd_browser.hpp"
#include "player.hpp"
#include "audio_out.hpp"
#include "ui.hpp"
#include "input.hpp"
#include "types.hpp"

class App {
 public:
  void begin();
  void loop();
 private:
  void handle(Action a);
  void handleBrowse(Action a);
  void handlePlaying(Action a);
  void playSelection();

  AudioOut audio_;
  SdBrowser browser_;
  Player player_;
  Ui ui_;
  Input input_;
  Screen screen_ = Screen::Browse;
};
```

- [ ] **Step 2: begin()**

```
Serial 115200
M5Cardputer.begin
Display rotation 1, font
audio_.begin()
browser_.begin() + listCurrent()
player_.begin(&audio_, &browser_)
ui_.begin()
input_.begin()
ui_.render(...)
```

- [ ] **Step 3: loop()**

```
M5Cardputer.update()
audio_.updateAmpFromHp()
player_.service()
Action a = input_.poll()
if (a != None) handle(a)
// throttle render ~15–20 fps or on dirty
ui_.render(screen_, browser_.snapshot(), player_.snapshot(), millis())
```

- [ ] **Step 4: handleBrowse**

- Up/Down → browser_.moveCursor
- Enter → if dir enter+list; if audio playSelection
- Space → if audio playSelection
- Back → up+list
- Enter when !sdOk → remount+list

`playSelection`:

```
join path + name → player_.open
screen_ = Playing
```

- [ ] **Step 5: handlePlaying**

- Space → togglePause
- VolUp/Down → adjustVolume
- SeekFwd/Back → seekRelative ±kSeekStepSeconds
- Back → screen_ = Browse (do not stop)
- Enter ignored or treated as nop

- [ ] **Step 6: Errors**

If `player_.takeError(buf)` → `ui_.showToast(buf, millis())` and optionally `screen_ = Browse` on hard errors.

- [ ] **Step 7: main.cpp**

```cpp
#include "app.hpp"
static App app;
void setup() { app.begin(); }
void loop() { app.loop(); }
```

- [ ] **Step 8: Full device build**

Run: `pio run -e cardputer-adv`  
Expected: SUCCESS

- [ ] **Step 9: Update README with flash + on-device checklist from spec**

- [ ] **Step 10: Commit**

```bash
git add src/app.hpp src/app.cpp src/main.cpp README.md
git commit -m "feat: wire app state machine for browse and playback"
```

---

### Task 11: On-device hardening pass

**Files:**
- Modify: decoders/player/audio_out/ui as bugs appear
- Modify: `include/config.hpp` buffer sizes if underrun

- [ ] **Step 1: Flash and run checklist**

```bash
pio run -e cardputer-adv -t upload && pio device monitor -b 115200
```

Work through spec checklist:

1. No SD → message + retry  
2. Nested browse + parent at root  
3. MP3 44.1 / 48k  
4. WAV 16-bit  
5. Bad WAV toast  
6. Pause, volume, seek  
7. Auto-next / DONE  
8. Esc browse while playing; switch track  
9. HP jack amp off  
10. Serial logs coherent  

- [ ] **Step 2: Fix underruns** if any — increase `dma_buf_count`, decode chunk size, audio task priority; ensure UI doesn’t starve CPU on core (pin audio task to core 1 if needed: `xTaskCreatePinnedToCore`).

- [ ] **Step 3: Commit fixes**

```bash
git add -A
git commit -m "fix: stabilize playback and SD edge cases on device"
```

---

### Task 12: Final polish

- [ ] **Step 1: Run host tests again**

Run: `pio test -e native`  
Expected: all PASS

- [ ] **Step 2: Ensure `cfg::kBootBeep` is false; remove temporary debug draws

- [ ] **Step 3: README complete** — controls table matching final key map, FAT32 note, suggested `/music` layout, download mode note for Adv (hold G0)

- [ ] **Step 4: Final commit**

```bash
git add README.md include/config.hpp
git commit -m "docs: finalize README and defaults for v1 player"
```

---

## Spec coverage checklist

| Spec item | Task |
|-----------|------|
| PlatformIO + Arduino + M5Cardputer | 1 |
| Pins / ES8311 / amp / HP | 1, 5 |
| Path + listing rules | 2, 6 |
| WAV 16-bit PCM | 3 |
| MP3 minimp3 | 4 |
| AudioOut I2S | 5 |
| SdBrowser + next in folder | 6, 7 |
| Player task, pause, vol, seek, auto-next | 7 |
| Terminal UI + hints | 8 |
| Key bindings | 9 |
| Browse ↔ Playing, Esc keeps audio | 10 |
| Toasts / SD retry | 10 |
| On-device checklist / README | 11, 12 |
| Native tests for pure logic | 2, 3 |
| No ID3/EQ/shuffle/WiFi | — omitted by design |

## Risk notes

1. **G46 dual meaning** (M5 ASDOUT vs amp enable): follow working Adv firmware; if amp never enables, verify schematic PDF before changing pins.  
2. **M5Cardputer keyboard on ADV** may need TCA8418 reader — if keys dead, probe board type and call `Keyboard.begin` with TCA8418 reader as in CardPuter_Mp3_Adv `board_init.cpp`. Add fix under Task 9/11.  
3. **minimp3 CPU** on S3 is fine for stereo 320 kbps; if not, reduce other work.  
4. **Fallback:** if ES8311+custom decode fails after Task 11, swap Player backend to ESP32-audioI2S while keeping UI/SdBrowser — out of plan unless blocked.

## Execution

Implement tasks **in order** (1→12). Prefer one commit per task. Do not skip native tests in Tasks 2–3. Device audio validation is required before calling v1 done.
