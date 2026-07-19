# Persistent Browser Location — design

Date: 2026-07-19  
Status: approved

## Goal

Remember the Browser’s exact location independently from the last played track, so the user can return to the folder and highlighted entry they were browsing after switching screens or rebooting.

## Problem

`SdBrowser` currently supplies both the visible Browser state and the Player’s next/previous track lookup. Restoring the last track calls `revealPath(last_path)`, which replaces the Browser directory with the played track’s parent. The app therefore cannot return to a separately browsed folder and cursor.

## Behavior

### Saved Browser location

Persist two values in `/.asvmp3/config.cfg`:

```ini
browser_path=/Albums/Favourites
browser_item=Track 03.mp3
```

- `browser_path` is the visible Browser folder.
- `browser_item` is the selected entry name, rather than a numeric index, so sorting or file insertions do not select the wrong item.
- Location is updated after Browser navigation/cursor movement. Writes are debounced to avoid rewriting the SD config on every key repeat, and forced before leaving Browser for Playing/Settings.

### Restore rules

1. Start at root as normal, load config, then restore `browser_path` and select `browser_item`.
2. A legacy config without either Browser key restores root.
3. If the saved folder cannot be listed **or the saved item is absent**, restore root `/` with cursor at its first entry. This is the explicitly selected fallback.
4. Hidden/non-audio items remain unselectable because only visible Browser entries are saved/restored.

### Interaction with On boot

| On boot setting | Result |
|---|---|
| `play` | Restore Browser location first; open and play `last_path` without moving Browser. Back/`P` returns to saved Browser location. |
| `browse` | Restore Browser location only; do not open/play `last_path`. |
| `off` | Restore Browser location only; ignore `last_path`. |

`last_path` remains unchanged: it is always persisted when a track begins or changes.

### Interaction with playback playlist

The Player must use the parent directory of `currentPath()` for next/previous/auto-next, not the visible Browser folder.

For a sibling lookup, Player will:

1. Capture the current Browser location.
2. Temporarily reveal the playing file’s directory and find its adjacent audio entry.
3. Restore the captured Browser location before opening the adjacent file.

This keeps next/previous/auto-next correct when the user has browsed elsewhere while audio continues, without requiring a second SD mount or a duplicate directory cache.

## Components

| Component | Responsibility |
|---|---|
| `BrowserLocation` value type | Holds path + selected visible entry name. Shared by Browser and Settings. |
| `SdBrowser` | Capture current location; restore a location, returning root for missing folder/item. |
| `Settings` | Load/save `browser_path` and `browser_item`; expose Browser location accessors. |
| `App` | Restore Browser location on boot, mark it dirty after Browser navigation, debounce writes, force flush before leaving Browser; change boot resume ordering. |
| `Player` | Temporarily switch Browser only for sibling lookup and restore the visible Browser location immediately. |

## Persistence details

- Keys are optional for backward compatibility.
- `browser_path` must begin with `/`; an invalid path is treated as missing and restores root.
- `browser_item` may be empty only for an empty/root default location. If it is nonempty but cannot be found in the listed folder, restore root.
- Configuration writes keep existing temp-file/rename safety behavior.
- Browser location persistence uses a short inactivity debounce (about 750 ms) plus immediate flush on Browser → Playing/Settings. A sudden power loss during the debounce may restore the immediately preceding Browser state; this avoids high-frequency SD writes while moving the cursor.

## UI / controls

No new screen or controls.

- `P` and Back already switch Playing → Browse; Browser remains on its saved/current location.
- Existing Browser controls change the remembered location.
- No visible indication is needed beyond the restored path and selection.

## Testing

### Build / existing tests

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv
~/.platformio/penv/bin/pio test -e native
```

### Device checklist

1. Browse to nested folder and select an entry; wait one second; reboot → same folder and entry.
2. Set **On boot = play** with a different saved Browser folder and last song → song plays; `P` or Back opens saved Browser folder/entry.
3. Set **On boot = browse** or **off** → saved Browser folder/entry restores; no audio starts.
4. While a song plays, browse another folder; use `P` to go to Playing; use `;`, `.`, and auto-next → adjacent tracks are from the song’s folder, not the visible Browser folder; `P`/Back returns to the other Browser folder.
5. Rename/remove selected Browser item or folder; reboot → root `/`, no crash.
6. Existing config without Browser keys → root `/`, no crash.

## Non-goals

- No browse-history stack.
- No separate setting to disable Browser location persistence.
- No persistence of scroll offset; the selected entry is made visible by existing scroll logic.
