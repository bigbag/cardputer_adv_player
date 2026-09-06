# Project guidance

## Code and writing rules

- Follow KISS and DRY principles. Keep solutions simple.
- Do not add comments when the code describes its operation.
- Match the existing style, even if you prefer a different style.
- Use Simplified Technical English (ASD-STE100) for documentation, comments, and docstrings.
  Use one idea per sentence. Use active voice. Use present tense.
  Use approved words in their approved meanings. Remove filler. Remove cliches.

## Project

- This project is an MP3, 16-bit PCM WAV, and bounded FLAC player for M5Stack Cardputer-ADV.
- The firmware uses C++17, Arduino, PlatformIO, and the M5Cardputer library.
- The hardware uses an ESP32-S3 on a Stamp-S3A module. The build disables PSRAM.
- Audio output uses the ES8311 codec and I2S. The display is 240 by 135 pixels.
- Read `README.md` for controls and device checks.
  Read `docs/architecture-audio.md` for the audio design.

## Commands

Use the Makefile. It selects a Python version that PlatformIO supports.
Use Python 3.10 through 3.13.
If necessary, set `PIO="python3.12 -m platformio"`.

| Command | Purpose |
| --- | --- |
| `make help` | Show commands and the selected PlatformIO launcher. |
| `make build` | Build the `cardputer-adv` firmware. |
| `make test` | Run the native Unity tests. |
| `make size` | Show firmware memory use. |
| `make flash` | Build and upload firmware. |
| `make flash-nostub` | Upload through the `cardputer-adv-nostub` environment. |
| `make monitor` | Open the serial monitor at 115200 baud. |

Run PlatformIO build and test commands in sequence. They share `.pio` build state.
Do not flash a device unless the user requests it.
Keep upload and monitor speeds at 115200 unless hardware checks support a change.

To run only the idle timer tests:

```sh
python3.12 -m platformio test -e native -f test_idle_timeout
```

## Source files

- `src/app.cpp` controls screens, commands, settings, and power.
- `src/player.cpp` controls the audio task, playback, and track selection.
- `src/audio_out.cpp` controls the ES8311 codec, I2S output, and volume.
- `src/decoders/` contains MP3, WAV, and FLAC decoders that produce stereo 16-bit PCM.
- `src/sd_browser.cpp` controls SD access, directory lists, and browser position.
- `src/settings.cpp` reads and writes `/.asvmp3/config.cfg` on the SD card.
- `src/ui.cpp` draws the display. `src/input.cpp` reads keyboard actions.
- `include/config.hpp` defines pins, resource limits, display layout, and audio parameters.
- `include/types.hpp` defines shared states and snapshots.
- `include/browser_history.hpp` and `include/idle_timeout.hpp` contain state helpers for the firmware and native tests.
- `lib/minimp3/` contains the third-party MP3 decoder. Do not edit it for unrelated application changes.
- `lib/dr_flac/` contains the pinned FLAC decoder and its provenance. Keep upstream code unchanged.

## Behavior to preserve

- Open and run decoders on the core-0 audio task. Keep decoding and I2S writes out of the UI loop.
  `Player::service()` handles automatic track changes in the application loop.
- Stop the audio task. Wait for it to exit before you close decoder resources, audio output, or SD access.
- Keep FLAC block, heap, and read-work limits in `include/config.hpp`.
  FLAC uses known sample totals and checks frame continuity. Failed seeks stop the decoder.
- Playback can continue while the display shows Browse or Settings.
  Track navigation uses the playing track's folder, not the visible browser folder.
- Keep the saved browser location separate from the last played track.
  Browser Back history stays in memory. A restart clears this history.
- Keep paths within the limits in `include/config.hpp`.
  Path and name limits count UTF-8 bytes, not characters.
  Do not split UTF-8 characters in labels.
- Use the existing settings file. Do not add another storage method.
  When you add a setting, update parsing, saving, defaults, snapshots, row controls, and redraw detection.
- Keep the screen timeout separate from idle shutdown.
  Playback prevents idle shutdown on every screen. Commands restart the idle interval.
  Start a full idle interval when playback pauses or ends.
- Deep sleep does not disconnect the battery. The firmware does not enable keyboard wake.
  Check SD write failures after changes to shutdown control.
- The headphone jack mutes the speaker in hardware. Do not add an assumed software mute pin.
- Fit settings rows above the hint bar. Use existing layout constants.

## Verification

- Run `make build` after firmware changes. Run `make test` after changes to code that native tests use.
- Native tests use Unity and `UNIT_TEST`.
  The native environment includes path utilities, audio DSP, MP3 metadata, and all three decoder sources.
  MP3 and FLAC tests run real decoders with native file adapters.
  WAV tests run the shared parser and arithmetic helpers, not firmware file I/O.
  Native tests also check header-only state helpers. They do not run the full firmware.
- Keep regression checks for observable failures, state transitions, and boundaries.
  For timeout changes, check disabled timeouts, command resets, playback transitions, and clock wrap.
  Check shutdown when the SD card is not available or does not permit writes.
- Use the on-device checklist in `README.md` for display, keyboard, SD, and audio changes.
  Measure sleep current on hardware before making battery-life claims.
- Report hardware checks that you cannot perform. A build or host test does not prove device behavior.
- Do not commit `.pio` output, temporary checks, or generated compilation databases.