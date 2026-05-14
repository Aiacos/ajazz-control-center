---
phase: 3
phase_slug: architectural-decisions
status: complete
completed: 2026-05-14
commit: 688a223
---

# Phase 3: Architectural Decisions — Summary

**Status:** complete

## What landed

Three architectural decisions ratified as per-decision artefacts under
`.planning/phases/03-architectural-decisions/`. No code touched —
decision-doc-only phase, exactly as scoped.

## Artefacts produced

Single atomic commit `688a223` covering:

1. `ARCH-01-parser-choice.md` — `nlohmann::json` 3.12.0 PRIVATE-linked
   to `ajazz_plugins`, with threat-model framing of `trust_roots.json`
   parsing inside the sandbox boundary.
1. `ARCH-02-mock-seam.md` — test-only `HotplugMonitor::injectEvent`
   shim behind `#ifdef AJAZZ_TESTING`, justified against the
   `FakeAsyncExecutor` precedent.
1. `ARCH-03-ownership-migration.md` — `DeviceRegistry` slot ownership
   migrates `unique_ptr<IDevice>` → `shared_ptr<IDevice>` in a single
   atomic Phase 4 commit, before Phase 5 wires time-sync auto-sync.
1. `.planning/REQUIREMENTS.md` traceability flipped Pending → Locked
   for ARCH-01..03; checklist rows ticked.

## Downstream phases unlocked

- **Phase 4 (Hot-plug Hardening)** — both prerequisites ratified:
  ARCH-02 mock seam (drives HOTPLUG-06 test harness) and ARCH-03
  ownership migration (closes HOTPLUG-01 UAF). Phase 4 is now the
  next executable phase.
- **Phase 7 (WR-01 Trust-Roots Parser Hardening)** — ARCH-01 parser
  choice ratified. Phase 7 remains gated on Phase 4-6 ordering per
  ROADMAP, but its architectural foundation is locked.

## Verification

ROADMAP Phase 3 success criteria — all 4 satisfied:

- **SC1** — ARCH-01-parser-choice.md records the `nlohmann::json`
  PRIVATE-linked choice with explicit threat-model framing of where
  `trust_roots.json` parsing sits relative to the plugin sandbox
  boundary.
- **SC2** — ARCH-02-mock-seam.md records the test-only `injectEvent`
  shim with rationale justifying the call against the
  `FakeAsyncExecutor` precedent at `tests/unit/test_action_engine.cpp:119`.
- **SC3** — ARCH-03-ownership-migration.md confirms the
  `unique_ptr<IDevice>` → `shared_ptr<IDevice>` migration lands in
  Phase 4, with explicit acknowledgement that no v1.1 code outside
  Phase 4 holds an `IDevice*` across an event-loop turn until then.
- **SC4** — `git show --name-only 688a223` lists only `.planning/`
  paths; no code change in Phase 3.

REQUIREMENTS.md verification:

- Checklist rows 15-17 (ARCH-01..03) flipped `- [ ]` → `- [x]`.
- Traceability table rows 103-105 status flipped `Pending` → `Locked`.

Commit verification:

- One atomic commit on `main`: `688a223`.
- Pushed to `origin/main` after `git fetch && git rebase` (0/0 divergence).
