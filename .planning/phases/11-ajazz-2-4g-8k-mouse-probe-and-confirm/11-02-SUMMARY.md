---
phase: 11-ajazz-2-4g-8k-mouse-probe-and-confirm
plan: '02'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: aj-series-mouse-dpi-lod
tags: [devices, mouse, dpi, lod, reconciliation, MOUSE-02, MOUSE-03]
dependency_graph:
  requires: [11-01]
  provides: [aj-series-dpi-stages, aj-series-lod]
  affects: [src/devices/mouse/src/aj_series.cpp]
tech_stack:
  added: []
  patterns: [atomic-dpi-table-upload, omnibus-options-packet, MockTransport-byte-assertions]
key_files:
  created: []
  modified:
    - src/devices/mouse/src/aj_series.cpp
    - src/devices/mouse/src/aj_series_protocol.hpp
    - tests/unit/test_aj_series_dpi_fn.cpp
decisions:
  - 'OPCODE SUPERSESSION: plan specified per-stage DPI via cmd 0x21 and LOD via cmd 0x23. Shipped code uses the corpus-confirmed AJ159 APEX map: DPI table via 0x54 (MouseSetOption1 / buildDpiTable, atomic 8-stage upload of 8x uint16-LE DPI + 8x {R,G,B}) and LOD as a field of the 0x53 omnibus (MouseSetOption0), NOT a standalone opcode. 0x21 is OLEDPICDATA, 0x23 is unallocated on the mouse path. RE/hardware wins per CLAUDE.md.'
  - DPI is atomic-table, NOT per-stage independent. The 0x54 vendor model re-uploads the full 8-stage table on every change (dossier 3.6); the plan's per-stage 0x21 write does not exist. setDpiStage mutates the cached stage then re-emits the whole table.
  - 8 DPI stages confirmed (dpiStageCount()==8); the ROADMAP's earlier 6 was a stale assumption corrected 2026-05-20.
  - NO kAj24g8kDpiCycleOrder constant and NO kAj24g8kEnvelope gating shipped. The Pitfall-28 capture-sourced cycle-order placeholder was never built; setActiveDpiStage on the shipped path is a simple active-index select, not a captured permutation.
metrics:
  mode: retrospective-reconciliation
  reconciled: '2026-05-27'
---

# Phase 11 Plan 02: DPI Stages + LOD (MOUSE-02/03) — Reconciliation Summary

**Mode:** retrospective-reconciliation. Documents what landed vs the plan.

**One-liner:** 8 DPI stages and lift-off-distance shipped on the corpus-confirmed
AJ159 APEX opcodes — DPI as an atomic 0x54 table upload, LOD as a field of the 0x53
omnibus — NOT the per-stage 0x21 / standalone 0x23 / capture-pending-cycle-order scaffold
the plan described.

## Tasks Completed (plan task -> shipped commit -> files)

| Plan task                                                       | Intended deliverable                                                                      | Shipped as                                                                                                                                                                                                                                                                | Commit(s)                                                              | Files                                                                                                                   |
| --------------------------------------------------------------- | ----------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Task 1 — 8-stage count + capture-sourced cycle order            | `dpiStageCount()==8` + `kAj24g8kDpiCycleOrder` non-identity permutation gated on envelope | PARTIAL. `dpiStageCount()==8` shipped (a8a4c9b, refined to 8 in 0e9bb04). The `kAj24g8kDpiCycleOrder` capture-pending constant was NOT shipped; `setActiveDpiStage` is a plain active-index select                                                                        | a8a4c9b, 0e9bb04                                                       | src/devices/mouse/src/aj_series.cpp, tests/unit/test_aj_series_dpi_fn.cpp                                               |
| Task 2 — per-stage DPI (0x21) + colour + independent LOD (0x23) | Per-stage `0x21` write + separate `0x23` LOD write, both gated on `kAj24g8kEnvelope`      | SUPERSEDED. Per-stage DPI rides the `0x54` atomic table (`buildMouseSetOption1` / `buildDpiTable`); LOD is `liftCutOff` byte in the `0x53` omnibus (`buildMouseSetOption0`). Independence holds (separate writes) but on different opcodes than planned; no envelope gate | a8a4c9b (DPI table), 253cd9c (omnibus LOD), 0e9bb04 (setter migration) | src/devices/mouse/src/aj_series.cpp, src/devices/mouse/src/aj_series_protocol.hpp, tests/unit/test_aj_series_dpi_fn.cpp |

## What Actually Shipped

- `dpiStageCount()` returns `8` (field-determined; 0e9bb04 documents the prior-6 was a
  scaffold cap).
- `setDpiStage(index, stage)` mutates the cached `m_dpiStages` then re-emits the full
  table via `buildMouseSetOption1(activeIdx, stageCount, dpis, colours)` — opcode `0x54`,
  8x uint16-LE DPI at vendor bytes 8..23, 8x {R,G,B} at bytes 40..63 (8th-stage B-channel
  overwritten by the BIT7 checksum — a documented vendor edge case).
- `buildDpiTable(DpiTable const&)` is the profile-aware variant carrying the per-profile
  slot byte the legacy builder omits.
- `setLiftOffDistanceMm(mm)` writes the `liftCutOff` field (byte 52) of the `0x53` omnibus
  via `buildMouseSetOption0(m_options)` — independent of the DPI write.

## Deviations from Plan

1. **Opcode supersession.** Plan: per-stage DPI `0x21`, LOD `0x23`. Shipped: DPI table
   `0x54`, LOD field in omnibus `0x53`. The `0x21`/`0x23` map was the pre-corpus
   hypothesis; the 2026-05-21 RE corrected it (`0x21` = OLEDPICDATA). RE wins.
1. **DPI is not per-stage-addressable.** The vendor wire format uploads the whole 8-stage
   table atomically; there is no single-stage DPI opcode. `setDpiStage` therefore mutates
   a cached stage and re-emits the full table.
1. **LOD is not a standalone opcode.** It is one byte of the `0x53` omnibus packet
   (alongside sensitivity, sleep, debounce, battery-LED). It is still independently
   *settable* (per MOUSE-03 intent) — a `setLiftOffDistanceMm` call emits its own write —
   but not via a dedicated `0x23` write.
1. **`kAj24g8kDpiCycleOrder` (Pitfall 28 cycle-order placeholder) was never built.** No
   captured permutation constant exists; `setActiveDpiStage` selects the active index
   directly. No `kAj24g8kEnvelope` gate exists either.

## Maturity & Witness

- **Decompile-confidence (no live USB capture):** the `0x54` DPI-table byte map and the
  `0x53` LOD field offset. Pinned by MockTransport byte-level + checksum tests in
  `test_aj_series_dpi_fn.cpp`, not by a wire capture (dossier §7: Medium-High).
- **Capture-pending:** the firmware DPI-cycle ORDER and NVM-persistence across power-cycle
  both need a physical 3151:5007 round-trip witness (out of agent scope).
- The 8-stage count itself is field-determined (hardware-observed count), not a guess.
