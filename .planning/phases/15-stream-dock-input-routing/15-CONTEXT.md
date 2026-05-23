# Phase 15: Stream Dock Input Routing - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + RE corpus + existing-code survey

<domain>
## Phase Boundary

Phase 15 makes physical input on a connected Stream Deck (AKP05E) fire bound actions. It is
the **first instantiation of the core `ActionEngine` in the app** — establishing the
device-event → binding → execution path that Phases 16, 19, and the rest of the family reuse.

**Delivers (INPUT-03/04/05):**

- A poll loop drives each connected Stream Deck (over the **same held-open handle** the Phase-14
  control service owns) and dispatches a key **press/release** to that key's bound action chain
  via `ActionEngine` (INPUT-03).
- Each of the **4 encoders** fires its bound actions on rotate **CW** vs **CCW** and on **press**;
  because the device emits a **press only (no release)**, the host **synthesises the release**
  (Companion convention) and delivers a genuine `EncoderReleased`; rotation is coalesced with a
  **16 ms `QTimer`** so fast spin produces one signal per frame (Pitfall 23) (INPUT-04).
- A touch-strip **tap on one of the 4 zones** fires the action bound to the **encoder under that
  zone**; a **swipe left/right** drives previous/next page (INPUT-05 — PROVISIONAL zone/coordinate
  map, hardware-gated, reconciled in Phase 25).

**Out of scope:** brightness slider / clear-all UI + binding persistence + pages (Phase 16 —
Phase 15 dispatches against whatever profile is loaded in memory); the plugin runtime (the
`ActionKind::Plugin` executor routes to the plugin host only in Phase 19 — here it may be a
logged no-op or wired to the existing in-process kinds); auxiliary surface rendering (Phase 23).
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Architecture

- The poll loop drives the **Phase-14 control service's held-open handle** — do NOT open a
  second handle (ARCH-03 single-handle invariant). The input loop is app-layer.
- Reuse the **core `ActionEngine`** (`src/core/include/ajazz/core/action_engine.hpp`) — it
  already interprets `Plugin`/`Sleep`/`KeyPress`/`RunCommand`/`OpenUrl`/`OpenFolder`/
  `BackToParent` and does folder navigation. Inject the **`QtExecutor`** (`src/app/src/qt_executor.hpp`)
  so `Sleep`/`delayMs` never block the HID poll thread (audit A2).
- The app supplies the `ActionExecutors` lambdas (keyPress / runCommand / openUrl / plugin).
  The `plugin` executor is a **seam for Phase 19** — Phase 15 wires the non-plugin kinds and
  leaves `plugin` as a logged stub (do not block on the plugin host).
- Reuse the existing input decode: `akp05::parseInputReport` (akp05.cpp:238) already produces
  key press/release, encoder turned/pressed/released, and touch tap/swipe-left/swipe-right/
  long-press; `Akp05Device` maps these to core `DeviceEvent` (akp05.cpp:487+). Do NOT add a
  second parser or alter the wire decode (RE source of truth).

### Bindings

- Dispatch from the in-memory `Profile`: key events → `Binding.onPress`/`onRelease`/`onLongPress`;
  encoder events → `EncoderBinding.onCw`/`onCcw`/`onPress` (`profile.hpp`). keyIndex is **1-based**
  on the wire (backend convention; the control-service phase established the translation).
- Touch **tap zone index (0..3) maps to the encoder under that zone** → fire that
  `EncoderBinding.onPress` (Companion convention); swipe → page nav (Phase 16's page model;
  Phase 15 emits the nav intent).

### Encoder release synthesis

- The device emits encoder **press only**; deliver `onPress` on press and synthesise the
  paired release. Confirm in research whether `Akp05Device` already emits a genuine
  `EncoderReleased` or still carries the v1.2 `value=0` half-step workaround (INPUT-01 was a
  Phase-10 item that was planned but never executed) — fix it here if still present.

### Claude's Discretion (planner/executor; surface trade-offs)

- Poll mechanism: `QTimer`-driven read on the GUI thread (cf. `BatteryService` 15 s pattern,
  but input needs a much tighter cadence) vs a dedicated read thread marshalling events to the
  GUI thread. Honor the akp05 read/ACK semantics (`akp05_init_sequence.md`); must not drop
  fast key/encoder events nor freeze the UI.
- Exact home of the poll loop (inside the Phase-14 `StreamDockControlService` vs a sibling
  `StreamDockInputService` that shares the held handle).
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Core / app reuse

- `src/core/include/ajazz/core/action_engine.hpp` — `ActionEngine`, `ActionExecutors`, `NavigationContext`, folder nav.
- `src/core/include/ajazz/core/profile.hpp` — `Binding` (onPress/onRelease/onLongPress), `EncoderBinding` (onCw/onCcw/onPress), `Action`/`ActionKind`.
- `src/core/include/ajazz/core/device.hpp` — core `DeviceEvent` API (line ~114: KeyPressed/KeyReleased/EncoderTurned/EncoderPressed/EncoderReleased/Touch\*).
- `src/app/src/qt_executor.hpp` — `QtExecutor` (non-blocking Sleep off the HID poll thread; audit A2).
- The Phase-14 control service (`src/app/src/stream_dock_control_service.{hpp,cpp}`) — the held-open handle to poll.

### Device input decode (reuse; do not alter)

- `src/devices/streamdeck/src/akp05.cpp:238` `parseInputReport` + `:487` InputEvent→DeviceEvent mapping; encoder press/release at `:278`.
- `src/devices/streamdeck/src/akp05_protocol.hpp:286` `InputEvent` struct (Kind enum incl. Touch\*).
- `docs/protocols/streamdeck/akp05.md` §Layout/§"Features that must work" — encoders emit press-only (synthesise release); touch 4 zones aligned to encoders; swipe = page turn (provisional §5).

### Decisions

- v1.2 INPUT-01/INPUT-02 (genuine `EncoderReleased`; 16 ms coalescer) — planned in Phase 10, not executed; fold the un-executed parts here.
- v1.1 ARCH-03 (single held-open handle).
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free: feed canned raw input frames (or `InputEvent`s) through the
  decode + dispatch path and assert the bound `ActionChain` ran (spy executors), incl. CW≠CCW,
  encoder press fires `onPress`, key press vs release pick the right chain, touch-zone-N → encoder-N
  `onPress`, and that 5 rapid rotation ticks coalesce to a single dispatch within the 16 ms window.
- Reuse `MockTransport` to feed input frames if the device read path is transport-driven.
- ASCII-only test names; `ctest --preset linux-release`.

</specifics>

<deferred>
## Deferred Ideas

- Brightness slider / clear-all UI + binding persistence + pages → Phase 16.
- `ActionKind::Plugin` routing to the plugin host → Phase 19 (Phase 15 leaves the plugin executor a stub).
- Touch zone/coordinate-map + swipe framing hardware reconciliation → Phase 25 (provisional §5).

</deferred>

______________________________________________________________________

*Phase: 15-stream-dock-input-routing*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; design context from replan research)*
