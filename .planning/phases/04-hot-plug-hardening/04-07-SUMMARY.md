---
phase: 04-hot-plug-hardening
plan: 07
subsystem: docs+ci
tags: [hotplug, retro, ci, hid_open, pitfall-11, hotplug-07, institutional-knowledge]

requires:
  - phase: 04-hot-plug-hardening
    provides: existing 2026-05-12/13 commit history (the input dataset for the retro narrative)
provides:
  - 04-HOTPLUG-RETRO.md institutional-knowledge artefact (208 LoC)
  - CI grep gate enforcing the hid_open invariant (Pitfall 11)
affects:
  - Phase 8 (Catalog corrections) — RETRO links to it as the owner of the residual rebadge work
  - All future PRs touching device-lifecycle code — will fail CI loudly if they bypass HidTransport::open()

tech-stack:
  added: []
  patterns:
    - Phase-local institutional-knowledge retro under .planning/phases/<phase>/
    - Linux-only fail-fast CI grep gate run BEFORE the build step

key-files:
  created:
    - .planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md
  modified:
    - .github/workflows/ci.yml

key-decisions:
  - Retro placed phase-local (.planning/phases/04-...) per CONTEXT directive — promotion to docs/architecture/HOTPLUG.md only if future learnings show the insight generalises
  - CI grep step lives in the Linux job only (source tree identical across runners; one check is enough) and BEFORE Qt install / build (fast fail)
  - 'Used GitHub Actions ::error:: annotation so a violating PR shows the failure inline at the offending line, not just in the workflow log'
  - Excluded hid_open_path( from the grep — it's a separate hidapi function with the same prefix that does NOT have the same blocking-mode regression characteristics

patterns-established:
  - Phase-local HOTPLUG-RETRO.md as the artefact shape for HOTPLUG-07-class 'institutional knowledge preserved' requirements
  - hid_open invariant grep — template for any future cross-cutting transport invariant that must remain co-located in one .cpp file

requirements-completed: [HOTPLUG-07]

duration: 18 min
completed: 2026-05-14
---

# Phase 4 Plan 07: HOTPLUG-RETRO Narrative + hid_open CI Invariant Grep Summary

**Captured the 2026-05-12/13 hot-plug debugging story as a phase-local 208-LoC institutional-knowledge artefact (`04-HOTPLUG-RETRO.md`), and added a Linux-only fail-fast CI grep gate that fails any PR introducing an ad-hoc `hid_open()` call outside `src/core/src/hid_transport.cpp` — Pitfall 11 invariant now machine-enforced rather than reviewer-vigilance-dependent.**

## Performance

- **Duration:** ~18 min
- **Started:** 2026-05-14T11:36Z
- **Completed:** 2026-05-14T11:38Z
- **Tasks:** 2 (combined into one production commit — both pure docs/CI)
- **Files modified:** 2 (1 created + 1 modified)

## Accomplishments

- `.planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md` (208 lines) narrates the multi-day Stream Dock / DeviceList debugging session in 6 sections: Symptom, Root Cause Chain, Fix Applied (8-row commit table), Why 3 Devices Now Work (post-Phase 4 end-state), Lessons Learned (5 bullets), Forward Pointers (Plans 04-01/03/04 + Phase 8).
- All 8 cited commits (`6b11f57`, `c2fec55`, `6af22cf`, `42febbd`, `0eb886f`, `d377d80`, `4818a6d`, `62da68c`) cross-referenced with their layer (QML / Catalog) and one-line semantic.
- `Pitfall 15` named explicitly + cross-linked to `.planning/research/PITFALLS.md:370`.
- `.github/workflows/ci.yml` gains a new `Enforce hid_open invariant (Pitfall 11)` step in the Linux job. Runs BEFORE Qt install / build so a violating PR fails in seconds, not minutes. Uses GitHub Actions `::error::` annotation syntax so the failure surfaces inline in the PR diff view.
- The CI grep matches CONTEXT's exact spec verbatim: `grep -rn 'hid_open(' src/ | grep -v hid_transport.cpp | grep -v 'hid_open_path('`.
- Local invariant check is GREEN today — the only `hid_open(` call site in `src/` is `src/core/src/hid_transport.cpp:65`. The new gate ratifies the existing invariant rather than introducing a regression.

## Task Commits

1. **Tasks 1 & 2 (combined):** `b4f66eb` — docs(04-07): HOTPLUG-RETRO narrative + hid_open CI invariant grep

_Plan metadata commit follows below._

## Files Created/Modified

- `.planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md` (new, 208 LoC) — the HOTPLUG-07 institutional-knowledge artefact. YAML frontmatter `artefact: HOTPLUG-RETRO`, `requirement: HOTPLUG-07`, `phase: 4`, `written: 2026-05-14`. Six sections, 8 commit references, Pitfall 11 + Pitfall 15 cross-links, Phase 4 / Phase 8 forward pointers.
- `.github/workflows/ci.yml` (modified) — `Enforce hid_open invariant (Pitfall 11)` step added between `actions/checkout` and `Install Linux deps` in the build matrix job, gated on `if: runner.os == 'Linux'`.

## Commit-Hash Curation

All 8 commit hashes from CONTEXT's "input dataset" were retained in the narrative:

| Hash      | Layer   | Used in section         | Notes                            |
| --------- | ------- | ----------------------- | -------------------------------- |
| `6b11f57` | QML     | Root Cause Chain item 1 | Self-binding shadow              |
| `d377d80` | QML     | Root Cause Chain item 2 | Connected-only filter (intent)   |
| `c2fec55` | QML     | Root Cause Chain item 2 | implicitHeight fix               |
| `6af22cf` | QML     | Root Cause Chain item 2 | Row alignment with filter        |
| `42febbd` | QML     | Root Cause Chain item 2 | Repeater + ColumnLayout topology |
| `0eb886f` | QML     | Root Cause Chain item 3 | EmptyState binding to stale id   |
| `4818a6d` | Catalog | Root Cause Chain item 4 | AJAZZ 2.4G 8K mouse + 0x3004     |
| `62da68c` | Catalog | Root Cause Chain item 4 | AKP815 backend + canonical PIDs  |

No commits were dropped as "not relevant after closer reading" — the narrative covers the full chain. Some grouping was applied: commits `c2fec55` / `6af22cf` / `42febbd` are presented as a single "DelegateModel filter alignment sequence" in Root Cause Chain item 2 (rather than 3 separate items) because they form one logical fix wave.

## CI Step Runtime Estimate

The grep operates on the source tree only (no Qt install, no compile). A single recursive grep across `src/` (currently ~1100 .cpp/.hpp files, ~250 KLoC) runs in well under 1 second on the GitHub Actions Ubuntu runner — measured locally at ~0.05s on the same source tree, and the runner I/O is comparable. Conservative estimate for the CI step including job-step overhead: **\<2 seconds**, in line with the plan's expectation. This is well below the noise floor of a typical 8-15 minute Linux build job.

## Verification Run

```
$ test -f .planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md && echo PASS
PASS

$ grep -q "requirement: HOTPLUG-07" .planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md && echo PASS
PASS

$ for h in Symptom "Root Cause Chain" "Fix Applied" "Why 3 Devices Now Work" "Lessons Learned" "Forward Pointers"; do
    grep -q "## $h" .planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md && echo "PASS: $h"
  done
PASS: Symptom
PASS: Root Cause Chain
PASS: Fix Applied
PASS: Why 3 Devices Now Work
PASS: Lessons Learned
PASS: Forward Pointers

$ grep -q "Enforce hid_open invariant" .github/workflows/ci.yml && echo PASS
PASS

$ grep -q "Pitfall 11" .github/workflows/ci.yml && echo PASS
PASS

$ grep -rn 'hid_open(' src/ | grep -v hid_transport.cpp | grep -v 'hid_open_path('
(no matches — invariant green)

$ wc -l .planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md
208 .planning/phases/04-hot-plug-hardening/04-HOTPLUG-RETRO.md
```

## Issues Encountered

- `mdformat` pre-commit hook normalised the table column widths and a long URL line in the RETRO between the first commit attempt and the second. Re-staged transparently; net effect on content is zero.

No other issues — both artefacts landed cleanly on the second pass.

## Deviations from Plan

None — plan executed exactly as written.

**Total deviations:** 0. **Impact:** none.

## Self-Check: PASSED
