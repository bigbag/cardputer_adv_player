# Cardputer Player

Minimal MP3/WAV player for **M5Stack Cardputer-ADV** (ESP32-S3 / Stamp-S3A).

The player reads files from a FAT32 SD card. It plays MP3 files and 16-bit PCM
WAV files through the ES8311 codec. Audio goes to the speaker or to the
3.5 mm jack. The 240×135 display shows a terminal-style user interface.

## Requirements

- Cardputer-ADV hardware
- [PlatformIO Core](https://platformio.org/) 6.x
- Python **3.10–3.13** for PlatformIO. PlatformIO does not support Python 3.14
  or later. The Makefile automatically uses `python3.12 -m platformio` when
  necessary.
- FAT32 microSD card with `.mp3` / `.wav` files

## Quick start

```bash
make build          # Compile the firmware.
make flash          # Build the firmware and upload it.
make monitor        # Open the serial monitor at 115200 baud.
make test           # Run the host unit tests.
make help           # Show command help.
```

You can override the PlatformIO launcher:

```bash
make build PIO="python3.12 -m platformio"
make flash PIO="python3.12 -m platformio"
```

These commands use PlatformIO directly:

```bash
pio run -e cardputer-adv
pio run -e cardputer-adv -t upload
pio device monitor -b 115200
pio test -e native
```

### Download mode (Cardputer-ADV)

Use this procedure when upload fails or the port does not appear:

1. Move the power switch to **OFF**.
2. Hold **G0**. Move the power switch to **ON**. Release **G0**.
3. Run `make flash`.
4. Move the power switch to **OFF**. Then move it to **ON**.

### Charging

Leave the side power switch **ON** while charging.

## SD card format

Use **FAT32**. Very small cards can use FAT16. The Arduino `SD` library does
**not** mount **exFAT**.

If the device shows **No SD card**, do this:

1. Format the card as **FAT32** with an MBR partition table. Do not use
   exFAT or APFS.
2. Use a full format on a PC: Windows **FAT32**, macOS Disk Utility
   **MS-DOS (FAT)**, or `mkfs.vfat`. Do not use a quick format. Quick formats
   on some cameras are not reliable.
3. If possible, use a card with a capacity of 32 GB or less that you know
   works. Manufacturers usually ship cards of 64 GB and larger as exFAT.
4. Put the card fully into the slot. Turn the power off and on after you
   insert the card.
5. On the Browse screen, press **Enter** to try the mount again.
6. Run `make monitor`. Check for `[sd] mount ok/fail @ … Hz` messages.

## Suggested SD layout

```text
/music/
  album1/
    track01.mp3
  album2/
    intro.wav
```

The browser starts at the SD card root. It supports nested folders.
The browser skips names that start with `.`.

File and folder names use UTF-8. The browser and player use the bundled
`efontJA_10` font for these names. The font includes Russian letters
(`Ё` and `ё`), Japanese characters, and other Unicode characters. The font
does not cover all Unicode characters. Characters outside the font appear as
a missing-glyph box. Long labels end with `...` at a complete UTF-8 character
boundary.

Names and absolute paths must fit in 255 UTF-8 bytes. This limit is not 255
characters. The browser skips names above this limit and shows
`* truncated`. The browser rejects folder paths above the limit. The player
rejects track paths above the limit. The browser stores at most 256 entries
per folder.

The device stores settings in this hidden directory on the SD card:

```text
/.asvmp3/config.cfg
```

## Controls

This key map matches the firmware file `src/input.cpp`:

### Navigation diamond (physical keys)

```text
       ;          up
    ,  .  /     left  down  right
```

- **`;`** — Up in Browse. Previous track during playback.
- **`.`** — Down in Browse. Next track during playback.
- **`,`** — Left. Decrease a value or the volume.
- **`/`** — Right. Increase a value or the volume.

### Browse

- **`;` / `.`** — Move the cursor up or down
- **`Enter`** — Open the selected folder or play the selected file
- **`Space`** — Play the highlighted audio file
- **`P`** — Show the Now Playing screen if the player has a track. Playback continues.
- **`S` or `Tab`** — Settings
- **`Backspace` or `` ` ``** — Parent directory
- **`Enter` when no SD card is present** — Retry the mount

> The Cardputer keyboard has no Esc key. Use **Backspace** (or `` ` ``) for
> Back.

### Now Playing

- **`Space`** — Play / pause
- **`;`** — Select the previous track. If the playback position exceeds 3 s,
  restart the current track instead.
- **`.`** — Select the next track in the folder. The `N` key has the same function.
- **`,`** — Volume down
- **`/`** — Volume up
- **`[` / `]`** — Seek back or forward approximately 5 s
- **`P`** — Return to Browse. Playback continues.
- **`S` or `Tab`** — Open Settings.
- **`Backspace`** — Return to Browse. Playback continues.

### Settings (`S` / Tab)

Rows (change a value with `,` / `/`):

- **Theme** — Phosphor / Amber / Cyan / VGA / Matrix / Hot / Blood
- **Volume** — 0–100%. Use low values for the jack. Use high values for the speaker.
- **Brightness** — lower / higher
- **Scr timeout** — 5s / 10s / 30s / 60s / never
- **Auto-next** — ON / OFF
- **On boot** — play (start the last track automatically) / browse (open the
  folder only; default) / off (ignore the last path)
- **Idle off** — never / 5m / 30m / 60m (default: never)

Keys:

- **`;` / `.`** — Move the selection up or down
- **`,` / `/`** — Decrease or increase the value
- **`Enter` / `Space`** — Cycle theme / screen timeout / auto-next / on boot
  / idle off
- **`Backspace` / `S`** — Exit Settings. The device saves each setting when you change it.

The jack mutes the speaker amplifier in hardware. The **Volume** setting has
one wide range. Use low levels for headphones. Use high levels for the
built-in speaker.

The device saves settings to `/.asvmp3/config.cfg` on each change. Saved
settings include the last played path and the on-boot mode.

The **On boot** setting controls startup behavior.
**play** restores and starts the last track if it still exists.
**browse** restores the Browser without playback. **off** ignores the saved track path.

The Browser also remembers its last folder and its highlighted entry. This
browser location is independent of the last played track. The device
restores this location after a reboot and when you return from Now Playing.
If the folder or the highlighted item no longer exists, the Browser opens
the root folder `/`.

In the current session, **Back** restores the selected entry and the list
viewport of the parent folder. The browser saves this state before it opens
the child folder. A restart clears this history.

When you start another file from Browse, the player replaces the current
track. If **Auto-next** is **ON**, the player starts the next audio file in
the **same folder** when the current track ends. The status shows `DONE` if
Auto-next is OFF or no next audio file exists.

## Display

- The backlight turns off after the screen timeout if no mapped command occurs.
  Playback continues. The default timeout is 10 s.
  You can set the timeout to **never** in Settings.
- A mapped command wakes the screen. The device also processes the command.
- The Now Playing screen updates the time and the progress bar without a
  full-screen redraw. This reduces flicker.

## Idle shutdown

- The **Idle off** setting sets a separate timeout for deep sleep.
- Commands restart the idle timer. Playback prevents shutdown on every
  screen.
- Pausing or ending playback starts a full idle interval. Playback errors
  also restart the timer.
- The player saves the settings before shutdown. If the save fails, the
  device stays awake and shows an error message.
- The config key is `idle_timeout_ms`: `0`, `300000`, `1800000`, or
  `3600000`. Missing or invalid values select **never**.
- Deep sleep does not disconnect the battery.
  Reset the device or turn the power off and on to start it again.
  The firmware does not enable keyboard wake.
  Measure sleep current on the hardware.

## Audio notes

- Formats: MP3 (minimp3), WAV PCM 16-bit mono/stereo
- Output: ES8311 + I2S
- The jack mute is **hardware** (the MCU has no detect pin). One software
  volume curve serves both output paths.

## On-device checklist

1. Boot without an SD card. The display shows a clear `No SD` screen.
   **Enter** retries the mount.
2. Open nested folders. Press Back at the root folder. The browser stays at the root.
3. Enter nested folders after you move the cursor and scroll in each parent.
   Press **Back**. Check that the browser restores each parent's selected entry and viewport.
   Restart the device. Check that the restart clears this Back history.
4. Play MP3 files with 44.1 kHz and 48 kHz sample rates, if available.
5. Play 16-bit WAV files in mono and stereo.
6. Open an invalid or non-PCM WAV file. Check that the device shows an error message.
7. Check pause, resume, volume, seek, and next/previous track controls.
8. Enable Auto-next. Check automatic track changes within the folder.
   Check that the last file sets the status to `DONE`.
9. Press **Backspace** or **P** to switch between Browse and Playing.
   Check that playback continues. Open a new file. Check that the player changes tracks.
10. Insert a headphone plug. Check that the jack mutes the speaker amplifier.
11. Change settings. Restart the device. Check that it restores settings from `/.asvmp3/config.cfg`.
12. Select an entry in a nested Browser folder. Restart the device.
    Check that it restores the folder and selected entry.
13. Set **On boot = play**. Restart the device. Check that the last track starts.
    Press **Back** or **P**. Check that the browser restores its saved location.
14. Check **On boot = browse** and **off**. Both modes restore the Browser without playback.
15. Browse other folders during playback.
    Check that next, previous, and automatic track changes still use the playing track's folder.
16. Remove the saved Browser folder or item. Check that the Browser opens root `/`.
17. Remove the Browser config keys. Check that the Browser opens root `/`.
18. Check that the serial console at 115200 baud shows mount, open, and error messages.

**Hardware validation status:** host tests and the firmware build pass.
This environment does not have a completed on-device checklist.

## Project layout

```text
include/          config, actions, types, path_utils
src/              app, ui, input, player, sd_browser, audio_out, decoders/
lib/minimp3/      third-party minimp3.h
test/             native Unity tests
docs/             audio architecture
Makefile          build, upload, and test commands
platformio.ini    firmware and native environments
```

## Architecture (short)

- **`App`** — Browse ↔ Playing ↔ Settings state machine
- **`SdBrowser`** — SD card mount and directory listing (`readdir`)
- **`Player`** — FreeRTOS audio task, auto-next, next/previous
- **`AudioOut`** — ES8311 + I2S + volume curve
- **`Mp3Decoder` / `WavDecoder`** — decode to stereo PCM
- **`Settings`** — SD card config file load/save
- **`Ui` / `Input`** — terminal-style UI + keys

## Architecture document

- Audio and device architecture: `docs/architecture-audio.md`
