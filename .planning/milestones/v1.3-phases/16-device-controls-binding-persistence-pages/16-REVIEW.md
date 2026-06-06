---
phase: 16-device-controls-binding-persistence-pages
reviewed: 2026-05-24T00:00:00Z
depth: standard
files_reviewed: 11
files_reviewed_list:
  - src/app/src/stream_dock_control_service.hpp
  - src/app/src/stream_dock_control_service.cpp
  - src/app/src/profile_controller.hpp
  - src/app/src/profile_controller.cpp
  - src/app/src/application.cpp
  - src/app/qml/ProfileEditor.qml
  - src/app/qml/KeyDesigner.qml
  - src/app/qml/Main.qml
  - tests/unit/test_stream_dock_controls.cpp
  - tests/unit/test_profile_persistence.cpp
  - tests/unit/test_profile_pages.cpp
findings:
  critical: 2
  warning: 5
  info: 2
  total: 9
status: issues_found
---

# Phase 16: Code Review Report

**Reviewed:** 2026-05-24
**Depth:** standard
**Files Reviewed:** 11
**Status:** issues_found

## Summary

Phase 16 adds QML live controls (brightness slider, clear-all) for the Stream Dock panel, persisted profile bindings via `ProfileController::commitKeyBinding`, and a page-carousel navigation path driven by `StreamDockInputService`. The core device-yank exception safety and path-traversal sanitization work is solid and correctly implemented. The QML_SINGLETON pattern is correctly applied with the `static_assert` build-break lock and `create()/registerInstance()` factory.

Two blockers were found: (1) `StreamDockControlService::create()` returns `nullptr` silently when `registerInstance()` was not called, which will cause a null-dereference crash inside the QML engine in both debug and release builds (as opposed to `ProfileController::create()` which at minimum has a `Q_ASSERT_X` that catches this in debug); (2) `commitKeyBinding` performs an unchecked `static_cast<uint16_t>(keyIndex)` and an unchecked `static_cast<ActionKind>(actionKind)` with no range validation, so a negative key index from a misbehaving caller silently creates a key at index 65535 in `Profile::keys`, and an out-of-range `actionKind` integer produces an invalid-enum value that can reach the `ActionEngine` switch statement causing undefined behavior.

Five warnings cover: the carousel index not being reset when a new profile is loaded (stale position); `resetActiveProfile` preserving `pages` while clearing bindings (inconsistent reset semantics); `saveActiveProfile` emitting both `profileSaved` and `profilesChanged` via the `saveProfile` delegate even when the user did not explicitly add a new profile to the library; a missing test for the negative-value clamp path of `setBrightness`; and the `QDir::mkpath(".")` idiom inside `saveActiveProfile` not checking its return value.

______________________________________________________________________

## Critical Issues

### CR-01: `StreamDockControlService::create()` returns `nullptr` silently -- QML engine null-dereference crash

**File:** `src/app/src/stream_dock_control_service.cpp:76`

**Issue:** When `registerInstance()` has not been called before the QML engine's first singleton access, `g_instance` is `nullptr`. The `create()` implementation guards `setObjectOwnership` with `if (g_instance != nullptr)` but then unconditionally returns `g_instance` (null). The QML engine receives a null `QObject*` pointer for the singleton and immediately dereferences it, producing a crash. This is a regression from the reference pattern: `ProfileController::create()` has a `Q_ASSERT_X` that at minimum terminates with a descriptive message in debug builds. `StreamDockControlService::create()` gives no diagnostic at all -- in debug the crash is a mysterious null-dereference rather than an assertion message; in release it is silent and deadly.

The `application.cpp` code in `exposeToQml()` (line 525) does call `registerInstance()` before the engine loads, so this is not triggered in the normal path. But the guard is absent and the divergence from the established pattern is a latent hazard for any future refactoring that reorders `exposeToQml()` calls.

**Fix:**

```cpp
StreamDockControlService* StreamDockControlService::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance != nullptr,
               "StreamDockControlService::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    return g_instance;
}
```

______________________________________________________________________

### CR-02: `commitKeyBinding` performs unchecked `int -> uint16_t` and `int -> ActionKind` casts -- profile corruption and potential UB in ActionEngine

**File:** `src/app/src/profile_controller.cpp:142-152`

**Issue:** `commitKeyBinding(int keyIndex, ...)` immediately casts `keyIndex` to `uint16_t` without validating the input:

```cpp
auto const idx = static_cast<std::uint16_t>(keyIndex);
auto& binding = m_profile.keys[idx]; // operator[] creates the entry
```

A negative `keyIndex` (e.g., `-1`) produces `idx = 65535` (well-defined two's complement conversion but semantically wrong). The entry `m_profile.keys[65535]` is silently created, polluting the profile that gets serialized to disk and reloaded. `repaintPage()` correctly skips the key because `profileKeyIndex >= 255`, but the corrupt entry persists in the JSON file.

Similarly, `actionKind` is cast directly to `ActionKind` without bounds checking:

```cpp
act.kind = static_cast<ajazz::core::ActionKind>(actionKind);
```

`ActionKind` is a `uint8_t`-backed enum class with values 0-6. An `actionKind` of, e.g., `200` produces an invalid enum value. The `ActionEngine` dispatches on `kind` with a `switch`; an unhandled case with no `default` label is undefined behavior on some compilers and silently ignored on others -- both outcomes are wrong.

While `KeyDesigner.qml` guards `selectedIndex >= 0` before calling `commitKeyBinding`, the function is `Q_INVOKABLE` and callable directly from QML with arbitrary integers. The C++ function must own its own validation because it is a public API boundary.

**Fix:**

```cpp
void ProfileController::commitKeyBinding(int keyIndex,
                                         QString const& iconPath,
                                         QString const& label,
                                         int actionKind,
                                         QString const& settingsJson) {
    // Validate keyIndex: must be in [0, 65534] (key 65535 is the overflow sentinel).
    if (keyIndex < 0 || keyIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitKeyBinding: keyIndex {} out of valid range [0, 65534], ignoring",
                       keyIndex);
        return;
    }
    // Validate actionKind: must map to a defined ActionKind value.
    constexpr int kMaxActionKind = static_cast<int>(ajazz::core::ActionKind::BackToParent);
    if (actionKind < 0 || actionKind > kMaxActionKind) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitKeyBinding: actionKind {} out of range [0, {}], ignoring",
                       actionKind, kMaxActionKind);
        return;
    }
    auto const idx = static_cast<std::uint16_t>(keyIndex);
    // ... rest unchanged
```

______________________________________________________________________

## Warnings

### WR-01: Carousel index `m_carouselIndex` is not reset when a new profile is loaded

**File:** `src/app/src/stream_dock_control_service.hpp:295` and `stream_dock_control_service.cpp:254-282`

**Issue:** The member comment states _"reset when a new profile is loaded"_ but there is no code that does this. When `profileChanged` fires (either from `loadProfile` or `resetActiveProfile`), the signal is connected to `repaintFromProfile()` (which always repaints `"root"`), but `m_carouselIndex` stays at whatever page the user had navigated to before the load. On the next `navigatePage` call after a profile switch, the clamping in `navigatePage` brings the index back in range, but if the new profile has fewer pages than the old one, the first swipe after a profile switch may feel like it did nothing (it clamped in place rather than advancing). More importantly, if the new profile has zero pages beyond root, navigatePage becomes a no-op as expected, but `m_carouselIndex` is still `> 0`, which is misleading internal state.

The comment is a specification that the implementation does not satisfy. Stale-but-harmless state is a quality defect that will confuse the next contributor.

**Fix:** Reset `m_carouselIndex = 0` inside `repaintFromProfile()` (or add a dedicated `resetCarousel()` slot connected to `profileChanged`):

```cpp
void StreamDockControlService::repaintFromProfile() {
    m_carouselIndex = 0; // new profile always starts at root
    repaintPage(QStringLiteral("root"));
}
```

______________________________________________________________________

### WR-02: `resetActiveProfile()` preserves `pages` while clearing bindings -- semantically inconsistent reset

**File:** `src/app/src/profile_controller.cpp:179-187`

**Issue:** `resetActiveProfile()` clears `keys`, `encoders`, and `mouseButtons` but explicitly keeps `pages` intact with the comment _"user may have folder navigation they did not author here."_ This produces an inconsistent state: page keys are still bound (pages are preserved) but root keys are gone. A user who clicks "Restore defaults" expecting a blank slate will see root keys cleared but folder pages still populated when they navigate to a folder page via the carousel. The header doc (`profile_controller.hpp:179`) says it _"Clears all keys, encoders, mouseButtons maps"_ without mentioning pages are kept, which contradicts the implementation's choice.

The original design intent may be correct (folder pages could be machine-generated and the user shouldn't lose them), but it needs to be documented consistently, and `resetActiveProfile()` should clear `pages[*].keys` as well if pages are preserved. As written, the reset is incomplete: the page keys that triggered device key-image assignments survive the reset, so after "Restore defaults" the device may still show folder key images from old pages on carousel navigation.

**Fix:** Either (a) also clear the bindings inside each preserved page:

```cpp
void ProfileController::resetActiveProfile() {
    m_profile.keys.clear();
    m_profile.encoders.clear();
    m_profile.mouseButtons.clear();
    // Also clear per-page key bindings while preserving page structure.
    for (auto& [id, page] : m_profile.pages) {
        page.keys.clear();
    }
    emit profileChanged();
    saveActiveProfile();
}
```

Or (b) also clear `m_profile.pages` entirely and update the header doc to describe the actual behavior.

______________________________________________________________________

### WR-03: `saveActiveProfile()` does not check the return value of `QDir::mkpath(".")`

**File:** `src/app/src/profile_controller.cpp:165-170`

**Issue:**

```cpp
QDir dir = QFileInfo(path).absoluteDir();
if (!dir.exists()) {
    dir.mkpath(QStringLiteral("."));
}
saveProfile(path);
```

`QDir::mkpath()` returns `bool`. If directory creation fails (e.g., permissions error, read-only filesystem, parent path is a file not a directory), the failure is silently swallowed and `saveProfile(path)` is called immediately after. `writeProfileToDisk` will then throw `ProfileIoError` and `saveFailed` will fire, but the error message will be about the write, not the directory creation failure, making debugging harder.

Additionally, the `!dir.exists()` check introduces a TOCTOU race: the directory could be created between the check and the `mkpath()` call, but `QDir::mkpath()` is idempotent (returns `true` if the path already exists), so the race is harmless. The real issue is the unchecked return value.

**Fix:**

```cpp
QDir dir = QFileInfo(path).absoluteDir();
if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    emit saveFailed(tr("Cannot create profiles directory: %1").arg(dir.absolutePath()));
    return;
}
saveProfile(path);
```

______________________________________________________________________

### WR-04: `saveProfile()` always emits `profilesChanged` -- misleads listeners when no new profile was added to the library

**File:** `src/app/src/profile_controller.cpp:67-68`

**Issue:**

```cpp
emit profileSaved(path);
emit profilesChanged();
```

`profilesChanged()` is documented as _"Emitted when the list of known profiles changes (added, removed, renamed)"_. Saving the active profile in-place does not change the list of known profiles; only the content of the active profile changes. Emitting `profilesChanged()` on every `saveProfile()` call misrepresents the event to all listeners (e.g., `TrayController::profileSwitchRequested` or a future profile-library widget that rebuilds itself on `profilesChanged`). This will cause spurious UI refreshes when the profile library grows.

**Fix:** Emit `profilesChanged()` only when a new profile id becomes reachable (i.e., when the saved path corresponds to an id not previously in the library), not unconditionally. For the current single-profile-in-memory implementation, emit `profilesChanged()` only from `saveActiveProfile()` when `m_profile.id` was not already in `knownProfileIds()` before the save. A minimal fix is to remove `emit profilesChanged()` from `saveProfile()` and add it only in `saveActiveProfile()` after a first-save.

______________________________________________________________________

### WR-05: No unit test covers negative `setBrightness(codename, -50)` -- clamping to 0 is untested

**File:** `tests/unit/test_stream_dock_controls.cpp`

**Issue:** The test suite validates that `setBrightness(codename, 150)` clamps to 100 (upper bound) but there is no test for the lower-bound clamp: `setBrightness(codename, -50)` should produce a LIG with brightness byte `0`. The `std::clamp(percent, 0, 100)` implementation correctly handles this, but the absence of a test means any future change that uses `static_cast<uint8_t>(percent)` directly (without clamping) would not be caught. Given the existing clamping test pattern, adding a symmetric lower-bound test is low effort.

**Fix:** Add a test case mirroring the existing upper-bound clamp test:

```cpp
TEST_CASE("StreamDockControlService: setBrightness clamps negative value to 0",
          "[stream-dock-controls][DISPLAY-09]") {
    // ... same fixture setup ...
    svc.setBrightness(QStringLiteral("akp05e"), -50);
    drainQueue();
    auto const ligIdx = findPacketByCmd(writes, 0x4C, 0x49, 0x47, writeCountAfterOpen);
    REQUIRE(ligIdx < writes.size());
    CHECK(writes[ligIdx][10] == 0); // clamped to 0
}
```

______________________________________________________________________

## Info

### IN-01: `commitKeyBinding` doc comment has ambiguous "0-based or 1-based" key index description

**File:** `src/app/src/profile_controller.hpp:138`

**Issue:** The `@param keyIndex` doc says _"0-based or 1-based key index as used by KeyDesigner (Profile::keys are std::uint16_t -- stored as-is)"_. The "or" is ambiguous -- the function stores the index as-is, so the caller fully determines the indexing scheme. `KeyDesigner.qml` passes `selectedIndex` (0-based), so `Profile::keys` will contain 0-based indices. But `repaintPage()` adds 1 to profile key indices before passing to the device backend. If a caller used 1-based indices here, the device would receive key index + 1, painting the wrong key. The doc comment should state clearly that the function expects 0-based indices matching `Profile::keys` conventions.

**Fix:** Update the doc comment:

```cpp
/// @param keyIndex  0-based key index (matches Profile::keys map keys). The paint
///                  service adds 1 when converting to the device's 1-based scheme.
///                  Passing a 1-based index will silently paint the wrong key.
```

______________________________________________________________________

### IN-02: `sanitizeProfileId` does not cap path length -- theoretical filesystem limit issue

**File:** `src/app/src/profile_controller.cpp:108`

**Issue:** The sanitizer filters unsafe characters but places no upper bound on the resulting filename length. On Linux the filename limit is 255 bytes; on macOS 255 UTF-8 characters; on Windows (non-extended-path) 260 total characters for the full path. A profile id with 300 safe characters (all `[A-Za-z0-9._-]`) produces a 305-character filename (`id + ".json"`). Combined with a long `AppDataLocation` prefix, this can silently exceed `PATH_MAX` on Windows.

This is not exploitable (the path stays under `AppDataLocation`) but write failures would produce confusing `ProfileIoError` messages on Windows without indicating the root cause.

**Fix:** Add a length cap after filtering, before the empty check:

```cpp
// Cap at 200 characters to stay comfortably under filename limits on all platforms.
if (result.size() > 200) {
    result.truncate(200);
}
```

______________________________________________________________________

_Reviewed: 2026-05-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
