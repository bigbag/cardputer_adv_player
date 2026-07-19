# cardputer_asv_mp3

Minimal MP3/WAV player for **M5Stack Cardputer-ADV** (ESP32-S3 / Stamp-S3A).

Browse a FAT32 microSD card, play MP3 and 16-bit PCM WAV through the ES8311 codec
(speaker or 3.5 mm jack), terminal-green UI on the 240×135 display.

## Requirements

- Cardputer-ADV hardware
- [PlatformIO Core](https://platformio.org/) 6.x
- Python **3.10–3.13** for PlatformIO (3.14+ is not supported — the Makefile auto-picks
  `python3.12 -m platformio` when needed)
- FAT32 microSD with `.mp3` / `.wav` files

## Quick start

```bash
make build          # compile firmware
make flash          # build + upload
make monitor        # serial log @ 115200
make test           # host unit tests
make help           # all targets
```

Override the PlatformIO launcher if needed:

```bash
make build PIO="python3.12 -m platformio"
make flash PIO="python3.12 -m platformio"
```

Equivalent raw PlatformIO:

```bash
pio run -e cardputer-adv
pio run -e cardputer-adv -t upload
pio device monitor -b 115200
pio test -e native
```

### Download mode (Cardputer-ADV)

If the port does not appear or upload fails:

1. Power switch **OFF**
2. Hold **G0**, power **ON**, release G0
3. `make flash`
4. Power-cycle normally

### Charging

Leave the side power switch **ON** while charging.

## SD card format

Use **FAT32** (or FAT16 on very small cards). The Arduino `SD` library does **not**
mount **exFAT**.

Practical tips if you see **No SD card**:

1. Format as **FAT32**, MBR partition table (not exFAT / not APFS).
2. Prefer a full-size format from a PC (Windows “FAT32”, macOS Disk Utility MS-DOS
   (FAT), or `mkfs.vfat`). Avoid “quick” quirks on some cameras.
3. Use a known-good card ≤32 GB if possible (64 GB+ often ships as exFAT).
4. Seat the card fully; power-cycle after insert.
5. On the Browse screen press **Enter** to retry mount.
6. Watch serial (`make monitor`) for `[sd] mount ok/fail @ … Hz` lines.

## Suggested SD layout

```text
/music/
  album1/
    track01.mp3
  album2/
    intro.wav
```

Any folder tree works; the browser starts at SD root. Hidden `.*` names are skipped.

## Controls

Authoritative map (matches firmware `src/input.cpp`):

### Navigation diamond (physical keys)

```text
       ;          up
    ,  .  /     left  down  right
```

| Key | Meaning |
|-----|--------|
| `;` | **Up** |
| `.` | **Down** |
| `,` | **Left** = decrease / vol down |
| `/` | **Right** = increase / vol up |

### Browse

| Key | Action |
|-----|--------|
| `;` / `.` | Cursor up / down |
| `Enter` | Open folder or play file |
| `Space` | Play highlighted audio file |
| `S` or `Tab` | **Settings** |
| `Backspace` or `` ` `` | Parent directory |
| `Enter` when no SD | Retry mount |

> No Esc on Cardputer — use **Backspace** (or `` ` ``) for Back.

### Now Playing

| Key | Action |
|-----|--------|
| `Space` | Play / pause |
| `,` | Volume down |
| `/` | Volume up |
| `[` / `]` | Seek −/+ ~5 s |
| `S` or `Tab` | **Settings** |
| `Backspace` | Back to browse (audio keeps playing) |

### Settings (`S` / Tab)

| Item | Left `,` / Right `/` |
|------|----------------------|
| Volume | − / + 5% |
| Brightness | dimmer / brighter |
| Scr timeout | cycle 5s → 10s → 30s → 60s → never |
| Auto-next | toggle ON / OFF |
| Theme | Phosphor / Amber / Cyan / Paper |

| Key | Action |
|-----|--------|
| `;` / `.` | Move row |
| `,` / `/` | Decrease / increase |
| `Enter` | Toggle timeout / auto-next |
| `Backspace` / `S` | Save & exit |

Settings are stored in **NVS** and restored on boot.

Starting another file from Browse replaces the current track. When a track ends, the
next audio file in the **same folder** auto-plays; if none remain, status shows `DONE`.

## Display

- Backlight turns **off after 10 seconds** with no key presses (audio keeps playing).
- Any key wakes the screen; the same key is also handled normally.
- Playing screen updates the time/progress bar without full-screen redraws (less blink).

## Audio notes

- Formats: MP3 (minimp3), WAV PCM 16-bit mono/stereo
- Output: ES8311 + I2S; amp enable / headphone detect handled in software
- Plugging headphones should mute the speaker amp

## On-device checklist

1. Boot with no SD → clear “No SD” UI; Enter retries  
2. Nested folders; parent at root stays root  
3. Play MP3 44.1 kHz and 48 kHz if available  
4. Play 16-bit WAV mono/stereo  
5. Reject bad/non-PCM WAV with toast  
6. Pause / resume, volume, seek  
7. Auto-next within folder; last file → DONE  
8. Esc to Browse while audio continues; open new file switches track  
9. Headphone jack mutes speaker amp  
10. Serial 115200 shows mount/open/errors  

**Hardware validation status:** host tests and firmware build pass; full on-device
checklist not yet signed off in this environment.

## Project layout

```text
include/          config, actions, types, path_utils
src/              app, ui, input, player, sd_browser, audio_out, decoders/
lib/minimp3/      vendored minimp3.h
test/             native Unity tests
docs/superpowers/ design spec + implementation plan
Makefile          build / flash / test wrappers
platformio.ini    cardputer-adv + native envs
```

## Architecture (short)

| Module | Role |
|--------|------|
| `App` | Browse ↔ Playing state machine |
| `SdBrowser` | SD mount + directory listing |
| `Player` | FreeRTOS decode task, auto-next |
| `AudioOut` | ES8311 + I2S + amp/HP |
| `Mp3Decoder` / `WavDecoder` | Decode to stereo PCM |
| `Ui` / `Input` | Terminal UI + keys |

## Spec / plan

- Design: `docs/superpowers/specs/2026-07-19-cardputer-adv-mp3-player-design.md`
- Plan: `docs/superpowers/plans/2026-07-19-cardputer-adv-mp3-player.md`
