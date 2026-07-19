# cardputer_asv_mp3

Minimal MP3/WAV player for M5Stack Cardputer-ADV.

## Build

```bash
pio run -e cardputer-adv
```

## Upload / monitor

```bash
pio run -e cardputer-adv -t upload
pio device monitor -b 115200
```

## Tests (host)

```bash
pio test -e native
```

## Controls

### Browse screen

| Key       | Action                          |
|-----------|---------------------------------|
| `;` / Up  | Move cursor up                 |
| `.` / Down| Move cursor down               |
| Enter     | Open folder / play file        |
| Space     | Play highlighted audio file    |
| Esc       | Go to parent folder            |
| Enter (SD fail) | Retry SD mount            |

### Now Playing screen

| Key       | Action                          |
|-----------|---------------------------------|
| Space     | Toggle pause/resume            |
| `]`       | Volume up (+5%)                |
| `[`       | Volume down (-5%)              |
| `,`       | Seek back 5 s                  |
| `/`       | Seek forward 5 s               |
| Esc       | Return to browse (audio keeps playing) |

## On-device checklist

1. Flash firmware with `pio run -e cardputer-adv -t upload`
2. Insert a FAT32 microSD card with MP3/WAV files
3. Power on — Browse screen should list SD root
4. Navigate folders with Up/Down, Enter to open
5. Select an audio file — Now Playing screen appears
6. Verify audio through speaker; plug in headphones to confirm auto-switch
7. Test pause (Space), volume ([ / ]), seek (, / /)
8. Press Esc — returns to browse, audio continues in background
9. Select another file — previous track stops, new one plays
10. Remove SD card and press Enter — toast shows error; reinsert and press Enter to remount

## Design

See [docs/superpowers/specs/](docs/superpowers/specs/) for the design specification and implementation plan.
