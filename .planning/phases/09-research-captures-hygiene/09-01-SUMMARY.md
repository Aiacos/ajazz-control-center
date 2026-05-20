---
phase: 09-research-captures-hygiene
plan: 01
subsystem: tooling
tags: [capture, hygiene, pre-commit, pitfall-17, privacy, capture-01, d-01]

requires: []
provides:
  - docs/policies/capture-data-hygiene.md — privacy rationale (Pitfall 17) + sanitised hex-fixture workflow (usbrply JSON → hex-to-cpparray.py)
  - scripts/reject-raw-captures.sh — pre-commit hook that rejects raw *.pcap / *.pcapng (case-insensitive) at any path + binaries >10 KB under the captures sink
  - .pre-commit-config.yaml — registers reject-raw-captures as a repo-local hook (always_run)
  - .planning/research/captures/ — scratch sink for raw pcap during a capture session; gitignored except README + .gitignore
affects: [09-02, 09-03, 09-04, 09-05, 09-06, 09-07]

tech-stack:
  added: []  # shell + pre-commit only; no new build deps
  patterns:
    - Commit-time rejection boundary (pre-commit repo-local hook) as the security gate, distinct from the lint-suppressing exclude block
    - Directory-level .gitignore that tracks only README + .gitignore, ignoring all other (binary) contents

key-files:
  created:
    - docs/policies/capture-data-hygiene.md
    - scripts/reject-raw-captures.sh
    - .planning/research/captures/.gitignore
    - .planning/research/captures/README.md
  modified:
    - .pre-commit-config.yaml

key-decisions:
  - 'D-01: CAPTURE-01 hygiene boundary lands FIRST in Phase 9 so all downstream capture activity (deferred to Phase 9.x) is gated by it.'
  - 'D-02: per-extension blocklist (*.pcap / *.pcapng, case-insensitive) + 10 KB size guardrail under .planning/research/captures/; rejection message points at the policy doc + scripts/hex-to-cpparray.py.'
  - Existing top-level exclude block (legacy docs/protocols/captures/.* + *.pcapng) preserved unmodified — those are lint suppressions, not commit rejections; the new hook is the authoritative rejection layer.

patterns-established:
  - 'Security-as-pre-commit-hook: a repo-local always_run hook is the enforcement point; the policy doc is the single source of truth the rejection message links to.'

requirements-completed: [CAPTURE-01]

duration: ~15min
completed: 2026-05-15
closed-out: 2026-05-20
---

# Phase 9 Plan 01: Capture-Data-Hygiene Boundary (CAPTURE-01) Summary

**The commit-time hygiene boundary that gates all v1.2 capture activity: a contributor (human or AI) physically cannot `git add` a raw `.pcap` / `.pcapng` or an oversized binary under the captures sink without the pre-commit hook rejecting it, with a message pointing at the sanitised-fixture workflow. Pure docs + shell-hook deliverable — no build, no hardware.**

## Closure note

The plan's three tasks were executed and committed as `772d13c`
(`feat(capture): reject raw pcap captures at commit time (CAPTURE-01)`,
2026-05-15) in a prior session, but the session ended before this
SUMMARY was written — leaving the plan flagged incomplete in GSD
tracking. Closed out during the `/gsd-autonomous` resume on 2026-05-20
after verifying all deliverables are present, committed, and the hook
behaves correctly. No re-execution was performed: the work was already
landed.

**Note on the plan's Task-3 automated verify** (`git log -1 --pretty=%B | grep CAPTURE-01`): this is a HEAD-relative assertion that passed
immediately after `772d13c` but no longer holds because later, unrelated
commits landed on top. The substantive verification (deliverables exist,
hook rejects/accepts correctly, capture commit present in history) all
pass — the stale HEAD check is a documentation artefact, not an
incompleteness.

## Deliverables (committed in `772d13c`, +348 lines)

| File                                     | Lines | Role                                                                                               |
| ---------------------------------------- | ----- | -------------------------------------------------------------------------------------------------- |
| `docs/policies/capture-data-hygiene.md`  | 171   | Privacy rationale (Pitfall 17) + forbidden set + sanitised workflow + INDEX schema + anti-features |
| `scripts/reject-raw-captures.sh`         | 92    | Hook script: extension blocklist + 10 KB sink size guardrail; ASCII-only policy-pointing rejection |
| `.planning/research/captures/.gitignore` | 6     | Catch-all `*` ignore, tracking only README + .gitignore                                            |
| `.planning/research/captures/README.md`  | 64    | Scratch-sink semantics + sanitised-fixture flow + cross-links                                      |
| `.pre-commit-config.yaml`                | +15   | Registers `reject-raw-captures` repo-local hook (always_run); legacy exclude preserved             |

## Verification matrix (re-confirmed 2026-05-20)

| #   | Check                                                               | Result                                                             |
| --- | ------------------------------------------------------------------- | ------------------------------------------------------------------ |
| 1   | `pre-commit run reject-raw-captures --all-files` on clean tree      | ✅ Passed                                                          |
| 2   | Hook against a fake `/tmp/test.pcap`                                | ✅ Rejected, message cites `docs/policies/capture-data-hygiene.md` |
| 3   | All 4 deliverable files present + committed (clean working tree)    | ✅                                                                 |
| 4   | CAPTURE-01 commit in history (`772d13c`)                            | ✅                                                                 |
| 5   | `git ls-files .planning/research/captures/` returns exactly 2 paths | ✅ (`.gitignore`, `README.md`)                                     |

## Scope boundary

This plan is the non-capture half of Phase 9 (`partial_scope: true`). It
closes ROADMAP success criterion D-01/CAPTURE-01. The remaining
criterion — sanitised capture fixtures for all 4 connected devices
(`akp03_variant_3004`, `ak980pro`, `ajazz_24g_8k`, `0c45_7016`) — requires
physical hardware + USB captures and is deferred to a Phase 9.x follow-up,
which consumes this hygiene boundary as a hard precondition.

## Cross-references

| Direction  | Target                                  | Purpose                                            |
| ---------- | --------------------------------------- | -------------------------------------------------- |
| out        | `docs/policies/capture-data-hygiene.md` | Policy authority the hook's message links to       |
| in         | `docs/protocols/CAPTURING.md` (09-02)   | Runbook built on top of this boundary              |
| in         | `scripts/hex-to-cpparray.py` (09-03)    | Conversion pipeline the policy workflow references |
| downstream | Phase 9.x (deferred)                    | Per-device capture fixtures gated by this boundary |
