---
phase: 24-family-coverage-akp03-153-815
plan: 01
subsystem: streamdeck device backends
tags: [stream-dock, akp03, akp153, akp815, di-seam, mock-transport, tdd-enabler]

# Dependency graph
requires:
  - phase: 14-stream-dock-control-service/14-02
    provides: StreamDockControlService public surface + keyIndex base mapping
  - phase: 15-stream-dock-input-routing/15-01
    provides: StreamDockInputService ProfileAccessor/ActionEngine seam

provides:
  - makeAkp03WithTransport: test-injection DI overload for AKP03 backend (CAPTURE-04)
  - makeAkp153WithTransport: test-injection DI overload for AKP153 backend (CAPTURE-04)
  - makeAkp815WithTransport: test-injection DI overload for AKP815 backend (CAPTURE-04)

affects:
  - Phase 24-02: family MockTransport byte test (test_stream_dock_family.cpp) uses these
    three overloads to drive each backend hardware-free

# Tech tracking
tech-stack:
  added: []
  patterns:
    - DI factory overload pattern (CAPTURE-04): make*WithTransport exposes anonymous-namespace
      COD-026 DI ctor across TU boundary; mirrors makeAkp05WithTransport at akp05.cpp:928

key-files:
  created: []
  modified:
    - src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp
    - src/devices/streamdeck/src/akp03.cpp
    - src/devices/streamdeck/src/akp153.cpp
    - src/devices/streamdeck/src/akp815.cpp

key-decisions:
  - 'Prerequisites confirmed: 14-02-SUMMARY.md and 15-01-SUMMARY.md both present before any
    code change (STOP-if-absent gate passed)'
  - 'All three backends already had the COD-026 DI ctor (DeviceDescriptor, DeviceId, TransportPtr);
    no new ctor needed - the make*WithTransport overloads are thin wrappers that call the
    existing DI ctor, mirroring makeAkp05WithTransport exactly'
  - 'Wire layer untouched: no change to *_protocol.hpp, PacketSize, opcodes, ImageTransform,
    init sequences, or any builder (RE source-of-truth; CLAUDE.md hard rule)'
  - 'COD-031 boundary intact: no nlohmann/json added anywhere'

requirements-completed: [DEVICES-10]

# Metrics
duration: ~4 min
completed: 2026-05-24
---

# Phase 24 Plan 01: AKP03/153/815 DI Overloads Summary

**Three make\*WithTransport factory overloads added to AKP03/153/815 backends, mirroring the makeAkp05WithTransport CAPTURE-04 pattern — the prerequisite test-injection seam for Plan 24-02's family MockTransport byte tests (DEVICES-10).**

## Performance

- **Duration:** ~4 min
- **Started:** 2026-05-24T19:21:59Z
- **Completed:** 2026-05-24T19:25:59Z
- **Tasks:** 2/2 completed
- **Files modified:** 4

## Accomplishments

### Task 1: STOP-if-SUMMARY-absent prerequisite gate

Both prerequisite SUMMARY files confirmed present before any code change:

- `.planning/phases/14-stream-dock-control-service/14-02-SUMMARY.md` — FOUND
- `.planning/phases/15-stream-dock-input-routing/15-01-SUMMARY.md` — FOUND

**Extracted for Plan 24-02 (Phase 15 / 14 public surfaces):**

**StreamDockControlService (14-02):**

- Public API: `setActiveDevice(codename)`, `assignKeyImage(keyIndex, path)`, `repaintFromProfile()`, `firmwareVersionFor()`
- Key-index mapping: Profile::keys are 0-based uint16_t; backends are 1-based; service adds +1 in `repaintFromProfile()`
- ProfileAccessor seam: `std::function<core::Profile const&()>` lambda injected by Application
- DeviceLookup seam: `std::function<shared_ptr<IDevice>(QString)>` lambda (enumerate registry by codename)
- `kDefaultBrightnessPercent = 80`

**StreamDockInputService (15-01):**

- Public API: `setActiveDevice(device)`, `zoneForX(x)` static, `pageNavRequested(int)` signal, `encoderReleaseSynthesised(uint16_t)` signal
- ActionEngine seam: `std::unique_ptr<core::ActionEngine>` injected by caller; service owns lifetime
- ProfileAccessor seam: same `std::function<core::Profile const&()>` lambda pattern
- Poll cadence: 8 ms Qt::PreciseTimer
- Rotation coalescer: 16 ms single-shot per encoder axis; accumulated magnitude in m_encAccum
- Encoder press -> synthetic release (no wire release frame needed per akp05.md:76)

### Task 2: makeAkp03/153/815WithTransport DI overloads

All three family backends already had the COD-026 DI ctor `(DeviceDescriptor, DeviceId, TransportPtr)` in their anonymous namespace. The new overloads are thin wrappers:

| Backend      | DI ctor (existing)                                   | New overload                   |
| ------------ | ---------------------------------------------------- | ------------------------------ |
| Akp03Device  | `Akp03Device(descriptor, id, transport)` (line 314)  | `makeAkp03WithTransport(...)`  |
| Akp153Device | `Akp153Device(descriptor, id, transport)` (line 242) | `makeAkp153WithTransport(...)` |
| Akp815Device | `Akp815Device(descriptor, id, transport)` (line 86)  | `makeAkp815WithTransport(...)` |

**Per-family construction notes (for 24-02):**

- **AKP03:** 6 LCD keys (2x3) + 3 non-LCD buttons + 3 encoders; 60x60 JPEG, Rot0, PacketSize=512; implements IDisplayCapable + IEncoderCapable + IClockCapable
- **AKP153:** 15 keys (3x5); 85x85 JPEG, Rot90+mirror, PacketSize=512; implements IDisplayCapable + IFirmwareCapable + IClockCapable; `makeAkp153` creates Akp153Device
- **AKP815:** 15 keys (5x3); 100x100 JPEG, Rot180, PacketSize=512; reuses AKP153 framing but different image format; implements IDisplayCapable + IFirmwareCapable + IClockCapable; `makeAkp815` creates Akp815Device (NOT reusing Akp153Device — it has its own Akp815Device class with its own Rot180 transform)

## Task Commits

1. **Task 1: STOP-if-SUMMARY-absent prerequisite gate** - no commit (read-only gate, both present)
1. **Task 2: Add makeAkp03/153/815WithTransport DI overloads** - `0aa6353` (feat)

## Files Created/Modified

- `src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp` - Added three new declarations (makeAkp03WithTransport, makeAkp153WithTransport, makeAkp815WithTransport), each placed adjacent to its family's existing make\* declaration with doxygen matching makeAkp05WithTransport style
- `src/devices/streamdeck/src/akp03.cpp` - Added makeAkp03WithTransport definition after makeAkp03; calls Akp03Device(d, id, transport) DI ctor
- `src/devices/streamdeck/src/akp153.cpp` - Added makeAkp153WithTransport definition after makeAkp153; calls Akp153Device(d, id, transport) DI ctor
- `src/devices/streamdeck/src/akp815.cpp` - Added makeAkp815WithTransport definition after makeAkp815; calls Akp815Device(d, id, transport) DI ctor

## RE Docs Cross-Checked

Before any code change, the following RE docs were read to confirm no wire-format change was needed or introduced:

| Doc                                   | Key facts confirmed                                                  |
| ------------------------------------- | -------------------------------------------------------------------- |
| `docs/protocols/streamdeck/akp03.md`  | 6 LCD keys + 3 encoders + 3 buttons; 60x60 JPEG Rot0; PacketSize 512 |
| `docs/protocols/streamdeck/akp153.md` | 15 keys 3x5; 85x85 JPEG; Rot90+mirror; PacketSize 512                |
| `docs/protocols/streamdeck/akp815.md` | 15 keys 5x3; 100x100 JPEG Rot180; PacketSize 512                     |

No provisional/unconfirmed values touched. All three backends' protocol headers (`akp03_protocol.hpp`, `akp153_protocol.hpp`, `akp815_protocol.hpp`) are unchanged (git diff --stat confirms zero protocol header changes).

## Deviations from Plan

None - plan executed exactly as written. All three backends already had the COD-026 DI ctor; no new ctor overload was needed in any backend (the plan correctly anticipated this: "If a family's production factory does not yet route a TransportPtr into its ctor, add the minimal DI ctor overload" — condition was false for all three).

## Verification Results

- `test -f 14-02-SUMMARY.md && test -f 15-01-SUMMARY.md` → `PREREQS_PRESENT`
- `cmake --build --preset linux-release` → clean (no errors, no new warnings)
- `grep makeAkp03WithTransport src/devices/streamdeck/src/akp03.cpp` → found
- `grep makeAkp153WithTransport src/devices/streamdeck/src/akp153.cpp` → found
- `grep makeAkp815WithTransport src/devices/streamdeck/src/akp815.cpp` → found
- `git diff --stat HEAD~1 HEAD -- *_protocol.hpp` → no changes
- `ctest --preset linux-release` → 617/617 green (no regression)

## Threat Mitigations Applied

| Threat  | Mitigation                                                                                      |
| ------- | ----------------------------------------------------------------------------------------------- |
| T-24-01 | Overloads are construction-only; confirmed by zero changes to \*\_protocol.hpp and builders     |
| T-24-02 | Per-family keyIndex bounds (AKP03 1..9, AKP153/815 1..15) remain in existing backends unchanged |

## Self-Check: PASSED

Files confirmed present:

- `src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp` (makeAkp03/153/815WithTransport declarations) - FOUND
- `src/devices/streamdeck/src/akp03.cpp` (makeAkp03WithTransport definition) - FOUND
- `src/devices/streamdeck/src/akp153.cpp` (makeAkp153WithTransport definition) - FOUND
- `src/devices/streamdeck/src/akp815.cpp` (makeAkp815WithTransport definition) - FOUND

Commits confirmed:

- `0aa6353` (Task 2) - FOUND

ctest suite: 617/617 green.
