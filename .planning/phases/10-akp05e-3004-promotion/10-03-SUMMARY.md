---
phase: 10-akp05e-3004-promotion
plan: '03'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: streamdeck-display-hardware-gate
tags: [devices, streamdeck, display, akp05e, hardware-smoke, DISPLAY-04, live-hw]
dependency_graph:
  requires: [akp05-1024-framing, akp05-image-pipeline-wiring]
  provides: []
  affects: []
tech_stack:
  added: []
  patterns: []
key_files:
  created: []
  modified: []
decisions:
  - DISPLAY-04 real-hardware 100-image power-cycle smoke test was NOT shipped. tests/integration/test_akp03_hardware_smoke.cpp does not exist; no AJAZZ_REAL_HARDWARE gate is wired in tests/integration/CMakeLists.txt; docs/protocols/streamdeck/akp03.md has no operator runbook section.
  - The DISPLAY-04 promotion gate is OPEN and the gated test itself is unshipped — deferred to Phase 25 (LIVE-HW) along with the live image-render witness.
metrics:
  mode: retrospective-reconciliation
  reconciled: '2026-05-27'
  tasks_completed: 0
  tasks_deferred: 3
  shipping_commits: []
---

# Phase 10 Plan 03: AKP05E Real-Hardware Smoke Test (DISPLAY-04) — Reconciliation

**Mode: retrospective-reconciliation.** Documents what landed (nothing) and the
deferral. Not an execution record.

**One-liner:** The DISPLAY-04 real-hardware 100-image power-cycle smoke test was
**NOT shipped** — neither the gated test, the CMake gating, nor the operator
runbook exist. The promotion gate is OPEN; deferred to Phase 25 (LIVE-HW).

## Status: NOT SHIPPED

Unlike plans 10-01 and 10-02 (whose deliverables landed ad-hoc in the `akp05*`
backend), plan 10-03 produced **no shipping commits**. All three tasks are absent
from the tree.

## Tasks (all deferred)

| Task | Plan name                                                        | Status      | Evidence                                                                                                   |
| ---- | ---------------------------------------------------------------- | ----------- | ---------------------------------------------------------------------------------------------------------- |
| 1    | Author 100-image power-cycle smoke test (hidden, hardware-gated) | NOT SHIPPED | `tests/integration/test_akp03_hardware_smoke.cpp` does not exist                                           |
| 2    | Wire smoke test into CMake behind AJAZZ_REAL_HARDWARE opt-in     | NOT SHIPPED | No `AJAZZ_REAL_HARDWARE` option / `test_akp03_hardware_smoke` source in `tests/integration/CMakeLists.txt` |
| 3    | Document operator runbook in akp03.md                            | NOT SHIPPED | `docs/protocols/streamdeck/akp03.md` has no "promotion gate" / "Pitfall 18" / "DISPLAY-04" section         |

Verification: a repo-wide grep for `AJAZZ_REAL_HARDWARE`, `hardware_smoke`,
`100-image`, and `power-cycle` returns only unrelated matches (the capabilities
header, the mouse backends, the control-service test, and the vendor-recon
runbook) — none in a Stream Dock integration smoke test.

## Deviations from Plan

1. **Entire plan unshipped.** No gated test, no CMake flag, no runbook. The plan
   was authored to LAND the gate (the test code) while leaving the green RUN to an
   operator. In practice even the test code was not written.

1. **Backend file rename context.** The plan named
   `tests/integration/test_akp03_hardware_smoke.cpp`; given the AKP05E
   reclassification (a43a1cd) any future smoke test should target the `akp05`
   backend / `makeAkp05` path. The unit-level MockTransport coverage that DID ship
   (`test_akp05_touch_strip.cpp`) proves the wire bytes, but only real hardware can
   prove the firmware survives a sustained image-upload burst (Pitfall 18).

## Maturity & witness — why akp05e stays partial

DISPLAY-04 is the structural promotion gate that would flip AKP05E from
"byte-correct in tests" to "verified functional on the bench". Because the gated
test was never authored AND no live image-render witness exists, the gate is
HONESTLY OPEN and `docs/_data/devices.yaml` correctly keeps `akp05e` at
`maturity: partial`. Per `dossier/akp-streamdeck.md`:

- §6: AKP05E is "hardware-witnessed (`V3.AKP05E.01.007`); wire bytes still
  hypothesised."
- §7.4: ALL AKP05 wire bytes (DRA/ENC/MAI/LOG/M_V) are corpus/Ghidra-derived and
  **never confirmed against a live USB capture**.
- §2.2: the Linux/hidraw `0x00` report-id prepend render bug (does the AKP05 icon
  render on hidraw with the prepend?) is **PENDING Fedora hardware confirmation**.

**Both the gated smoke test (DISPLAY-04) and the live image-render witness are
DEFERRED to Phase 25 (LIVE-HW).** Until an operator runs the smoke test green on a
physical `0x0300:0x3004` and the Fedora render bug is confirmed fixed, the
promotion stays open and the maturity stays `partial`.

## Self-Check

- `tests/integration/test_akp03_hardware_smoke.cpp` does NOT exist. Verified.
- No `AJAZZ_REAL_HARDWARE` gating in `tests/integration/CMakeLists.txt`. Verified.
- No DISPLAY-04 / Pitfall 18 runbook in `docs/protocols/streamdeck/akp03.md`. Verified.
- DISPLAY-04 promotion gate is OPEN; deferred to Phase 25. Recorded.
