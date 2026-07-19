# On-boot last-played behavior — design

Date: 2026-07-19  
Status: approved

## Goal

Let the user choose what happens with the saved last track on boot, without changing how the path is remembered during normal use.

## Non-goals

- No separate “save last path” toggle — path is always saved when playing.
- No change to auto-next within a folder.
- No change to in-session navigation or playback controls.

## Behavior

### Always (unchanged)

- While a track is playing (or after open / next / prev), persist absolute path as `last_path` in `/.asvmp3/config.cfg`.
- Path is still updated by auto-next.

### New setting: **On boot**

| Value   | Boot behavior                                              |
|---------|------------------------------------------------------------|
| `play`  | Reveal path in browser, open file, switch to Playing       |
| `browse`| Reveal path in browser (folder + cursor), stay on Browse   |
| `off`   | Ignore `last_path`; start at root browse as today without resume |

Default: **`play`** (matches current firmware behavior).

Missing / unknown config value → treat as `play`.

## Persistence

- Config key: `on_boot=play|browse|off`
- File: `/.asvmp3/config.cfg` (same load/save path as other settings)
- Accept aliases on load if useful: `resume`→`play`, `reveal`→`browse`, `none`/`false`/`0`→`off`
- Write canonical form on save: `play`, `browse`, or `off`

## UI

- New Settings row: label **On boot**
- Value text: `play` / `browse` / `off` (short for 240px display)
- Placement: after **Auto-next** (index 5; `Settings::kCount` becomes 6)
- Controls (same as theme / timeout / auto-next):
  - `,` / `/` (or seek keys): cycle values
  - `Enter` / `Space`: cycle forward
- Auto-save on change via existing `persistSettings()`

## Code touch points

| Area | Change |
|------|--------|
| `Settings` | Field + getters/setters/cycle; parse/save `on_boot`; label/formatValue; `kCount = 6` |
| `SettingsSnapshot` | Include value so UI dirty-check redraws |
| `App::resumeLastTrack()` | Branch on mode: play / browse-only / no-op |
| `App::handleSettings` | Wire cursor index 5 to cycle |
| `Ui::settingsChanged` | Compare new snapshot field |
| `README.md` | Document row + boot behavior |

No change to `Player`, decoders, or audio path.

## Boot flow (after SD + settings load)

```
resumeLastTrack():
  if on_boot == off → return
  if no/invalid last_path → return
  if !revealPath(last) → log + return
  if on_boot == browse → return   // stay Browse
  if player.open(last) → screen = Playing
```

## Testing (on device / checklist)

1. Default / missing key → boots and auto-plays last track (legacy behavior)
2. `on_boot=browse` → folder open, track highlighted, no audio start
3. `on_boot=off` → root (or normal first browse), no reveal
4. Change setting in UI → survives reboot
5. Playing still updates `last_path` in all three modes
6. Missing last file → no crash; log + stay browse

## Risks

- Settings list grows by one row — still fits Font0 list; no scroll needed at 6 rows.
- Existing configs without `on_boot` must default to play (covered above).
