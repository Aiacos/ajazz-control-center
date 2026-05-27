---
phase: 12-ak980-pro-promotion
plan: '01'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: keyboard-ak980pro
tags: [devices, keyboard, ak980pro, rate-limit, KEYBOARD-04, retrospective]
dependency_graph:
  requires: []
  provides: []
  affects: [src/devices/keyboard, register.cpp]
tech_stack:
  added: []
  patterns: [register.cpp-descriptor-edit, MockTransport-DI-seam]
key_files:
  created: []
  modified: []
status: NOT-SHIPPED
metrics:
  reconciled: '2026-05-27'
  tasks_completed: 0
  tasks_planned: 3
---

# Phase 12 Plan 01: Wireless RGB Rate-Limiter (KEYBOARD-04) — Reconciliation

**Mode:** retrospective-reconciliation. This SUMMARY documents what actually
landed in the shipped tree relative to the plan; it is NOT an execution record.

**One-liner:** The KEYBOARD-04 wireless RGB rate-limiter (`isWireless` descriptor
flag + `writeRgb` funnel + `<=10 writes/sec` throttle + MockTransport/fake-clock
test) was **NOT IMPLEMENTED**. None of the three planned tasks shipped.

## Tasks Completed

| Task | Name                                            | Commit | Files | Status      |
| ---- | ----------------------------------------------- | ------ | ----- | ----------- |
| 1    | Add `isWireless` to DeviceDescriptor + register | —      | —     | NOT SHIPPED |
| 2    | `writeRgb` funnel + injectable-clock rate-limit | —      | —     | NOT SHIPPED |
| 3    | MockTransport + fake-clock rate-limit unit test | —      | —     | NOT SHIPPED |

## Verification (as of 2026-05-27)

- `grep -rn isWireless src/core/include/ajazz/core/device.hpp` → **no match.**
  `DeviceDescriptor` has no `isWireless` field.
- `grep -rn "writeRgb\|kMinRgbInterval" src/devices/keyboard/` → **no match.**
  The only `isWireless` hits in the keyboard backend are the unrelated
  `buildPerKeyRgbWriteHeader(bool isWireless)` / `buildPerKeyRgbReadback(bool isWireless)`
  parameter names (per-key RGB mode-byte selection), not a transport-link flag.
- `tests/unit/test_ak980_rate_limit.cpp` → **does not exist.**
- `ak980pro` in `register.cpp` (lines 55-70) registers the descriptor with
  `.controlUsagePage = 0xFF13` but no `.isWireless`.

The RGB write methods in `proprietary_keyboard.cpp` (`setFirmwareLightingMode`'s
5-packet envelope, `setRgbStatic`, `setRgbEffect`, `setRgbBuffer`, `setRgbBrightness`)
still issue `m_transport->writeFeature(...)` / `m_transport->write(...)` directly —
there is no funnel and no inter-write spacing.

## Deviations from Plan

**Entire plan unimplemented.** The plan itself flagged this as "the ONE genuinely
build-testable deliverable of Phase 12 (no hardware needed)", yet it is the one
piece that never landed while the hardware-gated features (clock, battery) did.

- The plan's own CONDITIONAL-EXECUTION gate 1 cited a build precondition
  (`qt6-qtbase-private-devel` missing → C++ configure fails). The ad-hoc work that
  shipped Phase 12 features predated or sidestepped formal plan execution; the
  rate-limiter, which had no hardware dependency, was simply never picked up.
- Pitfall 24 (RGB-write flood starving the 2.4G keystroke path) is therefore
  **still unmitigated in code.** The DoS threat T-12-01 is open.

## Maturity & witness

- **Software mitigation:** ABSENT. No rate-limit funnel exists.
- **Hardware witness:** the planned real-hardware 60-second RGB-sweep
  keystroke-stall promotion-gate (needs a physical `0c45:8009` + 2.4G dongle)
  is moot — there is no software contract to witness yet.
- **Carry-forward:** KEYBOARD-04 remains genuinely open. Both the software
  contract (Tasks 1-3) and the physical witness are outstanding work for a future
  phase. This is the cleanest re-pickup candidate in Phase 12 because it needs no
  hardware to land the unit.

## Self-Check

- Confirmed `isWireless` absent from `device.hpp`, `writeRgb`/`kMinRgbInterval`
  absent from the keyboard backend, and `test_ak980_rate_limit.cpp` absent — all
  three plan artifacts are missing. Reconciliation verdict: NOT SHIPPED.
