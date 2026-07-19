# On-boot Last-Played Setting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Settings row **On boot** (`play` / `browse` / `off`) that controls whether the saved last track auto-plays, is only revealed in the browser, or is ignored on boot.

**Architecture:** Extend the existing `Settings` persistence and UI list with one enum field and config key `on_boot`. `App::resumeLastTrack()` branches on that mode after SD load. Path saving during play is unchanged.

**Tech Stack:** C++17, Arduino/ESP32 (M5Cardputer), PlatformIO, SD config file `/.asvmp3/config.cfg`. Host unit tests are not practical for SD-bound Settings (same as `autoNext`); verify via compile + on-device checklist.

**Spec:** `docs/superpowers/specs/2026-07-19-on-boot-last-played-design.md`

---

## File map

| File | Role |
|------|------|
| `include/types.hpp` | Add `OnBootMode` enum; extend `SettingsSnapshot` |
| `src/settings.hpp` | API: field, getters, cycle, `kCount = 6` |
| `src/settings.cpp` | Defaults, parse/save, label/formatValue |
| `src/app.cpp` | `resumeLastTrack()` branch; settings handlers index 5 |
| `src/ui.cpp` | Dirty-check new snapshot field |
| `README.md` | Document setting + boot behavior |

No new files. No changes to Player / audio / browser APIs (`revealPath` already exists).

---

### Task 1: Types — `OnBootMode` + snapshot field

**Files:**
- Modify: `include/types.hpp`

- [ ] **Step 1: Add enum and snapshot field**

In `include/types.hpp`, after the existing enums (near `PlayState`), add:

```cpp
enum class OnBootMode : uint8_t { Play, Browse, Off };
```

Extend `SettingsSnapshot`:

```cpp
struct SettingsSnapshot {
  int volumePercent;
  uint8_t brightness;
  uint32_t displayTimeoutMs;  // 0 = never
  bool autoNext;
  OnBootMode onBoot;
  size_t themeIndex;
  size_t cursor;
};
```

- [ ] **Step 2: Commit**

```bash
git add include/types.hpp
git commit -m "feat: add OnBootMode to settings snapshot types"
```

---

### Task 2: Settings API + persistence

**Files:**
- Modify: `src/settings.hpp`
- Modify: `src/settings.cpp`

- [ ] **Step 1: Update `settings.hpp`**

Change `kCount` and add API next to `autoNext`:

```cpp
  bool autoNext() const { return autoNext_; }
  void setAutoNext(bool on);
  void toggleAutoNext();

  OnBootMode onBoot() const { return onBoot_; }
  void setOnBoot(OnBootMode m);
  void cycleOnBoot(int delta);  // +1 / -1 through Play → Browse → Off

  // Absolute SD path of last played audio ("/Music/a.mp3"), or empty.
  const char* lastPath() const { return lastPath_; }
  void setLastPath(const char* absPath);  // no-op if unchanged

  static constexpr size_t kCount = 6;
```

Add private member (default Play):

```cpp
  bool autoNext_ = true;
  OnBootMode onBoot_ = OnBootMode::Play;
  size_t themeIndex_ = 0;
```

- [ ] **Step 2: Defaults + snapshot**

In `Settings::applyDefaults()` set:

```cpp
  onBoot_ = OnBootMode::Play;
```

In `Settings::snapshot()`:

```cpp
  s.autoNext = autoNext_;
  s.onBoot = onBoot_;
  s.themeIndex = themeIndex_;
```

- [ ] **Step 3: Getters / cycle**

Implement after `toggleAutoNext`:

```cpp
void Settings::setOnBoot(OnBootMode m) { onBoot_ = m; }

void Settings::cycleOnBoot(int delta) {
  // Order: Play(0) → Browse(1) → Off(2)
  int i = static_cast<int>(onBoot_) + delta;
  constexpr int n = 3;
  while (i < 0) i += n;
  while (i >= n) i -= n;
  onBoot_ = static_cast<OnBootMode>(i);
}
```

- [ ] **Step 4: Parse `on_boot` in `parseLine`**

After the `autonext` branch, before `theme` (or after `last_path` — order does not matter), add:

```cpp
  } else if (std::strcmp(key, "on_boot") == 0 || std::strcmp(key, "onboot") == 0) {
    // Canonical: play | browse | off. Aliases for hand-edited configs.
    for (char* p = val; *p; ++p) {
      *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    }
    if (std::strcmp(val, "browse") == 0 || std::strcmp(val, "reveal") == 0) {
      onBoot_ = OnBootMode::Browse;
    } else if (std::strcmp(val, "off") == 0 || std::strcmp(val, "none") == 0 ||
               std::strcmp(val, "false") == 0 || std::strcmp(val, "0") == 0) {
      onBoot_ = OnBootMode::Off;
    } else {
      // play, resume, unknown → Play (safe default / legacy)
      onBoot_ = OnBootMode::Play;
    }
```

Note: `val` is a local `char[]` already; lowercasing in place is fine.

- [ ] **Step 5: Save `on_boot`**

In `Settings::save()`, after writing `autonext`, before `last_path`:

```cpp
  const char* bootStr = "play";
  if (onBoot_ == OnBootMode::Browse) bootStr = "browse";
  else if (onBoot_ == OnBootMode::Off) bootStr = "off";
  std::snprintf(line, sizeof(line), "on_boot=%s\n", bootStr);
  ok = ok && wr(line);
```

Optional: extend the load log `Serial.printf` to include on_boot name (helpful on device).

- [ ] **Step 6: Label + formatValue (index 5)**

```cpp
const char* Settings::label(size_t index) const {
  switch (index) {
    case 0: return "Theme";
    case 1: return "Volume";
    case 2: return "Brightness";
    case 3: return "Scr timeout";
    case 4: return "Auto-next";
    case 5: return "On boot";
    default: return "?";
  }
}

// in formatValue:
    case 4:
      snprintf(buf, cap, "%s", autoNext_ ? "ON" : "OFF");
      break;
    case 5:
      if (onBoot_ == OnBootMode::Browse) {
        snprintf(buf, cap, "browse");
      } else if (onBoot_ == OnBootMode::Off) {
        snprintf(buf, cap, "off");
      } else {
        snprintf(buf, cap, "play");
      }
      break;
```

`moveCursor` already uses `kCount` — no change needed once `kCount == 6`.

- [ ] **Step 7: Commit**

```bash
git add src/settings.hpp src/settings.cpp
git commit -m "feat: persist on_boot play/browse/off setting"
```

---

### Task 3: App — resume + settings handlers

**Files:**
- Modify: `src/app.cpp`

- [ ] **Step 1: Branch `resumeLastTrack()`**

Replace the body of `App::resumeLastTrack()` with:

```cpp
void App::resumeLastTrack() {
  if (!browser_.sdOk()) return;
  if (settings_.onBoot() == OnBootMode::Off) {
    Serial.println("[app] on_boot=off — skip last track");
    return;
  }

  const char* last = settings_.lastPath();
  if (!last || last[0] != '/') return;

  if (!browser_.revealPath(last)) {
    Serial.printf("[app] last track missing: %s\n", last);
    return;
  }

  if (settings_.onBoot() == OnBootMode::Browse) {
    Serial.printf("[app] on_boot=browse — revealed %s\n", last);
    return;  // stay Screen::Browse
  }

  Serial.printf("[app] resume last: %s\n", last);
  if (player_.open(last)) {
    screen_ = Screen::Playing;
  }
}
```

Ensure `types.hpp` is visible (already via `app.hpp` / settings / types).

- [ ] **Step 2: Wire settings cursor index 5**

In `handleSettings`, for **VolUp / SeekFwd** switch, add:

```cpp
        case 4:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          changed = true;
          break;
        case 5:
          settings_.cycleOnBoot(+1);
          changed = true;
          break;
```

For **VolDown / SeekBack** switch, add:

```cpp
        case 4:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          changed = true;
          break;
        case 5:
          settings_.cycleOnBoot(-1);
          changed = true;
          break;
```

For **Enter / Space** switch, add:

```cpp
        case 4:
          settings_.toggleAutoNext();
          player_.setAutoNext(settings_.autoNext());
          changed = true;
          break;
        case 5:
          settings_.cycleOnBoot(+1);
          changed = true;
          break;
```

Do **not** change `rememberLastPath` / play path saving.

- [ ] **Step 3: Commit**

```bash
git add src/app.cpp
git commit -m "feat: honor on_boot mode when resuming last track"
```

---

### Task 4: UI dirty check

**Files:**
- Modify: `src/ui.cpp`

- [ ] **Step 1: Compare `onBoot` in `settingsChanged`**

```cpp
bool Ui::settingsChanged(const Settings& s) const {
  if (!hasLastSettings_) return true;
  SettingsSnapshot cur = s.snapshot();
  return cur.volumePercent != lastSettings_.volumePercent ||
         cur.brightness != lastSettings_.brightness ||
         cur.displayTimeoutMs != lastSettings_.displayTimeoutMs ||
         cur.autoNext != lastSettings_.autoNext ||
         cur.onBoot != lastSettings_.onBoot ||
         cur.themeIndex != lastSettings_.themeIndex ||
         cur.cursor != lastSettings_.cursor;
}
```

`drawSettings` already loops `Settings::kCount` — no UI layout change.

- [ ] **Step 2: Commit**

```bash
git add src/ui.cpp
git commit -m "fix: redraw settings when on_boot changes"
```

---

### Task 5: README + build check

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document the setting**

In `### Settings (`S` / Tab)`, add bullet after Auto-next:

```markdown
- **Auto-next** — ON / OFF
- **On boot** — play (auto-play last track) / browse (open folder only) / off (ignore last path)
```

Update keys line:

```markdown
- **`Enter` / `Space`** — Cycle theme / timeout / auto-next / on boot
```

Replace the reboot sentence:

```markdown
Settings (including last played path and on-boot mode) auto-save to `/.asvmp3/config.cfg` on change.
On boot, behavior follows **On boot**: **play** restores and starts the last track if it still exists;
**browse** only opens its folder and highlights it; **off** ignores the saved path.
```

If the on-device checklist mentions last-track restore, add a line that on-boot mode is configurable.

- [ ] **Step 2: Compile firmware**

```bash
pio run -e cardputer-adv
```

Expected: build succeeds (exit 0).

Also run existing native tests (should be unaffected):

```bash
pio test -e native
```

Expected: path_utils + wav_header tests PASS.

- [ ] **Step 3: Commit docs**

```bash
git add README.md
git commit -m "docs: document On boot last-track setting"
```

---

### Task 6: On-device verification checklist

No code. Flash when hardware is available:

```bash
pio run -e cardputer-adv -t upload && pio device monitor -b 115200
```

- [ ] **Step 1: Default / legacy**

1. Ensure config has no `on_boot` or `on_boot=play`, with a valid `last_path`.
2. Reboot → track auto-plays, Playing screen.

- [ ] **Step 2: Browse mode**

1. Settings → **On boot** → `browse`, exit.
2. Reboot → folder open, cursor on last file, no playback, Browse screen.
3. Serial: `on_boot=browse — revealed ...`

- [ ] **Step 3: Off mode**

1. Settings → **On boot** → `off`.
2. Reboot → normal root browse; no reveal.
3. Serial: `on_boot=off — skip last track`

- [ ] **Step 4: Path still saved**

1. With `on_boot=off`, play a track, note path.
2. Set `on_boot=play`, reboot → that track plays.

- [ ] **Step 5: Missing file**

1. Set `last_path` to a deleted file (or rename on PC), `on_boot=play`.
2. Reboot → no crash; stay browse; serial missing log.

---

## Spec coverage (self-review)

| Spec item | Task |
|-----------|------|
| `on_boot` play/browse/off | 1–2 |
| Default play / unknown → play | 2 (parse else + applyDefaults) |
| Aliases resume/reveal/none/false/0 | 2 Step 4 |
| Always save `last_path` | unchanged; Task 3 note |
| Settings row after Auto-next | 2 label index 5 |
| Cycle keys | 3 Step 2 |
| `resumeLastTrack` branch | 3 Step 1 |
| Snapshot / UI dirty | 1 + 4 |
| README | 5 |
| Device checklist | 6 |

No placeholders. Names consistent: `OnBootMode`, `onBoot()`, `cycleOnBoot`, config `on_boot`.
