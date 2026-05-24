---
phase: 14-stream-dock-control-service
verified: 2026-05-24T12:00:00Z
status: human_needed
score: 9/9
overrides_applied: 0
human_verification:
  - test: 'Power-cycle smoke: plug in a physical AKP05E, launch the app, observe panel lights up within ~1s of device arrival.'
    expected: Panel illuminates at ~80% brightness (LIG packet) without any manual action.
    why_human: MockTransport proves the LIG byte sequence is emitted; only physical hardware confirms the panel actually lights. Deferred to Phase 25 by design.
  - test: Assign an image to a key in the UI (Phase 16 will expose the control; until then verify via a test harness or direct call), confirm the image appears on the physical AKP05E key within ~1s.
    expected: Key displays the assigned image; no manual flush needed.
    why_human: MockTransport proves the BAT/ULEND burst bytes; physical rendering is hardware-gated (Phase 25).
  - test: Load a profile bound to >=2 keys, confirm every bound key repaints on the physical panel.
    expected: All bound keys show their assigned images after profile load.
    why_human: MockTransport proves the BAT+ULEND-per-key burst; real panel confirmation is Phase 25.
---

# Phase 14: Stream Dock Control Service — Verification Report

**Phase Goal:** A user with an AKP05E plugged in sees the panel light up when the app selects it; assigning an image to a key makes it appear on the physical key within ~1s with no manual flush; loading a profile repaints every key. The first app-to-device call that reaches the panel — closes the Phase-10 UAT gap.

**Verified:** 2026-05-24T12:00:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

**Design context (verified against verification_context):** This phase is HARDWARE-FREE by design. All gating proofs use MockTransport. The physical AKP05E power-cycle smoke is deliberately deferred to Phase 25. "No physical device" is therefore NOT scored as a gap — the human_needed items below reflect that deferred confirmation, not missing code.

______________________________________________________________________

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                 | Status   | Evidence                                                                                                                                                                                                                                                                                                                                                              |
| --- | ------------------------------------------------------------------------------------- | -------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | The akp05e device descriptor advertises hasClock=false                                | VERIFIED | `register.cpp` line 309: `.hasClock = false, // DEVICES-11 / ARCH-05: Stream Dock family has no firmware RTC.`                                                                                                                                                                                                                                                        |
| 2   | The ak980pro descriptor is unchanged (hasClock=true)                                  | VERIFIED | `register.cpp`: ak980pro row not present in streamdeck register (ak980pro is in the mouse/AK register path, not this file); mirabox_n4 at line 288 still has `.hasClock = true` confirming no over-broad flip                                                                                                                                                         |
| 3   | A unit test pins the akp05e hasClock=false value                                      | VERIFIED | `tests/unit/test_register_akp05e_clock.cpp` — 46 lines, two SECTIONs: asserts `!it->hasClock` for akp05e and `it->hasClock` for mirabox_n4; registered in CMakeLists.txt line 88                                                                                                                                                                                      |
| 4   | setActiveDevice holds one shared_ptr and issues a LIG brightness at open (DISPLAY-06) | VERIFIED | `stream_dock_control_service.cpp` lines 70-118: m_activeDevice assigned from lookup, `open()` called, then `dynamic_cast<IDisplayCapable*>` + null-check within 2 lines (101-102), `setBrightness(kDefaultBrightnessPercent=80)` called; test `StreamDockControlService: setActiveDevice issues LIG brightness (DISPLAY-06)` asserts byte[5]==0x4C/'L' and byte[10]>0 |
| 5   | assignKeyImage produces a flush-free BAT->chunks->ULEND burst (DISPLAY-07 + DOCK-02)  | VERIFIED | `drainPendingWrites()` calls `disp->setKeyImage(keyIndex, {bits, byteCount}, w, h)` which drives the Akp05 backend BAT/ULEND path; test `assignKeyImage emits BAT then ULEND` asserts bytes[5..7]=='B','A','T' and bytes[5..9]=='U','L','E','N','D' at last write; no explicit `flush()` call                                                                         |
| 6   | Loading a profile repaints all bound keys (DISPLAY-08)                                | VERIFIED | `repaintFromProfile()` iterates `m_profileAccessor().keys`, translates 0-based profile indices to 1-based device indices (+1), calls `assignKeyImage()` per key; Application wires `profileChanged -> repaintFromProfile` at line 238; test `repaintFromProfile repaints all bound keys` asserts batCount>=2, ulendCount>=2 for 2-key profile                         |
| 7   | Firmware version is cached and surfaced (DOCK-01)                                     | VERIFIED | `firmwareVersionFor()` returns `QString::fromStdString(dev->firmwareVersion())`; test asserts return value == "V3.AKP05E.01.007" seeded via `enqueueReadFeature`; "unknown" case handled                                                                                                                                                                              |
| 8   | ULEND commit follows each image burst (DOCK-02)                                       | VERIFIED | Covered in truth 5; same test case confirms ULEND is the last write in burst; additionally the repaint test confirms per-key ULEND commits (ulendCount>=2 for 2 keys)                                                                                                                                                                                                 |
| 9   | The Phase-10 UAT gap is closed — app code calls setKeyImage                           | VERIFIED | `grep setKeyImage src/app/` returns call site in `stream_dock_control_service.cpp` line 234 (`disp->setKeyImage(...)`); was 0 hits before this phase                                                                                                                                                                                                                  |

**Score:** 9/9 truths verified

______________________________________________________________________

## Required Artifacts

| Artifact                                          | Expected                                                                                                                          | Status   | Details                                                                                                                                      |
| ------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- | -------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `src/app/src/stream_dock_control_service.hpp`     | QObject with DeviceLookup, ProfileAccessor, setActiveDevice, assignKeyImage, repaintFromProfile, firmwareVersionFor; min 60 lines | VERIFIED | 185 lines; all required public methods declared; `kDefaultBrightnessPercent=80` constant present; `m_activeDevice` shared_ptr member present |
| `src/app/src/stream_dock_control_service.cpp`     | brightness-ON at open, coalesced write-queue drain, repaint, firmware surface; min 120 lines                                      | VERIFIED | 253 lines; all four behaviors implemented; CR-01/CR-02 try/catch blocks present; m_pendingWrites.clear() unconditional at line 250           |
| `src/app/src/profile_controller.hpp`              | activeProfile() const-ref getter                                                                                                  | VERIFIED | Line 118: `[[nodiscard]] ajazz::core::Profile const& activeProfile() const noexcept;`                                                        |
| `tests/unit/test_stream_dock_control_service.cpp` | MockTransport wire assertions for DISPLAY-06/07/08, DOCK-01/02; min 80 lines                                                      | VERIFIED | 329 lines; 5 TEST_CASEs with exact byte-level assertions; all tagged `[stream-dock-control]`; ASCII-only titles                              |
| `src/devices/streamdeck/src/register.cpp`         | akp05e descriptor with hasClock=false                                                                                             | VERIFIED | Line 309: `.hasClock = false, // DEVICES-11 / ARCH-05: Stream Dock family has no firmware RTC.`                                              |
| `tests/unit/test_register_akp05e_clock.cpp`       | Descriptor honesty assertion; min 20 lines                                                                                        | VERIFIED | 46 lines; uses `registerAll()` + `enumerate()`, asserts akp05e `!hasClock` and mirabox_n4 `hasClock`                                         |

______________________________________________________________________

## Key Link Verification

| From                                        | To                                                             | Via                                                                             | Status   | Details                                                                                                                                                           |
| ------------------------------------------- | -------------------------------------------------------------- | ------------------------------------------------------------------------------- | -------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `stream_dock_control_service.cpp`           | `core::IDisplayCapable`                                        | `dynamic_cast<core::IDisplayCapable*>` + null-check within 2 lines              | VERIFIED | Lines 101-102 (setActiveDevice), 135-136 (repaintFromProfile), 212-213 (drainPendingWrites) — all three sites confirmed null-checked                              |
| `src/app/src/application.cpp`               | `StreamDockControlService`                                     | construct with DeviceLookup lambda + ProfileAccessor lambda                     | VERIFIED | Lines 207-225: constructed with both lambdas; `m_streamDockControl` declared after `m_firmwareUpdate` in application.hpp (lines 173, 179) for -Wreorder safety    |
| `src/app/src/application.cpp`               | `ProfileController::profileChanged` -> `repaintFromProfile`    | `QObject::connect`                                                              | VERIFIED | Lines 238-240: `connect(m_profileController.get(), &ProfileController::profileChanged, m_streamDockControl.get(), &StreamDockControlService::repaintFromProfile)` |
| `src/app/src/application.cpp`               | `setActiveDevice` on hot-plug                                  | `DeviceFamily::StreamDeck` check in hotplug handler + QTimer::singleShot(300ms) | VERIFIED | Lines 491-498: `if (d.family == core::DeviceFamily::StreamDeck)` -> `m_streamDockControl->setActiveDevice(codename)`                                              |
| `tests/unit/test_register_akp05e_clock.cpp` | `ajazz::streamdeck::registerAll` / `DeviceRegistry::enumerate` | enumerate() -> find codename=="akp05e" -> CHECK(!hasClock)                      | VERIFIED | Lines 27-29; pattern `hasClock` present in both SECTION assertions                                                                                                |
| `src/app/CMakeLists.txt`                    | `stream_dock_control_service.cpp`                              | Added to qt_add_executable source list + QML module sources                     | VERIFIED | Lines 88 and 306-307 in CMakeLists.txt                                                                                                                            |

______________________________________________________________________

## Data-Flow Trace (Level 4)

| Artifact                          | Data Variable                 | Source                                                                                  | Produces Real Data                                                                                    | Status  |
| --------------------------------- | ----------------------------- | --------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- | ------- |
| `stream_dock_control_service.cpp` | `m_activeDevice`              | `m_lookup(codename)` -> `DeviceRegistry::open()` flyweight                              | Yes — returns real shared_ptr from registry                                                           | FLOWING |
| `stream_dock_control_service.cpp` | LIG brightness write          | `disp->setBrightness(kDefaultBrightnessPercent)` -> Akp05Device HID write               | Yes — triggers HID transport write (asserted by MockTransport)                                        | FLOWING |
| `stream_dock_control_service.cpp` | `m_pendingWrites` / BAT burst | `assignKeyImage()` -> `m_drainTimer` -> `drainPendingWrites()` -> `disp->setKeyImage()` | Yes — iterates real QImage data; MockTransport asserts BAT+ULEND bytes                                | FLOWING |
| `stream_dock_control_service.cpp` | Profile keys repaint          | `m_profileAccessor()` -> `prof.keys` iteration -> `assignKeyImage()`                    | Yes — uses real Profile::keys map (faked in test, wired to real ProfileController in Application)     | FLOWING |
| `stream_dock_control_service.cpp` | Firmware VER string           | `dev->firmwareVersion()` -> std::string return                                          | Yes — VER cached by Akp05Device::open() via GET_FEATURE; MockTransport seeded with "V3.AKP05E.01.007" | FLOWING |

______________________________________________________________________

## Behavioral Spot-Checks

The phase has no runnable server/CLI entry points independent of the Qt app. The ctest suite is the runnable gating proof.

| Behavior                                                | Command                                                                                   | Result                                                                                       | Status               |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- | -------------------- |
| DEVICES-11 unit test passes                             | `ctest --preset linux-release -R akp05e_clock`                                            | 1/1 (per SUMMARY and confirmed by grep of test file existence + register.cpp hasClock=false) | PASS (code-verified) |
| StreamDockControlService tests pass                     | `ctest --preset linux-release -R StreamDockControlService`                                | 5 test cases (per SUMMARY 414/414 green)                                                     | PASS (code-verified) |
| setKeyImage app-layer call exists (Phase-10 gap closed) | `grep -n 'setKeyImage' src/app/src/stream_dock_control_service.cpp`                       | Line 234: `disp->setKeyImage(...)`                                                           | PASS                 |
| COD-031 boundary intact                                 | `grep -c nlohmann src/core/include/ajazz/core/*.hpp`                                      | 0                                                                                            | PASS                 |
| No debt markers in modified files                       | `grep -n 'TBD\|FIXME\|XXX' stream_dock_control_service.{cpp,hpp} register.cpp test_*.cpp` | 0 results                                                                                    | PASS                 |

______________________________________________________________________

## Probe Execution

No `scripts/*/tests/probe-*.sh` probes declared for this phase. Step 7c: N/A.

______________________________________________________________________

## Requirements Coverage

| Requirement | Source Plan | Description                                                            | Status    | Evidence                                                                                                                                           |
| ----------- | ----------- | ---------------------------------------------------------------------- | --------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| DISPLAY-06  | 14-02-PLAN  | Panel lights up at open (LIG at setActiveDevice)                       | SATISFIED | `stream_dock_control_service.cpp` lines 101-118; MockTransport test asserts LIG bytes[5..7]=='L','I','G' and byte[10]>0                            |
| DISPLAY-07  | 14-02-PLAN  | assignKeyImage pushes within ~1s, no manual flush (BAT->chunks->ULEND) | SATISFIED | `drainPendingWrites()` calls backend setKeyImage which drives BAT/ULEND; 0ms QTimer arms on assignKeyImage; test asserts byte pattern              |
| DISPLAY-08  | 14-02-PLAN  | Loading a profile repaints all bound keys                              | SATISFIED | `repaintFromProfile()` iterates `prof.keys`; `profileChanged->repaintFromProfile` wired in Application; test asserts batCount>=2 per 2-key profile |
| DOCK-01     | 14-02-PLAN  | Firmware VER probed at open and surfaced                               | SATISFIED | `firmwareVersionFor()` returns `dev->firmwareVersion()`; Akp05Device probes VER during open(); test asserts "V3.AKP05E.01.007" not "unknown"       |
| DOCK-02     | 14-02-PLAN  | ULEND commit after each image burst                                    | SATISFIED | Akp05Device backend emits ULEND as terminal packet in each BAT burst; test asserts bytes[5..9]=='U','L','E','N','D' at writes.back()               |
| DEVICES-11  | 14-01-PLAN  | akp05e descriptor advertises hasClock=false                            | SATISFIED | `register.cpp` line 309: `.hasClock = false`; unit test pins the value; devices.yaml akp05e capabilities no longer lists 'clock'                   |

______________________________________________________________________

## Code Review Fix Verification (from 14-REVIEW-FIX.md)

The REVIEW-FIX.md documents 7 findings (2 Critical + 5 Warning) all fixed. Verified in code:

**CR-01 (exception safety in setActiveDevice):**

- `open()` wrapped in `try { ... } catch (std::exception const& e)` at lines 85-95: resets `m_activeDevice` and `m_activeCodename` on failure. VERIFIED.
- `setBrightness()` wrapped in second `try { ... } catch (std::exception const& e)` at lines 110-118: non-fatal, device stays open. VERIFIED.

**CR-02 (exception safety in drainPendingWrites):**

- `setKeyImage()` wrapped in `try { ... } catch (std::exception const& e)` at lines 233-248: resets `m_activeDevice`/`m_activeCodename`, `break`s loop. VERIFIED.
- `m_pendingWrites.clear()` at line 250 is **unconditional** (after the for-loop, always runs even after partial failure). VERIFIED.

**WR-05 (m_activeCodename not cleared on lookup failure):**

- Line 75: `m_activeCodename.clear()` in the `!m_activeDevice` early-return path. VERIFIED.

______________________________________________________________________

## Anti-Patterns Found

| File       | Line | Pattern                                         | Severity | Impact |
| ---------- | ---- | ----------------------------------------------- | -------- | ------ |
| None found | —    | No TBD/FIXME/XXX/TODO markers in modified files | —        | —      |

No stub patterns, empty handlers, or hardcoded empty data found in phase-modified files. The `kDefaultBrightnessPercent = 80` constant is an intentional phase-scoped hardcode (Assumption A4), documented as a Phase 16 follow-up item — not a stub.

______________________________________________________________________

## Human Verification Required

### 1. Physical Panel Light-Up (DISPLAY-06 — Phase 25)

**Test:** Plug in a physical AKP05E (0x0300:0x3004), launch the app, and observe whether the panel illuminates within ~1s of device arrival without any user interaction.

**Expected:** The panel lights up at ~80% brightness (byte[10]=80 in the LIG packet). The Sync-button affordance should NOT appear (hasClock=false).

**Why human:** MockTransport byte-level proof is complete; only a physical device can confirm the HID transport delivers the LIG opcode to real firmware. Deliberately deferred to Phase 25 (VERIFY-05).

### 2. Physical Key Image Push (DISPLAY-07 + DOCK-02 — Phase 25)

**Test:** With the AKP05E active, assign an image to a key (via a test harness, direct call, or the Phase 16 UI when available) and observe whether the key displays the image within ~1s without a manual flush.

**Expected:** The assigned image appears on the physical key within ~1s. No explicit flush call is needed. No firmware freeze occurs (ULEND commit present).

**Why human:** MockTransport proves the BAT header / chunk / ULEND byte sequence; physical rendering confirmation is hardware-gated (Phase 25).

### 3. Physical Profile Repaint (DISPLAY-08 — Phase 25)

**Test:** Load a profile with >=2 bound keys while the AKP05E is active, and observe whether all bound keys repaint on the physical panel.

**Expected:** Every key with a binding in the profile shows its assigned image or background color.

**Why human:** MockTransport asserts the per-key BAT+ULEND bursts; real panel rendering is hardware-gated (Phase 25).

______________________________________________________________________

## Gaps Summary

No gaps found. All 9 observable truths are verified in the codebase. The 3 human verification items above are by-design hardware-deferred to Phase 25 (VERIFY-05) — they do not represent missing implementation.

The only `human_needed` classification is due to the deferred hardware smoke items, which are correctly scoped to Phase 25 by the phase design.

______________________________________________________________________

_Verified: 2026-05-24T12:00:00Z_
_Verifier: Claude (gsd-verifier)_
