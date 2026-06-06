---
phase: 11-ajazz-2-4g-8k-mouse-probe-and-confirm
plan: '04'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: device-catalogue-honesty
tags: [devices, mouse, catalogue, maturity, reconciliation, DEVICES-07, ajazz_24g_8k]
dependency_graph:
  requires: [11-01, 11-02, 11-03]
  provides: [ajazz_24g_8k-catalogue-row]
  affects: [docs/_data/devices.yaml, README.md, docs/wiki/Supported-Devices.md]
tech_stack:
  added: []
  patterns: [devices.yaml-row-edit, AUTOGEN-readme-wiki]
key_files:
  created: []
  modified:
    - docs/_data/devices.yaml
    - README.md
    - docs/wiki/Supported-Devices.md
decisions:
  - 'MATURITY DEVIATION: the plan capped maturity at `partial` (three-witness rule, no functional promotion). The SHIPPED row reads `maturity: functional`. The promotion is justified by hardware-confirmed clock (0x28) + battery (0x05/byte-3) + control collection (0xFFFF/usage-0x02) on a physical unit — witnesses the plan assumed unavailable. The config opcodes (DPI/poll/LOD/RGB) remain decompile-confidence.'
  - "dpi_stages: 8 kept (field-determined; ROADMAP's earlier 6 was stale, corrected 2026-05-20)."
  - NO CAPTURE-PENDING marker in the shipped notes. The plan required the V1.0-vs-Max CAPTURE-PENDING cross-reference; the shipped row instead records the hardware-confirmed clock+battery findings and the corpus opcode map, because the 3151:5007 dialect was resolved by the corpus, not left pending.
  - Row name updated to 'AJAZZ AJ159 APEX' (b51aed9) — the 2.4G 8K SKU is an AJ159 APEX in 2.4G/8K mode per hardware; codename kept ajazz_24g_8k.
metrics:
  mode: retrospective-reconciliation
  reconciled: '2026-05-27'
---

# Phase 11 Plan 04: ajazz_24g_8k Catalogue Honesty (DEVICES-07) — Reconciliation Summary

**Mode:** retrospective-reconciliation. Documents what landed vs the plan.

**One-liner:** The `ajazz_24g_8k` catalogue row landed at `maturity: functional` with
hardware-confirmed clock/battery/control notes and `dpi_stages: 8` — NOT the `partial` /
`CAPTURE-PENDING` / V1.0-vs-Max-cross-reference row the plan prescribed, because hardware
witnesses the plan assumed unavailable were in fact obtained.

## Tasks Completed (plan task -> shipped commit -> files)

| Plan task                                                          | Intended deliverable                                                                                                                               | Shipped as                                                                                                                                                                                                                                                                              | Commit(s)                                                                                                                                                           | Files                                                               |
| ------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------- |
| Task 1 — reconcile row to honest `partial` + CAPTURE-PENDING notes | `maturity: partial`; notes cross-reference aj_series.md CAPTURE-PENDING; feature_summary moves un-witnessed items to partial/pending; dpi_stages 8 | DEVIATED. Shipped `maturity: functional`; notes record hardware-confirmed clock (0x28) + battery (0x05/byte-3) + control collection rather than a CAPTURE-PENDING reconciliation; `dpi_stages: 8` kept; `capabilities: [dpi, rgb, display, clock, battery]`; name -> "AJAZZ AJ159 APEX" | 831330e (clock+DPI face wiring), b51aed9 (APEX name), 376fb61/1f2be0c/df10529/4fd0bf4 (battery), b09302b (maturity-tier refresh), 2f50ced (DPI count 8 consistency) | docs/\_data/devices.yaml, README.md, docs/wiki/Supported-Devices.md |

## What Actually Shipped (current row, devices.yaml ~L424)

- `codename: ajazz_24g_8k`, `name: "AJAZZ AJ159 APEX"`, `vid 0x3151 pid 0x5007`.
- `maturity: functional`.
- `capabilities: [dpi, rgb, display, clock, battery]`.
- `dpi_stages: 8`.
- `notes`: records the hardware-confirmed `0x28` firmware-RTC clock (0xD7 marker,
  big-endian year, no checksum, via HidD_SetFeature), the hardware-confirmed battery read
  (status report `0x05` byte 3 over GET_FEATURE on `0xFFFF`/usage-`0x02`, with reconnect
  frame validation), and the P3.12.1/.2 wire-format rewrite.
- `feature_summary`: `works` lists the descriptor/factory wiring + the removed damaging
  `commit()` guard; `pending` carries the deferred profiles/macros/OTA/TFT-widget items.

## Deviations from Plan

1. **Maturity: `functional`, not `partial`.** The plan's `partial` ceiling assumed the
   three-witness rule could not be met without a capture + physical round-trip. In
   practice a physical 3151:5007 yielded hardware witnesses for the clock, battery, and
   control collection (2026-05-21/22), so the row was promoted. The DPI/poll/LOD/RGB
   *config* opcodes remain decompile-confidence — the `functional` tier rests on the
   confirmed clock/battery/control surface plus the corpus-confirmed (but not wire-captured)
   config opcodes.
1. **No `CAPTURE-PENDING` marker / no V1.0-vs-Max cross-reference in notes.** The corpus
   resolved the 3151:5007 dialect (64B/0x05/BIT7), so the row records confirmed findings
   instead of a pending reconciliation. (The V1.0-vs-Max question lives on the separate
   `aj199_family` 0x3554 row, which remains `scaffolded`.)
1. **Capability list and name shipped beyond the plan.** `battery` was added to
   `capabilities` (hardware-confirmed); the display name became "AJAZZ AJ159 APEX"
   (b51aed9) — neither was in the plan's scope.
1. **`dpi_stages: 8` held** as the plan required (no regression to 6).

## Maturity & Witness

- **Hardware-confirmed:** `0x28` OLED clock, battery report `0x05` byte-3, `0xFFFF`/usage-
  `0x02` control collection — these underpin the `functional` tier (dossier §7).
- **Decompile-confidence (no live USB capture):** the DPI/poll/LOD/RGB config opcode byte
  maps advertised under `dpi`/`rgb`. Pinned by MockTransport tests only.
- **Capture-pending / suspect:** an exact USB witness of the config opcodes on 3151:5007;
  the AJ199 (0x3554) dialect remains suspect on its own row.
