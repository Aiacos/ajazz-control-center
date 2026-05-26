---
phase: 23-auxiliary-display-surfaces
plan: 02
subsystem: app-layer device control (encoder profile-repaint)
tags: [stream-dock, display, aux-surface, tdd, profile-repaint, provisional, DISPLAY-10]

# Dependency graph
requires:
  - phase: 23-auxiliary-display-surfaces/23-01
    provides: assignTouchStripZone / assignEncoderImage / assignMainImage on StreamDockControlService + PendingKey discriminant drain (reused verbatim)
  - phase: 14-stream-dock-control-service/14-02
    provides: repaintPage / repaintFromProfile profile-accessor seam + QTimer drain (reused verbatim)
provides:
  - StreamDockControlService.repaintEncodersFromProfile: iterates Profile::encoders -> renders each bound encoder overlay -> assignTouchStripZone (DRA, x=encoderIndex*200, PROVISIONAL)
  - Application profileChanged connection: encoder overlays now repaint on profile load alongside keys (same existing connection, no second monitor)
  - PROVISIONAL ENC fallback: assignEncoderImage stays reachable (commented sibling); Phase 25 VERIFY-05 decides which path hardware honors
affects:
  - Phase 25 (VERIFY-05): live hardware reconciliation of ENC-vs-DRA and DRA rect geometry

# Tech tracking
tech-stack:
  added: []
  patterns:
    - Profile encoder-index mapping: Profile::encoders is 0-based (std::uint16_t); NO +1 offset needed (unlike Profile::keys which map 0-based to 1-based device keys). Index passed THROUGH to backend range-check (<4).
    - PROVISIONAL zone fill size: solid-fill background uses 200x100 px (the DRA zone dimensions per PROVISIONAL §5 geometry), so the fill maps cleanly to the zone rect the backend uploads.
    - Additional QObject::connect on profileChanged: the SAME profileChanged signal now drives both repaintFromProfile and repaintEncodersFromProfile -- two connections to the same signal is idiomatic Qt and reuses the Phase-14 drain without any second timer or accessor.

key-files:
  modified:
    - src/app/src/stream_dock_control_service.hpp
    - src/app/src/stream_dock_control_service.cpp
    - src/app/src/application.cpp
    - tests/unit/test_stream_dock_control_service.cpp

key-decisions:
  - 'Profile encoder-index base (confirmed): Profile::encoders stores 0-based std::uint16_t keys (same as Profile::keys). UNLIKE keys (which add +1 for the 1-based device index), encoder indices are passed THROUGH to assignTouchStripZone unchanged -- the backend zone count check uses 0..3 directly.'
  - 'ENC fallback representation: commented call path in repaintEncodersFromProfile body (assignEncoderImage line commented out with PROVISIONAL note); assignEncoderImage method is NOT deleted; both grep targets satisfied.'
  - 'Application wiring: second QObject::connect on the existing ProfileController::profileChanged signal (no slot wrapper); keys and encoder overlays repaint from the same signal emission, coalescing naturally through the shared QTimer drain.'
  - 'Solid-fill dimensions: 200x100 px used for background-fill encoder overlays (matches the DRA zone dimensions from PROVISIONAL §5 geometry); imagePath overlays use native loaded size (backend resizes).'

requirements-completed: [DISPLAY-10]

# Metrics
duration: ~25min
completed: 2026-05-26T22:04:00Z
---

# Phase 23 Plan 02: Encoder Profile-Repaint for StreamDockControlService Summary

**repaintEncodersFromProfile added to StreamDockControlService (iterates Profile::encoders -> DRA zone path via assignTouchStripZone) and wired into Application's existing profileChanged connection; 3 new MockTransport tests pass; ENC fallback kept; wire layer untouched.**

## Performance

- **Duration:** ~25 min
- **Started:** 2026-05-26T21:55:00Z (approx)
- **Completed:** 2026-05-26T22:04:00Z
- **Tasks:** 2/2 completed
- **Files modified:** 4

## Accomplishments

- Implemented `repaintEncodersFromProfile()` on `StreamDockControlService`, mirroring `repaintPage` for encoders: iterates `Profile::encoders` (0-based `std::uint16_t` keys -> `EncoderBinding`), renders each bound entry's `KeyState` via the SAME imagePath/background render logic as key repaint, and routes each overlay through `assignTouchStripZone(encoderIndex, img)` (DRA default, x=encoderIndex*200, PROVISIONAL §5).
- Confirmed profile encoder-index base: 0-based, no +1 offset (unlike `Profile::keys`). Index passed THROUGH to backend range-check.
- ENC fallback (`assignEncoderImage`) kept reachable as a commented call path inside `repaintEncodersFromProfile`, with PROVISIONAL §5 note routing reconciliation to Phase 25 (VERIFY-05). `assignEncoderImage` is NOT deleted.
- Wired `repaintEncodersFromProfile` into `Application` via a second `QObject::connect` on the existing `ProfileController::profileChanged` signal -- no second monitor, accessor, or QTimer (RESEARCH A5 compliant).
- 3 new MockTransport tests (RED `3adcfc4` -> GREEN `3b69dbc`):
  - Single bound encoder emits DRA zone burst with correct location and x bytes.
  - Two bound encoders each emit correct per-encoder DRA burst with per-encoder x.
  - Unbound encoder (neither imagePath nor background) produces zero writes.
- Full suite: 640/640 green (+3 new tests, was 637).
- Wire layer (akp05.cpp / akp05_protocol.hpp) NOT in git diff -- LOCKED confirmed.
- COD-031: 0 `#include nlohmann` in `src/core/include/` (boundary intact).

## Task Commits

1. **Task 1 RED: failing repaintEncodersFromProfile tests** - `3adcfc4` (test)
2. **Task 1 GREEN: repaintEncodersFromProfile implementation** - `3b69dbc` (feat)
3. **Task 2: wire into Application profileChanged** - `8c4bf01` (feat)

## Files Created/Modified

- `src/app/src/stream_dock_control_service.hpp` -- Added `repaintEncodersFromProfile()` public declaration with full Doxygen doc block (encoder-index base, DRA default, ENC fallback, PROVISIONAL §5 note)
- `src/app/src/stream_dock_control_service.cpp` -- Added `repaintEncodersFromProfile()` implementation: same profileAccessor seam as `repaintPage`; iterates `prof.encoders`; renders imagePath or background fill (200x100) per the PROVISIONAL §5 zone geometry; calls `assignTouchStripZone`; skips unbound entries; uint16_t overflow guard (WR-03 mirror); PROVISIONAL §5 ENC commented sibling
- `src/app/src/application.cpp` -- Added second `QObject::connect(m_profileController.get(), &ProfileController::profileChanged, m_streamDockControl.get(), &StreamDockControlService::repaintEncodersFromProfile)` immediately after the existing key-repaint connection
- `tests/unit/test_stream_dock_control_service.cpp` -- Added 3 new `[stream-dock-control][DISPLAY-10][repaint-encoders]` TEST_CASEs (ASCII-only titles); MockTransport assertions for DRA bytes[5..7]=D,R,A; byte[12]=encoderIndex; BE16 x at bytes[17..18]=encoderIndex*200; ULEND tail

## Key Design Decisions

### Profile Encoder-Index Mapping (Pitfall 3 / confirmed)

`Profile::encoders` keys are 0-based `std::uint16_t`. The AKP05E backend's touch-strip zone check is 0-based (zones 0..3). Therefore `repaintEncodersFromProfile` passes the encoder index THROUGH to `assignTouchStripZone` with NO +1 offset -- unlike `repaintPage` which adds +1 to convert 0-based profile keys to 1-based device key indices. This asymmetry is documented in the implementation comment.

### ENC Fallback Representation (PROVISIONAL §5)

The ENC fallback is kept as a commented-out call inside `repaintEncodersFromProfile`:
```cpp
// assignEncoderImage(static_cast<std::uint8_t>(encoderIndex), img)
```
with a comment block stating the ENC path is not called here (DRA is the vendor-preferred path) but is NOT deleted. `assignEncoderImage` continues to exist as a public method. Phase 25 (VERIFY-05) will determine which path the firmware honors and update the RE doc + code. LOCKED: hardware wins.

### Application Wiring: Second Connect on Existing Signal

Rather than adding a wrapper slot that calls both repaint methods, or merging the two calls into `repaintFromProfile`, the plan chose a second `QObject::connect` on the same `ProfileController::profileChanged` signal. Qt fires all connected slots for a single emission, so both key and encoder repaints execute atomically on each profile load, both routing through the same coalesced drain. This keeps `repaintFromProfile` and `repaintEncodersFromProfile` independently callable (e.g., from tests or future QML bindings) while also connecting them to the same lifecycle event. No second monitor, no second accessor, no second timer added (RESEARCH A5 compliant).

### Solid-Fill Dimensions for Background Encoder Overlays

For encoder entries with `background` (no `imagePath`), the solid fill is created at 200x100 px. This matches the PROVISIONAL §5 DRA zone dimensions (`rectWidth=200, rectHeight=100`). The backend resizes the image internally; using the zone dimensions for the fill ensures the color maps cleanly to the target zone rect without a scale distortion. For `imagePath` overlays the loaded size is used (the backend resizes).

## Deviations from Plan

### Auto-fixed Issues

None. Plan executed exactly as written.

## Follow-up Items (Not Phase 23-02 Scope)

| Item | Phase | Reason deferred |
|------|-------|-----------------|
| Live hardware confirmation (ENC vs DRA, DRA rect geometry) | 25 | VERIFY-05: physical AKP05E + uaccess ACL required |
| Editor UX for aux-surface assignment (per-encoder overlay assignment in Phase-16 UI) | 16/future | Phase 23 wires the service; editor UI is out of scope |
| Family coverage (AKP03/153/815 encoder surfaces) | 24 | Out of scope for AKP05E Phase 23 |
| Phase 16 posture for untrusted imagePath (T-23b-02 accepted) | 16/19 | Phase 23 uses Qt safe decoders; deep validation is Phase 16/19 |

## Threat Mitigations Applied

| Threat | Mitigation |
|--------|------------|
| T-23b-01: burst of encoder repaints | Routes through Phase-14 coalesced drain via assignTouchStripZone; last-write-wins per (SurfaceTag::TouchZone, index) in m_pendingWrites |
| T-23b-02: untrusted imagePath in profile | Reuses SAME Qt safe-decoder path as repaintPage (accepted for Phase 23; deep validation Phase 16/19) |
| T-23b-03: out-of-range encoder index | Index passed THROUGH to backend range-check (<4); unbound skip covers the no-imagePath/no-background case |
| T-23b-04: device yank mid encoder-repaint | drainPendingWrites already catches std::exception and releases handle (Phase 23-01 mitigated); repaintEncodersFromProfile only queues -- the catch is in the drain |

## Known Stubs

None. `repaintEncodersFromProfile` is fully wired to the DRA zone path via `assignTouchStripZone`. The PROVISIONAL §5 geometry `(x=encoderIndex*200, y=0, w=200, h=100)` is a code contract pinned by tests -- Phase 25 confirms whether the device accepts it.

## Threat Flags

None. No new network endpoints, auth paths, file access patterns, or schema changes introduced. All new surface is app-layer routing through the existing HID handle.

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/stream_dock_control_service.hpp` - FOUND (repaintEncodersFromProfile declared)
- `src/app/src/stream_dock_control_service.cpp` - FOUND (repaintEncodersFromProfile implemented; assignEncoderImage ENC reachable)
- `src/app/src/application.cpp` - FOUND (repaintEncodersFromProfile connected to profileChanged)
- `tests/unit/test_stream_dock_control_service.cpp` - FOUND (3 new repaint-encoders tests)

Commits confirmed:

- `3adcfc4` (Task 1 RED) - FOUND
- `3b69dbc` (Task 1 GREEN) - FOUND
- `8c4bf01` (Task 2) - FOUND

ctest suite: 640/640 green.
Wire layer: akp05.cpp + akp05_protocol.hpp NOT in git diff (LOCKED confirmed).
COD-031: 0 `#include nlohmann` in core/include/ (boundary intact).
PROVISIONAL §5 ENC fallback: assignEncoderImage reachable (line 273, commented sibling in repaintEncodersFromProfile).
repaintEncodersFromProfile in application.cpp: confirmed at line 449, driven from existing profileChanged signal.
