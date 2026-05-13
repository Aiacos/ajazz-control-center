# Project Retrospective

*A living document updated after each milestone. Lessons feed forward into future planning.*

## Milestone: v1.0 — milestone (retro-fit catalogue)

**Shipped:** 2026-05-13
**Phases:** 2 | **Plans:** 2 (retro) | **Sessions:** ~3 (review + remediation + close)

### What Was Built

- **SEC-003 plugin host wiring** — `OutOfProcessPluginHost` constructed and owned by `Application`, `LoadedPluginsModel` routed through the host, manifest signature verification gating plugin load, CMake exports for plugins headers.
- **`QML_SINGLETON` dual-instance sweep** — `qmlRegisterSingletonInstance` pattern uniformly applied across 6 affected services; `static_assert(!std::is_default_constructible_v<T>)` co-located with `QML_SINGLETON` macro at all 9 sites (6 sweep + 3 sibling) to trap regressions at build time.
- **Retroactive code review** — both phases reviewed via `gsd-code-review`; 8 fix commits + 1 deferral-doc commit landed on `main`; CR-01 (Win32 env pollution) explicitly deferred with documented fix path.

### What Worked

- **Retro-fit pattern is viable.** GSD was bootstrapped onto a brownfield repo *after* the work shipped, used purely for structured code review and audit. The two thematic clusters mapped cleanly onto phases without rewriting git history. The model: SUMMARY.md as the spec, REVIEW.md as the verification surrogate, no formal VERIFICATION.md needed.
- **`static_assert` co-location for invariants.** Phase 2's post-review enhancement (`static_assert(!std::is_default_constructible_v<T>)` next to `QML_SINGLETON`) converts a load-bearing comment into a build break. Generalizable pattern for any "you must use the factory, not the default ctor" rule.
- **Integration auditing on retro-fitted phases caught real value.** The `LoadedPluginsModel` touch-point between phases 1 and 2 (same C++ instance threaded through `setPluginHost` AND `registerInstance`) is exactly the kind of cross-phase wiring that retro-spec'd SUMMARY.md files gloss over but the integration checker traced explicitly.

### What Was Inefficient

- **No REQUIREMENTS.md / VERIFICATION.md for retro-fit.** Audit step 5 (3-source cross-reference) doesn't apply because two of the three sources don't exist. The audit had to fall back to Success Criteria from ROADMAP as the requirement set. This is fine for retro-fit, but worth a note: forward-planned milestones should not skip these.
- **SUMMARY.md drift on retro-fit.** Two SUMMARY claims didn't match shipped code (Phase 1 "PUBLIC" vs `PRIVATE`; Phase 2 `qmlRegisterSingletonInstance` vs `QML_SINGLETON` + factory). Both functionally equivalent but the SUMMARY is supposed to be authoritative. Cause: SUMMARY.md files were written from memory/commit messages, not from re-reading the code. Lesson below.
- **Audit path typo in workflow.** `complete-milestone.md` step 6 has `.planning/v{version}-v{version}-MILESTONE-AUDIT.md` (double version) but `offer_next` uses single. Caused some friction; fixable upstream.

### Patterns Established

- **Retro-fit phase template:** SUMMARY.md (one-liner + accomplishments + key files) + REVIEW.md (`gsd-code-review` output) + `*-FIX-DEFERRED.md` for documented deferrals. No CONTEXT/PLAN/VERIFICATION needed when the work has already shipped.
- **Audit status routing:** `tech_debt` (rather than `passed`) is the correct verdict when all success criteria pass but explicit deferrals are documented. Distinguishes "shipped clean" from "shipped with known follow-ups."
- **`git mv` for phase archival.** Preserves history when moving `.planning/phases/NN-name/` → `.planning/milestones/vX.Y-phases/NN-name/`; phase artifact provenance stays intact.

### Key Lessons

1. **Re-read code when writing retro-spec SUMMARY.md.** Memory and commit messages are not authoritative; only the code is. Two doc-drift items in v1.0 came from writing SUMMARY from the commit narrative rather than re-reading the final state of `src/app/CMakeLists.txt` and the singleton registration macros.
1. **Distinguish "deferred" from "broken" in audits.** CR-01 is flagged critical in REVIEW.md but it's documented, sized, and explicitly scheduled for the next Windows-touching milestone. That's `tech_debt`, not `gaps_found`. Audit rubric should reward documented deferral.
1. **Integration audits add value even on shipped code.** The cross-phase `LoadedPluginsModel` instance-sharing check was not obvious from either SUMMARY alone — it required reading `Application::bootstrap()` to verify ordering. Skipping integration audit because "phases already shipped to main" would have missed this.
1. **Brownfield + GSD retro-fit is a viable on-ramp.** You don't need to adopt GSD at the start of a project. Two phases + an audit + a milestone close turned an existing main branch into a GSD-tracked codebase without rewriting anything.

### Cost Observations

- Model mix: ~100% opus (Opus 4.7 1M-context) for the review + audit + close session.
- Sessions: ~3 total (initial retro-fit + code-review + close).
- Notable: `gsd-integration-checker` was the highest-value spawn — it's the only step that re-read live code rather than trusting `.planning/` artifacts. Future milestone closes should always spawn it even when all SUMMARY.md files claim "passed".

______________________________________________________________________

## Cross-Milestone Trends

### Process Evolution

| Milestone | Sessions | Phases    | Key Change                                                    |
| --------- | -------- | --------- | ------------------------------------------------------------- |
| v1.0      | ~3       | 2 (retro) | First GSD adoption on this repo; brownfield retro-fit pattern |

### Cumulative Quality

| Milestone | Tests                                                                 | Coverage           | Zero-Dep Additions       |
| --------- | --------------------------------------------------------------------- | ------------------ | ------------------------ |
| v1.0      | unit tests for `manifest_signer`, `branding_service`, `theme_service` | n/a (not measured) | 0 (no new external deps) |

### Top Lessons (Verified Across Milestones)

*(Pending future milestones to cross-validate. v1.0 lessons promoted here when re-verified.)*
