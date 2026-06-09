---
phase: 35-windows-plugin-support-security-hardening-milestone-verifica
plan: 02
subsystem: testing
tags: [security, verify, ci, grep-gate, plugin-trust, modularity-audit, qhostaddress, nlohmann, mirajazz]

# Dependency graph
requires:
  - phase: 30-plugin-host-modular-foundation
    provides: SdPluginServer loopback bind + Phase-30 comment-aware CI gate idiom
  - phase: 27-plugin-trust
    provides: VerifyVerdict 4-way enum + CR-01 tampered-refused-even-with-consent + per-plugin consent store
  - phase: 35-windows-plugin-support-security-hardening-milestone-verifica
    provides: Plan 35-01 WINPLG-01/02 classifier + LoadedPluginsPage chip
provides:
  - Scripted comment-aware VERIF-02 grep gates (scripts/verif-milestone-gates.sh) runnable locally + in CI
  - CI wiring of the four modularity gates (extends, not replaces, the Phase-30 loopback/SIGPIPE gate)
  - 'docs/milestone-v2.0-modularity-audit.md: gate results + PROVISIONAL objectName scan + honest per-phase live-evidence/HUMAN-UAT reconciliation'
  - Confirmation + test-lock that PLGSEC-01/02/03 invariants are green (verify-only; zero security-source drift)
affects: [35-03-loadedpluginspage-chips, milestone-v2.0-verification, release-audit]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "Comment-aware grep gate: grep -vP ':[0-9]+:\\s*(//|[*])' suppresses comment-only hits; live code still trips"
    - 'Verify-only plan: shipped security code is confirmed at file:line + locked by an existing green test; git diff --stat must show zero source change'

key-files:
  created:
    - scripts/verif-milestone-gates.sh
    - docs/milestone-v2.0-modularity-audit.md
  modified:
    - .github/workflows/ci.yml

key-decisions:
  - "Task 1 was pure verification — both verdict-mapping cases (tampered->Refused :265, unsigned->Unsigned :305) already exist, so NO test was added (plan: 'if it already exists, add nothing')"
  - Gate 3 asserts on the makeAkp03/05/153 function symbols (not the akp05 filename) so comment mentions of the removed backend do not false-trip
  - VERIF-02 CI step is a separate defense-in-depth step that re-checks QHostAddress::Any plus three modularity boundaries; the faster Phase-30 security gate stays first and intact

patterns-established:
  - Milestone modularity audit doc mirrors docs/plugin-event-parity.md table style (legend + per-row status + honest deferred reconciliation)
  - 'Never fabricate a screenshot/walk: deferred HUMAN-UAT items are cited to their 3{2,3,4}-HUMAN-UAT.md files, marked partial, not claimed done'

requirements-completed: [PLGSEC-01, PLGSEC-02, PLGSEC-03, VERIF-02]

# Metrics
duration: 18min
completed: 2026-06-08
---

# Phase 35 Plan 02: VERIF-02 Milestone Gates + PLGSEC Verify-Lock + Modularity Audit Summary

**Confirmed + test-locked the already-shipped PLGSEC-01/02/03 security invariants (zero source drift), scripted the four comment-aware VERIF-02 modularity grep gates into `scripts/verif-milestone-gates.sh` + CI, and authored the honest `docs/milestone-v2.0-modularity-audit.md`.**

## Performance

- **Duration:** ~18 min
- **Started:** 2026-06-08
- **Completed:** 2026-06-08
- **Tasks:** 2
- **Files modified:** 3 (2 created, 1 modified)

## Accomplishments

- **PLGSEC-01/02/03 confirmed green + test-locked, verify-only.** Walked each invariant
  to its cited file:line (VerifyVerdict 4-way `plugin_verify_gate.hpp:41-46`; CR-01 Refused
  branch `plugin_catalog_model.cpp:907-927`; FIX-CONSENT write `:1063-1076` + launch-sweep
  read `:173`; LocalHost bind `sd_plugin_server.cpp:93`; phone-home gate `:521-535`). Ran
  `ctest -R "PluginVerifyGate|PluginInstallFromFile|PluginCatalog|SdPluginServer"` -> 48/48
  PASS. `git diff --stat` shows ZERO change to the three security source files.
- **Verdict split already locked** — both `tampered->Refused` (`test_plugin_verify_gate.cpp:265`)
  and `unsigned->Unsigned` (`:305`) cases exist, plus the four `verdictToTrustLevel` mapping
  cases (`:179-195`). Per the plan, NO test was added.
- **Scripted the four VERIF-02 gates** (`scripts/verif-milestone-gates.sh`, bash `set -euo pipefail`, ASCII): mirajazz coupling, nlohmann core-include, makeAkp03/05/153 symbols,
  QHostAddress::Any. Each prints PASS/FAIL+count; exits non-zero on FAIL. All four PASS on
  the current tree (the only naive hits are comment-only). Comment-aware semantics verified
  with a synthetic live-code line (trips) vs a comment (skipped).
- **CI-wired** the gates as a Linux-only step in `.github/workflows/ci.yml` that EXTENDS —
  does not replace — the Phase-30 `QHostAddress::Any`/SIGPIPE gate (both present).
- **Authored `docs/milestone-v2.0-modularity-audit.md`** mirroring `docs/plugin-event-parity.md`
  style: (a) the four gate results + counts + comment-only naive hits, (b) a PROVISIONAL
  objectName-coverage scan (refreshed by Plan 35-03 chips), (c) an HONEST per-phase (30-35)
  live-evidence vs deferred-HUMAN-UAT reconciliation citing 32/33/34-HUMAN-UAT.md, with
  WINPLG-03 (chip-only, Wine launch deferred) and PI-03 (human-verify deferred) marked partial.
- **Full suite green:** 781/781 (`-LE qml`) + 17/17 qml = 798 total.

## Task Commits

1. **Task 1: Verify PLGSEC invariants + lock the verdict split** — no commit (verify-only;
   zero file changes — both verdict-mapping cases already exist, all 48 tests green, no
   security source touched). Documented here.
1. **Task 2: Scripted VERIF-02 gates + CI wiring + modularity audit doc** — `80201f2` (docs)

_Note: Task 1 is a pure verification gate; per the plan its only allowed edit was an added
test IF the unsigned->Unsigned case were absent — it was present, so nothing was committed._

## Files Created/Modified

- `scripts/verif-milestone-gates.sh` (created) - Four comment-aware VERIF-02 modularity grep
  gates; PASS/FAIL+count per gate; exit non-zero on FAIL.
- `docs/milestone-v2.0-modularity-audit.md` (created) - VERIF-02 deliverable: gate results,
  PROVISIONAL objectName scan, honest per-phase live-evidence/HUMAN-UAT reconciliation.
- `.github/workflows/ci.yml` (modified) - Added Linux-only "Enforce milestone modularity
  gates (VERIF-02)" step invoking the script; Phase-30 gate left intact.

## Decisions Made

- **Task 1 added no test** — both required verdict-mapping cases already exist; the plan
  explicitly says "if it already exists, add nothing."
- **Gate 3 keys on the function symbols** (`makeAkp05|makeAkp03|makeAkp153`), not the `akp05`
  filename, so the removed backend's doc comments do not false-trip.
- **objectName section marked PROVISIONAL** — Plan 35-02 runs in Wave 1 before Plan 35-03 adds
  the LoadedPluginsPage Windows status chips; Plan 35-03's live gate refreshes it.

## Deviations from Plan

None - plan executed exactly as written. (Task 1 correctly produced no edit, as the plan
anticipated for the already-present verdict cases.)

## Issues Encountered

- **Initial gate script aborted silently under `set -euo pipefail`.** A producing `grep` that
  found nothing exited non-zero and, via `pipefail`, failed the `$(... | strip_comments)`
  command substitution, aborting under `set -e`. Fixed by wrapping each producer in
  `{ grep ... || true; }` before the comment filter. Resolved before commit; gates now print
  all four results and exit 0.
- **pre-commit shfmt + mdformat reformatted the new files** (expected re-add dance per CLAUDE.md)
  — re-staged and re-committed; second run passed all hooks. No `--no-verify`.

## User Setup Required

None - no external service configuration required.

## Next Phase Readiness

- VERIF-02 deliverable complete; the four modularity gates are scripted + CI-wired + documented.
- PLGSEC-01/02/03 confirmed green + test-locked with zero security-source drift.
- **Plan 35-03** should finalize the LoadedPluginsPage Windows status chips with `objectName`s
  and refresh the PROVISIONAL objectName-coverage section of the audit doc via its live gate.
- Deferred (not blockers): the Multi/Toggle (32), PI render/round-trip (33), and Wayland/X11
  focus-walk (34) human-UAT walks remain for a windowed/hardware session; WINPLG-03 Wine launch
  is intentionally deferred (no Wine launcher this milestone, per the WINPLG-01 ADR).

## Self-Check: PASSED

______________________________________________________________________

*Phase: 35-windows-plugin-support-security-hardening-milestone-verifica*
*Completed: 2026-06-08*
