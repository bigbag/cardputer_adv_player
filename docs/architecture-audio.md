# Audio and device architecture

This document describes the audio path of the Cardputer-ADV MP3/WAV player.
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
   Mp3Decoder          WavDecoder          SdBrowser
   (minimp3)           (PCM 16-bit)        next/prev path
          │                  │
          └────────┬─────────┘
                   │ int16 stereo frames
                   ▼
            AudioOut::write()
                   │  digital gain (curve × boost)
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

1. The user selects a file. `App::playSelection()` calls `Player::open(absPath)`.
2. `Player::open`:
   - stops the previous audio task
   - stores the path and the name
   - creates the FreeRTOS task `"audio"` pinned to **core 0**, with stack
     `cfg::kAudioTaskStack` (24 KiB) and priority `cfg::kAudioTaskPrio`
3. **Audio task** (`Player::audioTaskMain`):
   - opens the decoder on this task. minimp3 needs stack space. This
     protects the loopTask stack from overflow.
   - reads the `AudioFormat` data (sample rate, duration, channels)
   - calls `AudioOut::setSampleRate(hz)`. This reinstalls I2S when the rate
     changes.
   - loop:
     - checks the `paused_`, `stopReq_`, and `seekDeltaMs_` atomic variables
     - calls `decoder_->decode(pcmBuf_, 512, &got)`
     - calls `out_->write(pcmBuf_, got)` to scale PCM samples and send them through `i2s_write`
     - sets `Done` when the decoder reports `Finished`
     - sets `autoNextPending_` if Auto-next is ON
4. **Loop task** `Player::service()`:
   - calls `nextTrack()` when an automatic track change is pending
   - calls `open()` if the search finds a next track
5. `App` saves `last_path` when the playing path changes.

### 3.1 Playback controls

The audio task performs I2S writes. The controls use atomic variables:

- `stopReq_` requests that the audio task stop.
- `paused_` makes the task wait with a short delay and no PCM output.
- `seekDeltaMs_` stores the total relative seek adjustment. The decode loop applies it.
- `autoNextPending_` tells the application loop that a track ends with Auto-next enabled.
  The loop starts the next file if one exists.

`Player` sends the volume setting to `AudioOut`.
`AudioOut` calculates the gain factors.

---

## 4. I2S / ES8311 configuration

### 4.1 I2S (ESP-IDF legacy driver)

- Port: `I2S_NUM_0`
- Mode: master TX
- Format: standard I2S, 16-bit, stereo (`I2S_CHANNEL_FMT_RIGHT_LEFT`)
- DMA: 8 buffers × 256 samples
- Default rate: **44100** Hz. The code reconfigures the rate per track (for
  example 48000 or 22050).

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

The UI has one volume control from 0 to 100%. The step is
`kVolumeStepPercent` (default 2%).

```text
effective_sample = clamp16(
    sample * (UI/100)^exp * kVolPcmBoost
)
```

`AudioOut::recomputeMul()` implements the factor as the integer fraction
`mulNum_ / mulDen_`:

- `exp = kVolCurveExpNum` (**3**, cubic). The curve permits small volume changes at low headphone levels.
- `boost = kVolPcmBoost` (**3**). The boost increases output at high speaker levels.
- During playback, the DAC register stays at `0xBF`.

Approximate levels:

- **UI ~10–45** — headphones, quiet listening
- **UI ~55–80** — speaker
- **UI ~85–100** — loud speaker. Audio with loud mastering can clip. The
  samples saturate.

Default UI volume: **30%**.

The player prints this serial message on change:

```text
[audio] ui=30% eff~…% (×num/den)
```

---

## 6. Decode formats

### MP3 (`Mp3Decoder` + minimp3)

- Streams from the SD card in chunks
- Always outputs stereo int16 frames to `AudioOut`
- Seek is approximate. Accuracy depends on the byte/time map of the decoder
  implementation.

### WAV (`WavDecoder`)

- Supports PCM **16-bit** only
- Duplicates mono to the left and right channels
- The decoder rejects non-PCM data and invalid headers.
  `Player::takeError()` supplies the error message. `App` displays the message.

The player rejects unsupported file extensions.
The browser lists `.mp3` and `.wav` audio files.

---

## 7. Threading and cores

| Context | Core | Work |
|---------|------|------|
| Arduino `loop` / UI | 1 (typical) | keys, display, settings, `service()` |
| FreeRTOS `"audio"` | **0** | open decoder, decode, I2S write |
| I2S DMA / IDF | — | background DMA to GPIO |

The decode step does not run on the UI task:

- minimp3 and path buffers need a large stack.
  The separate audio task prevents these buffers from exceeding the loop task's stack limit.
- Keep blocking `i2s_write` calls out of the keyboard and display loop.

---

## 8. Track navigation

- **Auto-next** — if enabled, the player starts the next `.mp3` or `.wav`
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

- `include/config.hpp` — pins, volume curve settings, task stack and
  priority
- `src/audio_out.cpp` — ES8311 + I2S + PCM gain
- `src/player.cpp` — task lifecycle, decode loop, next/prev
- `src/decoders/*` — MP3/WAV
- `src/sd_browser.cpp` — SD mount + listing + siblings
- `src/settings.cpp` — persistent settings, last path, and browser location
- `src/app.cpp` — connects UI controls to player/settings

---

## 10. Design choices (summary)

1. **Custom decode implementation.** The player uses minimp3 and a WAV decoder
   instead of ESP32-audioI2S. This gives direct control of PCM and volume.
2. **Software volume.** The DAC volume stays fixed during playback.
   One software curve controls both outputs. The player does not have separate output profiles.
3. **Hardware speaker mute.** The ADV hardware controls the mute.
   The firmware does not drive G46.
4. **Separate audio task.** Decoding runs on core 0.
   This task provides stack space for minimp3.
5. **Settings on the SD card** (`/.asvmp3/`). Settings survive a firmware
   update. A PC can read the file.

---
