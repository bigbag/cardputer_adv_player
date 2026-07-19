# Cardputer-ADV Minimal MP3 Player — Design Spec

**Date:** 2026-07-19  
**Hardware:** M5Stack Cardputer-ADV (K132-Adv)  
**Status:** Approved for implementation planning

## Goal

Build a minimal pocket MP3/WAV player for the Cardputer-ADV that browses a microSD card, plays audio through the onboard ES8311 path (speaker or 3.5 mm jack), and uses a simple terminal-style UI on the 240×135 display with on-screen key hints.

## Hardware Context

| Item | Detail |
|------|--------|
| SoC | ESP32-S3FN8 (Stamp-S3A), dual-core, 240 MHz, 8 MB flash |
| Display | ST7789V2 1.14", 240×135 |
| Keyboard | 56-key via TCA8418 (I2C) |
| Audio | ES8311 codec + NS4150B amp + 1 W speaker + 3.5 mm jack |
| Storage | microSD (SPI) |
| USB ID | `303a:1001` (Espressif) |

### Pin map (M5 docs + working Adv firmware)

**LCD (ST7789V2)**

| Function | GPIO |
|----------|------|
| DISP_BL / RGB PWR_EN | G38 |
| RST | G33 |
| RS | G34 |
| DAT | G35 |
| SCK | G36 |
| CS | G37 |

**Audio (ES8311) — playback path**

| Function | GPIO | Notes |
|----------|------|-------|
| I2C SDA | G8 | Shared with keyboard/IMU |
| I2C SCL | G9 | Shared with keyboard/IMU |
| I2S BCLK (SCLK) | G41 | |
| I2S LRCK | G43 | |
| I2S DOUT (DSDIN to codec DAC) | G42 | ESP32 → ES8311 |
| Headphone detect | G17 | LOW when jack inserted (community-verified on Adv) |
| Amp enable (NS4150B) | G46 | HIGH = speaker amp on; keep LOW if HP inserted |

Official M5 pinmap also labels G46 as ES8311 ASDOUT (mic ADC). For a player we treat G46 as **amp enable**, matching working Adv firmware ([CardPuter_Mp3_Adv](https://github.com/vicliu624/CardPuter_Mp3_Adv)). Mic input is out of scope for v1.

ES8311 I2C address: `0x18`. TCA8418 keyboard: `0x34`.

**microSD (SPI)**

| Function | GPIO |
|----------|------|
| CS | G12 |
| MOSI | G14 |
| CLK | G40 |
| MISO | G39 |

**Other**

| Function | GPIO |
|----------|------|
| Keyboard INT (TCA8418) | G11 |
| Battery ADC | G10 |

Note: Prefer explicit amp enable + HP detect handling in software (mute amp when G17 is LOW). Do not assume jack hardware alone is sufficient.

## Scope

### In scope (v1)

- Browse SD card directories
- Play `.mp3` and `.wav` (16-bit PCM WAV)
- Play / pause, next-in-folder on track end, volume, coarse seek
- File-browser-first UI with now-playing screen
- Terminal green visual theme
- Permanent one-line key hint bar
- PlatformIO + Arduino framework + M5Cardputer for board bring-up
- Custom audio path: minimp3 + simple WAV parser → I2S → ES8311

### Out of scope (v1)

- ID3 tags / cover art
- Playlists, shuffle, repeat-all
- Equalizer
- Resume last track across reboot
- Wi-Fi / BLE features
- Formats beyond MP3 and 16-bit PCM WAV
- LVGL or other heavyweight UI toolkits

## User Experience

### Screen flow

```
Boot → mount SD
         ├─ fail → Browse with "No SD card" + retry hint
         └─ ok   → Browse at SD root

Browse
  Enter on dir     → enter directory
  Enter/Space file → stop current (if any), start track, go Now Playing
  Esc / `          → parent directory (root stays root)

Now Playing
  Space            → play / pause
  , / .            → volume down / up
  [ / ]            → seek −5s / +5s (approx)
  Esc / `          → return to Browse; audio keeps playing
  open new file    → replaces current track
  track ends       → auto-play next audio file in same folder
  folder exhausted → stop, show DONE on Now Playing
```

### Browse screen

- Path line: `SD:/music/`
- List: directories first (alphabetical), then `.mp3`/`.wav` (alphabetical, case-insensitive)
- Hide `.*` entries
- Cursor highlight on selected row
- Cap listing at 256 entries per directory; if truncated, show a short notice
- Hint bar: `^v move  Ent open  Esc up`

### Now Playing screen

- Status: `PLAY` / `PAUSE` / `DONE`
- Filename (truncate or marquee if longer than width)
- Elapsed / duration and a simple progress bar
- Volume percent
- Hint bar: `Spc pause  [] seek  ,. vol  Esc list`

### Errors

Short toast/overlay (~1–2 s) for: no SD, unsupported WAV, decode failure, out of memory, repeated underrun. After serious play errors, return to idle and Browse (or stay on Now Playing with clear idle state when appropriate).

### Visual theme

- Background: black
- Foreground: green (`#00FF00` family)
- Dim green for secondary text / hints
- Inverted green-on-black (or filled bar) for selection
- Monospace / built-in font suitable for dense 240×135 layout

## Architecture

### Approach

**M5Cardputer + custom decode/output path** (not ESP8266Audio as primary):

- M5Cardputer / M5GFX / M5Unified: display, keyboard, power, SD SPI convenience where useful
- **Mp3Decoder:** minimp3
- **WavDecoder:** minimal RIFF/WAV header parser (PCM 16-bit mono/stereo)
- **AudioOut:** ES8311 I2C setup + I2S TX to DAC
- App-level state machine coordinates UI and playback

### Modules

| Module | Responsibility | Depends on |
|--------|----------------|------------|
| `App` | State machine (`Browse` / `Playing`), wires events | Input, Ui, SdBrowser, Player |
| `Input` | Poll Cardputer keyboard → typed actions | M5Cardputer |
| `Ui` | Render Browse, Now Playing, toasts, hint bar | M5 display |
| `SdBrowser` | Mount SD, list/navigate dirs, “next file in folder” | SD/FS |
| `Player` | Open/stop/pause, progress, volume, auto-next orchestration | Decoders, AudioOut, SdBrowser |
| `AudioOut` | ES8311 + I2S, sample-rate reconfig, hardware volume | I2C, I2S driver |
| `Mp3Decoder` | MP3 frames → PCM | minimp3, SD file stream |
| `WavDecoder` | WAV header + PCM read | SD file stream |

### Data flow

**Playback**

```
Input → App → Player.open(path)
                → detect format
                → decoder reads SD into compressed buffer
                → PCM frames into queue / ring buffer
                → AudioOut writes I2S → ES8311 → amp/jack
```

**UI**

```
App state + SdBrowser snapshot + Player snapshot → Ui.render()
```

### Concurrency

- **Audio task (FreeRTOS):** decode + feed I2S; higher priority than UI
- **Loop / UI path:** input polling + redraw on change (or modest frame rate)
- No long SD directory scans on the audio task
- Player controls (pause/stop/seek/volume) communicated via atomics/flags or a small mutex; never hold a mutex across blocking I2S waits longer than necessary

### Key bindings

| Key | Browse | Playing |
|-----|--------|---------|
| `↑` `↓` | Move cursor | — |
| `Enter` | Open dir or play file | — |
| `Esc` or `` ` `` | Parent directory | Back to Browse (audio continues) |
| `Space` | Play file if audio selected | Play / pause |
| `,` `.` | — | Volume down / up (~5% steps) |
| `[` `]` | — | Seek −5 s / +5 s |
| `Tab` | — | Optional jump to Browse (same as Esc) |

## Audio details

- Output format to codec: 16-bit, stereo I2S (mono sources upmixed)
- Default 44100 Hz; on track open, if rate differs (e.g. 48000, 22050), reconfigure I2S/ES8311 before starting
- Volume: ES8311 DAC level and/or digital scale; show 0–100% on UI
- MP3: frame-by-frame via minimp3 from a read-ahead SD buffer
- WAV: accept PCM 16-bit mono/stereo; reject other encodings with toast
- Seek: best-effort byte/time mapping (more accurate for WAV/CBR; acceptable drift on VBR MP3 for v1)
- End of track: `Player` requests next audio-only sibling from `SdBrowser` in current directory order; skips subfolders; if none, `DONE`
- Amp policy: on boot and on HP detect change, set amp enable HIGH only when headphones are not inserted
- Optional boot beep (short I2S tone) behind a debug flag to validate codec path before SD play

### ES8311 bring-up (from working Adv player)

Minimal register poke sequence used successfully on Cardputer-Adv (verify against datasheet during implementation; keep isolated in `AudioOut`):

| Reg | Val | Intent (approx.) |
|-----|-----|------------------|
| 0x00 | 0x80 | Reset / clock ctrl |
| 0x01 | 0xB5 | Clock manager |
| 0x02 | 0x18 | Clock / sample config |
| 0x0D | 0x01 | System |
| 0x12 | 0x00 | DAC power |
| 0x13 | 0x10 | ADC/DAC (DAC path) |
| 0x32 | 0xBF | DAC volume-ish |
| 0x37 | 0x08 | DAC misc |

I2S: master TX, standard I2S, 16-bit, stereo, pins BCLK=41, LRCK=43, DOUT=42.

## SD / filesystem

- SPI pins as in pin map
- Mount on boot; allow remount/retry from Browse when card was missing
- Case-insensitive extension filter: `.mp3`, `.wav`
- Max 256 listed entries per directory (configurable)
- Paths stored as POSIX-style strings from mount point (e.g. `/music/track.mp3`)

## Project layout

```
cardputer_asv_mp3/
  platformio.ini
  src/
    main.cpp
    app.cpp / app.hpp
    input.cpp / input.hpp
    ui.cpp / ui.hpp
    sd_browser.cpp / sd_browser.hpp
    player.cpp / player.hpp
    audio_out.cpp / audio_out.hpp
    decoders/
      mp3_decoder.cpp / mp3_decoder.hpp
      wav_decoder.cpp / wav_decoder.hpp
  include/
    config.hpp          # pins, limits, colors, task sizing
    actions.hpp         # input action enum / events
    types.hpp           # shared structs (DirEntry, PlayerSnapshot, …)
  lib/                  # vendored minimp3 or PlatformIO lib dep
  docs/superpowers/specs/
  README.md
```

### `platformio.ini` (intent)

- Platform: `espressif32@6.7.0` (known-good with M5Cardputer on Adv)
- Board: `m5stack-stamps3` (matches Stamp-S3A / working Adv projects)
- Framework: Arduino
- Flags: `ARDUINO_USB_CDC_ON_BOOT=1`, `ARDUINO_USB_MODE=1`, `ESP32S3` as needed
- `upload_speed = 1500000`
- Lib deps: `m5stack/M5Cardputer@^1.0.3` (pulls M5Unified/M5GFX)
- minimp3: single-header vendored under `lib/` or `include/`
- Do **not** pull FastLED/NeoPixel/ESP32Time unless a feature needs them

## Configuration (`include/config.hpp`)

- GPIO pin constants (as documented above)
- `kMaxDirEntries = 256`
- `kVolumeStepPercent = 5`
- `kSeekStepSeconds = 5`
- UI colors (black / green / dim green)
- Audio task stack size and priority
- SD SPI frequency (start conservative, e.g. 10–25 MHz, tune later)

## Error handling

| Condition | Behavior |
|-----------|----------|
| SD mount fail | Browse empty state + message; retry on key |
| Unsupported / corrupt file | Toast; do not crash; player idle |
| OOM on buffers | Refuse start; toast |
| I2S underrun | Log; attempt recover; repeated → stop + toast |
| Seek near EOF | Clamp; may trigger track end logic |

## Testing

### Build

- `pio run` succeeds for the Cardputer-ADV environment

### On-device checklist

1. Boot with no SD → clear error UI  
2. Browse nested folders; parent navigation at root is safe  
3. Play MP3 44.1 kHz and 48 kHz (mono and stereo if available)  
4. Play 16-bit WAV mono/stereo  
5. Reject bad WAV with toast  
6. Pause / resume; volume steps; seek forward/back  
7. Auto-next within folder; last file → DONE  
8. Esc to Browse while audio continues; opening another file switches track  
9. Headphone insert routes to jack / mutes speaker (hardware)  
10. Serial logs (115200) show mount, open, rate, and errors  

### Success criteria

- Can browse SD and play MP3/WAV on speaker or jack  
- UI is readable in terminal green with permanent hints  
- Controls usable on the stock keyboard without a manual  
- Stable on missing SD, bad files, and end-of-folder  

## Implementation notes

- Prefer fixed buffers allocated at startup for decode/PCM rings; check largest free heap before large allocs  
- Keep UI draw simple (full or dirty-region redraw); avoid large framebuffers beyond what M5GFX already uses  
- RGB LED power shares G38 with backlight enable logic on Stamp-S3A — do not break backlight when touching G38  
- Keyboard uses TCA8418 on Adv (not the older 74HC138 matrix). Prefer M5Cardputer’s ADV board detection (`board_M5CardputerADV`) and TCA8418 reader path; fall back to I2C probe of `0x34` / `0x18` if needed  
- Isolate all codec register setup and amp/HP GPIO in `AudioOut` with verbose serial logging  
- SD: FAT32; browse full tree (not only `/music`) per product choice — `/music` may be documented as a suggested layout only  

## Prior art (reference only)

[vicliu624/CardPuter_Mp3_Adv](https://github.com/vicliu624/CardPuter_Mp3_Adv) (fork of Moo93egy) is a **working** richer player on Cardputer-Adv:

- PlatformIO + `m5stack-stamps3` + M5Cardputer  
- Audio via **ESP32-audioI2S** (not minimp3) after ES8311/AW88298 board init  
- Winamp-like UI, ID3/cover, CJK fonts, spectrum, delete, screenshots — **out of scope** for our v1  

We reuse **hardware facts** (pins, ES8311 sequence, board/keyboard detection patterns, `platformio.ini` baselines) as reference. We do **not** fork that UI or adopt ESP32-audioI2S as the primary stack (Approach 2: custom minimp3 + WAV + ES8311/I2S). If custom decode stalls on codec stability, ESP32-audioI2S remains a documented fallback behind the same `AudioOut` pin config.

## Decisions log

| Topic | Choice |
|-------|--------|
| Product scope | Minimal pocket player (browse + transport + volume) |
| Toolchain | PlatformIO + Arduino |
| Formats | MP3 + WAV |
| UI structure | File browser first, then now-playing |
| Theme | Terminal green |
| Hints | Always-on hint bar |
| End of track | Auto-next in current folder (no loop) |
| Esc while playing | Back to browser, audio continues |
| Audio stack | Custom minimp3 + WAV + ES8311/I2S (not ESP8266Audio / not ESP32-audioI2S-first) |
| Prior art | Learn pins/init from CardPuter_Mp3_Adv; do not fork its feature set |
