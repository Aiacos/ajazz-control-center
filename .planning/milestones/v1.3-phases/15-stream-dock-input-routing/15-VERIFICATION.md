---
phase: 15-stream-dock-input-routing
verified: 2026-05-24T13:00:00Z
status: human_needed
score: 12/12 must-haves verified
overrides_applied: 0
human_verification:
  - test: 'Physical AKP05E hardware end-to-end: press a key, turn an encoder CW/CCW, press an encoder, and tap/swipe the touch strip on the connected device.'
    expected: Each action fires the bound action chain from the loaded profile. Encoder rotation coalesces to one dispatch per 16 ms window. Touch tap fires the under-zone encoder binding. Swipe left/right emits page-nav intent (logged to console in Phase 15). No UI freeze or thread stalls observed.
    why_human: Hardware-free tests prove decode->ActionEngine routing via MockTransport injection. Phase 25 (VERIFY-05) is the formal gate for live AKP05E witness. As noted in the verification context, physical confirmation is legitimately deferred.
  - test: Verify touch zone/swipe framing is correct on real hardware (provisional formula X*4/640).
    expected: Tapping near the left edge fires encoder 0 binding; near the right edge fires encoder 3 binding; swipes produce pageNavRequested(-1/+1) in the app log.
    why_human: zoneForX formula is marked PROVISIONAL (akp05.md para5); hardware reconciliation is Phase 25 scope.
---

# Phase 15: Stream Dock Input Routing Verification Report

**Phase Goal:** Pressing a key, turning/pressing one of the 4 encoders, or tapping/swiping the touch strip fires the bound action via the core ActionEngine (instantiated in the app for the first time).
**Verified:** 2026-05-24T13:00:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

### Observable Truths

| #   | Truth                                                                                                                                                                       | Status   | Evidence                                                                                                                                                                                                                                                                     |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | A canned KeyPressed frame for key N runs Profile.keys[N].onPress via ActionEngine; KeyReleased runs onRelease.                                                              | VERIFIED | Test #199 (INPUT-03) passes. `dispatch()` at stream_dock_input_service.cpp:186-195 routes KeyPressed to `prof.keys.find(ev.index)->second.onPress` and KeyReleased to `.onRelease` via `m_engine->run()`.                                                                    |
| 2   | An encoder CW frame fires encoders[i].onCw and a CCW frame fires encoders[i].onCcw (CW != CCW), routed through the 16 ms coalescer.                                         | VERIFIED | Test #200 (INPUT-04a) passes. `onEncoderTurned()` accumulates signed delta; `drainCoalescedRotation()` at line 265-288 dispatches onCw (delta>0) or onCcw (delta\<0) after 16 ms QTimer fires.                                                                               |
| 3   | 5 rapid EncoderTurned ticks in the same direction coalesce to ONE onCw dispatch within the 16 ms window.                                                                    | VERIFIED | Test #201 (INPUT-04c) passes. Single-shot coalescer arms once on first tick (isActive guard at line 256-258); timeout fires once; cwCount==1.                                                                                                                                |
| 4   | An encoder press fires encoders[i].onPress and the host synthesises the paired release (no wire release frame is required).                                                 | VERIFIED | Test #202 (INPUT-04b) passes. `dispatch()` at line 203-209 runs onPress then calls `synthesiseEncoderRelease(ev.index)` which emits `encoderReleaseSynthesised` signal.                                                                                                      |
| 5   | A touch tap with X in zone-N range fires encoders[N].onPress via the provisional X\*EncoderCount/TouchStripRangeX zone map; swipe-left/right emit a page-nav intent signal. | VERIFIED | Tests #203, #204, #205 pass. `zoneForX()` at line 159-171 is `min(x*4/640, 3)`; tap dispatch at line 222-227; swipe at line 228-233 emits `pageNavRequested(-1/+1)`. PROVISIONAL marker at line 158+161.                                                                     |
| 6   | A Sleep step in a chain defers via the injected QtExecutor and does NOT block the poll thread.                                                                              | VERIFIED | Test #206 passes. `pump()` returns before `keyPressCount>0`; QtExecutor scheduleAfter defers via QTimer::singleShot. ActionEngine constructed with `shared_ptr<Executor>(m_qtExecutor.get(), [](core::Executor*){})` at application.cpp:330.                                 |
| 7   | Pumping poll() twice does not re-open the transport (the service holds the shared handle, never re-opens).                                                                  | VERIFIED | Test #207 passes. `setActiveDevice()` holds `m_device` (shared_ptr); no `open()` call in service. Transport `isOpen()` stays true after two pumps.                                                                                                                           |
| 8   | Application owns one QtExecutor-backed core::ActionEngine and the StreamDockInputService (the first ActionEngine instantiation in the app).                                 | VERIFIED | `m_qtExecutor`, `m_actionEngine`, `m_streamDockInput` members declared in application.hpp:199-215 and constructed in application.cpp:237-338. No prior ActionEngine construction found in `src/app/src/`.                                                                    |
| 9   | On a Stream Deck hot-plug arrival, the input service drives the SAME held handle the Phase-14 control service owns (no second open()).                                      | VERIFIED | application.cpp:629-643 — on arrival, control service calls `setActiveDevice(codename)` then `m_deviceRegistry.open(devId)` (ARCH-03 flyweight returns same shared_ptr). Input service receives that handle via `m_streamDockInput->setActiveDevice(std::move(handle))`.     |
| 10  | The ActionEngine is constructed with real keyPress(stub)/runCommand(QProcess)/openUrl(QDesktopServices)/plugin(stub) executors and the QtExecutor so Sleep defers.          | VERIFIED | application.cpp:251-330. keyPress: log stub (line 255-258); runCommand: `QProcess::startDetached(program, args)` (line 296); openUrl: scheme-gated `QDesktopServices::openUrl` (lines 309-319); plugin: log stub (line 322). QtExecutor as non-owning shared_ptr (line 330). |
| 11  | The active profile accessor is wired from ProfileController::activeProfile() so dispatch resolves bindings against the loaded profile.                                      | VERIFIED | application.cpp:335-336: `[this]() -> core::Profile const& { return m_profileController->activeProfile(); }` injected into `StreamDockInputService`.                                                                                                                         |
| 12  | The swipe pageNavRequested signal is connected to a sink (logged intent in Phase 15; Phase 16 wires the page model).                                                        | VERIFIED | application.cpp:361-367: `QObject::connect(m_streamDockInput.get(), &StreamDockInputService::pageNavRequested, ...)` logs `"page nav intent {} (page model arrives Phase 16)"`.                                                                                              |

**Score:** 12/12 truths verified

### Code Review Fixes Verified

| Fix                                                 | Location                              | Evidence                                                                                                                                  |
| --------------------------------------------------- | ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| CR-01: poll() exception safety in pump()            | stream_dock_input_service.cpp:139-151 | `try { return m_device->poll(); } catch (std::exception const& e)` — deregisters callback, resets handle, stops both timers               |
| CR-02: Removed hotplug marshals to GUI thread       | application.cpp:661-664               | `QMetaObject::invokeMethod(m_streamDockInput.get(), [this]{ m_streamDockInput->setActiveDevice(nullptr); }, Qt::QueuedConnection)`        |
| CR-03: onEvent deregistered before m_device.reset() | stream_dock_input_service.cpp:104-106 | `if (m_device) { m_device->onEvent({}); }` before the null-device early return and before replacing the handle                            |
| WR-01: openUrl rejects non-http(s) schemes          | application.cpp:309-318               | `QUrl` strict construction; scheme check `!= "http" && != "https"` → WARN + return; `QDesktopServices::openUrl` only for approved schemes |

### Required Artifacts

| Artifact                                        | Expected                                                                                                                                                            | Status   | Details                                           |
| ----------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- | ------------------------------------------------- |
| `src/app/src/stream_dock_input_service.hpp`     | QObject with ProfileAccessor/ActionEngine injection ctors, setActiveDevice/pump/zoneForX, pageNavRequested + encoderReleaseSynthesised signals                      | VERIFIED | 201 lines. All required members present. No stub. |
| `src/app/src/stream_dock_input_service.cpp`     | poll-pump QTimer, DeviceEvent dispatch switch, 16 ms rotation coalescer, encoder release synthesis, provisional zone derivation, three real executors + plugin stub | VERIFIED | 302 lines. Full implementation.                   |
| `tests/unit/test_stream_dock_input_service.cpp` | MockTransport.enqueueRead-fed dispatch assertions for INPUT-03/04/05 + coalesce + Sleep-non-block + held-handle                                                     | VERIFIED | 545 lines. 10 TEST_CASEs (tests #199-#208).       |
| `src/app/src/application.hpp`                   | m_qtExecutor, m_actionEngine, m_streamDockInput members in -Wreorder-clean init order                                                                               | VERIFIED | Members at lines 199-215 in correct order.        |
| `src/app/src/application.cpp`                   | ActionEngine+QtExecutor construction, executor lambdas, shared-handle wiring on arrival, profile accessor, pageNavRequested connect                                 | VERIFIED | Full wiring at lines 237-368.                     |

### Key Link Verification

| From                          | To                                       | Via                                                                                                                                              | Status   | Details                                                                                                                   |
| ----------------------------- | ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | -------- | ------------------------------------------------------------------------------------------------------------------------- |
| stream_dock_input_service.cpp | core::ActionEngine                       | construct with ActionExecutors lambdas + injected QtExecutor; `m_engine->run(chain)` per dispatched event                                        | VERIFIED | `m_engine->run()` at lines 188, 193, 208, 225, 281, 283. ActionEngine owned as `unique_ptr<core::ActionEngine> m_engine`. |
| stream_dock_input_service.cpp | Akp05Device::poll / onEvent              | QTimer pumps `m_device->poll()`; onEvent callback -> dispatch(DeviceEvent)                                                                       | VERIFIED | `m_device->poll()` at line 140; `m_device->onEvent([this](auto const& ev){ dispatch(ev); })` at line 119.                 |
| stream_dock_input_service.cpp | core::Profile keys/encoders              | ProfileAccessor lambda -> `keys.find(index)/encoders.find(index)` -> run onPress/onRelease/onCw/onCcw                                            | VERIFIED | `prof.keys.find(ev.index)` at lines 187, 192; `prof.encoders.find(ev.index)` at lines 206, 224, 279.                      |
| application.cpp               | StreamDockControlService held handle     | On arrival: `m_streamDockControl->setActiveDevice(codename)` then `m_deviceRegistry.open(devId)` -> `m_streamDockInput->setActiveDevice(handle)` | VERIFIED | application.cpp:637-642. ARCH-03 flyweight returns same shared_ptr.                                                       |
| application.cpp               | core::ActionEngine                       | Construct with ActionExecutors{keyPress stub, runCommand QProcess, openUrl QDesktopServices, plugin stub} + m_qtExecutor                         | VERIFIED | application.cpp:251-330.                                                                                                  |
| application.cpp               | StreamDockInputService::pageNavRequested | `QObject::connect` to logged sink                                                                                                                | VERIFIED | application.cpp:364-367.                                                                                                  |

### Data-Flow Trace (Level 4)

The service renders no UI data; it is a pure dispatch pipeline. Data flows: raw HID bytes -> MockTransport/ITransport -> poll() -> DeviceEvent -> dispatch() -> ProfileAccessor -> ActionEngine -> executor. The pipeline is fully wired (not rendering dynamic display data, so the hollow-prop variant of this check does not apply).

### Behavioral Spot-Checks

| Behavior                               | Command                      | Result                 | Status |
| -------------------------------------- | ---------------------------- | ---------------------- | ------ |
| INPUT-03 key press fires onPress       | ctest -R "INPUT-03"          | Test #199 PASSED 0.02s | PASS   |
| INPUT-04a CW/CCW distinct dispatch     | ctest -R "INPUT-04a"         | Test #200 PASSED 0.05s | PASS   |
| INPUT-04c 5 ticks coalesce to 1        | ctest -R "INPUT-04c"         | Test #201 PASSED 0.03s | PASS   |
| INPUT-04b press + synthetic release    | ctest -R "INPUT-04b"         | Test #202 PASSED 0.02s | PASS   |
| INPUT-05a touch tap zone routing       | ctest -R "INPUT-05a"         | Tests #203+#204 PASSED | PASS   |
| INPUT-05b swipe emits pageNavRequested | ctest -R "INPUT-05b"         | Test #205 PASSED 0.02s | PASS   |
| Sleep defers via QtExecutor            | ctest -R "Sleep non-block"   | Test #206 PASSED 0.06s | PASS   |
| Held handle: no re-open                | ctest -R "Held handle"       | Test #207 PASSED 0.02s | PASS   |
| IN-01 removal path                     | ctest -R "IN-01"             | Test #208 PASSED 0.01s | PASS   |
| Full suite (424 tests)                 | ctest --preset linux-release | 424/424 PASSED 10.94s  | PASS   |

### Requirements Coverage

| Requirement | Source Plan                  | Description                                                                                    | Status    | Evidence                                                                              |
| ----------- | ---------------------------- | ---------------------------------------------------------------------------------------------- | --------- | ------------------------------------------------------------------------------------- |
| INPUT-03    | 15-01-PLAN.md, 15-02-PLAN.md | Poll loop drives Stream Deck; key press/release fires bound action via ActionEngine            | SATISFIED | Tests #199, dispatch at stream_dock_input_service.cpp:186-195, wired in Application   |
| INPUT-04    | 15-01-PLAN.md, 15-02-PLAN.md | Encoder CW/CCW/press fire bound actions; rotation coalesced at 16 ms; host synthesises release | SATISFIED | Tests #200-#202, coalescer at lines 250-288, synthesiseEncoderRelease at line 294-298 |
| INPUT-05    | 15-01-PLAN.md, 15-02-PLAN.md | Touch tap routes to encoder binding (provisional zone map); swipe emits page-nav intent        | SATISFIED | Tests #203-#205, zoneForX at line 159-171, swipe dispatch at line 228-233             |

All three requirement IDs (INPUT-03, INPUT-04, INPUT-05) claimed by both plans are present in REQUIREMENTS.md as Phase 15 items and are marked [x] complete. No orphaned requirements.

### Anti-Patterns Found

| File                          | Line                               | Pattern                                                      | Severity | Impact                                                                                                                              |
| ----------------------------- | ---------------------------------- | ------------------------------------------------------------ | -------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| stream_dock_input_service.cpp | 297-298 (synthesiseEncoderRelease) | `EncoderBinding` has no `onRelease` field — noted in comment | Info     | Documented as deferred to Phase 16 if onRelease is added. No impact on INPUT-04 as the observable synthesis signal fires correctly. |

No TBD/FIXME/XXX markers found in any phase 15 modified files. No unreferenced debt markers. No stubs in the dispatch path — stubs are confined to the keyPress and plugin executor lambdas, which are explicitly documented as Phase 21/19 seams and do not prevent INPUT-03/04/05 being satisfied (tests use spy executors).

### Human Verification Required

#### 1. Physical AKP05E End-to-End Input

**Test:** With the AKP05E connected, press each key, rotate each encoder CW and CCW, press each encoder, tap each zone on the touch strip, and swipe left and right.
**Expected:** Each action fires the bound chain for the loaded profile (visible in app log). Encoder rotation produces one log entry per ~16 ms window even during fast spin. Touch tap fires the encoder binding for the tapped zone. Swipe produces `"page nav intent -1/+1 (page model arrives Phase 16)"` in the log. No UI freeze.
**Why human:** MockTransport tests prove the decode->dispatch->ActionEngine routing at the software layer. Live hardware confirmation of the full path (including HID transport, USB enumeration, hot-plug wiring, and provisional touch zone/swipe framing) is deferred to Phase 25 (VERIFY-05) per the verification context. This is NOT a gap — it is the intended hardware-gated verification schedule.

#### 2. Touch Zone/Swipe Framing Reconciliation

**Test:** Tap at the far left, center-left, center-right, and far right of the touch strip on the physical device.
**Expected:** The four taps map to encoder bindings 0, 1, 2, and 3 respectively per the provisional `X*4/640` formula.
**Why human:** zoneForX is explicitly marked PROVISIONAL (akp05.md para5); the coordinate boundaries may need hardware-driven adjustment in Phase 25.

______________________________________________________________________

### Gaps Summary

No gaps found. All 12 must-have truths are VERIFIED by the codebase. The phase goal — "Pressing a key, turning/pressing one of the 4 encoders, or tapping/swiping the touch strip fires the bound action via the core ActionEngine (instantiated in the app for the first time)" — is implemented and unit-tested. The only remaining items are the hardware-deferred UAT tests legitimately scheduled for Phase 25 (VERIFY-05).

______________________________________________________________________

_Verified: 2026-05-24T13:00:00Z_
_Verifier: Claude (gsd-verifier)_
