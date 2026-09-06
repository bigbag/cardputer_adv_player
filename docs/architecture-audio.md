# Audio and device architecture

This document describes the audio path of the Cardputer-ADV MP3/WAV/FLAC player.
The path goes from the SD card to the speaker or to the 3.5 mm jack.

This document describes the current firmware.
The hardware controls the speaker mute. The firmware does not use an MCU GPIO for this function.

---

## 1. Hardware (Cardputer-ADV)

```text
                    ┌─────────────────────────────────────────────┐
                    │              ESP32-S3 (Stamp-S3A)           │
                    │                                             │
   microSD ─SPI──►  │  app / browser / settings                   │
                    │       │                                     │
                    │       ▼                                     │
                    │  Player (FreeRTOS audio task, core 0)       │
                    │       │  PCM stereo int16                   │
                    │       ▼                                     │
                    │  AudioOut  ──I2S TX──►  BCLK/LRCK/DOUT      │
                    │       │                                     │
                    │       └─I2C control─►  ES8311 0x18          │
                    └───────────┬───────────────────┬─────────────┘
                                │ DAC out           │
                                ▼                   ▼
                         ┌────────────┐      ┌─────────────┐
                         │  NS4150B   │      │  3.5 mm jack│
                         │  speaker   │      │  (HP out)   │
                         │  amp       │      └─────────────┘
                         └─────┬──────┘
                               ▼
                            1 W speaker
```

### 1.1 Jack vs speaker (hardware mute)

On the Cardputer-ADV, insertion of the 3.5 mm jack mutes the speaker
amplifier in hardware. A MOSFET or a mechanical switch on the jack does the
detection. The MCU does not drive a software amplifier-enable signal on this
product:

- Do **not** treat **G46** as `AMP_EN` in this firmware. On the ADV board,
  G46 is the ES8311 **ASDOUT** signal (microphone ADC output). G46 does not
  control the amplifier.
- The player has no software jack-detect path. One volume curve covers both
  outputs. The user sets a low level for headphones and a high level for the
  speaker.

### 1.2 Pin map (as used in `include/config.hpp`)

**I2C (codec control)**

- SDA — **G8**
- SCL — **G9**
- ES8311 address — **0x18**
- Bus rate for register writes — up to 400 kHz (`kEs8311I2cHz`)

**I2S (PCM to DAC)**

- BCLK (SCLK) — **G41**
- LRCK (WS) — **G43**
- DOUT (DSDIN → ES8311 DAC) — **G42**
- DIN / MCLK — unused (`I2S_PIN_NO_CHANGE`)

**microSD (SPI)**

- CS — **G12**
- MOSI — **G14**
- SCK — **G40**
- MISO — **G39**
- Clock — try to mount at 4 MHz first. If the mount fails, try 10 MHz and then 15 MHz.

**Display / keys** (these parts are not on the audio path)

- 240×135 LCD via M5GFX / M5Cardputer
- Keyboard via onboard controller (M5Cardputer.Keyboard)

---

## 2. Software stack

```text
┌──────────────────────────────────────────────────────────┐
│  App (Arduino loopTask, typically core 1)                │
│    UI · Input · Settings · SdBrowser · Player.service()  │
└────────────────────────────┬─────────────────────────────┘
                             │ open / pause / seek / volume / next
                             ▼
┌──────────────────────────────────────────────────────────┐
│  Player                                                  │
│    owns the path, state, and volume snapshot             │
│    spawns FreeRTOS task "audio" on core 0                │
└────────────────────────────┬─────────────────────────────┘
                             │
          ┌──────────────────┼──────────────────┐
          ▼                  ▼                  ▼
   Mp3Decoder         WavDecoder/          SdBrowser
   (minimp3)          FlacDecoder         next/prev path
          │                  │
          └────────┬─────────┘
                   │ int16 stereo frames
                   ▼
            AudioOut::write()
                   │  stereo mean, bounded Q15 gain
                   │  duplicate mono into both I2S slots
                   ▼
            i2s_write(I2S_NUM_0)
                   │
                   ▼
            ES8311 DAC  (register 0x32 fixed at 0xBF)
```

### 2.1 Modules

- **`App`** (`src/app.cpp`) controls the Browse, Playing, Settings, and System screens.
  It applies settings. It saves `last_path` and the browser location.
  It calls the `Player` controls.
- **`Ui`** (`src/ui.cpp`) draws the screens and a shared battery widget.
  System shows CPU, memory, uptime, SD status, and battery voltage.
  Every five seconds with the display on, the UI starts a batch of 16 voltage
  readings through `M5.Power`. The readings are at least 20 ms apart.
  `BatteryReading` supplies the complete average and its estimated percentage.
  System and the widget use the same result. A reading outside 2.0–4.5 V
  makes that batch unavailable. Screen wake clears the previous result and batch.
  Sampling does not wait between readings. Other System values refresh once per second.
  These updates do not restart the activity timers or control playback.
- **`Player`** (`src/player.cpp`) controls playback through its open, stop, pause,
  seek, next, and previous functions. It runs the FreeRTOS audio task.
  `service()` starts automatic track changes in the application loop.
  Track searches temporarily use the parent folder of `currentPath()`.
  The player then restores the visible Browser state.
- **`IDecoder`** (`src/decoders/decoder.hpp`) — common interface with
  `open`, `decode`, `seekMs`, `positionMs`, and `format`.
- **`Mp3Decoder`** uses the third-party **minimp3** library.
  It reads data from the SD card and produces PCM samples.
- **`WavDecoder`** supports 16-bit PCM WAV only.
  It copies each mono sample to both stereo channels.
- **`FlacDecoder`** uses the pinned **dr_flac** library.
  It checks native FLAC limits and produces stereo 16-bit PCM.
- **`DecoderInput`** supplies shared file operations for MP3 and FLAC.
  Firmware uses `SdInput`. Native checks can supply borrowed fixture input.
- **`AudioOut`** (`src/audio_out.cpp`) initializes the ES8311 over I2C.
  It transmits audio as an I2S master. It applies gain to PCM samples.
  It can play a test tone.
- **`SdBrowser`** lists the FAT directory with `readdir`.
  It opens folders. It finds the next or previous audio file in the current folder.
- **`Settings`** — stores `/.asvmp3/config.cfg` on the SD card. Keys
  include volume, theme, `last_path`, `browser_path`, `browser_item`, and
  `on_boot`.

---

## 3. Playback data path (runtime)

1. `App::playSelection()` calls `Player::open(absPath)`.
2. `Player::open()` waits for the previous task to release its resources.
   It stores the UI-owned path and name and clears the published track values.
   It starts one core-0 task with a 24576-byte stack.
3. The audio task opens the decoder and publishes its format.
   It sets the I2S rate and applies the latest requested volume.
4. Each loop applies control requests, decodes stereo PCM, and writes the output.
   A complete output write permits position publication.
   A short or failed write produces an output error.
5. The task closes its decoder before it releases resource ownership.
   It preserves final published track values.
6. `Player::service()` consumes Auto-next only after resource completion.
   Track selection remains in the application loop.

### 3.1 Playback controls and publication

The UI sends atomic stop, pause, seek, and volume requests.
Only the audio task changes active output state or accesses decoder resources.
The audio task applies volume and seek requests even while paused.
The UI toggles the requested pause value, not the delayed published state.

Snapshots read atomic state, format, position, and requested volume.
Individual fields can come from adjacent audio-loop iterations.
The snapshot is not an atomic copy of all fields together.
The UI consumes an error enum once and converts it to display text.

Pause stops new PCM production and lets queued audio finish.
Seek resets the I2S stream before it moves the decoder.
The reset reinstalls the driver at the current rate.
This clears driver-owned queued data but allocates on each seek.
A paused seek stays paused.

Explicit stop discards old output before resource release.
Natural EOF writes one DMA ring of silence before resource release.
This advances the queued tail before a later rate change.
An output failure prevents Auto-next.

`stop()` waits for an acquire load of the task completion flag.
The task's release store is its final access to Player.
The idle task can reclaim the FreeRTOS stack later.
I2S writes use a finite timeout, but an SD driver stall can still delay stop.
The application never closes SD under a live decoder task to escape a timeout.

---

## 4. I2S / ES8311 configuration

### 4.1 I2S (ESP-IDF legacy driver)

- Port: `I2S_NUM_0`
- Mode: master TX
- Format: standard I2S, 16-bit, stereo (`I2S_CHANNEL_FMT_RIGHT_LEFT`)
- DMA: 8 buffers × 256 samples
- Default rate: **44100** Hz. The code reconfigures the rate per track (for
  example 48000 or 22050).

Normal rate changes use `i2s_set_clk()` without reinstalling the driver.
A failed rate change gets one complete reinstall attempt.
If that attempt fails, playback reports an output error.
Each 128-frame write uses a 100 ms driver timeout.
This timeout is not a bound on a full track operation or an SD access.

### 4.2 ES8311 init sequence

The code writes these registers over I2C at start
(`AudioOut::esInitRegisters`):

| Reg  | Value | Role (summary)         |
|------|-------|------------------------|
| 0x00 | 0x80  | Reset / clock domain   |
| 0x01 | 0xB5  | Clock manager          |
| 0x02 | 0x18  | Clock config           |
| 0x0D | 0x01  | System                 |
| 0x12 | 0x00  | DAC power control |
| 0x13 | 0x10  | Headphone output control |
| 0x32 | 0xBF  | DAC volume at 0 dB |
| 0x37 | 0x08  | DAC equalizer bypass |

During playback, the DAC digital volume register (0x32) stays at `0xBF`.
The software applies the user volume setting to PCM samples.
This gives one volume curve for both outputs.
`AudioOut::end()` sets register 0x32 to `0x00` before it stops I2S output.

---

## 5. Volume model

The UI has one volume control from 0 to 100%.
The default is 30%. The step is 2%.
The player applies saved volume values without conversion.

`AudioOut::write()` computes the mean of each stereo frame.
It applies Q15 gain to that mean and writes the result into both I2S slots.
Both divisions truncate toward zero.

```text
mono = (int32(left) + int32(right)) / 2
gainQ15 = clamp(UI, 0, 100)^4 * 32768 / 100000000
output = mono * gainQ15 / 32768
I2S left = output
I2S right = output
```

The gain calculation uses 64-bit integers to prevent intermediate overflow.
Q15 precision rounds settings from 1% through 7% to zero.
The gain does not decrease as the UI setting increases.

Gain stays between zero and unity.
Mute produces zero. 100% preserves the mono sample.
Identical channels keep their level before gain.
Opposite channels cancel in the mono output.
The implementation does not add PCM boost or write codec gain on volume changes.
The DAC register stays at `0xBF` during playback.

The fourth-power curve moves the headphone range toward the middle of the UI scale.
A 50% setting matches 25% on the previous quadratic curve.
A 55% setting approximately matches the previous 30%.
The maximum gain at 100% stays unchanged.
Start headphone checks at a low setting.
Bounded PCM does not rule out analog distortion or unsafe headphone levels.
The user defers hardware listening comparisons.

Host checks cover all 65536 sample values at all 101 volume settings.
The checks confirm bounded output, mute, unity, and identical-channel preservation.

---

## 6. Decode formats

### MP3 (`Mp3Decoder` + minimp3)

- The decoder uses a heap-backed 16 KiB input window without PSRAM.
- Native tests and firmware use the same minimp3 decode loop.
  A positive short read is not EOF.
  A read failure before file end produces an error.
- The stream excludes a trailing ID3v1 tag.
  Complete final frames and short zero padding finish without an error.
  Nonzero junk produces a controlled error.
- Each decode call has a compressed-byte work limit.
  `NeedMore` permits another call when scanning or seek pre-roll needs more work.
- The decoder retains pending PCM between calls and returns stereo int16 frames.
  Mono expansion uses both output slots.
- Position counts frames delivered to the caller, not prefetched frames.
  Player publishes it after output delivery.
  Queued DMA means this position can lead the sound at the DAC.
- Valid Xing/Info frame counts supply frame-based duration.
  A bounded header scan supplies a fallback estimate.
  The time row marks an estimated duration with `~`.
  Unknown duration remains `--:--`.
- A usable Xing TOC supplies approximate time-to-byte mapping.
  Otherwise seek uses a bounded linear estimate.
  Nonzero seeks decode preceding frames within one input window to warm the reservoir.
  Seek-to-zero keeps the original beginning.
  These operations do not provide indexed, sample-exact, or gapless playback.

### WAV (`WavDecoder`)

- The decoder supports mono and stereo 16-bit PCM.
  It duplicates mono into stereo output.
- File access and native tests share one bounded RIFF chunk parser.
  Metadata size does not set parser memory use.
  Data can start beyond 512 bytes or appear before `fmt `.
- The parser checks RIFF, chunk, padding, and whole-frame boundaries.
  It rejects unsupported formats and inconsistent PCM fields.
- Duration and seek use shared 64-bit arithmetic.
  Seek clamps to the data region before narrowing and aligns to a whole frame.
  Failed seeks do not change position.
  Short PCM reads report complete frames and then an error.

### FLAC (`FlacDecoder` + dr_flac)

- The decoder supports native FLAC with one or two channels.
  Input uses 16 or 24 bits per sample at 8000 through 48000 Hz.
  Output uses stereo int16. Mono samples fill both channels.
  Conversion from 24 bits discards the low eight bits.
- STREAMINFO must declare a maximum block size from 16 through 4608 frames.
  The sample count must be known and nonzero.
  Duration must fit 32-bit milliseconds.
  The decoder checks these fields before it opens the library.
- The library uses a bounded allocator with a 64 KiB payload limit.
  Its decoded sample buffer and seek table share that limit.
  The small adapter state, allocation headers, SD state, and task stack are separate.
  The decoder does not allocate a second full-block PCM buffer.
- Open and each decode call have a 64 KiB read limit.
  Seek has a 256 KiB read limit.
  Resource-limit failures stop playback, even for otherwise valid files.
  Positive short reads continue. A zero read before file end is an error.
- Each decoded frame must match the stream format and sample timeline.
  CRC checks stay enabled. A skipped frame produces an error instead of shifted audio.
  Fixed-block frame numbers use the STREAMINFO maximum block size.
  Files that do not fit this mapping produce an error.
- Position counts delivered frames, not library read-ahead.
  Seek uses the declared sample timeline and discards old output through Player.
  Seek to the displayed end finishes without decoding the skipped audio.
  A failed seek preserves the last delivered position and stops the decoder.
  Large sample jumps reset the library first to avoid its 32-bit seek shortcut.
  Only close/open clears a decoder error.
- Unused metadata is skipped. The adapter does not decode album art.
  The pinned revision and checksum are in `lib/dr_flac/provenance.txt`.
  Ogg FLAC is disabled.

The browser lists `.mp3`, `.wav`, and `.flac` audio files.
Unsupported extensions do not open.
Hardware listening and seek-accuracy checks remain unverified.

---

## 7. Threading and cores

| Context | Core | Work |
|---------|------|------|
| Arduino `loop` / UI | 1 (typical) | keys, display, settings, `service()` |
| FreeRTOS `"audio"` | **0** | open/seek/close decoder, decode, output controls and I2S writes |
| I2S DMA / IDF | — | background DMA to GPIO |

The decode step does not run on the UI task:

- minimp3 and path buffers need a large stack.
  The separate audio task prevents these buffers from exceeding the loop task's stack limit.
- Keep blocking `i2s_write` calls out of the keyboard and display loop.

---

## 8. Track navigation

- **Auto-next** — if enabled, the player starts the next `.mp3`, `.wav`, or `.flac`
  in the playing track's folder after the decoder reports `Finished`.
  The browser sorts the listing. It places directories first.
  Next and previous track searches skip directories.
- **Next** (`.` / `N`) — finds the next audio file in the parent folder of
  `currentPath()` with `SdBrowser::nextAudioAfter`. The player then
  restores the visible browser location.
- **Prev** (`;`) — restarts the current track when the position is more
  than 3 s. Otherwise the player finds the previous sibling in the parent
  folder of `currentPath()` and restores the visible browser location.
- **Browser location** — the device restores `browser_path` and
  `browser_item` first. This location is independent of playback.
  The Browser opens root `/` if the folder or selected item does not exist.
- **Last path / boot** — `on_boot=play` opens `last_path` and keeps the
  Browser state. `browse` and `off` show the restored Browser and do not
  autoplay.

---

## 9. Key source files

- `include/config.hpp` — pins, buffer limits, task stack, and priority
- `src/audio_out.cpp` — ES8311 + I2S + PCM gain
- `src/audio_dsp.cpp` — stereo mean and bounded Q15 gain
- `src/player.cpp` — task lifecycle, decode loop, next/prev
- `src/decoders/*` — MP3/WAV/FLAC and shared input operations
- `src/sd_browser.cpp` — SD mount + listing + siblings
- `src/settings.cpp` — persistent settings, last path, and browser location
- `src/app.cpp` — connects UI controls to player/settings

---

## 10. Design choices (summary)

1. **Direct decoder integration.** The player uses minimp3, dr_flac, and a WAV decoder
   instead of ESP32-audioI2S. This gives direct control of PCM and volume.
2. **Software volume.** The DAC volume stays fixed during playback.
   One software curve controls both outputs. The player does not have separate output profiles.
3. **Hardware speaker mute.** The ADV hardware controls the mute.
   The firmware does not drive G46.
4. **Separate audio task.** Decoding runs on core 0.
   This task provides stack space for decoder operations.
5. **Settings on the SD card** (`/.asvmp3/`). Settings survive a firmware
   update. A PC can read the file.

---

## 11. Baseline diagnostics

`AUDIO_DIAG` defaults to zero in `include/config.hpp`.
The `cardputer-adv-diag` environment sets it to one.
Both firmware environments use M5Cardputer 1.1.1, M5Unified 0.2.18,
and M5GFX 0.2.25. M5GFX uses its release commit because version 0.2.25
is not available from the PlatformIO registry used for this build.

The audio task logs format and source channels when it opens a decoder.
`AudioFormat::channels` keeps its existing meaning.
`sourceChannels` reports the input channel count.
The task records the maximum decode duration with unsigned `micros()` subtraction.
Open and exit logs include free and minimum internal heap, the largest free
internal block, and the current task's stack high-water mark in bytes.
These values do not measure the space left on the UI task's stack.

The output logs failed or short I2S writes.
Decoder diagnostics include file byte positions when a frame or read fails.
Seek logs show the old, requested, and returned positions, plus elapsed microseconds.
Each seek log includes a heap and stack snapshot.
Output and decoder failures stop playback and prevent Auto-next.
ES8311 register reads do not add writes or change initialization values.
`i2s_get_clk()` reports the configured rate. Use a logic analyzer to measure
LRCK and BCLK.

Diagnostic timers, memory queries, and diagnostic log messages compile out when
`AUDIO_DIAG=0`. Diagnostics do not change the volume curve, stereo slots,
DMA sizes, decoder buffers, or task ownership.
Normal builds keep output error messages.
The host fixture generator and check command are in `README.md`.
The initial 44.1 kHz baseline log remains available.
The user defers further hardware validation.
