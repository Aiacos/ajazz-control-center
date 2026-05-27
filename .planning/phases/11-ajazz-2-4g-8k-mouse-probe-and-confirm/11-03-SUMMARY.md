---
phase: 11-ajazz-2-4g-8k-mouse-probe-and-confirm
plan: '03'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: aj-series-mouse-poll-rgb
tags: [devices, mouse, polling-rate, rgb, reconciliation, MOUSE-04, MOUSE-05]
dependency_graph:
  requires: [11-01, 11-02]
  provides: [aj-series-poll-rate, aj-series-rgb]
  affects: [src/devices/mouse/src/aj_series.cpp, src/app/qml/MousePanel.qml]
tech_stack:
  added: []
  patterns: [rate-to-num-lookup, single-zone-led-packet, MockTransport-byte-assertions]
key_files:
  created: []
  modified:
    - src/devices/mouse/src/aj_series.cpp
    - src/devices/mouse/src/aj_series_protocol.hpp
    - tests/unit/test_aj_series_wire_format.cpp
decisions:
  - 'OPCODE SUPERSESSION: plan specified poll rate via cmd 0x22 and per-zone RGB via cmd 0x30. Shipped code uses the corpus-confirmed map: poll rate via 0x04 SetReport with _RateToNum lookup (125->0x08, 250->0x04, 500->0x02, 1000->0x01, 2000->0x84, 4000->0x82, 8000->0x81), and LED via 0x07 SetLedParam (single 8-byte block). 0x22 is OLEDOPTION, 0x30 is screen-MCU boot, NOT poll/RGB. RE/hardware wins per CLAUDE.md.'
  - RGB is SINGLE-ZONE, not descriptor-driven multi-zone. The vendor 0x07 LED packet covers logo+scroll together with no per-zone addressing; rgbZones() honestly returns one virtual 'all' zone. The plan's Pitfall-22 descriptor-driven zone enumeration does not apply to this wire format.
  - USB 2.0 SOF-cap warning (MOUSE-04 / D-02) was NOT shipped in MousePanel.qml. The 8000 entry exists in the static ComboBox model but no hostPortIsUsb2 property and no SOF-cap warning element were added.
  - NO kAj24g8kEnvelope gating shipped.
metrics:
  mode: retrospective-reconciliation
  reconciled: '2026-05-27'
---

# Phase 11 Plan 03: Poll Rate + RGB (MOUSE-04/05) — Reconciliation Summary

**Mode:** retrospective-reconciliation. Documents what landed vs the plan.

**One-liner:** Polling rate (incl. 8000 Hz) shipped on the corpus-confirmed `0x04`
`_RateToNum` path and RGB on the single-zone `0x07` LED packet — NOT the `0x22` /
descriptor-driven-`0x30` / capture-pending scaffold the plan described; and the honest
USB 2.0 SOF-cap UI warning was NOT built.

## Tasks Completed (plan task -> shipped commit -> files)

| Plan task                                                | Intended deliverable                                                                                                    | Shipped as                                                                                                                                                                                                                            | Commit(s)                                                                           | Files                                                                                                                        |
| -------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| Task 1 — poll-rate (0x22) + descriptor-driven RGB (0x30) | `setPollingRateHz` on `0x22`; `rgbZones()` descriptor-driven; `setRgbStatic` on `0x30`; all gated on `kAj24g8kEnvelope` | SUPERSEDED. Poll rate rides `0x04` (`buildSetReportRate` via `pollRateToWireCode`); supported ladder includes 1000/2000/4000/8000. RGB is a single virtual "all" zone on the `0x07` LED packet (`buildSetLedParam`). No envelope gate | dfbf36b (poll+profile), a8a4c9b/0e9bb04 (LED migration), 0683a76 (brightness clamp) | src/devices/mouse/src/aj_series.cpp, src/devices/mouse/src/aj_series_protocol.hpp, tests/unit/test_aj_series_wire_format.cpp |
| Task 2 — MousePanel.qml honest USB 2.0 SOF-cap warning   | `hostPortIsUsb2` property + inline warning visible only at 8000 Hz on USB 2.0 (D-02)                                    | NOT SHIPPED. `MousePanel.qml` carries the static `[125..8000]` ComboBox model only; no `usb2`/`sof`/`cap`/`warn`/`hostPort` property or banner exists                                                                                 | (n/a — no matching commit)                                                          | src/app/qml/MousePanel.qml (unchanged for this task)                                                                         |

## What Actually Shipped

- `supportedPollingRatesHz()` -> `{125,250,500,1000,2000,4000,8000}`.
- `setPollingRateHz(hz)` -> `clampToSupportedRate` -> `buildSetReportRate(profile, hz)`,
  opcode `0x04`, wire code from `pollRateToWireCode` (`_RateToNum`); the high-bit-set codes
  (`0x84`/`0x82`/`0x81`) carry the high-rate flag, masked out of the BIT7 checksum.
- `rgbZones()` -> `{ name:"all", ledCount:1 }` (single virtual zone, honestly documented).
- `setRgbStatic` / `setRgbEffect` -> `emitLedPacket` -> `buildSetLedParam` (opcode `0x07`,
  8-byte effect/speed/value/mode/RGB block). `setRgbBuffer` is an honest no-op (no per-LED
  addressing). `setRgbBrightness` clamps 0..100% to the vendor 0..5 scale (0683a76).

## Deviations from Plan

1. **Opcode supersession.** Plan: poll `0x22`, RGB `0x30`. Shipped: poll `0x04`, LED `0x07`.
   The `0x22`/`0x30` map was the pre-corpus hypothesis; `0x22` = OLEDOPTION and `0x30` =
   screen-MCU boot per the corrected RE (dossier §3). RE wins.
1. **RGB is single-zone, not descriptor-driven.** The vendor LED packet has no per-zone
   addressing, so the Pitfall-22 descriptor-driven zone enumeration the plan mandated does
   not apply; `rgbZones()` honestly surfaces one virtual "all" zone.
1. **The USB 2.0 SOF-cap warning was not built.** `MousePanel.qml` was not touched for this
   task — the honesty UI for the 8000 Hz / USB 2.0 case (D-02) remains an open follow-up.
   The 8000 entry exists in the ComboBox model but is unbound.
1. **No `kAj24g8kEnvelope` gate.** As in 11-01/11-02, the capture-pending switch-point was
   never built; encoders write the corpus-confirmed opcodes directly.

## Maturity & Witness

- **Decompile-confidence (no live USB capture):** the `_RateToNum` ladder and `0x07` LED
  byte map. Pinned by MockTransport byte-level + checksum tests, not a wire capture
  (dossier §7: Medium-High).
- **Capture-pending:** an exact USB witness of the poll-rate and LED packets on a physical
  3151:5007. The 8000 Hz / USB 2.0 effective-throughput honesty UI is an unshipped
  follow-up, not merely capture-pending.
