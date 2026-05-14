---
phase: 3
phase_slug: architectural-decisions
planned: 2026-05-14
mode: decision-doc
status: Ready for execution
---

# Phase 3: Architectural Decisions — Plan

**Goal:** Three written architectural decisions (ARCH-01, ARCH-02, ARCH-03) recorded as per-decision artefacts under `.planning/phases/03-architectural-decisions/`, with `REQUIREMENTS.md` traceability flipped from Pending → Locked. No code lands in this phase.

**Source:** `03-CONTEXT.md` decisions section is the verbatim source for artefact bodies — Claude reformats into the per-decision template, no new content is invented.

## Shape

Decision-doc-only phase. Three sibling artefacts, one traceability table edit, one atomic commit. ~3 tasks.

## Scope

- **In:** Three new markdown artefacts (`ARCH-01-parser-choice.md`, `ARCH-02-mock-seam.md`, `ARCH-03-ownership-migration.md`); one edit to `.planning/REQUIREMENTS.md` Traceability table (rows 103-105: ARCH-01..03 status `Pending` → `Locked`); one commit.
- **Out:** Any code change. Any other REQUIREMENTS.md edit. Any plan/spec for Phase 4 / Phase 7 implementation work — those phases write their own plans.

## Artefact template (applied to all three)

Each per-decision file follows this structure (~30-50 lines each), content drawn verbatim from `03-CONTEXT.md` `<decisions>` section:

1. **Header** — `# ARCH-0X: <short title>` + status line (`**Status:** Locked 2026-05-14`).
1. **Decision** — 1-2 sentence summary (the locked choice).
1. **Rationale / Threat-model framing** — paragraph from CONTEXT.md.
1. **What this commits Phase <N> to** — bullet list of downstream-phase obligations.
1. **Alternatives rejected** — bullet list with one-line rejection rationale each.
1. **References** — bullets pointing to `.planning/research/SUMMARY.md`, the relevant research doc section, and `REQUIREMENTS.md` row.

## Tasks

### Task 1 · Write the three ARCH artefacts

Create three sibling files under `.planning/phases/03-architectural-decisions/`:

- **`ARCH-01-parser-choice.md`** — `nlohmann::json` 3.12.0 PRIVATE-linked to `ajazz_plugins` only. Content from `03-CONTEXT.md` lines 26-43. References: `STACK.md §3`, `SUMMARY.md` (material divergence), `REQUIREMENTS.md` ARCH-01 row.
- **`ARCH-02-mock-seam.md`** — `HotplugMonitor::injectEvent` test-only shim behind `#ifdef AJAZZ_TESTING`. Content from `03-CONTEXT.md` lines 44-61. References: `ARCHITECTURE.md Q2`, `FakeAsyncExecutor` precedent at `tests/unit/test_action_engine.cpp:119`, `REQUIREMENTS.md` ARCH-02 row.
- **`ARCH-03-ownership-migration.md`** — `DeviceRegistry` slot ownership `unique_ptr<IDevice>` → `shared_ptr<IDevice>` migration **in Phase 4**, single atomic commit across `src/core/`, `src/devices/streamdeck/`, `src/devices/keyboard/`, `src/devices/mouse/`. Content from `03-CONTEXT.md` lines 63-76. References: `PITFALLS.md Pitfall 1`, `SUMMARY.md §1`, `REQUIREMENTS.md` ARCH-03 row.

Each file ~30-50 lines, no new analysis — reformat-only of CONTEXT.md content into the artefact template.

### Task 2 · Update REQUIREMENTS.md Traceability table

Edit `.planning/REQUIREMENTS.md` lines 103-105: flip status column for ARCH-01, ARCH-02, ARCH-03 from `Pending` to `Locked`. Also flip the matching checklist rows (lines 15-17) from `- [ ]` to `- [x]`.

Do not touch any other row.

### Task 3 · Atomic commit + push

Single commit covering all four files:

- `.planning/phases/03-architectural-decisions/ARCH-01-parser-choice.md`
- `.planning/phases/03-architectural-decisions/ARCH-02-mock-seam.md`
- `.planning/phases/03-architectural-decisions/ARCH-03-ownership-migration.md`
- `.planning/REQUIREMENTS.md`

Commit message: `docs(phase-3): lock ARCH-01..03 — parser, mock seam, ownership migration`.

Body should reference the three SCs from ROADMAP Phase 3 + acknowledge that Phase 7 (ARCH-01) and Phase 4 (ARCH-02, ARCH-03) now have their architectural foundations locked.

Standard pre-push: `git fetch && git rebase origin/main && git push`.

## Verification

- Three artefact files exist at the documented paths, each with the 6-section template.
- `REQUIREMENTS.md` Traceability table shows `Locked` (not `Pending`) for ARCH-01..03.
- Checklist rows 15-17 of `REQUIREMENTS.md` are checked.
- All three Phase 3 ROADMAP success criteria 1-3 are satisfied (each artefact records the locked choice with the required framing).
- SC4 (no code lands in Phase 3) — `git diff --name-only origin/main..HEAD` lists only `.planning/` paths.
- One atomic commit on `main`, pushed.

## Routing

After completion: Phase 3 is closeable. The next phase to execute is **Phase 4: Hot-plug Hardening** which now has its two prerequisites locked (ARCH-02 mock seam, ARCH-03 ownership migration). Phase 7 (WR-01 Parser Hardening) is also unblocked by ARCH-01 but is gated on Phase 4-6 ordering per ROADMAP.

## Risks / Notes

- **Risk:** Drift between artefact text and `03-CONTEXT.md` decision body. **Mitigation:** Artefacts are reformat-only; quote `03-CONTEXT.md` lines verbatim where possible.
- **Risk:** Forgetting to flip the checklist rows in REQUIREMENTS.md (only flipping the Traceability table). **Mitigation:** Task 2 explicitly calls out both edits.
- **Note:** No `--no-verify` or hook-skipping is needed — this phase touches only `.planning/` markdown files; pre-commit hooks pass cleanly.
