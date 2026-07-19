# cardputer_asv_mp3

Minimal MP3/WAV player for **M5Stack Cardputer-ADV** (ESP32-S3 / Stamp-S3A).

Browse a FAT32 microSD card, play MP3 and 16-bit PCM WAV through the ES8311 codec (speaker or 3.5 mm jack), terminal-green UI on the 240×135 display.

## Requirements

- Cardputer-ADV hardware
- PlatformIO Core 6.x
- Python **3.10–3.13** for `pio` (if system Python is 3.14+, use e.g. `python3.12 -m platformio …`)
- FAT32 microSD with `.mp3` / `.wav` files

## Build / flash / monitor

```bash
# Build
python3.12 -m platformio run -e cardputer-adv
# or: pio run -e cardputer-adv   # when default Python is 3.10–3.13

# Upload (device in download mode if needed — see below)
python3.12 -m platformio run -e cardputer-adv -t upload

# Serial log
python3.12 -m platformio device monitor -b 115200
```

### Download mode (Cardputer-ADV)

1. Power switch **OFF**
2. Hold **G0**, power **ON**, release G0
3. Upload firmware
4. Power-cycle normally

### Charging

Leave the side power switch **ON** while charging.

## Host unit tests

```bash
python3.12 -m platformio test -e native
```

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

### Browse

| Key | Action |
|-----|--------|
| `;` | Cursor up |
| `/` | Cursor down |
| `Enter` | Open folder or play file |
| `Space` | Play highlighted audio file |
| `Esc` or `` ` `` | Parent directory |
| `Enter` when no SD | Retry mount |

Hint bar: `;/ move  Ent open  Esc up`

### Now Playing

| Key | Action |
|-----|--------|
| `Space` | Play / pause |
| `,` | Volume down (~5%) |
| `.` | Volume up (~5%) |
| `[` | Seek back ~5 s |
| `]` | Seek forward ~5 s |
| `Esc` or `` ` `` | Back to browse (**audio keeps playing**) |

Hint bar: `Spc pause  [] seek  ,. vol  Esc list`

Starting another file from Browse replaces the current track. When a track ends, the next audio file in the **same folder** auto-plays; if none remain, status shows `DONE`.

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

**Hardware validation status:** code builds and host tests pass; full on-device checklist not yet signed off in this environment.

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
