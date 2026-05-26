---
phase: 23-auxiliary-display-surfaces
plan: 01
subsystem: app-layer device control (aux surfaces)
tags: [stream-dock, display, aux-surface, tdd, dynamic-cast, qt-timer, provisional, DISPLAY-10]

# Dependency graph
requires:
  - phase: 14-stream-dock-control-service/14-02
    provides: StreamDockControlService held-handle + coalesced QTimer drain seam (REUSED verbatim)
  - phase: earlier phases
    provides: Akp05Device backend (IDisplayCapable/IEncoderCapable/ITouchStripDisplayCapable wire methods), MockTransport DI seam
provides:
  - StreamDockControlService.assignMainImage: MAI whole-strip assign through held handle (DISPLAY-10)
  - StreamDockControlService.assignEncoderImage: ENC per-encoder assign with 0-based index (DISPLAY-10 ENC)
  - StreamDockControlService.assignTouchStripZone: DRA rect-addressable zone assign (DISPLAY-10 DRA)
  - PROVISIONAL §5 framing: both ENC and DRA paths wired, neither deleted, geometry flagged for Phase 25
affects:
  - Phase 25 (VERIFY-05): live hardware reconciliation of ENC-vs-DRA and DRA rect geometry

# Tech tracking
tech-stack:
  added: []
  patterns:
    - PendingKey{SurfaceTag, index} discriminant: extends the Phase-14 pending-write map to carry aux surfaces alongside key images in the SAME coalesced drain
    - SurfaceTag enum (Key=0, Main=1, Encoder=2, TouchZone=3): compile-time surface identification with no runtime branching cost
    - switch-on-SurfaceTag in drainPendingWrites: single drain loop handles all 4 surface types with capability-pointer null-checks resolved once per cycle

key-files:
  modified:
    - src/app/src/stream_dock_control_service.hpp
    - src/app/src/stream_dock_control_service.cpp
    - tests/unit/test_stream_dock_control_service.cpp

key-decisions:
  - 'Pending-write map extension: PendingKey{SurfaceTag, index} struct key replaces bare uint8_t so all 4 surfaces share the SAME std::map and SAME QTimer drain (no second drain added, DOCK-02 preserved)'
  - 'drainPendingWrites single-pass: capability pointers resolved once (disp/enc/strip) then switch dispatches per entry; nullptr guards per capability surface (Pitfall 1 / T-23-04)'
  - 'Range passthrough: encoder index and zone id passed THROUGH to backend unchanged; backend guards refuse >=4 (Pitfall 3 / T-23-02 mitigated)'
  - 'PROVISIONAL §5 comment: both ENC and DRA paths wired and NEITHER deleted; encoder-zone->DRA rect geometry (x=zone*200, y=0, w=200, h=100) is a Ghidra-derived hypothesis; Phase 25 VERIFY-05 is live reconciliation'

requirements-completed: [DISPLAY-10]

# Metrics
duration: ~50min
completed: 2026-05-26T21:53:00Z
---

# Phase 23 Plan 01: Aux-Surface Assign Methods for StreamDockControlService Summary

**MAI/ENC/DRA app-layer wiring through the Phase-14 held handle + coalesced drain, with PendingKey surface-tag discriminant and PROVISIONAL §5 framing for Phase-25 live reconciliation.**

## Performance

- **Duration:** ~50 min
- **Started:** 2026-05-26T21:03:00Z (approx)
- **Completed:** 2026-05-26T21:53:00Z
- **Tasks:** 2/2 completed (Task 1 gate auto-approved per orchestrator pre-verification)
- **Files modified:** 3

## Accomplishments

- Task 1 STOP gate confirmed: both `stream_dock_control_service.{hpp,cpp}` present; `14-02-SUMMARY.md` present; `ctest -R StreamDockControlService` green (18/18); Phase-14 seam (held `m_activeDevice`, `m_drainTimer`, `m_pendingWrites`) recorded for reuse.
- Closed DISPLAY-10 wiring gap: `grep setMainImage|setEncoderImage|setTouchStripImage src/app/` now returns 9 call/doc sites (was 0 hits before this plan).
- Built `assignMainImage`, `assignEncoderImage`, `assignTouchStripZone` as siblings of `assignKeyImage` — all three route through `m_activeDevice` (no re-open, Pitfall 2) via dynamic_cast with null-check within 3 lines (Pitfall 1 / T-23-04).
- Extended `m_pendingWrites` from `std::map<uint8_t, QImage>` to `std::map<PendingKey, QImage>` with `PendingKey{SurfaceTag, index}` — the SAME single-shot `m_drainTimer` coalesces all 4 surface types (DOCK-02 preserved, no second timer).
- `drainPendingWrites` updated: resolves `IDisplayCapable*`, `IEncoderCapable*`, `ITouchStripDisplayCapable*` once per cycle, then `switch(tag)` dispatches; encoder index and zone id pass THROUGH to backend range-checks (T-23-02).
- PROVISIONAL §5 block comment above the encoder/zone routing: states both ENC and DRA paths are wired and neither deleted; documents the `(x=zone*200, y=0, rectWidth=200, rectHeight=100)` geometry as a Ghidra-derived hypothesis; routes reconciliation to Phase 25 (VERIFY-05).
- Wire layer (akp05.cpp / akp05_protocol.hpp) not in git diff — LOCKED as required.
- 637/637 ctest green (+5 new DISPLAY-10 tests).

## Task Commits

1. **Task 1: STOP gate confirmed** - no commit (read-only gate)
1. **Task 2 RED: failing wire tests** - `d32c901` (test)
1. **Task 2 GREEN: implementation** - `27c04db` (feat)

## Files Created/Modified

- `src/app/src/stream_dock_control_service.hpp` — Added `assignMainImage`, `assignEncoderImage`, `assignTouchStripZone` public declarations; added `SurfaceTag` enum + `PendingKey` struct; updated `m_pendingWrites` type; added PROVISIONAL §5 doc block
- `src/app/src/stream_dock_control_service.cpp` — Added three method implementations with dynamic_cast null-check pattern; updated `assignKeyImage` to use `PendingKey`; rewrote `drainPendingWrites` to dispatch on `SurfaceTag`; added PROVISIONAL §5 block comment
- `tests/unit/test_stream_dock_control_service.cpp` — Added 5 new `[stream-dock-control][DISPLAY-10]` test cases: MAI header assertion, ENC index byte assertion, DRA zone x=zone\*200 assertion, range-refusal for encoder>=4, range-refusal for zone>=4

## Key Design Decisions

### Pending-Write Map: PendingKey Discriminant (Open Question resolved)

Chose to extend the existing `std::map<uint8_t, QImage>` to `std::map<PendingKey, QImage>` with `PendingKey{SurfaceTag tag; uint8_t index}`. This reuses the SAME map and the SAME `m_drainTimer` without adding a parallel path. Alternative (separate pending maps per surface) was rejected: it would require separate timer arms and a more complex drain that violates the DOCK-02 single-drain invariant. The `PendingKey::operator<` sorts by `(tag, index)` giving deterministic drain order.

### drainPendingWrites Capability Resolution

All three capability pointers (`IDisplayCapable*`, `IEncoderCapable*`, `ITouchStripDisplayCapable*`) are resolved once at the top of the drain cycle, then each map entry dispatches by tag. This avoids repeated dynamic_cast in the hot path and keeps the null-check exactly once per capability per drain cycle (Pitfall 1 compliant).

### Range Passthrough (T-23-02)

Encoder index and zone id are stored in `PendingKey.index` and passed THROUGH to the backend's `setEncoderImage` / `setTouchStripImage` unchanged. The backend range-checks (`< EncoderCount=4` and `< zoneCount=4`) are the single enforcement point. The service does NOT pre-validate, which means an out-of-range assign queues an entry that the backend refuses at drain time — producing zero wire writes, as the range-refusal tests assert.

### PROVISIONAL §5 Framing

Both the ENC per-encoder path (`setEncoderImage` -> `CmdEncImage`) and the DRA touch-strip-zone path (`setTouchStripImage` -> `CmdSecondaryScreen`) are wired and reachable. The `grep` in acceptance criteria confirms both call sites exist in the service `.cpp`. The PROVISIONAL comment documents the Ghidra-derived geometry `(x=zone*200, y=0, w=200, h=100)` and routes the live reconciliation to Phase 25 (VERIFY-05) — "Do NOT assert the geometry as confirmed."

## Deviations from Plan

### Auto-fixed Issues

None. Implementation matched the plan exactly. drainPendingWrites restructuring was planned (the plan explicitly said "extend the pending representation to carry a small surface-tag discriminant rather than adding a parallel timer").

## Follow-up Items (Not Phase 23-01 Scope)

| Item                                                           | Phase      | Reason deferred                                                      |
| -------------------------------------------------------------- | ---------- | -------------------------------------------------------------------- |
| Live hardware confirmation (ENC vs DRA, DRA rect geometry)     | 25         | VERIFY-05: physical AKP05E + uaccess ACL required                    |
| Editor UX for aux-surface assignment (controls in Phase-16 UI) | 23-02 / 16 | Phase 23-01 wires the service; UI assignment is Phase 16/23-02 scope |
| Family coverage (AKP03/153/815 aux surfaces)                   | 24         | Out of scope for AKP05 Phase 23                                      |
| Boot logo (LOG) / M_V secondary-screen logo                    | backlog    | vendor §10 P3, not captured                                          |

## Threat Mitigations Applied

| Threat                                                 | Mitigation                                                                                                                      |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------- |
| T-23-01: oversized aux image desyncs firmware          | Backend caps JPEG at 65535; service coalesces through Phase-14 QTimer drain (DOCK-02); no app-side encode (ARCH-04)             |
| T-23-02: out-of-range encoder/zone pokes wrong surface | Index/zone passed THROUGH to backend range-check; range-refusal tests assert zero writes for index/zone=4                       |
| T-23-03: device yank mid aux-burst                     | setTouchStripImage returns false (not throw); drainPendingWrites catches std::exception and releases held handle                |
| T-23-04: dynamic_cast nullptr deref on non-aux device  | Null-check within 3 lines of every dynamic_cast (3 sites in assign methods, 3 sites in drainPendingWrites per-cycle resolution) |
| T-23-05: untrusted image source                        | Qt safe image decoders only (Phase 23 accepted posture; deep validation Phase 16/19)                                            |

## Known Stubs

None. All three methods are fully wired to the backend. The PROVISIONAL geometry `(x=zone*200, y=0, w=200, h=100)` is a code contract (not a UI stub) — the test pins what OUR code emits, and Phase 25 confirms whether the device accepts it.

## Threat Flags

None. No new network endpoints, auth paths, file access patterns, or schema changes introduced. All new surface is app-layer routing through the existing HID handle.

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/stream_dock_control_service.hpp` - FOUND (assignMainImage, assignEncoderImage, assignTouchStripZone declared)
- `src/app/src/stream_dock_control_service.cpp` - FOUND (setMainImage/setEncoderImage/setTouchStripImage call sites confirmed)
- `tests/unit/test_stream_dock_control_service.cpp` - FOUND (5 new DISPLAY-10 tests)

Commits confirmed:

- `d32c901` (Task 2 RED) - FOUND
- `27c04db` (Task 2 GREEN) - FOUND

ctest suite: 637/637 green.
Wire layer: akp05.cpp + akp05_protocol.hpp NOT in git diff (LOCKED confirmed).
COD-031: 0 nlohmann occurrences in core/include/ (boundary intact).
PROVISIONAL §5 comment: present in stream_dock_control_service.cpp (grep confirmed).
ENC path reachable: `grep setEncoderImage src/app/src/stream_dock_control_service.cpp` returns call site.
DRA path reachable: `grep setTouchStripImage src/app/src/stream_dock_control_service.cpp` returns call site.
