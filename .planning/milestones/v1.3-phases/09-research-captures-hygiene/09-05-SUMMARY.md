---
phase: 09-research-captures-hygiene
plan: 05
subsystem: architecture
tags: [adr, arch-04, image-pipeline, akp03, qt6, qimage, jpeg, default-verdict, d-05, partial-scope]

# Dependency graph
requires: []
provides:
  - .planning/phases/09-research-captures-hygiene/ARCH-04.md (ARCH-04 default-verdict ADR — image-pipeline location)
  - PROJECT.md Key Decisions row for ARCH-04
affects:
  - Phase 10 — AKP03 variant_3004 Promotion (DISPLAY-01/02/03) — binds to `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` location; gates on Phase 9.x finalization run
  - Phase 9.x (deferred) — captures-confirmation finalization for ARCH-04 (real `0x0300:0x3004` image-upload capture per Pitfall 22)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - ADR shape mirrors v1.1 ARCH-01-parser-choice.md (Status / Context / Decision / Rationale / Alternatives / References)
    - Status field distinguishes "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" from v1.1 "Locked" — D-05 honesty contract preserved
    - Captures-confirmation trigger section documents the three Pitfall 22 outcomes (verdict stands / parameter delta / structural flip)

key-files:
  created:
    - .planning/phases/09-research-captures-hygiene/ARCH-04.md
  modified:
    - .planning/PROJECT.md (Key Decisions table — ARCH-04 row added)

key-decisions:
  - Default verdict: Option C — Qt6 QImage::scaled + QImageWriter JPEG host-side in src/devices/streamdeck/src/image_pipeline.{hpp,cpp}, PRIVATE-linked to ajazz_devices_streamdeck
  - Option A (inline in Akp03Device::setKeyImage) rejected — couples device class to encoding mechanics; akp03.cpp already 536 LoC; fails three-witness rule for image-pipeline correctness in isolation
  - Option B (new ajazz_imaging static lib) deferred to v1.3+ — YAGNI for one consumer in v1.2; lift mechanically when AKP815 800x480 strip-image upload or AK980 PRO TFT image upload (DISPLAY-05) lands
  - D-05 honesty contract preserved — ADR explicitly labeled DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION) in title + frontmatter + Status line; Phase 10 plans MUST cite the conditional
  - Finalization gate documented — Phase 9.x captures run confirms 1024-byte chunks + 60x60 JPEG Rot0 + last-chunk Transfer-Done 0x01 flag (Pitfall 22 confirmation matrix)

patterns-established:
  - Phase-9-partial-scope ADR shape — "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" status + captures-confirmation trigger section is the template for ARCH-05 + ARCH-06 in this phase
  - PROJECT.md Key Decisions row uses ⏳ glyph + "default verdict — pending capture confirmation" wording to surface the conditional outside the ADR file itself
  - Cross-link from ADR to v1.1 ARCH-01 template for shape provenance

requirements-completed: [ARCH-04]

# Metrics
duration: 3min
completed: 2026-05-15
---

# Phase 9 Plan 05: ARCH-04 image-pipeline location default-verdict ratification Summary

**Single-file ADR at `.planning/phases/09-research-captures-hygiene/ARCH-04.md` ratifying the AKP03 image-encoding pipeline at default verdict: Option C (Qt6 `QImage::scaled` + `QImageWriter` JPEG host-side in `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`, PRIVATE-linked to `ajazz_devices_streamdeck`); Option B (new `ajazz_imaging` static lib) deferred to v1.3+; D-05 honesty contract preserved via explicit "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" labeling in title + frontmatter + Status line.**

## Performance

- **Duration:** ~3 min
- **Started:** 2026-05-15T07:54:35Z
- **Completed:** 2026-05-15T07:57:05Z
- **Tasks:** 1 (`type="auto"`)
- **Files created:** 1
- **Files modified:** 1

## Accomplishments

- `.planning/phases/09-research-captures-hygiene/ARCH-04.md` lands as a 235-line ADR ratifying the image-pipeline location at default verdict.
- The ADR explicitly carries the "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" label in three load-bearing places: the title line, the `status` frontmatter field, and the bold Status line. D-05 honesty contract is structurally enforced — any reader of the file in any form (frontmatter parse, raw read, rendered markdown) sees the conditional.
- The ADR documents the three Pitfall 22 outcomes for the deferred captures-confirmation run: (1) verdict stands as written, (2) parameter delta only (descriptor table absorbs it; Option C still wins), (3) structural firmware-side encode delta requiring a follow-up ADR (vanishingly low probability per OSS-corpus convergence).
- Option A (inline in `Akp03Device::setKeyImage`) and Option B (new `ajazz_imaging` static lib) are documented with rejection / deferral rationale, including the explicit promotion path for Option B when a second consumer (AKP815 or AK980 PRO TFT) materialises.
- PROJECT.md Key Decisions table has a new ARCH-04 row with the ⏳ "default verdict — pending capture confirmation" wording, so the conditional surfaces in the project-level decision log without requiring a reader to open the ADR file.
- Phase 10 binding section in the ADR specifies the exact path (`src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`), CMake target wiring (PRIVATE-link to `ajazz_devices_streamdeck`), and an additional include-leak grep gate (`grep -rn QImage src/devices/streamdeck/include/` must return 0) — Phase 10 plans now have concrete, citeable architectural constraints.

## Task Commits

Single atomic commit for the ADR (Phase 10 binding work is doc-only; no code touched):

1. **Task 1** — `60f3140` (`docs(arch-04): ratify AKP03 image-pipeline location at default verdict (ARCH-04)`)

The PROJECT.md update + this SUMMARY + STATE/ROADMAP/REQUIREMENTS updates ship in the final-metadata commit per the plan's atomic-commit structure (mirrors 09-03 pattern).

## Files Created/Modified

| File                                                       | Status   | Purpose                                                                                                                                                                                                                             |
| ---------------------------------------------------------- | -------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `.planning/phases/09-research-captures-hygiene/ARCH-04.md` | created  | 235-line ADR — default verdict for AKP03 image-pipeline location (Option C); D-05 honesty contract preserved; finalization gate documented; Phase 10 binding section specifies exact paths + CMake wiring + include-leak grep gate. |
| `.planning/PROJECT.md`                                     | modified | Key Decisions table — new 2026-05-15 row for ARCH-04 with ⏳ "default verdict — pending capture confirmation" wording.                                                                                                              |

## Three Options Considered

| Option                                                                        | Decision          | Why                                                                                                                                                                                                                                                                  |
| ----------------------------------------------------------------------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **A** — inline in `Akp03Device::setKeyImage` in `akp03.cpp`                   | REJECTED          | Couples device class to encoding mechanics; `akp03.cpp` already 536 LoC; unit-testing in isolation requires friend-class or test-only public method (anti-patterns); fails three-witness rule for image-pipeline correctness.                                        |
| **B** — new `ajazz_imaging` static library                                    | DEFERRED          | YAGNI for v1.2 — single consumer (`ajazz_devices_streamdeck`); a static library with one client is just a translation unit with extra CMake ceremony. Promote when AKP815 800x480 strip-image upload or AK980 PRO TFT (DISPLAY-05, deferred to v1.2.x) materialises. |
| **C** — `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`, PRIVATE-linked | **WIN (DEFAULT)** | Smallest blast radius; no new dep (Qt6::Gui already on PRIVATE link line); pure-function test seam aligns with MockTransport architecture from 09-04; preserves COD-031 boundary + adds analogous `QImage` include-leak grep gate.                                   |

## Confidence Rationale

**HIGH** — three-way OSS-corpus agreement per `.planning/research/SUMMARY.md` §ARCH-04:

- **mirajazz** crate (Rust) — host-side JPEG encode, 1024-byte chunked HID writes.
- **opendeck-akp03** — host-side JPEG encode, same chunking shape.
- **ajazz-sdk** family — host-side JPEG encode, same chunking shape.

All three independent reverse-engineering corpora encode JPEG host-side; device firmware accepts raw JPEG bytes chunked at the HID layer with no re-encoding. This is a three-witness rule pass on the *encoding side* of the pipeline; the *transport side* (chunk size + Transfer-Done flag) is a separate three-witness target for the Phase 9.x captures-confirmation run.

## Captures-Confirmation Trigger (what would flip the verdict)

Documented in the ADR §"Captures-confirmation trigger". Phase 9.x finalization run requires one captured AKP03 variant_3004 image-upload sequence through the BAT opcode. Three outcomes:

| Outcome                                                                                            | Action                                                                                                                                                                                                                                           |
| -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **1.** Confirms 1024-byte chunks + 60x60 JPEG Rot0                                                 | Verdict stands. Promote ARCH-04 from "DEFAULT VERDICT" to "Locked"; Phase 10 plans become non-conditional.                                                                                                                                       |
| **2.** Chunk size != 1024 *or* image format != 60x60 JPEG Rot0 (realistic Pitfall 22 outcome)      | Option C still wins (location unchanged). `Akp03Descriptor` parameters change in the descriptor table; update `docs/_data/devices.yaml` + `docs/protocols/streamdeck/akp03.md`; update this ADR's `default_verdict` field + promote to "Locked". |
| **3.** Firmware-side encode delta requiring host-side manipulation beyond Qt6::QImageWriter (≈ 0%) | File ARCH-04.1 follow-up reconsidering Option B early. Theoretical only — contradicts three independent OSS implementations.                                                                                                                     |

The first two outcomes are the realistic scenarios. The third is theoretical and contradicts the existing OSS evidence base.

## Decisions Made

- **ADR shape mirrors v1.1 `ARCH-01-parser-choice.md`** (Status / Context / Decision / Rationale / Alternatives / References), with the `status` frontmatter field carrying the difference: `"DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)"` vs v1.1's `"Locked"`. Cross-link to the v1.1 template documents the provenance.
- **"DEFAULT VERDICT" label appears in three load-bearing places** (title + frontmatter `status` field + bold Status line under the title) so no honesty-corruption path can elide it — any reader of the file in any form (frontmatter parser, raw read, rendered markdown) sees the conditional.
- **Include-leak grep gate added in Phase 10 Binding section**: `grep -rn QImage src/devices/streamdeck/include/` should return 0. This is analogous to the existing COD-031 nlohmann gate and protects the `Qt6::Gui` dependency from leaking out of the PRIVATE link line.
- **Option B promotion path documented explicitly** — function signatures are designed today (free functions + descriptor-table parameterisation) to make the future lift to `src/imaging/` mechanical when a second consumer materialises. Reduces v1.3+ migration cost without paying any v1.2 cost.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] mdformat reformatted ADR on first commit attempt**

- **Found during:** Task 1 commit
- **Issue:** Pre-commit `mdformat` hook reformatted YAML frontmatter (removed quoting on simple-scalar values; switched from double-quoted to bare for `title:` / `status:` / `default_verdict:` / `finalization_gate:` / `binds:`; kept single-quoted on `confidence:` because it contains a colon) plus minor markdown reflow (paragraph wrapping consistency). The plan explicitly notes: "mdformat will likely reformat once. Re-stage and retry once if mdformat or typos rewrites the file."
- **Fix:** Re-staged the reformatted file and retried the commit. Plan's `<verification>` grep counts all still pass on the reformatted output (DEFAULT VERDICT = 6, PENDING CAPTURE CONFIRMATION = 3, image_pipeline = 7, Option C = 6, Pitfall 22 = 5, Phase 10 = 13). No semantic change.
- **Files modified:** `.planning/phases/09-research-captures-hygiene/ARCH-04.md` (by mdformat hook).
- **Verification:** Second `git commit` invocation passed all hooks (mdformat clean, typos clean, conventional-commit clean, reject-raw-captures clean). Resulting commit `60f3140`.
- **Committed in:** `60f3140` (final state).

______________________________________________________________________

**Total deviations:** 1 auto-fixed (mdformat reformat, anticipated by the plan).
**Impact on plan:** None. Plan explicitly anticipated this case; semantics unchanged.

## Issues Encountered

None substantive. The mdformat reformat above was the only hook activity that required a re-stage; all other hooks (gitleaks, conventional-commit, typos, reject-raw-captures) passed first-try.

## Verification Block (plan §verification)

| #   | Check                                                                                          | Result                                                                                                            |
| --- | ---------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| 1   | `grep -c "DEFAULT VERDICT" ARCH-04.md` >= 2                                                    | PASS (count = 6)                                                                                                  |
| 2   | `grep -c "PENDING CAPTURE CONFIRMATION"` >= 2                                                  | PASS (count = 3)                                                                                                  |
| 3   | `grep -q "image_pipeline" && grep -q "Option C" && grep -q "Pitfall 22" && grep -q "Phase 10"` | PASS (all four present; counts 7 / 6 / 5 / 13)                                                                    |
| 4   | `pre-commit run --files ARCH-04.md` exits 0                                                    | PASS (after one mdformat re-stage; final commit clean across all hooks)                                           |
| 5   | `git log -1 --pretty=%B` contains `ARCH-04`                                                    | PASS (commit message subject: `docs(arch-04): ratify AKP03 image-pipeline location at default verdict (ARCH-04)`) |
| 6   | File line count >= 80                                                                          | PASS (235 lines)                                                                                                  |

## Threat Surface Scan

No new threat surface introduced by this plan — the deliverable is a doc-only ADR with no code change, no new endpoint, no auth path, no file-system access pattern, no schema change. The threat register entries from the plan (T-09-19 + T-09-20) are mitigated as specified:

- **T-09-19 (Repudiation - Phase 10 implementer inherits verdict as final):** Mitigated by "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" appearing in title + frontmatter `status` + bold Status line under the title. Three load-bearing surface points; no honesty-corruption path elides them all.
- **T-09-20 (Spoofing - silent verdict flip without captures run):** Mitigated by the explicit "Captures-confirmation trigger" section documenting the three Pitfall 22 outcomes and the requirement for a follow-up ADR (ARCH-04.1) for outcome 3. The default-verdict status field is the canonical reference; any verdict change requires an explicit follow-up ADR rather than an in-place edit.

## Next Phase Readiness

- **Phase 10 (AKP03 variant_3004 Promotion) unblocked architecturally** — DISPLAY-01/02/03 plans can now specify file paths, CMake target wiring, and unit-test fixture locations against a citeable ADR. Plans MUST reference `.planning/phases/09-research-captures-hygiene/ARCH-04.md` in their `<context>` block and cite the conditional status.
- **Phase 10 execution gated** on Phase 9.x finalization run per D-05 honesty contract. STATE.md `pending_todos` should carry an ARCH-04 finalization entry so the gate is structurally visible to the planner.
- **No downstream blockers** for further Phase 9 partial-scope plans (09-06 ARCH-05, 09-07 ARCH-06) — those follow the same ADR shape established here.
- **No new C++ link-time dependency, no PyPI dependency, no runtime code change.** COD-031 boundary preserved (`grep -rn nlohmann src/core/include/` remains 0).

## Known Stubs

None. The ADR is a complete, ratifiable architectural decision document at default verdict; the conditionality is documented, not stubbed.

## Self-Check: PASSED

- `.planning/phases/09-research-captures-hygiene/ARCH-04.md` — FOUND (235 lines, all verification grep counts pass).
- `.planning/PROJECT.md` — modified (ARCH-04 row added to Key Decisions table; row format matches surrounding rows).
- Commit `60f3140` — FOUND in `git log` (`docs(arch-04): ratify AKP03 image-pipeline location at default verdict (ARCH-04)`).

______________________________________________________________________

*Phase: 09-research-captures-hygiene*
*Completed: 2026-05-15*
