---
phase: 11-ajazz-2-4g-8k-mouse-probe-and-confirm
plan: '01'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: aj-series-mouse-wire-format
tags: [devices, mouse, wire-format, reconciliation, MOUSE-01, ajazz_24g_8k]
dependency_graph:
  requires: []
  provides: [aj-series-opcode-map, aj-series-protocol-header]
  affects: [src/devices/mouse, docs/protocols/mouse/aj_series.md]
tech_stack:
  added: []
  patterns: [vendor-opcode-builders, BIT7-checksum-stamp, MockTransport-byte-assertions]
key_files:
  created: []
  modified:
    - src/devices/mouse/src/aj_series_protocol.hpp
    - src/devices/mouse/src/aj_series.cpp
    - docs/protocols/mouse/aj_series.md
    - docs/protocols/mouse/aj_series_vendor.md
    - tests/unit/test_aj_series_wire_format.cpp
decisions:
  - "OPCODE SUPERSESSION: the plan's documented 0x21/0x22/0x23/0x30 opcode hypothesis was a pre-corpus guess. The 2026-05-21 RE corrected it. Shipped code uses the corpus-confirmed AJ159 APEX map: 0x54 DPI table, 0x53 omnibus (LOD/sensitivity/sleep), 0x04 poll rate (via _RateToNum), 0x07 LED. 0x21/0x22/0x24 are OLED picture/GIF opcodes, NOT DPI/poll. RE/hardware wins per CLAUDE.md."
  - NO kAj24g8kEnvelope switch-point shipped. The V1.0-vs-Max capture-pending enum the plan specified was never built; the corpus resolved the 3151:5007 dialect directly (64-byte / report-id 0x05 / BIT7) so no speculative envelope guard was needed.
  - AJ199 (0x3554) V1.0-vs-Max divergence is real but SKU-specific (suspect dialect mismatch), not the 3151:5007 path the plan was scoped to.
metrics:
  mode: retrospective-reconciliation
  reconciled: '2026-05-27'
---

# Phase 11 Plan 01: AJ-series Wire-Format Footing (MOUSE-01) — Reconciliation Summary

**Mode:** retrospective-reconciliation. The implementation shipped ad-hoc ahead of GSD
bookkeeping. This documents what actually landed and where it deviated from the plan.

**One-liner:** The 3151:5007 wire-format footing shipped as a byte-precise AJ159 APEX
vendor opcode library on a 64-byte / report-id-0x05 / BIT7 envelope — NOT the
capture-pending switch-point + documented-opcode scaffold the plan described.

## Tasks Completed (plan task -> shipped commit -> files)

| Plan task                                                         | Intended deliverable                                                                       | Shipped as                                                                                                                                                                                                                    | Commit(s)                                                                    | Files                                                                                                                        |
| ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| Task 1 — aj_series.md V1.0-vs-Max reconciliation section          | Additive `CAPTURE-PENDING` doc section naming OemDrv-17B vs HIDUsb-20B + opcode divergence | Partially superseded — the corpus dossier resolved the 3151:5007 dialect as 64-byte/0x05/BIT7; the AJ199 17B/20B variants are documented as a separate SKU dialect-mismatch (suspect) rather than the 3151:5007 open question | 656fb1c (vendor-correct primitives), 831330e (doc refresh)                   | docs/protocols/mouse/aj_series.md, docs/protocols/mouse/aj_series_vendor.md                                                  |
| Task 2 — install `kAj24g8kEnvelope` switch-point + pin Unresolved | A named capture-pending enum every encoder routes through; test pins `Unresolved`          | NOT SHIPPED. The switch-point scaffold was never built; the corpus-confirmed opcode map ships directly in `aj_series_protocol.hpp` (`FeaCmd` enum + `build*` builders + `stampBit7Checksum`)                                  | 656fb1c, 0e9bb04 (setter migration), 3f82ebf (MockTransport fixture + smoke) | src/devices/mouse/src/aj_series_protocol.hpp, src/devices/mouse/src/aj_series.cpp, tests/unit/test_aj_series_wire_format.cpp |

## What Actually Shipped

`aj_series_protocol.hpp` defines the AJ159 APEX vendor wire format directly:

- `kReportSize = 65` (1 report-id + 64 body), `kReportId = 0x05`.
- `FeaCmd` opcode enum: `SetReport=0x04` (poll), `SetLedParam=0x07` (LED),
  `MouseSetOption0=0x53` (omnibus LOD/sensitivity/sleep), `MouseSetOption1=0x54`
  (DPI table), `SetTftLcdData=0x25`, `SetOledClock=0x28`, plus key-matrix / macro /
  TFT builders.
- `stampBit7Checksum()` — `sum(pkt[1..62]) & 0x7F` at `pkt[64]` (CheckSumType.BIT7).

This is the line-precise reimplementation of the corpus opcode table, landed as an
additive header (656fb1c) then wired into `AjSeriesMouse` (0e9bb04).

## Deviations from Plan

1. **Opcode supersession (the core deviation).** The plan plans against documented
   opcodes `0x21` (DPI) / `0x22` (poll) / `0x23` (LOD) / `0x30` (RGB). These were a
   pre-corpus hypothesis. The 2026-05-21 RE corrected them: the real map is `0x54` /
   `0x04` / `0x53` / `0x07`, and `0x21`/`0x22`/`0x24` are OLED picture/GIF opcodes
   (dossier §3.10), not mouse-config opcodes. The shipped code is correct; the plan
   text is stale. RE/hardware wins (CLAUDE.md).
1. **The `kAj24g8kEnvelope` / `AjEnvelopeVariant::Unresolved` switch-point was never
   built.** A grep of `src/devices/mouse/` and the test tree finds zero occurrences.
   The plan's entire "capture-pending honesty contract" premise rested on the 3151:5007
   dialect being unresolved; the corpus resolved it (64B/0x05/BIT7 universal dialect),
   so no speculative-envelope guard was needed.
1. **Shipped beyond the plan:** hardware-confirmed work the plan did not scope — the
   `0x28` OLED firmware-RTC clock (0a1952e) and battery telemetry (376fb61, …, 4fd0bf4)
   — landed in this same area. Covered under plans 11-03/11-04 reconciliation.
1. **The 17B/20B V1.0-vs-Max envelope reconciliation did NOT vanish — it moved.** It is
   documented as the AJ199 family (0x3554) dialect-mismatch (dossier §5), flagged
   **suspect**, not as the 3151:5007 question. The plan conflated the two SKU families.

## Maturity & Witness

- **Hardware-confirmed:** `0xFFFF` / usage-`0x02` control collection (69c64a1); report-id
  `0x05` for the config path (dossier §7).
- **Decompile-confidence (renderer, no live USB capture):** the `0x54`/`0x53`/`0x04`/`0x07`
  byte maps and `_RateToNum` ladder. Pinned by MockTransport byte-level tests, not by a
  wire capture.
- **Capture-pending:** an exact USB round-trip witness of every config opcode on a
  physical 3151:5007 remains outstanding.
