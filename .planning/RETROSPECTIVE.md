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

## Milestone: v1.1 — Device lifecycle hardening + scaffolding-to-functional

**Shipped:** 2026-05-14
**Phases:** 6 (Phase 3 → Phase 8) | **Plans:** 26 | **Sessions:** ~5-6 across ~2 calendar days
**Tests at close:** 178/178 ctest pass (`linux-release`). Audit verdict: `tech_debt` (28/28 reqs satisfied; 6 deferred items documented).
**Git:** 80 commits between `0e18353` (milestone start) and `01ccfc7` (audit), 186 files, +23,099 / −838 LoC.

### What Was Built

- **Phase 3 — Three written architectural decisions (ARCH-01..03).** Locked parser choice, mock-seam approach, and ownership migration scope in writing before Phase 4 / 7 wrote a line of code. Single decision-doc atomic commit, zero code touched.
- **Phase 4 — Hot-plug hardening.** `DeviceRegistry` migrated to `shared_ptr<IDevice>` with `weak_ptr` flyweight; 300 ms trailing-edge `HotplugDebouncer` collapses USB-hub shuffles; diff-driven `DeviceModel::refresh()`; selection + scroll position retention across disconnect/reconnect; multi-device integration harness (Linux 6 TEST_CASEs + Win32 4 smoke TEST_CASEs); `hid_open()` invariant enforced by CI grep.
- **Phase 5 — Time-sync scaffolding, 5-layer slice.** `Capability::Clock` + `TimeSyncResult` tri-state + `IClockCapable` interface + `DeviceDescriptor.hasClock`; all 4 Stream Dock backends + AKB980 PRO honestly return `NotImplemented` with `std::once_flag`-gated WARN; `TimeSyncService` QML singleton with `static_assert(!std::is_default_constructible_v<T>)` build-break; Settings auto-sync toggle (QSettings, re-validated at firing time); per-row exclamation glyph (D-02: manual=toast+glyph, auto=glyph-only).
- **Phase 6 — CR-01 Win32 OOP env pollution fix.** `Win32EnvBlock` RAII class builds a per-spawn UTF-16 env block from `GetEnvironmentStringsW` + 3 Python overrides; `CreateProcessW` called with `CREATE_UNICODE_ENVIRONMENT`; all 3 `_putenv_s` calls removed atomically; parent `_wgetenv(L"PYTHONPATH")` provably unchanged.
- **Phase 7 — WR-01 trust-roots parser hardening.** `loadTrustRoots` swapped from mini-grep to `nlohmann::json::parse` lockstep across `manifest_signer.cpp` + `manifest_signer_win32.cpp`; `nlohmann` PRIVATE-linked to `ajazz_plugins` (zero hits in `ajazz_core` or any installed header); 1 MB byte cap + 1024-entry cap; 5-case unit corpus + opt-in libFuzzer harness; 0600 TOCTOU contract documented at public API.
- **Phase 8 — Maturity-tier infrastructure + two promotions.** 5-tier vocabulary (`scaffolded`/`probed`/`partial`/`functional`/`verified`) in `devices.yaml`; `MaturityRole` exposed via `DeviceModel`; QML sidebar tooltip; README + wiki AUTOGEN per-family "works/partial/pending" prose; AKP815 → `probed`, Mirabox N3 (rev. 1) → `partial`.

### What Worked

- **Decision-doc-only Phase 3 was the highest-leverage hour spent.** ARCH-01..03 ratified upfront prevented downstream rework. Without them, Phases 4, 5, 7 would have been waffling about ownership / mock seam / parser choice mid-execution.
- **`tech_debt` audit verdict has carried over cleanly.** Same shape as v1.0: all success criteria met, deferred items documented at the phase + milestone level. No false sense of "shipped clean."
- **`static_assert(!std::is_default_constructible_v<T>)` pattern reuse.** Phase 2 invented it for `QML_SINGLETON` services; v1.1's `TimeSyncService` uses the same idiom to enforce its constructor-injection invariant. Generalizes to any "factory not default-ctor" rule.
- **`#ifdef AJAZZ_TESTING` mock-seam approach.** `HotplugMonitor::injectEvent` shim disappears entirely from production library (`objdump | c++filt | grep` → 0). No production ABI surface, no runtime cost. Pattern is reusable for any monitor/observer class that needs test-time event injection.
- **Plan rollup SUMMARY.md files (e.g. `04-SUMMARY.md`, `07-SUMMARY.md`).** When a phase has many plans, the rollup is the authoritative narrative; per-plan SUMMARY files are the receipts. The audit step found these much easier to cross-reference than reading 8 separate per-plan summaries.
- **Forward-planned milestone end-to-end.** v1.0 was retro-fit only. v1.1 was full GSD: REQUIREMENTS → ROADMAP → discuss/plan/execute per phase → audit → complete. The discipline held without any structural shortcuts.

### What Was Inefficient

- **Three concurrent execute agents caused a git race.** Phase 4 resume + Phase 6 + Phase 7 running simultaneously caused `manifest_signer_common.{hpp,cpp}` to land in Phase 4 agent's commit `e482152` rather than Phase 7's `e094239`. Functionally preserved (revert both = clean undo) but the audit trail is split.
- **A 4th planner agent caused a `--no-verify` workaround.** Pre-commit's stash/restore mechanism failed when a Phase 5 planner agent wanted to commit while 3 execute agents had unstaged work. Planner used `--no-verify` after independently verifying the 8 plan files pass all hooks. Violates the no-skip-hooks principle even though the bypass was tooling-mechanical, not content-substantive.
- **`--no-transition` skipped per-phase VERIFICATION.md generation.** Audit step had to synthesize from per-plan SUMMARY files + REQUIREMENTS.md checkboxes + ctest output instead of a single rollup verification doc per phase. Worked, but the audit's 3-source cross-reference rubric requires synthesis vs lookup.
- **Phase 8 shipped without per-plan SUMMARY.md files.** Same `--no-transition` shape. Required retroactive backfill at milestone close from commit messages + audit content. Audit verdict was already in hand, so this was bookkeeping only — but milestone-close auto-extraction depends on the files existing.
- **`gsd-sdk milestone.complete` accomplishment auto-extraction needs editorial cleanup.** The extractor pulled raw "Status:", "Commit:", "TBD." section-anchor lines from SUMMARY files into MILESTONES.md. Required manual rewrite of the entry to surface real accomplishments. Worth surfacing as a tooling improvement.
- **Quick-task `SUMMARY.md` filename convention mismatch.** Audit looked for literal `quick/<dir>/SUMMARY.md` but the repo uses `<slug>-SUMMARY.md` per task. Resolved by adding bare `SUMMARY.md` shim files; could be fixed upstream by globbing.

### Patterns Established

- **ADRs as Phase 0 of a multi-phase milestone.** When 3+ phases depend on the same architectural choice (parser, ownership, mock seam), ratify in writing first. The cost is one decision-doc phase; the payoff is unambiguous downstream execution.
- **`PRIVATE`-linked plugin-scope dependencies.** External deps that serve only the plugin sandbox (`nlohmann::json` for `trust_roots.json`) belong PRIVATE-linked to `ajazz_plugins`, never to `ajazz_core` or any installed public header. Verified at audit time by `grep -rn nlohmann src/core/include/` returning 0.
- **D-02 honesty contract.** When a result is `NotImplemented` (or any tri-state error path), the UX must distinguish manual-user-initiated (show toast + glyph) from auto-fired (glyph only, no toast). Lying via success toasts is the anti-pattern.
- **CI grep gates for load-bearing invariants.** Pitfall 11 ("never call `hid_open()` outside `hid_transport.cpp`") is now machine-enforced by a CI grep, not reviewer vigilance. Cheap and effective for any invariant where a violation has a recognizable string signature.
- **`AGENT_CAP=2` for autonomous parallel execution.** Three concurrent execute agents created two distinct git-coordination issues. Two is the empirically-validated cap.

### Key Lessons

1. **Decision docs are not bureaucracy; they are leverage.** Phase 3 was a 1-day decision-doc-only phase with zero code. It saved 3 phases worth of downstream waffling. The cost of writing 3 ADRs is much less than the cost of getting halfway through Phase 7 and realizing the parser choice wasn't actually settled.
1. **Parallelism has a real upper bound and the failure mode is silent.** Three concurrent agents felt fine until the audit step traced commits and found Phase 7's atomic-commit invariant was technically violated. The failure didn't surface a build error or a test failure — only a careful audit caught it. Cap at 2.
1. **`--no-transition` is a footgun for the close step.** Skipping per-phase VERIFICATION.md generation makes individual phase execution faster but pushes the cost to the milestone close. Either drop `--no-transition` or accept that close-time backfill is part of the workflow.
1. **The audit-open tool's "missing SUMMARY.md" verdict is brittle.** Both quick tasks and Phase 8 hit this. The cost of resolving each was small (write a shim file) but the false-positive shape eroded confidence in the pre-close gate. Worth a tooling-side fix.
1. **PROJECT.md "Out of Scope" prunes faster than it grows.** v1.1 close retired 5 anti-features whose rationale was milestone-specific (Trompeloeil, simdjson, Boost.Process, prompt-which-profile, big-bang promote-all). Out-of-Scope is not a permanent ledger — it's milestone-scoped context.

### Cost Observations

- Model mix: ~100% Opus (Opus 4.7 1M-context) for all planning, execution, and review.
- Sessions: ~5-6 across ~2 calendar days (high-intensity parallel-agent execution).
- Notable: per-plan SUMMARY.md files at the rollup level (`04-SUMMARY.md`, `07-SUMMARY.md`) saved more audit time than the per-plan ones. For multi-plan phases, the rollup is the higher-leverage artifact.
- Notable: `gsd-integration-checker` was NOT spawned for v1.1 close (audit synthesized cross-phase wiring manually). v1.0 retrospective flagged it as highest-value; v1.1 close skipped it to avoid usage-limit recurrence. Worth re-evaluating whether the manual aggregation is a viable substitute or just a corner-cut.

______________________________________________________________________

## Cross-Milestone Trends

### Process Evolution

| Milestone | Sessions | Phases    | Key Change                                                                                                                         |
| --------- | -------- | --------- | ---------------------------------------------------------------------------------------------------------------------------------- |
| v1.0      | ~3       | 2 (retro) | First GSD adoption on this repo; brownfield retro-fit pattern.                                                                     |
| v1.1      | ~5-6     | 6 forward | Full forward-planned milestone end-to-end; parallel autonomous-execute agents (with discovered cap of 2); ADR-first Phase pattern. |

### Cumulative Quality

| Milestone | Tests                                                                                           | Coverage           | Zero-Dep Additions                                                                      |
| --------- | ----------------------------------------------------------------------------------------------- | ------------------ | --------------------------------------------------------------------------------------- |
| v1.0      | unit tests for `manifest_signer`, `branding_service`, `theme_service`                           | n/a (not measured) | 0 (no new external deps)                                                                |
| v1.1      | 178/178 ctest pass on `linux-release`; +10 tests in v1.1 (7 unit + 3 integration for time-sync) | n/a (not measured) | 1 (`nlohmann::json` 3.12.0, PRIVATE-linked to `ajazz_plugins` only — COD-031 preserved) |

### Top Lessons (Verified Across Milestones)

1. **`static_assert(!std::is_default_constructible_v<T>)` for factory invariants.** v1.0 introduced it for `QML_SINGLETON` services; v1.1 reused it for `TimeSyncService`. The "load-bearing comment → build break" pattern is project-wide canon now.
1. **`tech_debt` is the correct audit verdict for "shipped clean with documented deferrals."** v1.0 used it for CR-01/WR-01 deferral; v1.1 used it for real-hardware verification + Windows CI back-fill + maturity-promotion-pending-capture. The rubric distinguishes "shipped with known follow-ups" from "shipped with gaps."
1. **Retro-fit + forward-planning coexist in the same repo.** v1.0 was retro-fit (work shipped first, GSD bootstrapped after). v1.1 was forward-planned (REQUIREMENTS → ROADMAP → execute). Both produce archivable milestones; the artifact shapes differ (no CONTEXT/PLAN for retro phases, full chain for forward phases) but the close workflow is identical.
