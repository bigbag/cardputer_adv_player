# Audio & device architecture

Cardputer-ADV MP3/WAV player — how sound gets from SD card to speaker / 3.5 mm jack.

This document describes the **implemented** path in firmware (not older design drafts that assumed MCU amp/HP GPIO control).

---

## 1. Hardware (Cardputer-ADV)

```text
                    ┌─────────────────────────────────────────────┐
                    │              ESP32-S3 (Stamp-S3A)           │
                    │                                             │
   microSD ─SPI──►  │  app / browser / settings                   │
                    │       │                                     │
                    │       ▼                                     │
                    │  Player (FreeRTOS audio task @ core 0)      │
                    │       │  PCM stereo int16                   │
                    │       ▼                                     │
                    │  AudioOut  ──I2S TX──►  BCLK/LRCK/DOUT      │
                    │       │                                     │
                    │       └──I2C ctrl──►  ES8311 @ 0x18         │
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

On Cardputer-ADV the **3.5 mm jack insertion mutes the speaker amp in hardware**
(MOSFET / mechanical detect on the jack switch). The MCU does **not** drive a
reliable software amp-enable for this product path:

- **Do not** treat **G46** as `AMP_EN` in this firmware — on ADV it is ES8311
  **ASDOUT** (mic ADC), not amp control.
- There is **no software jack-detect** path in the player. One volume curve
  covers both outputs; user sets level for headphones (low) or speaker (high).

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
- Clock — start 4 MHz, step up if mount allows

**Display / keys** (not on the audio path, for context)

- 240×135 LCD via M5GFX / M5Cardputer
- Keyboard via onboard controller (M5Cardputer.Keyboard)

---

## 2. Software stack

```text
┌──────────────────────────────────────────────────────────┐
│  App (Arduino loopTask, typically core 1)                │
│    UI · Input · Settings · SdBrowser · Player.service()  │
└────────────────────────────┬─────────────────────────────┘
                             │ open / pause / seek / vol / next
                             ▼
┌──────────────────────────────────────────────────────────┐
│  Player                                                  │
│    owns path, state, volume snapshot                     │
│    spawns FreeRTOS task "audio" @ core 0                 │
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
            ES8311 DAC  (reg 0x32 fixed 0xBF)
```

### 2.1 Modules

- **`App`** (`src/app.cpp`) — screen state machine (Browse / Playing / Settings);
  applies settings; remembers `last_path`; calls `Player` controls.
- **`Player`** (`src/player.cpp`) — open/stop/pause/seek/next/prev; FreeRTOS
  decode task; auto-next handoff via `service()` on the loop task.
- **`IDecoder`** (`src/decoders/decoder.hpp`) — common interface:
  `open` / `decode` / `seekMs` / `positionMs` / `format`.
- **`Mp3Decoder`** — vendored **minimp3**; SD stream → PCM.
- **`WavDecoder`** — 16-bit PCM WAV only (mono upmixed to stereo).
- **`AudioOut`** (`src/audio_out.cpp`) — ES8311 I2C bring-up, I2S master TX,
  volume multiply, optional test beep.
- **`SdBrowser`** — FAT listing (`readdir`), folder navigation, next/prev audio
  siblings for playlist-style skip.
- **`Settings`** — `/.asvmp3/config.cfg` on SD (volume, theme, last path, …).

---

## 3. Playback data path (runtime)

1. User selects a file → `App::playSelection()` → `Player::open(absPath)`.
2. `Player::open`:
   - stops any previous audio task
   - stores path/name
   - creates FreeRTOS task `"audio"` pinned to **core 0**, stack
     `cfg::kAudioTaskStack` (24 KiB), priority `cfg::kAudioTaskPrio`.
3. **Audio task** (`Player::audioTaskMain`):
   - opens decoder **on this task** (minimp3 needs stack; avoids loopTask overflow)
   - reads `AudioFormat` (sample rate, duration, channels)
   - `AudioOut::setSampleRate(hz)` — reinstalls I2S if rate changed
   - loop:
     - honor `paused_` / `stopReq_` / `seekDeltaMs_` atomics
     - `decoder_->decode(pcmBuf_, 512, &got)`
     - `out_->write(pcmBuf_, got)` → scaled PCM → `i2s_write`
     - on `Finished` → set `Done`, optionally set `autoNextPending_`
4. **Loop task** `Player::service()`:
   - if auto-next pending → `nextTrack()` → new `open()`
5. `App` persists `last_path` when the playing path changes.

### 3.1 Control plane (atomics)

UI never blocks on I2S. Controls are flags:

- `stopReq_` — tear down task
- `paused_` — spin with short delay (no PCM out)
- `seekDeltaMs_` — accumulated relative seek, applied in decode loop
- `autoNextPending_` — finished track; loop task starts next file

Volume is set on `Player` and pushed into `AudioOut` (recomputes multiply factors).

---

## 4. I2S / ES8311 configuration

### 4.1 I2S (ESP-IDF legacy driver)

- Port: `I2S_NUM_0`
- Mode: master TX
- Format: standard I2S, 16-bit, stereo (`I2S_CHANNEL_FMT_RIGHT_LEFT`)
- DMA: 8 buffers × 256 samples
- Default rate: **44100** Hz; reconfigured per track (e.g. 48000, 22050)

### 4.2 ES8311 init sequence

Written over I2C at begin (`AudioOut::esInitRegisters`):

| Reg  | Value | Role (summary)         |
|------|-------|------------------------|
| 0x00 | 0x80  | Reset / clock domain   |
| 0x01 | 0xB5  | Clock manager          |
| 0x02 | 0x18  | Clock config           |
| 0x0D | 0x01  | System                 |
| 0x12 | 0x00  | ADC power (off/mic N/A)|
| 0x13 | 0x10  | DAC power / route      |
| 0x32 | 0xBF  | DAC volume (fixed hot) |
| 0x37 | 0x08  | DAC misc               |

**DAC digital volume (0x32) is held fixed at `0xBF`.** User volume is applied in
software on PCM samples so the curve can stay wide and predictable.

---

## 5. Volume model

Single UI volume **0–100%** (step `kVolumeStepPercent`, default 2%).

```text
effective_sample = clamp16(
    sample * (UI/100)^exp * kVolPcmBoost
)
```

Implemented as integer fraction `mulNum_ / mulDen_` in `AudioOut::recomputeMul()`:

- `exp = kVolCurveExpNum` (**3**, cubic) — large quiet zone for headphones
- `boost = kVolPcmBoost` (**3**) — headroom at the top for the small speaker
- DAC reg stays `0xBF`

Rough guide:

- **UI ~10–45** — headphones / quiet
- **UI ~55–80** — speaker
- **UI ~85–100** — loud speaker (may clip hot masters; samples are saturated)

Default UI volume: **30%**.

Serial on change:

```text
[audio] ui=30% eff~…% (×num/den)
```

---

## 6. Decode formats

### MP3 (`Mp3Decoder` + minimp3)

- Stream from SD in chunks
- Output always **stereo int16** frames to `AudioOut`
- Seek is approximate (byte/time map depending on decoder implementation)

### WAV (`WavDecoder`)

- PCM **16-bit** only
- Mono → duplicated to L/R
- Non-PCM / bad headers → open fails → toast via `Player::takeError`

Unsupported extensions never enter the decoder (browser filters `.mp3` / `.wav`).

---

## 7. Threading & cores

| Context | Core | Work |
|---------|------|------|
| Arduino `loop` / UI | 1 (typical) | keys, display, settings, `service()` |
| FreeRTOS `"audio"` | **0** | open decoder, decode, I2S write |
| I2S DMA / IDF | — | background DMA to GPIO |

Why decode is not on the UI task:

- minimp3 + path buffers need **large stack** (canary crashes were seen on loop stack)
- Blocking `i2s_write` must not stall keyboard/display

---

## 8. Track navigation (audio-adjacent)

- **Auto-next** — on decode `Finished`, next `.mp3`/`.wav` in the **same folder**
  (sorted listing; dirs first in UI but skipped for next/prev audio).
- **Next** (`.` / `N`) — `SdBrowser::nextAudioAfter`
- **Prev** (`;` / `P`) — if position > 3 s restart current; else previous file
- **Last path** — `Settings::lastPath` in `/.asvmp3/config.cfg`; on boot
  `SdBrowser::revealPath` + auto `Player::open`

---

## 9. Key source files

- `include/config.hpp` — pins, volume curve knobs, task stack/prio
- `src/audio_out.cpp` — ES8311 + I2S + PCM gain
- `src/player.cpp` — task lifecycle, decode loop, next/prev
- `src/decoders/*` — MP3/WAV
- `src/sd_browser.cpp` — SD mount + listing + siblings
- `src/settings.cpp` — persistent volume / last path
- `src/app.cpp` — wires UI controls to player/settings

---

## 10. Design choices (summary)

1. **Custom decode stack** (minimp3 + WAV) instead of ESP32-audioI2S-first —
   smaller, explicit control of PCM and volume.
2. **Software volume only** + fixed hot DAC — one wide curve; no Spk/HP profiles.
3. **No MCU amp/HP GPIO** — match real ADV schematic behavior; avoid driving G46.
4. **Decode on dedicated core-0 task** — stability under minimp3 stack use.
5. **Settings on SD** (`/.asvmp3/`) — survives flash; inspectable on a PC.

---