---
quick_id: 260514-h0w
title: Recheck Phase 4 CONTEXT vs new Stream Dock research
date: 2026-05-14
status: complete
related_phase: 4
---

# Quick 260514-h0w — Recheck Phase 4 CONTEXT vs new Stream Dock research

## Trigger

User added significant Stream Dock material in a parallel session: ~690 lines across `docs/protocols/streamdeck/{akp03,akp05,akp153}.md` + `docs/research/vendor-protocol-notes.md`, two new files (`docs/protocols/streamdeck/akp815.md`, `docs/protocols/streamdeck/_research-sources.md`), plus source-side edits to `src/core/include/ajazz/core/device.hpp`, `src/devices/streamdeck/src/akp03.{cpp,hpp}` / `akp05.{cpp,hpp}` and `tests/unit/test_akp03_protocol.cpp`. Asked to recheck whether Phase 4 CONTEXT.md (just committed in `7f13736`) is still valid.

A `gsd-plan-phase` background agent was already running for Phase 4 against the pre-recheck CONTEXT.md — stopped before it could produce a plan based on potentially stale assumptions.

## Method

1. Stopped the background plan agent (`a520da1f4b9a54fcc`).
1. Diffed `src/core/include/ajazz/core/device.hpp` to confirm whether `DevicePtr` / `IDevice` API changed.
1. Read the new docs (`akp815.md`, `_research-sources.md`) and skimmed the bigger diffs (`akp153.md`, `vendor-protocol-notes.md`).
1. Walked each Phase 4 decision (D-01..D-06) against the new findings.
1. Amended CONTEXT.md where decisions needed clarification (no decision was overturned).

## Findings

| Phase 4 decision                                | Verdict                 | Detail                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| ----------------------------------------------- | ----------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| D-01 silent toast policy                        | ✅ Valid                | Toast UX is unaffected by catalog growth.                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| D-02 hot-plug error surface = log only          | ✅ Valid + reinforced   | The new `akp815.md` documents an existing case: a device referenced 17 times in catalog code but with **no backend in `register.cpp`**. When that device is plugged in, hidapi sees `0x5548:0x6672` but `DeviceRegistry::open()` returns nullptr — exactly the "log-only, no UI surface" path D-02 describes. Added an explicit "catalog-referenced, no backend" case to D-02.                                                                                                                                 |
| D-03 fine-grained `dataChanged`                 | ✅ Valid                | Update strategy is row-count-agnostic.                                                                                                                                                                                                                                                                                                                                                                                                                                                                         |
| D-04 row per backend (codename)                 | ⚠️ Needed clarification | Finding 16 catalogues at least 8 different VID/PID pairs that all map to the same `akp03` protocol (rebadging across Mirabox N3/N3-rev3/N3EN, Soomfon SE, Mars Gaming MSD-TWO, TreasLin N3, Redragon SS-551). Codename → VID/PID is many-to-one. The decision is still right (one row labelled by codename), but the planner could be misled by D-04's prose into adding per-VID/PID rows. Added a "Rebadge clarification" paragraph to D-04 stating row identity is `codename`, period; rebadges share a row. |
| D-05 300 ms debounce                            | ✅ Valid                | Timing is independent of catalog content.                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| D-06 weak_ptr cache in `DeviceRegistry::open()` | ✅ Valid                | Cache key is the runtime `DeviceId`; it works for any number of registered backends.                                                                                                                                                                                                                                                                                                                                                                                                                           |

`device.hpp` change was purely additive: added `EncoderReleased` to `DeviceEvent::Kind` and refined the `EncoderPressed` doc. No `DevicePtr`, `IDevice` API, or `Result` enum impact. ARCH-03 migration plan stays valid as written.

## Catalog corrections — explicitly Phase 8 scope

The new research documents several catalog-side bugs:

- AKP815 has no backend (referenced 17 times); needs `register.cpp` entry + thin AKP153 subclass.
- AKP153/AKP153E PIDs are wrong in `register.cpp` (`0x0300:0x1001/0x1002` legacy vs `0x5548:0x6674` / `0x0300:0x1010` per `[ajazz-sdk]`).
- AKP03 layout undercounts: real device is 6 LCD keys + 3 non-LCD buttons + 3 encoders, not 6 keys + 1 encoder.
- AKP05 / N4 layout is over-counted: real device is 10 LCD keys (2×5) + 4 encoders + LCD touchscreen strip + USB hub, not 15 keys (3×5) + 4 encoders + touch strip.
- `src/app/src/streamdock_catalog_fetcher.cpp:117` maps `N4 → akp815`, which is wrong (`N4 → akp05`).
- 7 third-party rebadges of the AKP03 protocol are unregistered.

**None of these are Phase 4 scope.** Phase 4's contract is to keep hot-plug machinery stable while Phase 8 adds/corrects backend registrations behind the curtain. Recorded as a deferred note in CONTEXT.md.

## Output

- `.planning/phases/04-hot-plug-hardening/04-CONTEXT.md` — amended with rebadge clarification on D-04, AKP815 case on D-02, three new canonical refs (`vendor-protocol-notes.md` Finding 16, `_research-sources.md`, `akp815.md`), and a Phase 8 deferred-ideas note.
- This SUMMARY.md.

No source-code changes — recheck was scoped to `.planning/` artefacts only. The user's uncommitted work in `src/devices/streamdeck/`, `docs/`, and `tests/` was not touched.

## Verdict

**Phase 4 work is still valid.** Two clarifications added — neither overturns a decision. Safe to resume `/gsd-autonomous --only 4 --interactive` from the plan step (the previous plan agent was killed before producing output).

## Files

- `.planning/phases/04-hot-plug-hardening/04-CONTEXT.md` (amended)
- `.planning/quick/260514-h0w-recheck-phase-4-context-vs-new-stream-do/260514-h0w-SUMMARY.md` (this file)
