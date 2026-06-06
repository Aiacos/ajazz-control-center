---
phase: 10-akp05e-3004-promotion
plan: '02'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: streamdeck-input
tags: [devices, streamdeck, input, encoder, akp05e, honesty, INPUT-01, INPUT-02, DEVICES-05]
dependency_graph:
  requires: [akp05-1024-framing]
  provides: [akp05-genuine-EncoderReleased, akp05e-no-clock-honesty]
  affects: [src/devices/streamdeck/src/akp05.cpp, docs/_data/devices.yaml, src/devices/streamdeck/src/register.cpp]
tech_stack:
  added: []
  patterns: [DeviceEvent-dispatch, devices.yaml-honesty-edit]
key_files:
  created: []
  modified:
    - src/devices/streamdeck/src/akp05.cpp
    - src/devices/streamdeck/src/akp05_protocol.hpp
    - src/devices/streamdeck/src/register.cpp
    - docs/_data/devices.yaml
decisions:
  - INPUT-01 genuine EncoderReleased is satisfied by the akp05 backend dispatch (akp05.cpp:500-503): a dedicated EncoderReleased branch (value=0 is the release-state value, not a synthesized zero-delta turn). No half-step workaround was present in the akp05 backend to remove.
  - DEVICES-05 / DEVICES-11 clock demotion landed via the register.cpp descriptor flip (07c5902) AND the devices.yaml capabilities edit; akp05e maturity stays partial (NOT functional) per the RE dossier honesty contract.
  - INPUT-02 16ms encoder coalescer was NOT shipped. src/app/src/encoder_coalescer.{hpp,cpp} does not exist. Deferred — no DeviceEvent->QML encoder path consumes it yet.
metrics:
  mode: retrospective-reconciliation
  reconciled: '2026-05-27'
  tasks_completed: 2
  tasks_deferred: 1
  shipping_commits: [a43a1cd, 07c5902]
---

# Phase 10 Plan 02: AKP05E Encoder Input + Clock Honesty (INPUT-01/02 / DEVICES-05) — Reconciliation

**Mode: retrospective-reconciliation.** Documents what landed ad-hoc and how it
deviated from the plan. Not an execution record.

**One-liner:** Genuine `EncoderReleased` dispatch shipped in the AKP05E backend
(INPUT-01) and the false `clock` capability was demoted (DEVICES-05/DEVICES-11),
but the 16ms encoder coalescer (INPUT-02) was **NOT shipped** — the
`encoder_coalescer` files do not exist.

## Primary Deviation: backend file rename + one unshipped deliverable

As in plan 10-01, the device was reclassified to AKP05E (a43a1cd) so the encoder
work lives in `akp05.cpp` (the plan named `akp03.cpp`). More significantly, one
of the three plan deliverables — the encoder coalescer (INPUT-02) — never
shipped.

## Tasks Completed / Deferred

| Task | Plan name                                                             | Status                                                           | Commit                                       | Files                                       |
| ---- | --------------------------------------------------------------------- | ---------------------------------------------------------------- | -------------------------------------------- | ------------------------------------------- |
| 1    | Remove encoder value=0 workaround; genuine EncoderReleased (INPUT-01) | SHIPPED (no workaround existed in akp05 backend)                 | 3ee11f1 / e652913 era akp05.cpp              | akp05.cpp                                   |
| 2    | 16ms trailing-edge encoder coalescer (INPUT-02)                       | **NOT SHIPPED**                                                  | —                                            | (none — encoder_coalescer.{hpp,cpp} absent) |
| 3    | Remove clock from akp05e devices.yaml; promote maturity (DEVICES-05)  | SHIPPED — clock removed; maturity stays partial (NOT functional) | 07c5902 (register.cpp flip) + a43a1cd (yaml) | register.cpp, devices.yaml                  |

## What actually shipped (verified against the tree)

### INPUT-01 — genuine EncoderReleased

`akp05.cpp` poll() dispatch (lines ~493-503) maps the parser's `InputEvent::Kind`
directly to `DeviceEvent::Kind`:

```cpp
case akp05::InputEvent::Kind::EncoderPressed:
    devEv.kind = DeviceEvent::Kind::EncoderPressed;
    devEv.value = 1;
    break;
case akp05::InputEvent::Kind::EncoderReleased:
    devEv.kind = DeviceEvent::Kind::EncoderReleased;
    devEv.value = 0;
    break;
```

`EncoderReleased` is a first-class branch — `value=0` here is the release-state
value, NOT a synthesized zero-delta `EncoderTurned`. `EncoderTurned` is dispatched
on its own from a genuine rotation report. The parser (`akp05_protocol.hpp:273-278`)
classifies press vs. release from the button byte (`btn != 0x00` -> Pressed, else
Released) and decodes all four encoders. The "value=0 half-step workaround" the
plan targeted at `akp03.cpp:289-293` does **not exist** in the AKP05 backend —
there was nothing to remove. INPUT-01 is satisfied by construction.

### DEVICES-05 / DEVICES-11 — clock honesty

`docs/_data/devices.yaml` akp05e row: `capabilities: [display, encoder, touch, macros]` — **no `clock`**. The runtime descriptor `register.cpp` was flipped to
`.hasClock = false` in **07c5902** (also tracked under Phase 14-01 / DEVICES-11;
this is the same single commit). The yaml capabilities were finalized in the
a43a1cd reclassification.

## Deviations from Plan

1. **INPUT-02 encoder coalescer NOT SHIPPED.** `src/app/src/encoder_coalescer.hpp`
   and `.cpp` do not exist; `tests/unit/test_encoder_coalescer.cpp` does not exist;
   no `kCoalesceMs` / `encoderCoalesced` symbol exists anywhere in `src/` or
   `tests/`. The plan itself flagged this as conditional (Task 2 step 4: "Do NOT
   wire the coalescer into application.cpp unless the encoder-event-to-QML routing
   already exists — there is currently NO DeviceEvent->QML encoder path"). That
   QML routing still does not exist, so the coalescer primitive was never authored.
   **Deferred** — pick up when the encoder UI consumes live DeviceEvents.

1. **No workaround to remove (INPUT-01).** The plan assumed a residual
   `value==0` half-step hack in `akp03.cpp:289-293`. The AKP05 backend was authored
   with a clean `EncoderReleased` branch from the start, so INPUT-01 reduces to a
   verification rather than a fix.

1. **Maturity stays `partial`, NOT `functional`.** The plan's DEVICES-05 Task 3
   said bump `scaffolded -> functional`. The shipped yaml keeps `partial`. This is
   a deliberate, correct downgrade from the plan: the wire bytes are hypothesised
   and there is no live image witness (see Maturity & witness). Promoting to
   `functional` would be dishonest per the dossier.

1. **Backend file rename (akp03 -> akp05).** Same reclassification as 10-01.

1. **ARCH-05 no-RTC verdict.** The clock demotion is consistent with the ARCH-05
   default verdict (no host-settable RTC on the Stream Dock family). ARCH-05.1's
   RTC flip applies to `ak980pro` ONLY and does not affect this row.

## Maturity & witness

Same as plan 10-01: `akp05e` stays `partial`. Per `dossier/akp-streamdeck.md` §6
(wire bytes "still hypothesised"), §7.4 (all AKP05 wire bytes corpus/Ghidra-derived,
never live-captured), and §2.2 (the Linux/hidraw `0x00` report-id prepend render
bug, PENDING Fedora confirmation). The encoder *parser* is hardware-witnessed at
the report level, but the missing coalescer and the absent live-render witness keep
the device short of `functional`. The live witness is Phase 25 (LIVE-HW).

## Self-Check

- `akp05.cpp` has a dedicated `EncoderReleased` dispatch branch; no
  `EncoderTurned ... value = 0` half-step. Verified.
- `encoder_coalescer.{hpp,cpp}` absent; `kCoalesceMs`/`encoderCoalesced` not in
  tree. Verified (NOT SHIPPED).
- devices.yaml akp05e: no `clock` capability; maturity `partial`. Verified.
- register.cpp `.hasClock = false` for akp05e (07c5902). Verified.
