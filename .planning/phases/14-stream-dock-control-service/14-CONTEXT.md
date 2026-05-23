# Phase 14: Stream Dock Control Service - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions (Full Elgato SDK 1:1 / WebSocket runtime / replan-from-scratch) + RE corpus + existing-code survey

<domain>
## Phase Boundary

Phase 14 is the **load-bearing foundation** of the v1.3 milestone: the first app→device
call that actually reaches the AKP05E panel. It closes the Phase-10 UAT gap where the
capture-verified wire layer (`BAT`/`LIG`/`CLE`/`ULEND`) existed but **no app code ever
called it** — `open()` left the panel dark, assigned images never reached the device.

**Delivers (DISPLAY-06/07/08, DOCK-01/02, DEVICES-11):**

- A persistent control service that holds the active Stream Deck open for the session
  (single held-open HID handle) and issues a brightness-ON `LIG` at `open()` so the panel lights.
- Live key-image push on assign (~1s, no manual flush): encode → `BAT` header → 1024-byte
  chunks → `ULEND` commit.
- Repaint all keys from the saved profile bindings on profile load.
- `VER` firmware-version probe at open, cached and surfaced.
- `akp05e` descriptor honesty fix: `hasClock=true` → `false`.

**Out of scope for Phase 14** (later phases): input routing → ActionEngine (Phase 15);
brightness slider / clear-all UI + binding persistence + pages (Phase 16); the plugin SDK
(Phases 17-22); auxiliary surfaces — encoder overlays / main strip / touch strip (Phase 23,
hardware-gated); family coverage (Phase 24); hardware verification (Phase 25).
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Architecture

- The control service is an **app-layer** component (`src/app/src/`), not core — it owns Qt
  threading/timers and the held-open device handle. COD-031 boundary preserved (no
  `nlohmann::json` in `ajazz_core` or installed headers).
- **Single held-open HID handle** for the active device across the session (v1.1 ARCH-03
  `weak_ptr` flyweight invariant preserved) — NOT open/close per push.
- The service is the **single device paint path** that Phases 15 (input), 16 (controls/
  persistence), and 19 (plugin bridge) all reuse. Design it as the reuse surface now.
- Drive image encode through the existing **`image_pipeline.{hpp,cpp}`** (ARCH-04) — do not
  add a second encode path.

### Wire format (RE is source of truth — do NOT alter)

- The `BAT` header is **capture-verified** (JPEG size BE16@10-11, key index 1-based@12;
  real capture `43 52 54 00 00 42 41 54 00 00 08 7C 0D`). Phase 14 is **app-side wiring,
  not protocol change**.
- Chunked uploads use **1024-byte** packets; last-chunk Transfer-Done flag per the protocol.
- Emit a **`ULEND`** commit after each image burst (DOCK-02 — fixes the vendor "device
  freezes after rapid setKeyImage" defect; vendor §10 P0). Builders already exist in
  `akp05_protocol.hpp`.
- Probe firmware with **`VER`** (`CRT…VER`) at open; cache + surface the response
  (DOCK-01 — vendor returns "unknown" today).

### Honesty

- `akp05e` advertises `hasClock=false` (DEVICES-11) — the Stream Dock family has **no
  firmware RTC** per ARCH-05; corrects `register.cpp` and closes Phase-10 UAT #6. (Contrast
  `ak980pro`, which keeps `hasClock=true` per ARCH-05.1 — do not touch that row.)

### Device facts (verified)

- AKP05E = `0300:3004`, fw `V3.AKP05E.01.007`, routed to `makeAkp05`; **10 LCD keys (2×5),
  4 endless pressable rotary encoders, touch strip**. `register.cpp` descriptor already wires
  `encoderCount=4` + `hasTouchStrip=true`.

### Claude's Discretion (planner/executor decides; surface trade-offs)

- Exact class name/shape of the control service (e.g. `StreamDockControlService`) and whether
  it wraps or composes the existing `lighting_service` / `device_model` / `profile_controller`.
- Write-queue threading model (dedicated write thread vs Qt timer-driven drain) — honor the
  device ACK/flush semantics from `akp05_init_sequence.md`; coalesce so a burst of key
  assignments does not stall the UI thread.
- How "active device" selection is represented in the app (reuse existing `device_model` /
  `application` composition if present).
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### RE / protocol (source of truth)

- `docs/protocols/streamdeck/akp05.md` — AKP05E hardware + "Features that must work" matrix.
- `docs/protocols/streamdeck/akp05_vendor.md` — full opcode table incl. `VER`, `ULEND`, `LIG`, `BAT`, `CLE`; ImageStruct write-queue + ACK-timeout model.
- `docs/protocols/streamdeck/akp05_init_sequence.md` — per-device open sequence (`VER` probe at open; no clear-at-open; write/read thread model).

### Device backend (reuse, do not alter wire format)

- `src/devices/streamdeck/src/akp05.cpp` + `akp05_protocol.hpp` — `BAT`/`LIG`/`CLE`/`ULEND`/`VER` builders, 1024 PacketSize, input parser, EncoderCount=4.
- `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` — ARCH-04 Qt6 RGBA→JPEG encode.
- `src/devices/streamdeck/src/register.cpp` — AKP05E descriptor (line ~294-307; `hasClock` at ~305 → fix to false).
- `src/core/include/ajazz/core/capabilities.hpp` — `IDisplayCapable` (setKeyImage/setKeyColor/clearKey/setBrightness/flush) + `DisplayInfo`.

### App-layer reuse candidates (researcher to confirm exact roles)

- `src/app/src/application.hpp` — composition root.
- `src/app/src/device_model.hpp` — app-side device representation / active-device state.
- `src/app/src/lighting_service.{hpp,cpp}` — existing brightness/RGB service (may host or compose with the control service).
- `src/app/src/profile_controller.{hpp,cpp}` — profile load/save (repaint-on-load source).

### Decisions / ADRs

- `.planning/phases/09-research-captures-hygiene/ARCH-04.md` (image pipeline location), `ARCH-05.md` (no Stream Dock RTC → `hasClock=false`).
- v1.1 ARCH-03 (device-registry `weak_ptr` flyweight; single held-open handle invariant).
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification spans `MockTransport` byte-level wire assertions (no hardware needed for the
  bulk of Phase 14): assert the `LIG` brightness-ON at open, the `BAT`→1024-chunk→`ULEND`
  burst sequence, the `VER` probe frame, and `hasClock=false` on the `akp05e` descriptor.
- A real-hardware power-cycle smoke (assigned image survives, no freeze across a burst) is the
  promotion witness but is **hardware-gated** — bench-run if the AKP05E is connected, else
  defer the live witness to Phase 25 (keep the MockTransport tests as the gating proof here).
- ASCII-only test names (Win32 CMD codepage). `ctest --preset linux-release`.
  </specifics>

<deferred>
## Deferred Ideas

- Input routing, brightness slider/clear-all UI, binding persistence, pages → Phases 15-16.
- Plugin SDK (server completion, manifest/spawn, bridge, Property Inspector, store) → Phases 17-22.
- Auxiliary surfaces (encoder overlays / main strip / touch strip / DRA) → Phase 23 (HW-gated).
- Boot logo (`LOG`), `M_V`, `GIFVER` → backlog (vendor §10 P3).
  </deferred>

______________________________________________________________________

*Phase: 14-stream-dock-control-service*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; no separate discuss-phase round needed — design context captured during replan research)*
