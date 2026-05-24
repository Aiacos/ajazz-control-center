---
phase: 16-device-controls-binding-persistence-pages
verified: 2026-05-24T14:00:00Z
status: human_needed
score: 8/8 must-haves verified
overrides_applied: 0
human_verification:
  - test: Drag the brightness slider in the Keys tab and observe the physical AKP05E panel
    expected: Panel visibly dims and brightens in proportion to the slider value, with no flooding of writes (debounce holds)
    why_human: LIG write is confirmed by MockTransport byte assertion; actual panel response requires the physical device with uaccess ACL
  - test: Press 'Clear all keys' button with the AKP05E connected
    expected: All LCD key surfaces go blank; CLE opcode confirmed by MockTransport test but physical blanking is the witness
    why_human: Physical panel blanking cannot be asserted without hardware
  - test: Commit a binding (icon + label + action), click Apply, quit and relaunch the app, reconnect device
    expected: After restart, the device repaints the bound key with the saved icon and label — edit survives restart
    why_human: Round-trip is verified by fresh-controller unit test; the physical repaint-after-restart leg requires hardware and an actual app restart
  - test: Swipe left/right on the AKP05E touch strip with a multi-page profile loaded
    expected: Device LCD keys change to show the bindings of the adjacent page; root page appears on swipe back
    why_human: pageNavRequested carousel + repaintPage is MockTransport-verified; touch swipe -> physical key change requires hardware
---

# Phase 16: Device Controls + Binding Persistence + Pages — Verification Report

**Phase Goal:** Brightness slider + "clear all" drive the device live; key/encoder/touch bindings persist and survive restart; multi-page/folder profiles drive host-side page navigation.
**Verified:** 2026-05-24T14:00:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

______________________________________________________________________

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                                                           | Status   | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| --- | ------------------------------------------------------------------------------------------------------------------------------- | -------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | A brightness Slider in the ProfileEditor Keys tab drives a LIG write with the dragged value via the control service (debounced) | VERIFIED | `ProfileEditor.qml:292-312` — 80 ms single-shot `Timer` debounce on `onMoved`; final `setBrightness` on `onPressedChanged(!pressed)`. Test #209 asserts `ligPkt[10]==42` via MockTransport.                                                                                                                                                                                                                                                                                                                                             |
| 2   | A "Clear all keys" control issues a CLE write (`clearKey(0xFF)`) via the control service                                        | VERIFIED | `ProfileEditor.qml:315-319` — `SecondaryButton` `onClicked: StreamDockControlService.clearAll(root.codename)`. Test #212 asserts `clePkt[5..7]=='C','L','E'`.                                                                                                                                                                                                                                                                                                                                                                           |
| 3   | Dragging the slider does NOT flood LIG writes — N drag steps coalesce to a small bounded number of writes                       | VERIFIED | `ProfileEditor.qml:292-302` — single-shot Timer (interval: 80, repeat: false) with `onMoved: brightnessDebounce.restart()` ensures at most one write per 80 ms window plus one on release.                                                                                                                                                                                                                                                                                                                                              |
| 4   | The control service is QML-exposed as a QML_SINGLETON via `create()`/`registerInstance()` (never the bare macro alone)          | VERIFIED | `stream_dock_control_service.hpp:77-78,306-308` — `QML_NAMED_ELEMENT(StreamDockControlService)` + `QML_SINGLETON` + `static_assert(!is_default_constructible_v<StreamDockControlService>)`. `application.cpp:525` — `StreamDockControlService::registerInstance(m_streamDockControl.get())`. `stream_dock_control_service.cpp:77-79` — `create()` uses `Q_ASSERT_X` (not conditional null return).                                                                                                                                      |
| 5   | Key, encoder, and touch bindings persist to profile and survive restart; `deviceCodename` / `"device"` wire-key preserved       | VERIFIED | `profile_controller.cpp:144-147` — `defaultProfilePath()` resolves under `AppDataLocation/profiles/`. `profile_controller.cpp:150-188` — `commitKeyBinding()` with input validation (CR-02). `KeyDesigner.qml:100-106` — `updateSelectedBinding` calls `ProfileController.commitKeyBinding`. `Main.qml:145-146` — `saveActiveProfile` / `loadActiveProfile` replacing the old "not implemented" toasts. `profile.cpp:792` — `"device"` wire key intact. Tests #214-222 prove round-trip for keys, encoders, pages, and repaint-on-load. |
| 6   | After load, the control service repaints the saved keys (BAT+ULEND per bound key) via `profileChanged`                          | VERIFIED | `application.cpp:354-357` — `profileChanged -> repaintFromProfile` connect. `stream_dock_control_service.cpp:168-174` — `repaintFromProfile()` delegates to `repaintPage("root")`. Test #222 asserts `batCount >= 2` and `ulendCount >= 2` after `loadProfile`.                                                                                                                                                                                                                                                                         |
| 7   | `repaintPage(pageId)` resolves "root" -> `Profile::keys`; other id -> `Profile::pages[id].keys`; missing id is a no-op          | VERIFIED | `stream_dock_control_service.cpp:177-257` — `repaintPage` uses `prof.pages.find(pageId)` (not `at()`), logs warning and returns on miss (T-16c-01). Tests #223/224/225 assert root keys painted for "root", child keys for "folderA", zero writes for "doesNotExist".                                                                                                                                                                                                                                                                   |
| 8   | `pageNavRequested(+1/-1)` drives a carousel over top-level pages; single-root profile is a no-op                                | VERIFIED | `stream_dock_control_service.cpp:259-287` — `navigatePage()` builds `["root"] + sorted(pages.keys)`, clamps index to `[0, size-1]`, early-returns when `size <= 1`. `application.cpp:365-368` — `pageNavRequested -> navigatePage` connect. Tests #227/228/229 verify next-page repaint, previous-page repaint, and single-root no-op.                                                                                                                                                                                                  |

**Score:** 8/8 truths verified

______________________________________________________________________

### Required Artifacts

| Artifact                                      | Expected                                                                                                              | Status   | Details                                                                                                                                                                                                          |
| --------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/app/src/stream_dock_control_service.hpp` | Q_INVOKABLE setBrightness/clearAll; QML_SINGLETON create/registerInstance + static_assert; repaintPage + navigatePage | VERIFIED | All present. `Q_INVOKABLE void setBrightness(...)` line 260; `Q_INVOKABLE void clearAll(...)` line 272; `void repaintPage(...)` line 197; `Q_SLOT void navigatePage(...)` line 217. `static_assert` at line 306. |
| `src/app/qml/ProfileEditor.qml`               | Brightness Slider (debounced) + Clear-all button bound to `StreamDockControlService`, visible only on LCD-key devices | VERIFIED | Lines 270-319 present: `visible: root._showKeys && root.codename !== ""`, `Timer{interval:80; repeat:false}`, `StreamDockControlService.setBrightness` and `StreamDockControlService.clearAll` calls.            |
| `tests/unit/test_stream_dock_controls.cpp`    | MockTransport wire assertions for setBrightness->LIG, clearAll->CLE, debounce-bound, clamp                            | VERIFIED | 282 lines; 5 TEST_CASEs covering LIG assertion (byte[10]==42), upper clamp (100), lower clamp (0 from WR-05), CLE assertion, and non-display no-op.                                                              |
| `src/app/src/profile_controller.hpp`          | defaultProfilePath() + Q_INVOKABLE commitKeyBinding + saveActiveProfile/loadActiveProfile; static_assert              | VERIFIED | Lines 123-187 present. `static_assert(!is_default_constructible_v<ProfileController>)` at line 249.                                                                                                              |
| `src/app/qml/KeyDesigner.qml`                 | Bindings ListModel rows commit to ProfileController on edit                                                           | VERIFIED | Lines 97-106: `ProfileController.commitKeyBinding(root.selectedIndex, row.iconSource, row.label, row.actionKind, row.actionParams)`.                                                                             |
| `tests/unit/test_profile_persistence.cpp`     | commit -> save -> fresh-controller load round-trip + repaint assertion                                                | VERIFIED | 517 lines; 9 TEST_CASEs covering path resolution, traversal sanitization, commit/mutation, empty-field nullopt, key round-trip, encoder round-trip, pages round-trip, saveActiveProfile/mkpath, repaint-on-load. |
| `tests/unit/test_profile_pages.cpp`           | repaintPage(child) paints child bindings; pageNavRequested(+1/-1) switches+repaints; single-root no-op                | VERIFIED | 466 lines; 7 TEST_CASEs per plan 16-03 spec.                                                                                                                                                                     |

______________________________________________________________________

### Key Link Verification

| From                              | To                                                  | Via                                                                                                       | Status | Details                                                                                                               |
| --------------------------------- | --------------------------------------------------- | --------------------------------------------------------------------------------------------------------- | ------ | --------------------------------------------------------------------------------------------------------------------- |
| `ProfileEditor.qml`               | `StreamDockControlService`                          | `Slider onMoved/onPressedChanged + Timer debounce -> setBrightness; Button onClicked -> clearAll`         | WIRED  | `ProfileEditor.qml:296,310,318` confirmed                                                                             |
| `stream_dock_control_service.cpp` | `core::IDisplayCapable`                             | `dynamic_cast<core::IDisplayCapable*>` + null-check within 3 lines, then `setBrightness`/`clearKey(0xFF)` | WIRED  | 5 cast sites at lines 139, 182, 320, 349, 375 — each followed by `if (disp == nullptr) { ... }` on the very next line |
| `application.cpp`                 | `StreamDockControlService`                          | `StreamDockControlService::registerInstance(m_streamDockControl.get())` in `exposeToQml()`                | WIRED  | `application.cpp:525`                                                                                                 |
| `KeyDesigner.qml`                 | `ProfileController`                                 | `updateSelectedBinding` -> `ProfileController.commitKeyBinding(...)`                                      | WIRED  | `KeyDesigner.qml:100-106`                                                                                             |
| `profile_controller.cpp`          | `core::writeProfileToDisk`                          | `saveProfile(path)` -> `writeProfileToDisk(fsPath, m_profile)`                                            | WIRED  | `profile_controller.cpp:64-79`                                                                                        |
| `profile_controller.cpp`          | `core::profileFromJson` (via `readProfileFromDisk`) | `loadProfile(path)` -> `readProfileFromDisk(fsPath)` -> `emit profileChanged()`                           | WIRED  | `profile_controller.cpp:51-62`                                                                                        |
| `application.cpp`                 | `StreamDockInputService::pageNavRequested`          | `connect(m_streamDockInput, pageNavRequested, m_streamDockControl, navigatePage)`                         | WIRED  | `application.cpp:365-368`                                                                                             |

______________________________________________________________________

### Data-Flow Trace (Level 4)

| Artifact                                 | Data Variable                                                     | Source                                                                                                          | Produces Real Data                                                                      | Status  |
| ---------------------------------------- | ----------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- | ------- |
| `StreamDockControlService::repaintPage`  | `bindingSet` (pointer to `std::unordered_map<uint16_t, Binding>`) | `m_profileAccessor()` -> `ProfileController::activeProfile()` -> `m_profile.keys` or `m_profile.pages[id].keys` | Yes — mutated by `commitKeyBinding` and deserialized from disk by `readProfileFromDisk` | FLOWING |
| `StreamDockControlService::navigatePage` | `prof.pages` (unordered_map for carousel list)                    | `m_profileAccessor()` at call time                                                                              | Yes — same profile accessor                                                             | FLOWING |
| `ProfileEditor.qml Slider`               | `brightnessSlider.value`                                          | User drag; initial `value: 80`                                                                                  | User-driven at runtime; initial value matches `kDefaultBrightnessPercent`               | FLOWING |
| `ProfileController::commitKeyBinding`    | `m_profile.keys[idx]`                                             | QML editor via `updateSelectedBinding`; mutates in-place                                                        | Yes — written to C++ Profile immediately                                                | FLOWING |

______________________________________________________________________

### Behavioral Spot-Checks

| Behavior                                   | Command                                           | Result                    | Status |
| ------------------------------------------ | ------------------------------------------------- | ------------------------- | ------ |
| setBrightness LIG write                    | `ctest --test-dir build/linux-release -I 209,213` | Test #209-213: 5/5 passed | PASS   |
| Profile persistence round-trip             | `ctest --test-dir build/linux-release -I 214,222` | Test #214-222: 9/9 passed | PASS   |
| Page repaint (root/child/missing/carousel) | `ctest --test-dir build/linux-release -I 223,229` | Test #223-229: 7/7 passed | PASS   |
| Full suite regression                      | `ctest --test-dir build/linux-release`            | 445/445 passed            | PASS   |

______________________________________________________________________

### Probe Execution

No probe scripts declared for Phase 16. Hardware-free proof via MockTransport unit tests; physical witness deferred to Phase 25 per plan design.

| Probe  | Command | Result | Status                                 |
| ------ | ------- | ------ | -------------------------------------- |
| (none) | N/A     | N/A    | SKIP — no probe scripts for this phase |

______________________________________________________________________

### Requirements Coverage

| Requirement | Source Plan   | Description                                                                                                | Status    | Evidence                                                                                                                                                                   |
| ----------- | ------------- | ---------------------------------------------------------------------------------------------------------- | --------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DISPLAY-09  | 16-01-PLAN.md | Brightness slider (`LIG`) + "clear all" (`CLE`) drive the device live                                      | SATISFIED | `setBrightness` -> LIG: test #209; `clearAll` -> CLE: test #212; QML slider/button: `ProfileEditor.qml:279-319`; QML_SINGLETON wired: `application.cpp:525`                |
| PROFILE-01  | 16-02-PLAN.md | Key/encoder/touch bindings persist; `deviceCodename` \<-> `"device"` wire key preserved; repaint on reload | SATISFIED | Round-trip tests #218/219/220; repaint-on-load test #222; `"device"` key: `profile.cpp:792`; `commitKeyBinding` input validation (CR-02): `profile_controller.cpp:155-171` |
| PROFILE-02  | 16-03-PLAN.md | Multi-page/folder profiles, host-side page nav, no device page opcode                                      | SATISFIED | `repaintPage`: tests #223-226; carousel `navigatePage`: tests #227-229; `application.cpp:365-368` wires `pageNavRequested`; no STP opcode added (wire files untouched)     |

No orphaned requirements found — REQUIREMENTS.md maps DISPLAY-09/PROFILE-01/PROFILE-02 to Phase 16 with `[x]` checked, no unmapped Phase 16 entries.

______________________________________________________________________

### Anti-Patterns Found

| File   | Line | Pattern                                        | Severity | Impact |
| ------ | ---- | ---------------------------------------------- | -------- | ------ |
| (none) | —    | No TBD/FIXME/XXX found in phase-modified files | —        | —      |

Debt-marker scan: zero `TBD`, `FIXME`, or `XXX` markers found in the 10 phase-modified files.

Stub scan: no hardcoded empty returns, no `return null` in UI paths, no `=> {}` stubs in the phase files. The `loadProfileById` stub in `profile_controller.cpp` (returns `loadFailed` with an informative message) is documented as a follow-up (issue #24) and is not within this phase's scope.

______________________________________________________________________

### Special Verification Context Checks

The following items from `<verification_context>` were explicitly verified:

| Check                                                                               | Result                                                                                                                                                                                                                                                                                           |
| ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `StreamDockControlService::create()` has `Q_ASSERT_X` guard (CR-01)                 | VERIFIED — `stream_dock_control_service.cpp:77-79`: `Q_ASSERT_X(g_instance != nullptr, ...)`                                                                                                                                                                                                     |
| `commitKeyBinding` rejects out-of-range `keyIndex` and invalid `actionKind` (CR-02) | VERIFIED — `profile_controller.cpp:155-171`: guards with `AJAZZ_LOG_WARN` + early return for `keyIndex < 0 \|\| keyIndex > 65534` and `actionKind > kMaxActionKind`                                                                                                                              |
| `m_carouselIndex` reset in `repaintFromProfile` (WR-01)                             | VERIFIED — `stream_dock_control_service.cpp:172`: `m_carouselIndex = 0;` at top of `repaintFromProfile()`                                                                                                                                                                                        |
| `saveActiveProfile` checks `mkpath` return value (WR-03)                            | VERIFIED — `profile_controller.cpp:200-202`: `if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) { emit saveFailed(...); return; }`                                                                                                                                                          |
| QML_SINGLETON uses `qmlRegisterSingletonInstance` pattern (not bare macro alone)    | VERIFIED — `application.cpp:525`: `StreamDockControlService::registerInstance(m_streamDockControl.get())`. The bare `QML_SINGLETON` macro is present in the header for Qt discovery but the live-instance contract is enforced by `create()` calling `Q_ASSERT_X` on the pre-registered pointer. |
| Profile persistence JSON wire key `"device"` (not `deviceCodename`) preserved       | VERIFIED — `profile.cpp:792`: `} else if (key == "device") {` in `profileFromJson`; `writeProfileToDisk` goes through `profileToJson` which is tested to round-trip the `"device"` key                                                                                                           |
| Profile-id path sanitization blocks `../` traversal                                 | VERIFIED — `profile_controller.cpp:114-139`: allowlist `[A-Za-z0-9._-]`, leading-dot strip, empty -> "default", length cap 200. Test #215 asserts `"../etc/passwd"` produces no `..` in the path.                                                                                                |
| COD-031 boundary: no `nlohmann` in `src/core/include`                               | VERIFIED — `grep -c nlohmann src/core/include/**/*.hpp` returned no matches                                                                                                                                                                                                                      |

______________________________________________________________________

### Human Verification Required

Physical device confirmation for Phase 16 behaviors is deferred to Phase 25 per plan design. The following items require human testing with the AKP05E connected:

#### 1. Brightness Slider — Live Panel Response

**Test:** With the AKP05E connected (uaccess ACL in place), drag the brightness slider in the Keys tab from 0 to 100 and back.
**Expected:** Panel visibly dims at low values and brightens at high values; no perceptible flooding or flicker during drag (debounce holds); slider value is applied on pointer release.
**Why human:** LIG wire write is MockTransport-verified (test #209). Physical panel response requires the device.

#### 2. Clear All Keys — Live Blanking

**Test:** With bound key images visible on the AKP05E, click "Clear all keys".
**Expected:** All LCD key surfaces go blank immediately.
**Why human:** CLE wire write is MockTransport-verified (test #212). Physical blanking requires the device.

#### 3. Binding Persistence Across Restart

**Test:** Commit a binding (icon + label + OpenUrl action) via the Keys tab editor, click Apply, quit the app fully, relaunch, reconnect the AKP05E.
**Expected:** The device repaints the bound key with the saved icon/label immediately on reconnect. The editor displays the saved binding. The cycle matches the fresh-controller round-trip proven in test #221/222.
**Why human:** Unit test #221 proves `saveActiveProfile` writes the correct file and test #222 proves `loadProfile` -> `repaintFromProfile` paints the keys via MockTransport. The physical "brightness ON + image visible after restart" leg requires hardware.

#### 4. Page Navigation via Swipe

**Test:** Load a multi-page profile (at least 2 top-level pages with distinct key bindings). Swipe right on the touch strip. Swipe left.
**Expected:** Swipe right switches to the next page's key bindings (different images paint); swipe left returns to the previous page's bindings. Single-root profile ignores swipe (confirmed by test #229 no-op).
**Why human:** `pageNavRequested -> navigatePage -> repaintPage` is fully MockTransport-verified (tests #227/228/229). Physical touch-strip swipe -> key change requires hardware; also defers provisional touch zone/swipe map reconciliation (Phase 25 VERIFY-05).

______________________________________________________________________

### Gaps Summary

No gaps. All 8 must-have truths are VERIFIED in the codebase with substantive implementation and correct wiring. The 4 human-verification items are genuine hardware-dependent tests, not code gaps — this is by design per `<verification_context>` ("Physical AKP05E confirmation is DEFERRED to Phase 25. 'No physical device' is NOT a gap").

______________________________________________________________________

_Verified: 2026-05-24T14:00:00Z_
_Verifier: Claude (gsd-verifier)_
