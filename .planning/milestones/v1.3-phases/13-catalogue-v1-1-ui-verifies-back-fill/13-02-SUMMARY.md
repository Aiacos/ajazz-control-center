---
phase: 13-catalogue-v1-1-ui-verifies-back-fill
plan: 02
subsystem: testing
tags: [verify, qml, time-sync, maturity, operator-checklist]

# Dependency graph
requires:
  - phase: 05-time-sync
    provides: TimeSyncService (autoSync QSettings Time/AutoSync, 300 ms debounced arrival, manualSyncSucceeded / syncSucceeded / syncFailed signal split)
  - phase: 10-devices-akp03
    provides: DEVICES-05 akp05e (was akp03_variant_3004) hasClock demotion - GATING for VERIFY-01 (verified-not-landed in this worktree on 2026-05-28)
  - phase: 12-devices-ak980pro
    provides: DEVICES-06 ak980pro hasClock demotion - GATING for VERIFY-01 (verified-not-landed in this worktree on 2026-05-28)
provides:
  - Operator-runnable VERIFY-CHECKLIST.md covering VERIFY-01..04 with as-built UI wording, prerequisites block, and PASS / FAIL / BLOCKED acceptance lines.
affects: [v1.2-milestone-acceptance, future-time-sync-firmware-implementations]

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'Operator checklist anchored to as-built source: each acceptance citation links a UI string to the .cpp / .qml line that emits it, so a UI regression that drifts wording is caught on next operator pass.'
    - "Pitfall 19 honesty contract enforced via signal-routing audit: the green 'Time synced' Notification pill is wired ONLY to manualSyncSucceeded (Main.qml:170-173); NotImplemented routes through syncFailed; auto-sync never emits manualSyncSucceeded; therefore a NotImplemented result cannot produce a success toast on any path."

key-files:
  created:
    - .planning/phases/13-catalogue-v1-1-ui-verifies-back-fill/VERIFY-CHECKLIST.md
  modified: []

key-decisions:
  - "Authored against the as-built v1.1 UI, NOT the plan interfaces block: DeviceRow.qml no longer carries the sync button or maturity tooltip (refactored to SettingsRow.qml inside ProfileEditor's Settings tab). Checklist tests reality."
  - "VERIFY-01 'BLOCKED, not FAIL' rule preserved: at the moment of authoring, register.cpp still sets .hasClock=true on both akp05e (USB 0x0300:0x3004) and ak980pro - i.e. DEVICES-05 (Phase 10) and DEVICES-06 (Phase 12) demotions have NOT landed on this worktree branch yet, contrary to the user prompt's assertion. The checklist instructs the operator to record BLOCKED + dependency rather than file a false FAIL."
  - Acceptance wording sourced verbatim from the running QML / cpp (SettingsRow.qml lines 196-235, SettingsPage.qml lines 204-225, Main.qml lines 167-173, time_sync_service.cpp lines 38, 84-93, 155-166, 208-210, 255). No idealised strings.

patterns-established:
  - 'Source-of-truth UI element block per verify: each verify cites the exact .qml / .cpp file + line range that emits the asserted on-screen string. A drift in either side is then trivially diff-able against the checklist.'
  - '5-tier maturity acceptance: tier word match + per-tier description string + per-tier colour family - tested as a triple so a single broken colour or label is caught.'

requirements-completed: [VERIFY-01, VERIFY-02, VERIFY-03, VERIFY-04]

# Metrics
duration: 6min
completed: 2026-05-28
---

# Phase 13 Plan 02: VERIFY-CHECKLIST for VERIFY-01..04 Summary

**Authored an operator-runnable checklist for the four v1.1-deferred real-hardware visual UI verifications (sync surface gating on hasClock, global auto-sync persistence + 300 ms arrival firing, the Pitfall 19 NotImplemented-must-never-show-success contract, and the 5-tier maturity surface), with acceptance wording sourced verbatim from the as-built SettingsRow.qml / SettingsPage.qml / Main.qml / time_sync_service.cpp.**

## Performance

- **Duration:** ~6 min
- **Started:** 2026-05-28T08:17:37Z
- **Completed:** 2026-05-28T08:23:00Z (approximate; commit timestamp 5c1a552)
- **Tasks:** 1 of 2 (Task 1 autonomous-deliverable; Task 2 `checkpoint:human-verify` is DEFERRED - certification of the authored checklist + the later operator run-through are both gated activities the agent cannot perform in this environment)
- **Files created:** 1
- **Files modified:** 0

## Accomplishments

- VERIFY-CHECKLIST.md exists at `.planning/phases/13-catalogue-v1-1-ui-verifies-back-fill/VERIFY-CHECKLIST.md` (404 lines, ASCII-only, mdformat-clean).
- All four verifies (VERIFY-01..04) authored with: pre-conditions, numbered steps, exact expected on-screen wording, and a PASS / FAIL / BLOCKED acceptance line.
- Prerequisites block names the build precondition (`qt6-qtbase-private-devel` for Qt6 CorePrivate / qzipreader_p.h on Fedora) and the 4 physical devices + uaccess ACL gating.
- Clock-demotion ordering dependency surfaced for VERIFY-01: DEVICES-05 (Phase 10, akp05e -> hasClock=false) and DEVICES-06 (Phase 12, ak980pro -> hasClock=false) verifiable via grep; if absent at run-time the operator records BLOCKED + dependency, NOT FAIL.
- VERIFY-03 enforces the Pitfall 19 honesty contract: any green `Time synced: <codename>` Notification pill on a NotImplemented result is an immediate FAIL, full stop. Verified the contract by tracing the signal routing: `syncNote` (Main.qml:167-173) is wired exclusively to `manualSyncSucceeded`, which is only emitted on the OK path of `setSystemTimeOn` (time_sync_service.cpp:160-162) - NotImplemented routes through `syncFailed` (line 164) and never reaches the pill on any path (manual OR auto).
- As-built UI map up front: the plan's interfaces block described a layout where the sync button + maturity tooltip lived inline on each `DeviceRow.qml`. That layout has been refactored - the surfaces moved to the device "Settings" tab (`SettingsRow.qml`). The checklist tests the as-built UI, not the plan's idealised description.

## Task Commits

1. **Task 1: Author the VERIFY-01..04 operator checklist with acceptance wording** - `5c1a552` (docs)
1. **Task 2: Certify the VERIFY-CHECKLIST is complete and as-built-accurate** - DEFERRED (`checkpoint:human-verify`); the operator review of the authored checklist AND the later real-hardware checklist run are both gated activities for an operator with the build + 4 devices.

## Files Created/Modified

- `.planning/phases/13-catalogue-v1-1-ui-verifies-back-fill/VERIFY-CHECKLIST.md` - The phase deliverable. Operator-runnable checklist for VERIFY-01..04 with as-built UI wording, prerequisites block, results-summary table.

## Decisions Made

- **Test the as-built UI, not the plan's interfaces description.** The plan was written when the sync button + maturity tooltip lived on `DeviceRow.qml`; they have since moved into `SettingsRow.qml` inside the per-device `Settings` tab. The checklist was rewritten against the on-disk source to keep the acceptance wording matching what the operator will actually see; an "As-built UI map" table at the top documents the routing so the operator does not look for absent surfaces.
- **VERIFY-01 BLOCKED-when-demotion-absent, not FAIL.** The user prompt asserts Phase 10 / Phase 12 demotions ARE landed; the live register.cpp shows BOTH `akp05e` (line 305) and `ak980pro` (keyboard register.cpp line 61) still set `.hasClock = true`. The checklist preserves the plan's BLOCKED-not-FAIL semantics for this case so an operator running it on an old branch does not file a spurious regression report.
- **VERIFY-04 absent-tier-BLOCKED, not FAIL.** Coverage of all 5 maturity tiers cannot be exercised on 4 connected devices alone; the sidebar's connected-only filter (`visible: connected` in DeviceList.qml) hides offline catalogue rows. The checklist records absent tiers as BLOCKED (with the tier names listed) so a partial-coverage run still reports cleanly.

## Deviations from Plan

None - plan executed exactly as written. The plan correctly anticipated three sources of ambiguity (build precondition, demotion ordering, partial tier coverage) and instructed the operator to record BLOCKED rather than FAIL in each case; the checklist propagates that semantic. Wording was sourced from the as-built UI (a plan-directed activity, not a deviation).

## Issues Encountered

- mdformat reformatted the file in-place on the first commit attempt (9 whitespace adjustments). Re-staged and committed clean on the second attempt. The pre-commit `mdformat` hook is well-behaved on this file; no `--no-verify` was needed.
- An IMPORTANT informational note for downstream readers: the worktree's `CLAUDE.md` (the file present on `feat/streamdock` HEAD reachable from the worktree) is an older version than the project root `CLAUDE.md` shown in the session reminder. The two converge on the load-bearing rules (no `--no-verify`, atomic commits, ASCII tests, etc.). This is observed but not actioned by this plan.

## User Setup Required

None - no external service configuration required. The downstream operator activity (running the checklist) requires `qt6-qtbase-private-devel` to be installed by the operator on their own machine before a build can configure - that is documented in the checklist's "Prerequisites" block. It is a per-operator-machine setup, not a project tree mutation.

## Threat Flags

None new. The plan's threat register (T-13b-01 Spoofing / Pitfall 19, T-13b-02 Tampering / wording drift, T-13b-03 Repudiation / missing-demotion FAIL) is fully covered by VERIFY-03 acceptance, the verbatim-from-source acceptance citations, and VERIFY-01's BLOCKED-not-FAIL rule respectively.

## Deferred Activity (checkpoint:human-verify)

Task 2 is `type="checkpoint:human-verify"` and is intentionally NOT executed by the agent (the plan author marked `autonomous: false` for this reason). The operator activity has two distinct gated steps:

1. **Certify the authored checklist** (Task 2 of this plan). The user reads `VERIFY-CHECKLIST.md`, confirms all four verifies have pre-conditions / numbered steps / verbatim expected wording / a PASS / FAIL / BLOCKED acceptance line, that the prerequisites block is correct, and that VERIFY-03 enforces the Pitfall 19 contract. Resume signal: `approved` (or describe fixes).

1. **Run the four verifies against the running app with hardware** (the deferred downstream operator session, separate from this plan). Requires the build precondition fix (qt6-qtbase-private-devel) + the 4 devices + uaccess ACLs intact. Fills the results-summary table at the bottom of the checklist.

Per the user's explicit instruction in the executor prompt: marking this plan complete is appropriate because the *authoring* IS the autonomous deliverable; the gated downstream certification + run is a separate activity scheduled by the operator.

## Next Phase Readiness

- VERIFY-CHECKLIST.md is ready for operator review (Task 2 step 1) and later run (Task 2 step 2).
- For the operator-run step to PASS cleanly on VERIFY-01, the DEVICES-05 (Phase 10) and DEVICES-06 (Phase 12) clock-demotion patches need to land first; running the checklist before that returns BLOCKED on the affected sub-checks (not a regression).
- The phase's other plan (13-01, DEVICES-08/09 catalogue + microdia_dongle stub) is independently autonomous-executable and not blocked by anything this plan produced.

## Self-Check: PASSED

- File `.planning/phases/13-catalogue-v1-1-ui-verifies-back-fill/VERIFY-CHECKLIST.md`: FOUND
- File `.planning/phases/13-catalogue-v1-1-ui-verifies-back-fill/13-02-SUMMARY.md`: FOUND
- Commit `5c1a552` (Task 1, VERIFY-CHECKLIST docs commit): FOUND in `git log --all`
- Plan-automated verify gate (presence of VERIFY-01..04, `Time/AutoSync`, `qt6-qtbase-private-devel`, `Pitfall 19`, ASCII-only): re-verified after mdformat reformat, all pass

______________________________________________________________________

*Phase: 13-catalogue-v1-1-ui-verifies-back-fill*
*Completed: 2026-05-28*
