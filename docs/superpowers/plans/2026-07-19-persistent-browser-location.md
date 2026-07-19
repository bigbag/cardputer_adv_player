# Persistent Browser Location Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist and restore the Browser folder and highlighted entry independently from the last played track, while keeping player next/previous/auto-next bound to the playing track’s folder.

**Architecture:** Add a shared `BrowserLocation` value type (path + selected entry name). `Settings` serializes it in config; `SdBrowser` captures and restores it, falling back to root if either part no longer exists. `App` saves Browser changes with a short debounce and restores the location before evaluating `On boot`. Player temporarily reveals its own track directory only for sibling lookup, then restores the visible Browser state.

**Tech Stack:** C++17, Arduino/ESP32, M5Cardputer, PlatformIO, FAT SD config at `/.asvmp3/config.cfg`.

**Spec:** `docs/superpowers/specs/2026-07-19-persistent-browser-location-design.md`

---

## File map

| File | Change |
|---|---|
| `include/types.hpp` | Define `BrowserLocation` shared value type |
| `src/sd_browser.hpp/.cpp` | Capture/restore Browser location; root fallback |
| `src/settings.hpp/.cpp` | Persist `browser_path` + `browser_item` |
| `src/app.hpp/.cpp` | Restore at boot; debounce/flush Browser state; keep `On boot` behavior correct |
| `src/player.cpp` | Preserve visible Browser state around sibling lookup |
| `README.md` | Document persistent Browser location and missing-entry fallback |

No new files. Existing native tests do not compile `SdBrowser`/`Settings` because they depend on Arduino SD APIs; use the existing native suite plus a firmware build and on-device acceptance checks.

---

### Task 1: Shared Browser location type

**Files:**
- Modify: `include/types.hpp`

- [ ] **Step 1: Add `BrowserLocation` after `BrowseSnapshot`**

```cpp
struct BrowserLocation {
  char path[cfg::kMaxPathLen];
  char item[cfg::kMaxNameLen];  // selected visible entry; empty for no selection
};
```

`types.hpp` already includes `config.hpp`, so the capacity constants are available. Keep this type separate from `BrowseSnapshot`: it is a durable value with no entry pointer, cursor index, or scroll offset.

- [ ] **Step 2: Build the firmware**

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv
```

Expected: successful compile and link.

- [ ] **Step 3: Commit**

```bash
git add include/types.hpp
git commit -m "feat: add BrowserLocation value type"
```

---

### Task 2: SdBrowser capture and root-safe restore

**Files:**
- Modify: `src/sd_browser.hpp`
- Modify: `src/sd_browser.cpp`

- [ ] **Step 1: Declare the API**

Add after `revealPath` in `src/sd_browser.hpp`:

```cpp
  // Current folder + selected visible entry (not cursor index / scroll).
  BrowserLocation location() const;
  // Restore folder and selected entry. Missing folder/item resets to root.
  bool restoreLocation(const BrowserLocation& location);
```

- [ ] **Step 2: Implement capture**

Add to `src/sd_browser.cpp`:

```cpp
BrowserLocation SdBrowser::location() const {
  BrowserLocation out{};
  std::strncpy(out.path, path_, sizeof(out.path) - 1);
  if (count_ > 0 && cursor_ < count_) {
    std::strncpy(out.item, entries_[cursor_].name, sizeof(out.item) - 1);
  }
  return out;
}
```

- [ ] **Step 3: Implement restore with required root fallback**

Add a small local helper near the path helpers:

```cpp
static bool isAbsolutePath(const char* path) {
  return path && path[0] == '/';
}
```

Implement:

```cpp
bool SdBrowser::restoreLocation(const BrowserLocation& location) {
  auto root = [&]() {
    openPath("/");
    return false;
  };

  // Empty path is a legacy/no-location config: root is the valid default.
  if (location.path[0] == '\0') {
    return openPath("/");
  }
  if (!isAbsolutePath(location.path) || !openPath(location.path)) {
    return root();
  }

  // Empty item is valid only for an empty folder. A selected entry that
  // disappeared must use the user-selected root fallback.
  if (location.item[0] == '\0') {
    return count_ == 0 ? true : root();
  }
  for (size_t i = 0; i < count_; ++i) {
    if (std::strcmp(entries_[i].name, location.item) == 0) {
      setCursor(i);  // existing ensureScroll makes it visible
      return true;
    }
  }
  return root();
}
```

This deliberately does **not** retain a numeric cursor: changed sorting or inserted files must not select the wrong entry.

- [ ] **Step 4: Build**

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv
```

Expected: success.

- [ ] **Step 5: Commit**

```bash
git add src/sd_browser.hpp src/sd_browser.cpp
git commit -m "feat: capture and restore browser location"
```

---

### Task 3: Settings persistence for Browser location

**Files:**
- Modify: `src/settings.hpp`
- Modify: `src/settings.cpp`

- [ ] **Step 1: Add accessors and storage**

In `Settings` public API after last-path methods:

```cpp
  const BrowserLocation& browserLocation() const { return browserLocation_; }
  void setBrowserLocation(const BrowserLocation& location);
```

Add private storage:

```cpp
  BrowserLocation browserLocation_{};
```

- [ ] **Step 2: Reset defaults**

In `applyDefaults()`, zero the durable location:

```cpp
  browserLocation_ = {};
```

- [ ] **Step 3: Implement safe setter**

After `setLastPath`:

```cpp
void Settings::setBrowserLocation(const BrowserLocation& location) {
  if (location.path[0] == '/') {
    std::strncpy(browserLocation_.path, location.path,
                 sizeof(browserLocation_.path) - 1);
  } else {
    browserLocation_.path[0] = '\0';
  }
  browserLocation_.path[sizeof(browserLocation_.path) - 1] = '\0';
  std::strncpy(browserLocation_.item, location.item,
               sizeof(browserLocation_.item) - 1);
  browserLocation_.item[sizeof(browserLocation_.item) - 1] = '\0';
}
```

- [ ] **Step 4: Parse optional config keys**

In `parseLine`, add branches:

```cpp
  } else if (std::strcmp(key, "browser_path") == 0) {
    if (val[0] == '/') {
      std::strncpy(browserLocation_.path, val, sizeof(browserLocation_.path) - 1);
      browserLocation_.path[sizeof(browserLocation_.path) - 1] = '\0';
    } else {
      browserLocation_.path[0] = '\0';
    }
  } else if (std::strcmp(key, "browser_item") == 0) {
    std::strncpy(browserLocation_.item, val, sizeof(browserLocation_.item) - 1);
    browserLocation_.item[sizeof(browserLocation_.item) - 1] = '\0';
```

A config that has neither key keeps the zero/default location and therefore restores root. Do not reject configs with a non-empty item before path: order should not matter.

- [ ] **Step 5: Save canonical values**

After `last_path` in `Settings::save()`:

```cpp
  std::snprintf(line, sizeof(line), "browser_path=%s\n", browserLocation_.path);
  ok = ok && wr(line);
  std::snprintf(line, sizeof(line), "browser_item=%s\n", browserLocation_.item);
  ok = ok && wr(line);
```

Increase the stale minimum-size comment so it accurately describes the written key count. Keep `kMinBytes` as a conservative plausibility floor; it does not need to equal exact config size.

- [ ] **Step 6: Build and commit**

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv
git add src/settings.hpp src/settings.cpp
git commit -m "feat: persist browser path and selected item"
```

Expected: build succeeds before commit.

---

### Task 4: App boot restore and debounced persistence

**Files:**
- Modify: `include/config.hpp`
- Modify: `src/app.hpp`
- Modify: `src/app.cpp`

- [ ] **Step 1: Add named debounce configuration**

In `namespace cfg`, near `kToastMs`:

```cpp
constexpr uint32_t kBrowserLocationSaveDelayMs = 750;
```

- [ ] **Step 2: Add App helpers and fields**

In `app.hpp` declarations:

```cpp
  void rememberBrowserLocation();
  void flushBrowserLocation(bool showError);
  void restoreBrowserLocation();
```

In App fields:

```cpp
  bool browserLocationDirty_ = false;
  uint32_t browserLocationChangedAtMs_ = 0;
```

- [ ] **Step 3: Implement helpers**

```cpp
void App::rememberBrowserLocation() {
  settings_.setBrowserLocation(browser_.location());
  browserLocationDirty_ = true;
  browserLocationChangedAtMs_ = millis();
}

void App::flushBrowserLocation(bool showError) {
  if (!browserLocationDirty_) return;
  if (settings_.save()) {
    browserLocationDirty_ = false;
  } else if (showError) {
    ui_.showToast("Save fail (SD?)", millis());
  }
}

void App::restoreBrowserLocation() {
  if (!browser_.restoreLocation(settings_.browserLocation())) {
    Serial.println("[app] browser location unavailable — root");
  }
}
```

- [ ] **Step 4: Restore Browser before applying On boot behavior**

In `App::begin()`, after settings load and before `resumeLastTrack()`, call:

```cpp
  restoreBrowserLocation();
```

Replace `resumeLastTrack()` so it does not call `browser_.revealPath()`:

```cpp
void App::resumeLastTrack() {
  if (!browser_.sdOk() || settings_.onBoot() != OnBootMode::Play) return;

  const char* last = settings_.lastPath();
  if (!last || last[0] != '/' || !SD.exists(last)) {
    if (last && last[0] == '/') {
      Serial.printf("[app] last track missing: %s\n", last);
    }
    return;
  }

  Serial.printf("[app] resume last: %s\n", last);
  if (player_.open(last)) {
    screen_ = Screen::Playing;
  }
}
```

With this ordering, `browse` and `off` restore Browser only; `play` opens the track without replacing Browser location.

- [ ] **Step 5: Queue Browser changes and force writes at transitions**

At the end of `handleBrowse`, after its switch, call `rememberBrowserLocation()` for every Browser action that can change location (Up, Down, Enter, Space, Back). Do not mark it for `Action::None`.

Before every Browser → Playing path, ensure the selected Browser location is captured. In `playSelection()`, call `rememberBrowserLocation()` before `player_.open(absPath)`. Existing `rememberLastPath()` then writes both fields in the same settings save.

In the global `Action::Settings` block, if `screen_ == Screen::Browse`, call:

```cpp
    rememberBrowserLocation();
    flushBrowserLocation(true);
```

In the global `Action::TogglePlayer` block, when leaving Browse for Playing, call the same two lines before `screen_ = Screen::Playing`.

- [ ] **Step 6: Debounce and coalesce settings writes**

At the end of `App::loop()`, before rendering, flush after inactivity:

```cpp
  if (browserLocationDirty_ &&
      now - browserLocationChangedAtMs_ >= cfg::kBrowserLocationSaveDelayMs) {
    flushBrowserLocation(false);
  }
```

When `persistSettings()` or `rememberLastPath()` successfully calls `settings_.save()`, set `browserLocationDirty_ = false`, because the full settings file includes the current Browser location. Keep existing toast behavior for regular settings saves.

- [ ] **Step 7: Build and commit**

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv
git add include/config.hpp src/app.hpp src/app.cpp
git commit -m "feat: restore and debounce browser location"
```

---

### Task 5: Keep playback sibling navigation independent

**Files:**
- Modify: `src/player.hpp`
- Modify: `src/player.cpp`

- [ ] **Step 1: Add a private helper declaration**

In `src/player.hpp`, add:

```cpp
  bool adjacentTrack(bool next, char* outPath, size_t outCap);
```

- [ ] **Step 2: Implement adjacent lookup without leaving Browser changed**

In `src/player.cpp`, before `nextTrack()`:

```cpp
bool Player::adjacentTrack(bool next, char* outPath, size_t outCap) {
  if (!browser_ || currentPath_[0] != '/' || !outPath || outCap == 0) return false;

  const BrowserLocation visibleLocation = browser_->location();
  bool found = false;
  if (browser_->revealPath(currentPath_)) {
    found = next ? browser_->nextAudioAfter(currentName_, outPath, outCap)
                 : browser_->prevAudioBefore(currentName_, outPath, outCap);
  }
  browser_->restoreLocation(visibleLocation);
  return found;
}
```

- [ ] **Step 3: Use the helper**

Replace the directory lookup portions of `nextTrack()` and `prevTrack()`:

```cpp
bool Player::nextTrack() {
  char nextPath[cfg::kMaxPathLen];
  return adjacentTrack(true, nextPath, sizeof(nextPath)) && open(nextPath);
}
```

```cpp
  char prevPath[cfg::kMaxPathLen];
  if (adjacentTrack(false, prevPath, sizeof(prevPath))) {
    return open(prevPath);
  }
```

Leave the existing “restart current when past 3 seconds” and “restart first track” logic unchanged.

- [ ] **Step 4: Build and run native suite**

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv
~/.platformio/penv/bin/pio test -e native
```

Expected: firmware build succeeds; all existing native tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/player.hpp src/player.cpp
git commit -m "fix: keep browser location during track navigation"
```

---

### Task 6: Documentation and device acceptance

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document behavior**

After the paragraph explaining On boot modes, add:

```markdown
The Browser also remembers its last folder and highlighted entry independently of the
last played track. This location is restored after reboot and when returning from Now
Playing. If its folder or highlighted item no longer exists, Browser safely opens root `/`.
```

Update the on-device checklist with the six acceptance cases from the design spec.

- [ ] **Step 2: Run final verification**

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv
~/.platformio/penv/bin/pio test -e native
git diff --check
```

Expected: all commands return success.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: explain persistent browser location"
```

- [ ] **Step 4: On-device validation**

Flash:

```bash
~/.platformio/penv/bin/pio run -e cardputer-adv -t upload
```

Verify:

1. Browse to nested folder and select an entry; wait one second; reboot → same folder/item.
2. `On boot = play`, with Browser saved somewhere else → song plays; Back or `P` returns to saved Browser location.
3. `On boot = browse` and `off` → saved Browser restores; no track begins.
4. While a song plays, browse a different folder; player next/previous/auto-next still use the playing song’s folder; Browser stays in the different folder.
5. Delete/rename saved item or folder; reboot → root `/`, no crash.
6. Delete Browser keys from config; reboot → root `/`, no crash.

---

## Plan self-review

| Requirement | Coverage |
|---|---|
| Save folder + item name | Tasks 1, 3, 4 |
| Missing folder/item → root | Task 2 |
| Restore independent of `last_path` / On boot | Task 4 |
| Avoid high-frequency SD writes | Task 4 debounce + forced transitions |
| Player playlist independent from visible Browser | Task 5 |
| Backward compatibility | Task 3 defaults + Task 6 test |
| Documentation + build/device checks | Task 6 |

No placeholder steps; names are consistent: `BrowserLocation`, `location()`, `restoreLocation()`, `browserLocation()`, `rememberBrowserLocation()`.
