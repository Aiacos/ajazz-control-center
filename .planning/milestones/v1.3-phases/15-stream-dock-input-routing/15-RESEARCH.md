# Phase 15: Stream Dock Input Routing - Research

**Researched:** 2026-05-23
**Domain:** Qt 6 / C++20 app-layer input-dispatch service wiring the existing capture-verified AKP05E HID decode (`parseInputReport`) to the core `ActionEngine` — the first `ActionEngine` instantiation in the app.
**Confidence:** HIGH (every claim grounded in in-tree source read this session; no external packages)

\<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions

**Architecture**

- The poll loop drives the **Phase-14 control service's held-open handle** — do NOT open a second handle (ARCH-03 single-handle invariant). The input loop is app-layer.
- Reuse the **core `ActionEngine`** (`src/core/include/ajazz/core/action_engine.hpp`) — it already interprets `Plugin`/`Sleep`/`KeyPress`/`RunCommand`/`OpenUrl`/`OpenFolder`/`BackToParent` and does folder navigation. Inject the **`QtExecutor`** (`src/app/src/qt_executor.hpp`) so `Sleep`/`delayMs` never block the HID poll thread (audit A2).
- The app supplies the `ActionExecutors` lambdas (keyPress / runCommand / openUrl / plugin). The `plugin` executor is a **seam for Phase 19** — Phase 15 wires the non-plugin kinds and leaves `plugin` as a logged stub (do not block on the plugin host).
- Reuse the existing input decode: `akp05::parseInputReport` (akp05.cpp:238) already produces key press/release, encoder turned/pressed/released, and touch tap/swipe-left/swipe-right/long-press; `Akp05Device` maps these to core `DeviceEvent` (akp05.cpp:487+). Do NOT add a second parser or alter the wire decode (RE source of truth).

**Bindings**

- Dispatch from the in-memory `Profile`: key events → `Binding.onPress`/`onRelease`/`onLongPress`; encoder events → `EncoderBinding.onCw`/`onCcw`/`onPress` (`profile.hpp`). keyIndex is **1-based** on the wire.
- Touch **tap zone index (0..3) maps to the encoder under that zone** → fire that `EncoderBinding.onPress` (Companion convention); swipe → page nav (Phase 16's page model; Phase 15 emits the nav intent).

**Encoder release synthesis**

- The device emits encoder **press only**; deliver `onPress` on press and synthesise the paired release. Confirm in research whether `Akp05Device` already emits a genuine `EncoderReleased` or still carries the v1.2 `value=0` half-step workaround (INPUT-01, Phase-10, never executed) — fix it here if still present.

### Claude's Discretion (planner/executor; surface trade-offs)

- Poll mechanism: `QTimer`-driven read on the GUI thread (cf. `BatteryService` 15 s pattern, but input needs a much tighter cadence) vs a dedicated read thread marshalling events to the GUI thread. Honor the akp05 read/ACK semantics; must not drop fast key/encoder events nor freeze the UI.
- Exact home of the poll loop (inside the Phase-14 `StreamDockControlService` vs a sibling `StreamDockInputService` that shares the held handle).

### Deferred Ideas (OUT OF SCOPE)

- Brightness slider / clear-all UI + binding persistence + pages → Phase 16.
- `ActionKind::Plugin` routing to the plugin host → Phase 19 (Phase 15 leaves the plugin executor a stub).
- Touch zone/coordinate-map + swipe framing hardware reconciliation → Phase 25 (provisional §5).
  \</user_constraints>

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                                                                                                                                                                               | Research Support                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| ---------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| INPUT-03               | A poll loop drives each connected Stream Deck; a physical key press/release fires its bound action via the core `ActionEngine` (instantiated in the app for the first time).                                                              | `Akp05Device::poll()` (akp05.cpp:471) already drains up to 8 reports/call, decodes via `parseInputReport`, and dispatches `DeviceEvent` to a registered `onEvent` callback. **No app code calls `poll()` or registers `onEvent`** (grep confirms zero hits in `src/app/`). Phase 15 builds the app-layer driver: a QTimer pumping `poll()` on the Phase-14 held handle, an `onEvent` callback that looks up the bound `Binding` in the active `Profile` and feeds the chain to a freshly-constructed `ActionEngine`. See Architecture Patterns §Input driver.                                                                                                                                                                                        |
| INPUT-04               | 4 encoders fire on CW vs CCW and on press; device emits press-only so host **synthesises the release** and delivers a genuine `EncoderReleased` (replacing the `value=0` half-step workaround); rotation coalesced with a 16 ms `QTimer`. | `parseInputReport` (akp05.cpp:262-280) already emits a genuine `EncoderTurned` (signed delta, +1 CW / 0xFF=-1 CCW) AND distinguishes `EncoderPressed`/`EncoderReleased` by byte-11 edge — but akp05.md §"Edge cases" states the device **never emits a release frame** (press-only). So the wire decode's release branch is dormant in practice; the **synthesis must happen in the app dispatch layer** (emit `onPress` chain on press, then synthesise the paired release). The 16 ms rotation coalescer is an **app-layer accumulator** (last-write-wins delta per encoder, drained on a single-shot 16 ms timer). The `value=0` half-step workaround lives in **akp03.cpp:396** (a different file) — akp05 does NOT carry it. See Pitfall 1 + 2. |
| INPUT-05               | Touch tap on one of 4 zones fires the action bound to the encoder under that zone; swipe left/right drives prev/next page (provisional zone/coordinate map — hardware-gated, Phase 25).                                                   | `parseInputReport` (akp05.cpp:285-315) decodes `TouchTap`/`TouchSwipeLeft`/`TouchSwipeRight`/`TouchLongPress` with the absolute X (0..639) in `value`; `poll()` (akp05.cpp:504-519) re-packs gesture+X into `DeviceEvent::value` (\`(gesture\<<16)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| \</phase_requirements> |                                                                                                                                                                                                                                           |                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |

## Summary

Phase 15 is **app-layer dispatch wiring**, not protocol or decode work. The device backend already delivers fully-decoded input: `Akp05Device::poll()` (akp05.cpp:471) drains up to 8 HID reports per call, runs the capture-verified `akp05::parseInputReport`, and emits a `core::DeviceEvent` to a registered `onEvent` callback. The core `ActionEngine` (action_engine.cpp) is a complete, callback-driven interpreter for `KeyPress`/`RunCommand`/`OpenUrl`/`Sleep`/`OpenFolder`/`BackToParent`/`Plugin` chains, and `QtExecutor` (qt_executor.hpp) is the ready-made non-blocking `Executor` for `Sleep`/`delayMs`. **None of this is wired in the app** — `grep` for `ActionEngine`, `ActionExecutors`, `->poll()`, and `onEvent` across `src/app/` returns zero functional hits. Phase 15 is the first `ActionEngine` instantiation, exactly as the requirement states.

The work is therefore: (1) build an app-layer input driver (a QTimer pumping `poll()` on the Phase-14 control service's held `shared_ptr<IDevice>` — no second handle) that registers an `onEvent` callback; (2) construct one `ActionEngine` with `ActionExecutors` lambdas (keyPress / runCommand / openUrl wired; `plugin` a logged stub for Phase 19) and the app's `QtExecutor`; (3) in the callback, map each `DeviceEvent` to the bound chain in the active `Profile` (`keys[i].onPress/onRelease/onLongPress`, `encoders[i].onCw/onCcw/onPress`) and call `engine.run(chain)`; (4) add an **app-layer encoder-rotation coalescer** (16 ms single-shot QTimer accumulating signed delta per encoder, draining one dispatch per frame — Pitfall 23); and (5) **synthesise the encoder release** in the dispatch layer (the device is press-only per akp05.md, so emit `onPress` on the press edge and a paired synthetic release).

Two subtle reconciliations: (a) the `value=0` half-step "workaround" the requirement references lives in **akp03.cpp:396**, NOT akp05 — the akp05 decode already emits a real `EncoderTurned` with signed delta and a distinct `EncoderReleased` enum, so Phase 15's INPUT-04 obligation is to (i) drive the press→synthetic-release pairing in the app and (ii) confirm the akp05 decode is not relied on for a release frame the hardware never sends; do NOT edit the wire decode. (b) Touch **zone derivation does not exist** — the backend exposes gesture+X only; Phase 15 adds the provisional `X→zone` map (hardware-reconciled in Phase 25). Verification is entirely hardware-free: feed canned input frames through `MockTransport::enqueueRead()`, pump `poll()`, and assert spy executors ran the right chain (CW≠CCW, press fires `onPress`, key press vs release pick the right chain, tap-zone-N→encoder-N `onPress`, and 5 rapid rotation ticks coalesce to one dispatch within 16 ms).

**Primary recommendation:** Create `src/app/src/stream_dock_input_service.{hpp,cpp}` (a sibling QObject sharing the Phase-14 held handle, NOT a second handle) that owns one `core::ActionEngine` + the app `QtExecutor`, pumps `poll()` on a tight QTimer, registers `onEvent`, and dispatches `DeviceEvent`→`Profile` binding→`engine.run()`. Add a 16 ms rotation coalescer and app-layer encoder-release synthesis. Wire keyPress/runCommand/openUrl executors; stub `plugin` with a log line (Phase 19 seam). Do NOT touch `akp05.cpp`/`akp05_protocol.hpp` (RE source of truth). MockTransport-fed dispatch tests are the gating proof; live-hardware is Phase 25.

## Architectural Responsibility Map

| Capability                          | Primary Tier                                           | Secondary Tier                                         | Rationale                                                                                                                                                              |
| ----------------------------------- | ------------------------------------------------------ | ------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Pump HID reads (`poll()`)           | App input service (QTimer)                             | Device backend (`poll()` drains transport)             | Backend has no internal reader thread for akp05; `poll()` is host-driven. App owns the cadence. [VERIFIED: akp05.cpp:471 loop reads transport, no thread spawned]      |
| Decode raw frame → `DeviceEvent`    | Device backend (`parseInputReport` + `poll()` mapping) | —                                                      | Capture-verified RE; do NOT duplicate. [VERIFIED: akp05.cpp:238,487]                                                                                                   |
| Register input callback             | App input service (`onEvent`)                          | Device backend (stores + invokes cb)                   | The callback is the app's dispatch entry point. [VERIFIED: device.hpp:201, akp05.cpp:466]                                                                              |
| Interpret an action chain           | Core `ActionEngine`                                    | App `ActionExecutors` (the side-effect lambdas)        | Core is Qt/OS-free; app supplies keyPress/runCommand/openUrl/plugin. [VERIFIED: action_engine.hpp:76,100]                                                              |
| Non-blocking Sleep/delay            | App `QtExecutor` (injected into ActionEngine)          | Core `ActionEngine` (defers via `Executor`)            | Audit A2: Sleep must not block the poll thread. [VERIFIED: qt_executor.hpp:26, action_engine.cpp:63]                                                                   |
| Map `DeviceEvent` → bound chain     | App input service                                      | `ProfileController::activeProfile()` (Phase-14 getter) | Profile lives in the app; service reads `keys`/`encoders` maps. [VERIFIED: profile.hpp:154-155, 14-02-PLAN.md Task 1 adds activeProfile()]                             |
| Encoder rotation coalescing (16 ms) | App input service (accumulator + single-shot QTimer)   | —                                                      | Pitfall 23 signal-storm; per-encoder signed-delta accumulate, one dispatch/frame. [VERIFIED: pattern absent today — new]                                               |
| Encoder release synthesis           | App input service                                      | —                                                      | Device is press-only (akp05.md §Edge cases); host synthesises the paired release. [CITED: docs/protocols/streamdeck/akp05.md:76]                                       |
| Touch X → zone(0..3) derivation     | App input service                                      | —                                                      | No zone derivation exists; backend exposes gesture+X only. Provisional map; HW-reconciled Phase 25. [VERIFIED: grep — no zone code; akp05.cpp:509-517 packs gesture+X] |
| Plugin action execution             | (stub) App input service logs no-op                    | Phase 19 plugin host                                   | `plugin` executor is a Phase-19 seam; Phase 15 must not block on it. [per CONTEXT]                                                                                     |

## Standard Stack

This is a C++20 / Qt 6 in-tree phase. **No new external packages.** The "stack" is existing in-tree components to reuse.

### Core (reuse — do not reimplement)

| Component                                      | Location                                                                  | Purpose                                                                                              | Why reuse                                                           |
| ---------------------------------------------- | ------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| `Akp05Device::poll()`                          | `src/devices/streamdeck/src/akp05.cpp:471`                                | Drains ≤8 reports/call, decodes, dispatches `DeviceEvent` to `onEvent` cb                            | Host-driven; the input source. [VERIFIED]                           |
| `akp05::parseInputReport`                      | `src/devices/streamdeck/src/akp05.cpp:238`                                | Raw frame → `InputEvent` (key/encoder/touch); capture-verified                                       | RE source of truth; do NOT alter. [VERIFIED]                        |
| `IDevice::onEvent` / `poll`                    | `src/core/include/ajazz/core/device.hpp:201,211`                          | Register callback / pump reads                                                                       | The dispatch entry point. [VERIFIED]                                |
| `core::ActionEngine`                           | `src/core/include/ajazz/core/action_engine.hpp:100` + `action_engine.cpp` | Callback-driven chain interpreter (KeyPress/RunCommand/OpenUrl/Sleep/OpenFolder/BackToParent/Plugin) | The reuse target; first app instantiation. [VERIFIED]               |
| `core::ActionExecutors`                        | `action_engine.hpp:76`                                                    | The 4 app-supplied side-effect lambdas (+ optional sleep telemetry)                                  | App provides keyPress/runCommand/openUrl/plugin. [VERIFIED]         |
| `app::QtExecutor`                              | `src/app/src/qt_executor.hpp`                                             | Non-blocking `Executor` (QTimer::singleShot) for Sleep/delay                                         | Audit A2; inject into ActionEngine. [VERIFIED]                      |
| `core::Profile` / `Binding` / `EncoderBinding` | `src/core/include/ajazz/core/profile.hpp:100-173`                         | `keys[i].onPress/onRelease/onLongPress`; `encoders[i].onCw/onCcw/onPress`                            | The binding source for dispatch. [VERIFIED]                         |
| `ProfileController::activeProfile()`           | `src/app/src/profile_controller.hpp` (added in Phase 14 Task 1)           | Const-ref getter to the loaded `Profile`                                                             | The active-profile source. [VERIFIED: 14-02-PLAN.md Task 1]         |
| `StreamDockControlService` (Phase 14)          | `src/app/src/stream_dock_control_service.{hpp,cpp}`                       | Owns the held `shared_ptr<IDevice>` for the session                                                  | Share its handle; do NOT re-open (ARCH-03). [CITED: 14-02-PLAN.md]  |
| `MockTransport`                                | `tests/unit/fixtures/mock_transport.hpp`                                  | `enqueueRead(bytes)` feeds canned input frames; `writes()` captures output                           | The hardware-free dispatch proof. [VERIFIED: mock_transport.hpp:96] |
| `makeAkp05WithTransport`                       | `src/devices/streamdeck/src/akp05.cpp:928`                                | Test factory injecting a MockTransport                                                               | Lets tests feed input + assert dispatch. [VERIFIED]                 |

### Supporting

| Component                                                           | Location                                                               | Purpose                                                                            | When to Use                                                                                                                                                                                                                                              |
| ------------------------------------------------------------------- | ---------------------------------------------------------------------- | ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `QTimer` (Qt::PreciseTimer for poll, Qt::CoarseTimer for coalescer) | Qt 6                                                                   | Poll cadence + 16 ms rotation coalescer                                            | Poll loop + Pitfall-23 accumulator. `BatteryService` 15 s pattern is the QTimer precedent (battery_service.cpp:44). [VERIFIED]                                                                                                                           |
| `core::EventBus`                                                    | `src/core/include/ajazz/core/event_bus.hpp`                            | Thread-safe pub/sub fan-out of `DeviceEvent` (built for ">100 Hz encoder" devices) | OPTIONAL — only if events need fan-out to multiple subscribers (plugin host Phase 19). For Phase 15 a direct `onEvent` callback into the service is simpler; consider EventBus if Phase 19's bridge will also subscribe. See Open Question 2. [VERIFIED] |
| QML singleton factory pattern                                       | `lighting_service.hpp:96` (`static_assert(!is_default_constructible)`) | `create()`/`registerInstance()` if QML-exposed                                     | UI hint = yes; but Phase 15 dispatch is headless C++ — likely a plain QObject suffices (UI is Phase 16). Decide + note. [VERIFIED]                                                                                                                       |

### Alternatives Considered

| Instead of                                  | Could Use                                                       | Tradeoff                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| ------------------------------------------- | --------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| New `StreamDockInputService` (sibling)      | Fold the poll loop into the Phase-14 `StreamDockControlService` | CONTEXT marks this discretionary. A sibling keeps the paint path (Phase 14) and the input path (Phase 15) as separate single-responsibility classes that both share the held handle; the control service already owns the `shared_ptr<IDevice>`, so the input service needs a way to obtain it (accessor or shared `DeviceLookup`). Recommended: **sibling** for SRP, sharing the handle via the control service's getter or the same `DeviceLookup` lambda (the flyweight guarantees one backend). |
| Direct `onEvent` callback → service         | `EventBus` pub/sub                                              | EventBus is built for multi-subscriber fan-out at >100 Hz; Phase 15 has one consumer. Direct callback is simpler and lower-latency; switch to EventBus only when Phase 19's plugin bridge also needs the stream. See Open Question 2.                                                                                                                                                                                                                                                               |
| GUI-thread QTimer pump of `poll()`          | Dedicated HID reader `QThread`                                  | A tight GUI-thread QTimer (e.g. 8–16 ms) keeps everything single-threaded and avoids cross-thread `shared_ptr<IDevice>` hazards (the backend is thread-affine, device.hpp:141). `poll()` drains ≤8 reports/call non-blocking (timeout 0). A reader thread is only justified if a measured event-drop appears under fast spin (revisit Phase 25). Recommended: **GUI-thread QTimer** with a poll interval tight enough that 8 reports/cycle never overflow.                                          |
| 16 ms QTimer coalescer in the service (C++) | Coalesce at the QML observer layer (INPUT-02 original wording)  | INPUT-02's "QML observer layer" framing predates the app having any input path. Since Phase 15 dispatches to `ActionEngine` (not QML repaints), the coalescer belongs in the **dispatch service** (accumulate signed delta per encoder, fire one `onCw`/`onCcw` chain per frame). QML repaint coalescing is a Phase-16/separate concern. Recommended: coalesce in the service.                                                                                                                      |

**Installation:** None — no new dependencies. Build via existing CMake targets (`ajazz_app`, `ajazz_core`, `ajazz_devices_streamdeck`).

## Package Legitimacy Audit

**Not applicable.** Phase 15 installs **no external packages** (npm/PyPI/crates). It is a C++20/Qt6 in-tree change reusing first-party components and the existing Catch2 harness. slopcheck/registry verification steps are inapplicable to a pure in-repo C++ phase. No `[SLOP]`/`[SUS]` exposure.

## Architecture Patterns

### System Architecture Diagram

```
   AKP05E 0300:3004 (keys / 4 encoders / touch strip)
                 │  raw HID input reports (interrupt-IN)
                 ▼
        ITransport (hidapi_hidraw)  ◄── MockTransport.enqueueRead() in tests
                 │  read(buf, timeout=0)
                 ▼
   ┌─────────────────────────────────────────────────────────┐
   │ core: Akp05Device::poll()  (drains ≤8 reports/call)       │
   │   parseInputReport(frame) ─► InputEvent                   │
   │     key 1..10 → KeyPressed/KeyReleased                    │
   │     0x2X → EncoderTurned(signed Δ) / Pressed / Released   │
   │     0x3X → TouchTap/SwipeL/SwipeR/LongPress (+ X 0..639)   │
   │   → DeviceEvent  (value re-packs gesture<<16|X for touch)  │
   │   → invokes registered onEvent(cb)                        │
   └───────────────────────────┬─────────────────────────────┘
                               │ DeviceEvent  (on GUI thread via QTimer pump)
                               ▼
   ┌─────────────────────────────────────────────────────────┐
   │ app: StreamDockInputService (NEW, GUI thread)            │
   │   • QTimer pumps controlService.heldDevice()->poll()      │
   │   • onEvent(DeviceEvent) dispatch:                        │
   │       KeyPressed   → Profile.keys[i].onPress              │
   │       KeyReleased  → Profile.keys[i].onRelease            │
   │       (long-press timer) → keys[i].onLongPress            │
   │       EncoderTurned→ accumulate Δ per i ─►16ms coalescer  │
   │                       drain: Δ>0 onCw / Δ<0 onCcw         │
   │       EncoderPressed→ encoders[i].onPress + SYNTH release  │
   │       TouchTap     → zone=X*4/640 → encoders[zone].onPress │
   │       SwipeL/R     → page prev/next intent (Phase 16)     │
   │   • engine.run(chain)                                     │
   └───────────────────────────┬─────────────────────────────┘
                               │ ActionChain
                               ▼
   ┌─────────────────────────────────────────────────────────┐
   │ core: ActionEngine (NEW instance — first in app)         │
   │   KeyPress/RunCommand/OpenUrl/Plugin → ActionExecutors    │
   │   Sleep/delayMs → QtExecutor (non-blocking, A2)          │
   │   OpenFolder/BackToParent → nav stack push/pop           │
   └───────────────────────────┬─────────────────────────────┘
                               ▼
        app ActionExecutors lambdas (keyPress / runCommand / openUrl)
        plugin → logged no-op stub (Phase 19 seam)
```

The diagram traces a single input event from the wire to the executed side effect. File-to-implementation mapping is in the Standard Stack tables.

### Recommended Project Structure

```
src/app/src/
├── stream_dock_input_service.hpp     # NEW — QObject, QTimer poll, ActionEngine owner, onEvent dispatch
├── stream_dock_input_service.cpp     # NEW — poll pump, binding lookup, coalescer, release synth, executors
├── application.{hpp,cpp}             # EDIT — own m_streamDockInput; construct ActionEngine+QtExecutor; share handle
src/devices/streamdeck/src/
├── akp05.cpp / akp05_protocol.hpp    # DO NOT EDIT (RE source of truth — reuse parseInputReport/poll)
tests/unit/
├── test_stream_dock_input_service.cpp  # NEW — MockTransport.enqueueRead feeds frames; spy executors assert dispatch
```

### Pattern 1: ActionEngine construction with app executors + QtExecutor (the first instantiation)

**What:** Construct one `core::ActionEngine` with `ActionExecutors` lambdas for the OS side effects, and pass the app's `QtExecutor` (a `shared_ptr<core::Executor>`) so `Sleep`/`delayMs` defer instead of blocking the poll thread.
**When to use:** Once, owned by the input service (or Application), set the active profile via `engine.setProfile()` on `profileChanged`.
**Example:**

```cpp
// app layer — first ActionEngine instantiation
core::ActionExecutors executors;
executors.keyPress   = [](std::string_view json){ /* synthesise OS key — see Don't Hand-Roll */ };
executors.runCommand = [](std::string_view json){ /* QProcess::startDetached(argv) */ };
executors.openUrl    = [](std::string_view url){ /* QDesktopServices::openUrl */ };
executors.plugin     = [](std::string_view id, std::string_view){      // Phase 19 SEAM
    AJAZZ_LOG_INFO("input", "plugin action {} ignored (plugin host arrives Phase 19)", id);
};
// QtExecutor must outlive the engine (qt_executor.hpp lifetime note); own it in Application.
auto engine = std::make_unique<core::ActionEngine>(std::move(executors), m_qtExecutor /*shared_ptr*/);
```

> `ActionEngine` ctor signature: `ActionEngine(ActionExecutors = {}, std::shared_ptr<Executor> = nullptr)` (action_engine.hpp:111). `QtExecutor` derives `core::Executor` (qt_executor.hpp:35). [VERIFIED]

### Pattern 2: poll-pump + onEvent dispatch on the shared held handle

**What:** A QTimer fires every N ms and calls `heldDevice->poll()`; the backend invokes the registered `onEvent` callback synchronously for each decoded event. The callback maps the event to a binding chain and runs it. The held `shared_ptr<IDevice>` comes from the **Phase-14 control service** — never a fresh `open()` (ARCH-03 single handle).
**When to use:** While a Stream Deck is the active device.
**Example:**

```cpp
// register once when the active Stream Deck is acquired
m_held = m_controlService->heldDevice();   // share Phase-14's shared_ptr; do NOT re-open
m_held->onEvent([this](core::DeviceEvent const& ev){ dispatch(ev); });   // cb runs on poll thread (GUI here)
// QTimer pump
connect(m_pollTimer, &QTimer::timeout, this, [this]{ if (m_held) m_held->poll(); });
m_pollTimer->start(/* tight cadence, e.g. 8ms */);
```

> `poll()` reads with timeout 0 and loops ≤8 times (akp05.cpp:474-475), so it returns promptly even with no input. `onEvent` doc: "callback invoked from the I/O thread" (device.hpp:199) — here the I/O thread IS the GUI thread (we pump on it), so dispatching straight into `ActionEngine` is safe. [VERIFIED]

### Pattern 3: 16 ms encoder-rotation coalescer (Pitfall 23 closed)

**What:** On each `EncoderTurned`, add the signed delta to a per-encoder accumulator (`std::array<int, EncoderCount>`) and arm a single-shot 16 ms `QTimer` (if not already armed). On timeout, for each encoder with a non-zero accumulated delta, fire `onCw` (delta>0) or `onCcw` (delta\<0) **once** (or N times if the design wants per-tick semantics — see Open Question 3), then clear the accumulators.
**When to use:** All encoder rotation dispatch — fast spin must produce one dispatch per frame, not one per tick.
**Example:**

```cpp
void onEncoderTurned(std::uint16_t enc, std::int32_t delta){
    m_encAccum[enc] += delta;
    if (!m_coalesceTimer->isActive()) m_coalesceTimer->start(16);   // single-shot, Qt::CoarseTimer
}
void drainCoalesced(){
    auto const& prof = m_profileAccessor();
    for (std::uint16_t e = 0; e < akp05::EncoderCount; ++e){
        if (m_encAccum[e] == 0) continue;
        auto it = prof.encoders.find(e);
        if (it != prof.encoders.end())
            m_engine->run(m_encAccum[e] > 0 ? it->second.onCw : it->second.onCcw);
        m_encAccum[e] = 0;
    }
}
```

### Pattern 4: Encoder press → synthetic release pairing (INPUT-04)

**What:** akp05.md §"Edge cases" states the device emits **press only, never a release**. So on `EncoderPressed`, fire `encoders[i].onPress` and then synthesise the paired release (Companion convention — keeps the press/release API uniform). The synthesis is app-layer; do NOT rely on the wire decode for a release frame.
**When to use:** Every encoder press.
**Note:** The akp05 decode (akp05.cpp:277-278) *can* emit `EncoderReleased` when byte 11 transitions to 0x00, but the hardware reportedly never sends that frame. Treat `EncoderPressed` as a momentary event and synthesise the release immediately (or after a short fixed interval if a hold semantic is later needed). Verify the real frame behaviour on hardware in Phase 25; do NOT change the decode meanwhile.

### Anti-Patterns to Avoid

- **Opening a second HID handle for input.** Violates ARCH-03; defeats the flyweight. Share the Phase-14 control service's held `shared_ptr<IDevice>`.
- **Adding a second input parser.** `parseInputReport` is capture-verified and the RE source of truth. Reuse `poll()`/`onEvent`; never re-decode raw frames in the app.
- **Editing `akp05.cpp` / `akp05_protocol.hpp`.** Wire decode is off-limits (CLAUDE.md). The release-synthesis and zone-derivation are app-layer.
- **Blocking the poll thread in a Sleep action.** Inject `QtExecutor` into the `ActionEngine`; never call a default `BlockingExecutor` on the GUI/poll thread.
- **Dispatching one `onCw`/`onCcw` per raw tick during fast spin.** Pitfall 23 signal-storm — coalesce on the 16 ms timer.
- **Hardcoding the touch zone map as fact.** The X→zone split is provisional (§5); flag it, keep it parameterised, reconcile in Phase 25.
- **Capturing a raw `IDevice*` across event-loop turns.** Hold the `shared_ptr` (UAF window — Pitfall 2 / Phase 4 D-06).

## Don't Hand-Roll

| Problem                                                          | Don't Build                                      | Use Instead                                                                                    | Why                                                                                                                                                         |
| ---------------------------------------------------------------- | ------------------------------------------------ | ---------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Raw HID frame decode                                             | A new input parser in the app                    | `akp05::parseInputReport` via `Akp05Device::poll()`                                            | Capture-verified RE; byte-tested; CLAUDE.md off-limits. [VERIFIED: akp05.cpp:238]                                                                           |
| Chain interpretation (KeyPress/RunCommand/OpenUrl/Sleep/folders) | A bespoke action dispatcher                      | `core::ActionEngine`                                                                           | Complete callback-driven interpreter; folder nav + delays already handled. [VERIFIED: action_engine.cpp]                                                    |
| Non-blocking Sleep / delayMs                                     | `std::this_thread::sleep_for` on the poll thread | `app::QtExecutor` injected into ActionEngine                                                   | Audit A2; QTimer::singleShot defers without blocking. [VERIFIED: qt_executor.hpp]                                                                           |
| Held device handle                                               | A new `open()` for input                         | Phase-14 control service's held `shared_ptr<IDevice>`                                          | ARCH-03 single handle; flyweight is weak_ptr-keyed. [CITED: 14-RESEARCH.md Pattern 2]                                                                       |
| OS key synthesis                                                 | A raw X11/Win32/macOS keycode poker              | A cross-platform key-injection approach decided in the keyPress executor (see Open Question 4) | Phase 15 wires the executor; the *backend* of keyPress (uinput / SendInput / CGEvent) is a portability decision — confirm scope with the planner. [ASSUMED] |
| Shell command launch                                             | `system()`                                       | `QProcess::startDetached`                                                                      | No blocking, no shell-injection of the whole string; argv array. [ASSUMED — confirm settingsJson shape]                                                     |
| URL open                                                         | Manual platform branch                           | `QDesktopServices::openUrl`                                                                    | Standard Qt; one line. [ASSUMED]                                                                                                                            |
| Codename→device resolution                                       | A new lookup                                     | The `DeviceLookup` lambda Application already injects (5× precedent)                           | Consistency; flyweight-backed. [VERIFIED: application.cpp:84-198]                                                                                           |
| Input fan-out to many consumers (Phase 19)                       | Ad-hoc callback list                             | `core::EventBus` (if multi-subscriber needed)                                                  | Built COW pub/sub for >100 Hz; only adopt when Phase 19's bridge subscribes. [VERIFIED: event_bus.hpp]                                                      |

**Key insight:** Every hard part of Phase 15 — frame decode, chain interpretation, non-blocking sleep — already exists and is tested. The phase is glue: a poll pump, a `DeviceEvent`→`Binding` mapping, a 16 ms coalescer, release synthesis, and four executor lambdas (three real, one stubbed). The highest-risk temptation is re-decoding frames or opening a second handle; both are explicitly forbidden.

## Runtime State Inventory

> Phase 15 is **not** a rename/refactor/migration phase. It is greenfield app wiring (a new input service + first ActionEngine instantiation). No stored data, service config, OS-registered state, secrets, or build artifacts carry a renamed string.

| Category            | Items Found                                   | Action Required |
| ------------------- | --------------------------------------------- | --------------- |
| Stored data         | None — no datastore keys change.              | None.           |
| Live service config | None.                                         | None.           |
| OS-registered state | None.                                         | None.           |
| Secrets/env vars    | None.                                         | None.           |
| Build artifacts     | None (new source files + CMake target edits). | None.           |

**Nothing found in any category** — verified: Phase 15 adds new app-layer files and wires existing core components; it renames nothing.

## Common Pitfalls

### Pitfall 1: Assuming akp05 carries the `value=0` half-step workaround (it doesn't)

**What goes wrong:** The requirement says "replacing the `value=0` half-step workaround at `akp05.cpp`". Reading akp05.cpp expecting that code wastes time and risks an unnecessary wire-decode edit.
**Why it happens:** INPUT-01 was written for **AKP03** (`akp03.cpp:396` — `devEv.value = 0;` in the `EncoderReleased` mapping). akp05's `parseInputReport` already emits a genuine `EncoderTurned` (signed delta) AND a distinct `EncoderReleased` enum; it has no half-step kludge.
**How to avoid:** Treat INPUT-04's "replace the workaround" as satisfied at the akp05 decode level. Phase 15's actual INPUT-04 work is **app-layer**: (a) the 16 ms rotation coalescer and (b) the press→synthetic-release pairing. Do NOT edit `akp05.cpp`.
**Warning signs:** A diff touching `akp05.cpp` or `akp05_protocol.hpp`.

### Pitfall 2: Encoder press-only vs the decode's dormant release branch

**What goes wrong:** Wiring `EncoderReleased` from the wire and never seeing it fire (hardware sends press-only), so a binding that expects a release pair never completes.
**Why it happens:** akp05.cpp:277-278 decodes a release when byte 11 == 0x00, but akp05.md §"Edge cases" says the device never emits that frame.
**How to avoid:** Synthesise the release in the dispatch layer on the press edge (Pattern 4). Do NOT depend on a wire release frame. Reconcile the true frame behaviour on hardware in Phase 25.
**Warning signs:** Encoder "press" actions that never release; tests that only pass because they hand-craft a release frame the device never sends.

### Pitfall 3: Touch zone derivation does not exist yet (and is provisional)

**What goes wrong:** Expecting the backend to hand you a zone index (0..3). It does not — `poll()` packs `(gesture<<16)|X` into `DeviceEvent::value` (akp05.cpp:509-517); there is no zone code anywhere.
**Why it happens:** akp05.md §122-130 lists a *suggested* "bytes 10..11 = touch zone index" mapping, but the in-code parser instead decodes an absolute X (0..639) and the doc itself flags every byte as provisional.
**How to avoid:** Derive `zone = X * EncoderCount / TouchStripRangeX` (i.e. `X*4/640`) in the app, route tap-zone-N → `encoders[N].onPress`. Keep the formula in one named helper and mark it provisional (Phase 25 hardware reconciliation). The `gesture` nibble is recovered from the upper 16 bits.
**Warning signs:** A test asserting a zone index read directly off the wire; a hardcoded zone table presented as confirmed.

### Pitfall 4: Sleep/delay blocking the poll thread

**What goes wrong:** A chain with a `Sleep` or `delayMs` blocks the GUI/poll thread, freezing input and the UI.
**Why it happens:** Constructing `ActionEngine` without injecting `QtExecutor` falls back to the process-wide `BlockingExecutor`, which sleeps in place (action_engine.cpp:30, executor.hpp).
**How to avoid:** Always pass the app's `QtExecutor` (a `shared_ptr<core::Executor>`) to the `ActionEngine` ctor; ensure it outlives the engine (own both in Application). [VERIFIED: qt_executor.hpp lifetime note]
**Warning signs:** UI freeze when a key bound to a multi-step delayed chain is pressed.

### Pitfall 5: Dropping fast input by polling too slowly / dropping the shared_ptr

**What goes wrong:** A 15 s `BatteryService`-style cadence would drop nearly all input. Also, if the input service resolves the device per-call and lets the `shared_ptr` drop, the handle closes (and re-opens) — churn + missed events.
**Why it happens:** Copy-pasting the slow-poll or per-call-lookup patterns from BatteryService/TimeSyncService.
**How to avoid:** Poll on a tight cadence (≈8 ms; `poll()` drains ≤8 reports/cycle so the effective ceiling is ~1000 reports/s). Hold the device `shared_ptr` (shared with Phase 14) for the session. Encoder-heavy devices burst at >100 Hz (event_bus.hpp:33) — size the cadence accordingly.
**Warning signs:** Missed key/encoder events under fast interaction; repeated "device opened" log lines.

### Pitfall 6: ASCII-only test names + ctest filter flag

**What goes wrong:** Em-dash/arrow in Catch2 names mangle under the Win32 CMD codepage and break `-R` filtering; `--test-regex` is a typo.
**How to avoid:** ASCII-only `TEST_CASE`/`SECTION` titles; `ctest --preset linux-release -R <name>` (flag is `--tests-regex`/`-R`). (CLAUDE.md hard rules.)

### Pitfall 7: `dynamic_cast` to a capability returns nullptr

**What goes wrong:** If the held device isn't a Stream Deck (or input service is asked about a non-display device), a cast crashes on deref.
**How to avoid:** The input path uses `IDevice::poll()`/`onEvent` directly (no cast needed for input). If any capability cast is added, null-check within 3 lines (project Pitfall 2; time_sync_service.cpp:246). [VERIFIED]

## Code Examples

### DeviceEvent → binding dispatch (INPUT-03/04/05 core)

```cpp
// Source pattern: device.hpp DeviceEvent::Kind + profile.hpp Binding/EncoderBinding — VERIFIED
void StreamDockInputService::dispatch(core::DeviceEvent const& ev) {
    auto const& prof = m_profileAccessor();                 // ProfileController::activeProfile()
    switch (ev.kind) {
    case core::DeviceEvent::Kind::KeyPressed:
        if (auto it = prof.keys.find(ev.index); it != prof.keys.end())
            m_engine->run(it->second.onPress);              // INPUT-03
        // (arm a long-press timer here if onLongPress is non-empty)
        break;
    case core::DeviceEvent::Kind::KeyReleased:
        if (auto it = prof.keys.find(ev.index); it != prof.keys.end())
            m_engine->run(it->second.onRelease);            // INPUT-03
        break;
    case core::DeviceEvent::Kind::EncoderTurned:
        onEncoderTurned(ev.index, ev.value);                // accumulate → 16ms coalescer (Pattern 3)
        break;
    case core::DeviceEvent::Kind::EncoderPressed:
        if (auto it = prof.encoders.find(ev.index); it != prof.encoders.end())
            m_engine->run(it->second.onPress);              // INPUT-04 press
        synthesiseEncoderRelease(ev.index);                 // INPUT-04 host-synth release (Pattern 4)
        break;
    case core::DeviceEvent::Kind::EncoderReleased:          // dormant on hardware (press-only); harmless
        break;
    case core::DeviceEvent::Kind::TouchStrip: {
        auto const gesture = static_cast<std::uint32_t>(ev.value) >> 16;   // packed by poll() (akp05.cpp:517)
        auto const x       = static_cast<std::uint16_t>(ev.value & 0xFFFF);
        if (gesture == 0 /* TouchTap */) {
            auto const zone = static_cast<std::uint16_t>(x * akp05::EncoderCount / akp05::TouchStripRangeX);  // PROVISIONAL §5
            if (auto it = prof.encoders.find(zone); it != prof.encoders.end())
                m_engine->run(it->second.onPress);          // INPUT-05 tap-zone-N → encoder-N onPress
        } else if (gesture == 1 /* SwipeLeft */) {
            emit pageNavRequested(/*prev*/ -1);             // INPUT-05 swipe → page nav intent (Phase 16)
        } else if (gesture == 2 /* SwipeRight */) {
            emit pageNavRequested(/*next*/ +1);
        }
        break;
    }
    default: break;   // Connected/Disconnected handled by hotplug elsewhere
    }
}
```

> Touch `value` packing: `(gesture<<16)|x` where `gesture = Kind - TouchTap` (akp05.cpp:513-517). `EncoderTurned` value is signed delta +1 CW / -1 CCW (akp05_protocol.hpp:291). Profile maps are `unordered_map<uint16_t, Binding>` / `<uint16_t, EncoderBinding>` (profile.hpp:154-155). [VERIFIED]

### Hardware-free dispatch test (the gating proof)

```cpp
// Source pattern: MockTransport.enqueueRead + makeAkp05WithTransport + spy executors — VERIFIED seams
auto transport = std::make_unique<ajazz::tests::MockTransport>();
auto* obs = transport.get();
transport->open();
auto dev = ajazz::streamdeck::makeAkp05WithTransport(descriptor, id, std::move(transport));

bool keyPressFired = false;
core::ActionExecutors spies;
spies.keyPress = [&](std::string_view){ keyPressFired = true; };
auto engine = core::ActionEngine(std::move(spies));   // BlockingExecutor fine in a test with no Sleep

// Build a profile: key 3 onPress = [{kind=KeyPress}]
core::Profile prof; prof.keys[3].onPress = { core::Action{ .kind = core::ActionKind::KeyPress } };

// Feed a canned KEY-PRESSED frame for key 3 (tag@9 = 3, byte10 != 0):
std::vector<std::uint8_t> frame(16, 0); frame[9] = 0x03; frame[10] = 0x01;
obs->enqueueRead(frame);
dev->onEvent([&](core::DeviceEvent const& ev){ /* service.dispatch(ev) using `prof` + `engine` */ });
dev->poll();                                          // decodes + invokes cb
CHECK(keyPressFired);                                 // INPUT-03

// CW vs CCW: tag@9 in 0x20.., byte10 = +1 (CW) then 0xFF (CCW); assert onCw≠onCcw fired.
// Coalescer: enqueue 5 EncoderTurned frames, pump poll() 5×, processEvents, assert ONE onCw dispatch after 16ms.
```

> `MockTransport::enqueueRead(bytes)` feeds the next `read()` (FIFO; mock_transport.hpp:96). `parseInputReport` needs ≥16 bytes and reads tag at offset 9, edge at 10, encoder button at 11 (akp05.cpp:239-280). ASCII-only test names; tag `[stream-dock-input]`. [VERIFIED]

## State of the Art

| Old Approach                                            | Current Approach                                                               | When Changed            | Impact                                                                                                                   |
| ------------------------------------------------------- | ------------------------------------------------------------------------------ | ----------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| AKP03 `value=0` half-step encoder-release kludge        | akp05 emits genuine `EncoderTurned` (signed) + distinct `EncoderReleased` enum | akp05 backend (current) | Phase 15 does NOT inherit the kludge; synthesis is app-layer (Pitfall 1). [VERIFIED: akp03.cpp:396 vs akp05.cpp:262-280] |
| No app input path (decode existed, nothing consumed it) | Phase 15 adds the poll-pump + ActionEngine dispatch                            | This phase              | First `ActionEngine` instantiation; `grep poll()/onEvent src/app/` was 0 hits. [VERIFIED]                                |
| Touch "zone index on the wire" (akp05.md suggestion)    | Absolute X decoded; app derives zone provisionally                             | akp05.cpp parser        | Zone map is app-layer + provisional; HW-reconciled Phase 25. [VERIFIED: akp05.cpp:285-315]                               |

**Deprecated/outdated:**

- akp05.md header banner (lines 6-9) still says "current akp05.cpp models 15 keys (3×5)" — **stale**; the backend is 10 keys (2×5), `KeyCount=10` (akp05_protocol.hpp:69). Cosmetic; do not let it mislead.
- akp05.md "Wire protocol" section is flagged "Unverified from a real capture" / "hypothesised" — but the firmware VER and key/encoder/touch input tags were since confirmed on a physical AKP05E (2026-05-20 per CLAUDE.md). The §5 touch zone/swipe framing remains PROVISIONAL.

## Assumptions Log

| #   | Claim                                                                                                                                         | Section                       | Risk if Wrong                                                                                                                                                                                                          |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | A sibling `StreamDockInputService` (vs folding into the Phase-14 control service) is the right shape.                                         | Standard Stack / Alternatives | Low — discretionary per CONTEXT; either works. Re-shape is mechanical.                                                                                                                                                 |
| A2  | A GUI-thread QTimer pump (≈8 ms) is sufficient cadence; no dedicated reader thread needed.                                                    | Pitfall 5 / Alternatives      | Medium — verifiable only on hardware under fast spin (Phase 25). `poll()` drains ≤8/cycle.                                                                                                                             |
| A3  | The provisional touch zone formula is `zone = X * 4 / 640`.                                                                                   | Pitfall 3 / Code Examples     | Medium — §5 is provisional; the true zone boundaries are hardware-gated (Phase 25). Keep parameterised.                                                                                                                |
| A4  | The encoder release is synthesised immediately after the press chain (no hold semantic).                                                      | Pattern 4                     | Low-Medium — if a binding wants a long-hold encoder press, the timing needs revisiting; bindings today have onPress only for encoders (no onLongPress on EncoderBinding).                                              |
| A5  | `keyPress` executor backend (uinput / SendInput / CGEvent) is in Phase-15 scope, OR may be deferred to a later phase as a stub like `plugin`. | Don't Hand-Roll               | Medium — cross-platform OS key injection is a non-trivial portability surface; the planner must decide whether Phase 15 implements it or stubs it (PLUGIN-12 `system.hotkey` is a Phase-21 item). See Open Question 4. |
| A6  | `runCommand` parses `settingsJson` into argv for `QProcess::startDetached`; `openUrl` uses `QDesktopServices`.                                | Don't Hand-Roll               | Low-Medium — depends on the `settingsJson` schema, which the profile/PI defines (Phase 20). For Phase 15 a minimal parse suffices; confirm shape.                                                                      |
| A7  | Coalescer fires ONE `onCw`/`onCcw` per 16 ms frame regardless of accumulated tick count.                                                      | Pattern 3 / Open Q3           | Medium — "one signal per frame" (INPUT-02) vs "run the chain once per net tick" are different semantics; pick deliberately.                                                                                            |

## Open Questions

1. **Touch zone boundaries and swipe→page wiring (PROVISIONAL §5).**

   - What we know: the backend gives absolute X (0..639) + gesture; akp05.md flags the zone/swipe map as hypothetical. Companion left-aligns 4 encoders in a 5-column grid and maps swipe to "5th button third row".
   - What's unclear: the real zone X-boundaries on the physical strip, and whether swipe should emit a generic page-nav intent (Phase 16 owns pages) or directly drive `ActionEngine` `OpenFolder`/`BackToParent`.
   - Recommendation: Phase 15 emits a `pageNavRequested(direction)` signal (or runs a configurable page action) and uses the even `X*4/640` split, both marked provisional; Phase 16 wires the page model and Phase 25 reconciles the zone boundaries against hardware.

1. **Direct `onEvent` callback vs `EventBus` for input fan-out.**

   - What we know: `EventBus` (event_bus.hpp) is a thread-safe COW pub/sub built explicitly for >100 Hz device input fan-out to "profile engine, plugin host, QML UI". Phase 15 has one consumer (the dispatch service); Phase 19 adds a second (the plugin bridge).
   - What's unclear: whether to adopt EventBus now (future-proof for Phase 19) or use a direct `onEvent` callback now and migrate later.
   - Recommendation: Use a direct `onEvent` callback into the input service for Phase 15 (simpler, lower latency, single consumer). Note EventBus as the migration target when Phase 19's bridge needs the same stream — the input service can publish to EventBus then.

1. **Coalescer semantics: one dispatch per frame, or N (per net tick)?**

   - What we know: INPUT-02 says "single signal per repaint frame; total delta accumulated and emitted once." But an action chain bound to `onCw` may be intended to fire per detent.
   - What's unclear: whether "5 rapid ticks → one dispatch" (the CONTEXT verification) means one chain run total, or one chain run carrying a magnitude.
   - Recommendation: Fire the chain **once** per 16 ms frame per direction (matches the CONTEXT verification "5 ticks coalesce to a single dispatch"); if per-tick magnitude is later needed, pass the accumulated delta into the chain context. Confirm with the planner.

1. **`keyPress` executor: implement OS key injection now, or stub it?**

   - What we know: `ActionExecutors.keyPress` takes a `settingsJson` keycode; the actual injection is platform-specific (Linux uinput/XTEST, Windows SendInput, macOS CGEvent). PLUGIN-12 (`system.hotkey`) is a Phase-21 item and is "opt-in, never always-on".
   - What's unclear: whether Phase 15's first-light goal includes real OS key synthesis or whether keyPress/runCommand/openUrl may be partially stubbed like `plugin`.
   - Recommendation: Wire `runCommand` (`QProcess::startDetached`) and `openUrl` (`QDesktopServices`) fully (low-risk, cross-platform); decide with the planner whether `keyPress` is implemented now or stubbed-with-log until Phase 21. The INPUT-03 verification (a key press fires its bound action via ActionEngine) is satisfiable with spy executors regardless.

1. **Active-device selection when multiple Stream Decks are connected.**

   - What we know: Phase 14 simplified "active device" to "first connected Stream Deck" (per 14-02-PLAN.md). The input service must drive the same device the control service holds.
   - What's unclear: multi-deck disambiguation (Phase 16 active-device UI).
   - Recommendation: Share the Phase-14 control service's held handle / active codename; do not introduce a competing selection. Note the single-deck simplification.

## Environment Availability

| Dependency                                          | Required By                                  | Available                     | Version                                            | Fallback                                                                                                                                       |
| --------------------------------------------------- | -------------------------------------------- | ----------------------------- | -------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| Qt 6 (Core/Gui)                                     | QObject + QTimer + QProcess/QDesktopServices | ✓                             | 6.7+ (project baseline)                            | —                                                                                                                                              |
| CMake + Ninja                                       | Build                                        | ✓                             | project presets                                    | —                                                                                                                                              |
| Catch2                                              | Unit tests                                   | ✓                             | in-tree harness                                    | —                                                                                                                                              |
| `MockTransport`                                     | Hardware-free dispatch test (`enqueueRead`)  | ✓                             | tests/unit/fixtures/mock_transport.hpp             | —                                                                                                                                              |
| `QtExecutor`                                        | Non-blocking Sleep in ActionEngine           | ✓                             | src/app/src/qt_executor.{hpp,cpp} (exists, unused) | —                                                                                                                                              |
| OS key-injection backend (uinput/SendInput/CGEvent) | `keyPress` executor IF implemented now       | ✗ unverified at research time | —                                                  | Stub `keyPress` with a log line (like `plugin`) until Phase 21; see Open Question 4.                                                           |
| AKP05E hardware (`0300:3004`)                       | Live encoder-spin / touch verification       | ✗ at research time            | —                                                  | **Defer to Phase 25** — MockTransport-fed frames are the Phase 15 gating proof. systemd ≥258 `uaccess` caveat if connected (replug/`setfacl`). |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** OS key injection (stub until Phase 21 if needed); AKP05E hardware (MockTransport gates Phase 15; live verify is Phase 25).

## Validation Architecture

> `workflow.nyquist_validation` is `true` in `.planning/config.json` — section included.

### Test Framework

| Property           | Value                                                                               |
| ------------------ | ----------------------------------------------------------------------------------- |
| Framework          | Catch2 (in-tree), ctest                                                             |
| Config file        | CMake presets (`linux-release`, `linux-debug`, `dev`, `fuzz`)                       |
| Quick run command  | `ctest --preset linux-release -R stream_dock_input` (new `[stream-dock-input]` tag) |
| Full suite command | `ctest --preset linux-release`                                                      |

### Phase Requirements → Test Map

| Req ID    | Behavior                                                                                                          | Test Type                                                          | Automated Command                                   | File Exists? |
| --------- | ----------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ | --------------------------------------------------- | ------------ |
| INPUT-03  | Canned key-pressed frame → `keys[i].onPress` chain runs (spy executor fires); key-released → `onRelease`          | unit (MockTransport.enqueueRead + spy ActionExecutors)             | `ctest --preset linux-release -R stream_dock_input` | ❌ Wave 0    |
| INPUT-04a | Encoder CW frame fires `onCw`, CCW frame fires `onCcw` (CW≠CCW)                                                   | unit (MockTransport feed)                                          | same                                                | ❌ Wave 0    |
| INPUT-04b | Encoder press fires `encoders[i].onPress` and a synthetic release is produced (host-synth, no wire release frame) | unit                                                               | same                                                | ❌ Wave 0    |
| INPUT-04c | 5 rapid rotation ticks coalesce to ONE dispatch within the 16 ms window                                           | unit (pump poll×5 + processEvents + QTRY)                          | same                                                | ❌ Wave 0    |
| INPUT-05a | Touch tap with X in zone-N range → `encoders[N].onPress` runs (provisional X\*4/640 split)                        | unit (feed gesture=0 frame at chosen X)                            | same                                                | ❌ Wave 0    |
| INPUT-05b | Swipe-left/right emits the page-nav intent (signal)                                                               | unit (feed gesture=1/2 frame, assert signal)                       | same                                                | ❌ Wave 0    |
| (cross)   | Sleep step in a chain does NOT block the poll thread (QtExecutor injected)                                        | unit (chain with Sleep + QtExecutor; assert continuation deferred) | same                                                | ❌ Wave 0    |
| (cross)   | Single held handle: input pump does not re-open the transport (shares Phase-14 handle)                            | unit (assert no second open recorded)                              | same                                                | ❌ Wave 0    |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R stream_dock_input` (+ `-R akp05` when touching the device-path verification).
- **Per wave merge:** `ctest --preset linux-release` (full ~408-case suite; trust the live count per CLAUDE.md, do not hand-edit).
- **Phase gate:** full suite green before `/gsd:verify-work`; MockTransport-fed dispatch assertions are the gating proof (live encoder/touch on hardware deferred to Phase 25).

### Wave 0 Gaps

- [ ] `tests/unit/test_stream_dock_input_service.cpp` — covers INPUT-03/04/05 + Sleep-non-block + held-handle, via `MockTransport.enqueueRead` + spy `ActionExecutors` + a fake profile + a fake/shared `DeviceLookup`.
- [ ] Confirm the offscreen QML smoke target (`tests/qml/`) needs no case (Phase 15 dispatch is headless C++; UI wiring is Phase 16) — likely no QML test.
- [ ] Register the new test in `tests/unit/CMakeLists.txt`, linking `stream_dock_input_service.cpp` + the streamdeck device source + `action_engine.cpp` (mirror `test_firmware_update_service.cpp` / `test_action_engine.cpp` link blocks).
- [ ] No framework install needed (Catch2 in-tree).

## Security Domain

`security_enforcement` is not set false in config; default = enabled. Phase 15 has a **moderate** security surface: it executes action chains that can synthesise OS key presses and run shell commands from profile data.

### Applicable ASVS Categories

| ASVS Category         | Applies | Standard Control                                                                                                                                                                                                                                                                                                                                                                                                                             |
| --------------------- | ------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | no      | (plugin auth is Phase 17)                                                                                                                                                                                                                                                                                                                                                                                                                    |
| V3 Session Management | no      | —                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| V4 Access Control     | no      | —                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| V5 Input Validation   | yes     | Input frames already range-checked/clamped in the backend (key 1..KeyCount; encoder index < EncoderCount; touch X clamped to 0..639, out-of-range discarded — SEC-009/CWE-20, akp05.cpp:292). The app must derive the zone with a bounded formula (`X*4/640` ∈ 0..3) and clamp. `settingsJson` for runCommand/keyPress is **profile-authored** (Phase 14 trust note: local/user-authored in Phase 15). [VERIFIED: akp05.cpp:251,264,289-294] |
| V6 Cryptography       | no      | —                                                                                                                                                                                                                                                                                                                                                                                                                                            |

### Known Threat Patterns for this stack

| Pattern                                                      | STRIDE              | Standard Mitigation                                                                                                                                                                                                                       |
| ------------------------------------------------------------ | ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Profile-supplied `runCommand` argv → shell/command injection | Elevation/Tampering | Use `QProcess::startDetached(program, args)` with an explicit argv list — never `system()` or a shell string. Profile is user-authored in Phase 15; full untrusted-source handling is Phase 19/22. [ASSUMED — confirm settingsJson shape] |
| Profile-supplied `keyPress` synthesising arbitrary OS input  | Elevation           | If implemented now, gate behind the same opt-in posture PLUGIN-12 `system.hotkey` mandates (never always-on); otherwise stub until Phase 21. [ASSUMED — Open Question 4]                                                                  |
| Out-of-range touch X → wrong/oob zone index                  | Tampering/DoS       | Backend already discards X ≥ 640; app zone formula is bounded 0..3. [VERIFIED: akp05.cpp:292]                                                                                                                                             |
| Sleep/delay chain freezing the poll/UI thread (DoS)          | DoS                 | Inject `QtExecutor` so delays defer via QTimer::singleShot (A2). [VERIFIED: qt_executor.hpp]                                                                                                                                              |
| Use-after-free of `IDevice` across poll turns (device yank)  | Tampering/DoS       | Share/hold the Phase-14 `shared_ptr<IDevice>`; backend zombie-contract no-ops after removal. [VERIFIED: device.hpp:145]                                                                                                                   |

## Project Constraints (from CLAUDE.md)

- **COD-031:** no `nlohmann::json` in `ajazz_core` or any installed public header — the input service is app-layer; `ActionEngine` parses no JSON (it hands `settingsJson` to the app executors verbatim). Keep JSON out of core.
- **RE is source of truth — do NOT alter wire decode:** `akp05.cpp`/`akp05_protocol.hpp` (`parseInputReport`, opcodes, tag layout) are capture-verified — reuse, never edit. Cross-check `docs/protocols/streamdeck/akp05*.md` before any protocol-adjacent change.
- **hidapi_hidraw backend only** — no libusb paths.
- **ASCII-only test names**; ctest filter is `--tests-regex`/`-R` (not `--test-regex`); working preset `ctest --preset linux-release`.
- **Atomic Conventional Commits** (`feat(app):`/`feat(streamdeck):`), on `feat/streamdock`; never skip pre-commit hooks; **cap concurrent execute agents at 2**.
- **Qt 6 / QML gotchas:** if the input service is QML-exposed, use `qmlRegisterSingletonInstance` + `create()`/`registerInstance()` + `static_assert(!is_default_constructible)` (lighting_service.hpp:96). Phase 15 dispatch is likely headless (UI is Phase 16) — decide and note.
- **Schema doc is source of truth for JSON wire keys** (`deviceCodename` ⇄ `"device"`) — relevant when reading the profile; Phase 15 reads the in-memory `Profile`, does not serialise.
- **Cross-platform strictness:** GCC `-Wreorder` on member init order (declare `m_streamDockInput` consistently with its init position); Apple Clang `-Wunused-const-variable`; MSVC `/W4 /WX` C4996 (prefer `_s` variants). `QProcess`/`QDesktopServices` are cross-platform.

## Sources

### Primary (HIGH confidence — in-tree source read this session)

- `src/devices/streamdeck/src/akp05.cpp` — `parseInputReport` (238), encoder decode (262-280), touch decode + X clamp (285-315), `poll()` drain + DeviceEvent mapping + gesture/X packing (471-533), `open()` idempotent + VER probe (442-451), `makeAkp05WithTransport` (928).
- `src/devices/streamdeck/src/akp05_protocol.hpp` — `KeyCount=10`/`EncoderCount=4`/`TouchStripRangeX=640`/`PacketSize=1024` (69-97), `InputEvent` struct + Kind enum (286-302).
- `src/devices/streamdeck/src/akp03.cpp` — the `value=0` half-step `EncoderReleased` workaround (393-396) that INPUT-01 referenced (a different file from akp05).
- `src/core/include/ajazz/core/device.hpp` — `DeviceEvent`/`Kind` (111-127), `onEvent`/`poll` contract + thread/zombie notes (175-215).
- `src/core/include/ajazz/core/action_engine.hpp` + `src/core/src/action_engine.cpp` — `ActionExecutors` (76-88), `ActionEngine` ctor + `run`/`runFrom` dispatch incl. Sleep deferral + folder nav (28-131).
- `src/core/include/ajazz/core/profile.hpp` — `Action`/`ActionKind` (42-73), `Binding` onPress/onRelease/onLongPress (100-105), `EncoderBinding` onCw/onCcw/onPress (113-118), `Profile.keys`/`encoders` maps (154-155).
- `src/core/include/ajazz/core/event_bus.hpp` — COW pub/sub fan-out for >100 Hz input (optional Phase-19 migration target).
- `src/app/src/qt_executor.hpp/.cpp` — non-blocking `Executor` (exists, currently unused); lifetime note.
- `src/app/src/application.cpp` — 5× DeviceLookup lambda precedent (84-198), hotplug/debouncer wiring (199-211).
- `src/app/src/battery_service.cpp` — QTimer poll precedent (44-50); `lighting_service.hpp` (96) QML singleton + static_assert pattern; `device_model.hpp` (167) `connectedCodenames()`.
- `.planning/phases/14-stream-dock-control-service/14-02-PLAN.md` — the held-open `StreamDockControlService`, `ProfileController::activeProfile()` getter, the shared handle Phase 15 drives.
- `tests/unit/fixtures/mock_transport.hpp` — `enqueueRead`/`writes`/`open` (96-142); `tests/unit/test_akp05_touch_strip.cpp` — the makeAkp05WithTransport + QGuiApplication offscreen test pattern.
- `docs/protocols/streamdeck/akp05.md` — encoder press-only synthesis convention (76, 159-160), touch 4-zone + swipe-page semantics (71-75, 122-130), provisional §5 flag.
- `CLAUDE.md`, `.planning/config.json` (`nyquist_validation: true`).

### Secondary (MEDIUM)

- Project memory notes (devices.yaml doc-only vs runtime; RE-corpus location) — context, cross-checked against source.

### Tertiary (LOW)

- None relied upon. No WebSearch/Context7 needed (in-tree C++ phase).

## Metadata

**Confidence breakdown:**

- Standard stack / reuse surfaces: HIGH — every component (poll/onEvent, ActionEngine, QtExecutor, Profile, MockTransport) read in-tree this session.
- Architecture (poll-pump, DeviceEvent→binding dispatch, ActionEngine wiring): HIGH — decode + engine + executor seams all confirmed; service shape discretionary per CONTEXT.
- Pitfalls: HIGH — derived from in-tree source (akp03 vs akp05 release divergence, press-only doc, missing zone code, QtExecutor blocking fallback).
- Touch zone map (A3), coalescer semantics (A7), keyPress backend (A5): MEDIUM — flagged as Open Questions; §5 is provisional + hardware-gated (Phase 25).

**Research date:** 2026-05-23
**Valid until:** 2026-06-22 (stable in-tree codebase; re-verify `akp05.cpp` line numbers, `profile.hpp` maps, and the Phase-14 `StreamDockControlService`/`activeProfile()` surface if the branch advances materially before planning).
