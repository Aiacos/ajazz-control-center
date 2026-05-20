---
phase: 09-research-captures-hygiene
plan: 06
subsystem: architecture
tags: [adr, arch-05, clock, settime, rtc, default-verdict, d-05, partial-scope, pitfall-19, three-witness, honesty-contract]

# Dependency graph
requires: []
provides:
  - .planning/phases/09-research-captures-hygiene/ARCH-05.md (ARCH-05 default-verdict ADR — per-device setTime outcome)
  - PROJECT.md Key Decisions row for ARCH-05
affects:
  - Phase 10 — AKP03 variant_3004 Promotion (DEVICES-05) — devices.yaml row for akp03_variant_3004 removes `clock` from `capabilities:` citing ARCH-05; maturity promotes `scaffolded` -> `functional`
  - Phase 12 — AK980 PRO Promotion (DEVICES-06) — devices.yaml row for ak980pro removes `clock` from `capabilities:` citing ARCH-05; maturity promotes `scaffolded` -> `partial`
  - Phase 13 — VERIFY-01 (Sync-button visibility on hasClock=false rows) + VERIFY-03 (exclamation glyph on NotImplemented rows) — real-hardware verification of the honesty contract this ADR architecturally establishes
  - Phase 9.x (deferred) — captures-confirmation finalization for ARCH-05 (captures from all 4 connected devices during native-app time-set UI actions per Pitfall 19 three-witness rule)

# Tech tracking
tech-stack:
  added: []
  patterns:
    - ADR shape mirrors v1.1 ARCH-01-parser-choice.md via ARCH-04 template (Status / Context / Default Verdict / Three-witness rule / Anti-feature / Acceptable alternative / Captures-confirmation trigger / Binding / Honesty contract / References)
    - Status field carries "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" per D-05 honesty contract — same shape as ARCH-04 sibling
    - Per-device decision table (akp03_variant_3004 / ak980pro / ajazz_24g_8k / microdia_dongle_7016) — decision is BINARY per device, not graduated, per ARCH-05 requirement text
    - Pitfall 19 three-witness rule framing (capture / round-trip / negative witnesses) is the architectural backbone — round-trip witness STRUCTURALLY unavailable for clock on AKP03 + ak980pro is the load-bearing argument
    - Anti-feature section explicitly forbids synthesizing a fake setSystemTimeOn from bytes that "look like time" (Pitfall 19 confirmation-bias trap)
    - Acceptable alternative section reroutes user-visible "clock" UX through `display` capability (host-rendered TftClockWidget) NOT through `IClockCapable::setTime`

key-files:
  created:
    - .planning/phases/09-research-captures-hygiene/ARCH-05.md
  modified:
    - .planning/PROJECT.md (Key Decisions table — ARCH-05 row added)
    - .planning/STATE.md (decisions + metrics + session info)
    - .planning/ROADMAP.md (Phase 9 progress 4/7 -> 5/7)
    - .planning/REQUIREMENTS.md (ARCH-05 marked complete in traceability table + active-list checkbox)

key-decisions:
  - Per-device default verdict — akp03_variant_3004 hasClock=false; ak980pro hasClock=false; ajazz_24g_8k N/A (doesn't advertise clock); microdia_dongle_7016 N/A (dongle, capabilities:[] at probed tier). IClockCapable::setTime stays NotImplemented across all 5 functional Stream-Dock backends.
  - Four-corpus convergence — mirajazz, opendeck-akp03, ajazz-sdk, TaxMachine AK820 Pro all independently show NO RTC opcode. HIGH confidence default verdict.
  - Pitfall 19 three-witness rule — round-trip witness STRUCTURALLY unavailable for clock on AKP03 + ak980pro (no firmware-rendered clock widget to read back from). Two of three witnesses unavailable; even a positive capture witness alone cannot satisfy promotion criteria.
  - Anti-feature explicitly forbidden — synthesizing a fake setSystemTimeOn from bytes that "look like time" (CRT/TIM/RTC mnemonics). v1.1 D-02 honesty contract reinforced.
  - Acceptable alternative — host-rendered TftClockWidget on AK980 PRO's 1.14" TFT routed through `display` capability (DISPLAY-05, v1.2.x deferred), NOT through IClockCapable::setTime. AKP03 clock-on-keyface is already host-rendered image via BAT opcode.
  - PROJECT.md Out-of-Scope row 68 ("Real IClockCapable::setTime wire formats - no AJAZZ device exposes a host-settable RTC") preserved unchanged.
  - D-05 honesty contract preserved — ADR explicitly labeled DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION) in title + frontmatter status + bold Status line.

patterns-established:
  - ARCH default-verdict ADR template now applied to TWO ADRs (ARCH-04 ARCH-05); ARCH-06 in 09-07 will be the third application. Shape provenance from v1.1 ARCH-01-parser-choice.md.
  - Per-device decision pattern for cross-device architectural verdicts — when a single capability has different outcomes per device, the ADR enumerates each connected device explicitly with reason + downstream binding (DEVICES-NN row demotion).
  - Honesty contract reinforcement via structural impossibility — when the three-witness rule cannot be satisfied due to physical/architectural reasons (no clock widget exists to read back from), the ADR documents the structural impossibility as load-bearing for the default verdict's strength.

requirements-completed: [ARCH-05]

# Metrics
duration: 3min
completed: 2026-05-15
---

> (NOTE 2026-05-20: this device — USB 0x3004 — was later firmware-confirmed to be an AKP05E, codename akp05e; see STATE.md.)


# Phase 9 Plan 06: ARCH-05 per-device setTime default-verdict ratification Summary

**Single-file ADR at `.planning/phases/09-research-captures-hygiene/ARCH-05.md` ratifying the per-device `IClockCapable::setTime` outcome at default verdict: NO RTC opcode in any of the four reference corpora (mirajazz + opendeck-akp03 + ajazz-sdk + TaxMachine AK820 Pro); `hasClock=false` on `akp03_variant_3004` and `ak980pro`; `setTime` stays `NotImplemented` across all functional Stream-Dock backends; PROJECT.md Out-of-Scope row preserved unchanged; D-05 honesty contract preserved via explicit "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" labeling in title + frontmatter `status` + bold Status line; v1.1 D-02 honesty contract reinforced.**

## Performance

- **Duration:** ~3 min
- **Started:** 2026-05-15T08:04:05Z
- **Completed:** 2026-05-15T08:06:59Z
- **Tasks:** 1 (`type="auto"`)
- **Files created:** 1
- **Files modified:** 1 (PROJECT.md Key Decisions row; further state/roadmap/requirements updates ship in the final-metadata commit)

## Accomplishments

- `.planning/phases/09-research-captures-hygiene/ARCH-05.md` lands as a 317-line ADR ratifying the per-device `setTime` outcome at default verdict.
- The ADR explicitly carries the "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" label in three load-bearing surface points: the title line, the `status` frontmatter field, and the bold Status line under the title. D-05 honesty contract is structurally enforced — any reader of the file in any form (frontmatter parse, raw read, rendered markdown) sees the conditional.
- The ADR enumerates the per-device decision explicitly: `akp03_variant_3004` (`hasClock=false`, demote in Phase 10 DEVICES-05); `ak980pro` (`hasClock=false`, demote in Phase 12 DEVICES-06); `ajazz_24g_8k` (N/A, never advertised clock, Phase 11 verifies absence); `microdia_dongle_7016` (N/A, dongle, `capabilities: []` at probed tier per ARCH-06).
- The Pitfall 19 three-witness rule is the architectural backbone of the verdict's strength: round-trip witness is STRUCTURALLY unavailable on AKP03 + ak980pro (no firmware-rendered clock widget to read back from). Two of three witnesses unavailable; even a positive capture witness alone cannot satisfy promotion criteria. This makes the default verdict robust against any future capture that "looks like time."
- The anti-feature section explicitly forbids synthesizing a fake `setSystemTimeOn` from bytes that "look like time" (CRT/TIM/RTC mnemonic candidates documented with their non-time interpretations: `TIMer` sleep timer, `LIG` brightness coincidence, `CONNECT` keep-alive padding, factory-test opcodes in pre-production firmware).
- The acceptable-alternative section reroutes the user-visible "clock" UX through the `display` capability (host-rendered `TftClockWidget` on AK980 PRO's 1.14" TFT via DISPLAY-05 cmd `0x72`), NOT through `IClockCapable::setTime`. This preserves the v1.1 D-02 honesty contract while still delivering a clock UX through the appropriate capability seam.
- PROJECT.md Key Decisions table has a new ARCH-05 row with the ⏳ "default verdict — pending capture confirmation" wording, surfacing the conditional in the project-level decision log alongside the existing ARCH-04 row.
- Phase 10 (DEVICES-05) + Phase 12 (DEVICES-06) + Phase 13 (VERIFY-01/03) bindings explicitly documented with the `notes:` line text that downstream plans will inherit verbatim.

## Task Commits

Single atomic commit for the ADR (mirrors the 09-05 pattern):

1. **Task 1** — `5410c2a` (`docs(arch-05): ratify per-device setTime outcome at default verdict (ARCH-05)`)

The PROJECT.md update + this SUMMARY + STATE/ROADMAP/REQUIREMENTS updates ship in the final-metadata commit per the plan's two-commit structure.

## Files Created/Modified

| File                                                       | Status   | Purpose                                                                                                                                                                                                                                                                                                                                                              |
| ---------------------------------------------------------- | -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `.planning/phases/09-research-captures-hygiene/ARCH-05.md` | created  | 317-line ADR — default verdict for per-device `IClockCapable::setTime` outcome; D-05 honesty contract preserved; per-device decision table; Pitfall 19 three-witness rule applied; anti-feature explicitly forbidden; host-rendered `TftClockWidget` documented as acceptable alternative; captures-confirmation trigger documented; Phase 10/12/13 binding section. |
| `.planning/PROJECT.md`                                     | modified | Key Decisions table — new 2026-05-15 row for ARCH-05 with ⏳ "default verdict — pending capture confirmation" wording; cites four-corpus convergence + Pitfall 19 structural-unsatisfiability of three-witness rule + anti-feature prohibition + acceptable alternative (host-rendered `TftClockWidget` via DISPLAY-05).                                             |

## Per-Device Decision Matrix

| Device                 | VID:PID       | `hasClock` (after ARCH-05) | `setTime` behavior | Phase    | REQ ID     | Reason                                                                                                                                                                                    |
| ---------------------- | ------------- | -------------------------- | ------------------ | -------- | ---------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `akp03_variant_3004`   | 0x0300:0x3004 | **false** (demoted)        | `NotImplemented`   | Phase 10 | DEVICES-05 | No RTC opcode in mirajazz/opendeck-akp03/ajazz-sdk; LCD clock-on-keyface is host-rendered image via `display` capability (`BAT` opcode), not firmware time.                               |
| `ak980pro`             | 0x0c45:0x8009 | **false** (demoted)        | `NotImplemented`   | Phase 12 | DEVICES-06 | TaxMachine AK820 Pro clean-room capture at same VID:PID has no RTC-set opcode. 1.14" TFT clock face is host-pushed image via cmd `0x72` (`IMAGE_COMMAND`, deferred to v1.2.x DISPLAY-05). |
| `ajazz_24g_8k`         | 0x3151:0x5007 | N/A (never advertised)     | N/A                | Phase 11 | (verify)   | Mouse; never advertised clock in v1.1 catalogue. Phase 11 probe-and-confirm verifies absence (no RTC opcode expected in any AJ-series corpus or vendor-protocol-notes finding).           |
| `microdia_dongle_7016` | 0x0c45:0x7016 | N/A (no capabilities)      | N/A                | Phase 13 | DEVICES-08 | Separate wireless dongle on different USB bus branch from `ak980pro` (per ARCH-06 default verdict). Enters catalogue at `probed` tier with `capabilities: []`; clock not in scope.        |

## Pitfall 19 Three-Witness Rule Application

| Witness    | Required for promotion                                      | Status for `clock` on AKP03 / ak980pro                                                                                                                                                                             |
| ---------- | ----------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Capture    | Vendor app sends byte sequence ONLY on time-set UI action   | Absent in all four reference corpora; Phase 9.x finalization expected to confirm absence in vendor-app captures during time-set UI actions.                                                                        |
| Round-trip | Device exhibits observable state change matching value sent | **STRUCTURALLY UNAVAILABLE** — no firmware-rendered clock widget to read back from on AKP03 (per `docs/protocols/streamdeck/akp03.md:113-114`); ak980pro 1.14" TFT clock is host-pushed image (TaxMachine corpus). |
| Negative   | Wrong value produces different observable state             | **CANNOT BE TESTED** — round-trip witness absent means there is nothing observable to differ between correct and wrong values.                                                                                     |

Two of three witnesses are STRUCTURALLY unavailable. The default verdict's strength comes from this structural impossibility, not just from empirical absence in the corpora.

## Confidence Rationale

**HIGH** — four-corpus convergence (one more witness than ARCH-04's three-way agreement):

- **`mirajazz`** v0.12.1 (Rust, AKP03 family) — documented opcodes: `DIS`/`LIG`/`LBLIG`/`SETLB`/`BAT`/`CLE`/`STP`/`HAN`/`CONNECT`. No RTC opcode.
- **`opendeck-akp03`** (Andrey V. Rust, AKP03 PID matrix) — no RTC opcode in any covered SKU; `0x3004` not in published matrix.
- **`ajazz-sdk`** (vendor partial SDK) — opcode table same shape; no RTC opcode.
- **TaxMachine `ajazz-keyboard-software-linux`** (clean-room AK820 Pro at same VID:PID as `ak980pro`) — captured command table includes `MODE_COMMAND` (cmd 0x13), `IMAGE_COMMAND` (cmd 0x72), `SLEEP_TIME` (cmd 0x17), START/FINISH envelope; NO `set_time`/`setRtc` entry.

All four independent reverse-engineering corpora converge on the no-RTC finding. This is one more witness than ARCH-04's three-way agreement, and the Pitfall 19 structural-impossibility argument adds an *architectural* reason on top of the empirical convergence.

## Captures-Confirmation Trigger (what would flip the verdict)

Documented in the ADR §"Captures-confirmation trigger". Phase 9.x finalization run requires captures from all 4 connected devices during native-app time-set UI actions. The default verdict flips only if all three Pitfall 19 witnesses hold simultaneously for any one device:

| Witness        | What it requires for flip                                                                                                                                                                                                |
| -------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Capture (+)    | Phase 9.x captures show a byte sequence sent by vendor app ONLY on time-set UI action.                                                                                                                                   |
| Round-trip (+) | Device exhibits verifiable state change. For AKP03: requires a firmware-rendered clock widget that has never been documented in any corpus. For ak980pro: requires a TFT clock face NOT dependent on host pixel uploads. |
| Negative (+)   | Deliberately wrong value produces different observable state.                                                                                                                                                            |

The realistic Phase 9.x outcome is the verdict stands. If the finalization run does flip the verdict on one device (vanishingly unlikely), a follow-up ADR (`ARCH-05.1`) ratifies the flip — never an in-place edit to this document, preserving the audit trail.

## Decisions Made

- **ADR shape mirrors ARCH-04** (`status: DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)`), which in turn mirrors v1.1 `ARCH-01-parser-choice.md`. Cross-link in §References documents the shape provenance. The default-verdict template is now applied to two ADRs in this phase (ARCH-04 + ARCH-05); ARCH-06 in 09-07 will be the third.
- **"DEFAULT VERDICT" label appears in three load-bearing places** (title + frontmatter `status` field + bold Status line under the title) — no honesty-corruption path can elide it. Any reader of the file in any form sees the conditional.
- **Per-device decision pattern established** — when a single capability has different outcomes per device, the ADR enumerates each connected device explicitly with reason + downstream binding (DEVICES-NN row demotion path). This is the canonical shape for any future cross-device architectural verdict.
- **Anti-feature section is explicit and load-bearing** — synthesizing a fake `setSystemTimeOn` is forbidden by name, with specific mnemonic candidates (`TIM`/`CRT`/`RTC`) documented as non-evidence (their plausible non-time interpretations are listed). PR review against this ADR is the human gate per the threat model.
- **Host-rendered alternative documented** — `TftClockWidget` on AK980 PRO's 1.14" TFT via DISPLAY-05 (cmd `0x72`, v1.2.x deferred). This is the acceptable canonical path for the user-visible "clock" UX, routed through `display` not `IClockCapable::setTime`.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] mdformat reformatted ADR + PROJECT.md on first commit attempt**

- **Found during:** Task 1 commit (first attempt)
- **Issue:** Pre-commit `mdformat` hook reformatted the YAML frontmatter on `ARCH-05.md` (removed double-quoting on simple-scalar values for `title:` / `status:` / `default_verdict:` / `finalization_gate:` / `binds:`; kept single-quoting on `confidence:` because it contains a colon) plus a minor markdown reflow (ordered-list numbering normalization from `1.` -> `1.`/`1.`/`1.` per mdformat's renumbering style). PROJECT.md was also reformatted minimally. The plan explicitly anticipated this case ("mdformat will likely reformat once. Re-stage and retry once if so.") — same shape as 09-05.
- **Fix:** Re-staged both files and retried the commit. Plan's `<verification>` grep counts all still pass on the reformatted output (DEFAULT VERDICT = 6, PENDING CAPTURE CONFIRMATION = 5, Pitfall 19 = 9, three-witness = 3, TftClockWidget|host-rendered = 7, DEVICES-05 = 7, DEVICES-06 = 6). No semantic change.
- **Files modified:** `.planning/phases/09-research-captures-hygiene/ARCH-05.md` + `.planning/PROJECT.md` (by mdformat hook).
- **Verification:** Second `git commit` invocation passed all hooks (gitleaks clean, mdformat clean, typos clean, conventional-commit clean, reject-raw-captures clean). Resulting commit `5410c2a`.
- **Committed in:** `5410c2a` (final state).

______________________________________________________________________

**Total deviations:** 1 auto-fixed (mdformat reformat, anticipated by the plan; same shape as 09-05).
**Impact on plan:** None. Plan explicitly anticipated this case; semantics unchanged.

## Issues Encountered

None substantive. The mdformat reformat above was the only hook activity that required a re-stage; all other hooks (gitleaks, conventional-commit, typos, reject-raw-captures) passed first-try.

## Verification Block (plan §verification)

| #   | Check                                                                                              | Result                                                                                                 |
| --- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| 1   | `grep -c "DEFAULT VERDICT" ARCH-05.md` >= 2                                                        | PASS (count = 6)                                                                                       |
| 2   | `grep -c "PENDING CAPTURE CONFIRMATION"` >= 2                                                      | PASS (count = 5)                                                                                       |
| 3   | `grep -q "Pitfall 19"` AND `grep -q "three-witness"` AND `grep -q "TftClockWidget\|host-rendered"` | PASS (counts: Pitfall 19 = 9; three-witness = 3; TftClockWidget                                        |
| 4   | `grep -q "hasClock=false"`                                                                         | PASS (count = 5)                                                                                       |
| 5   | `pre-commit run --files ARCH-05.md` exits 0                                                        | PASS (after one mdformat re-stage; final commit clean across all hooks)                                |
| 6   | `git log -1 --pretty=%B` contains `ARCH-05`                                                        | PASS (commit subject: `docs(arch-05): ratify per-device setTime outcome at default verdict (ARCH-05)`) |
| 7   | File line count >= 80                                                                              | PASS (317 lines)                                                                                       |
| 8   | Automated grep: `NO RTC opcode\|no RTC opcode`                                                     | PASS (count = 6)                                                                                       |
| 9   | Automated grep: `DEVICES-05\|DEVICES-06`                                                           | PASS (counts: DEVICES-05 = 7; DEVICES-06 = 6)                                                          |

## Threat Surface Scan

No new threat surface introduced by this plan — the deliverable is a doc-only ADR with no code change, no new endpoint, no auth path, no file-system access pattern, no schema change. The threat register entries from the plan (T-09-21 + T-09-22 + T-09-23) are mitigated as specified:

- **T-09-21 (Spoofing - future PR synthesizes fake `setTime` from byte pattern that "looks like time"):** Mitigated by the ADR's explicit anti-feature section (Pitfall 19 confirmation-bias trap documented with specific mnemonic candidates and their non-time interpretations); three-witness rule documented as the gate; v1.1 D-02 honesty contract reinforced.
- **T-09-22 (Repudiation - implementer ships `setTime` impl + flips `hasClock=true` without satisfying three witnesses):** Mitigated by the ADR documenting the structural impossibility of round-trip witness on AKP03 + ak980pro (no firmware-rendered clock widget to read back from); PR review against this ADR is the human gate.
- **T-09-23 (Information Disclosure - UI shows "Time synced" success toast when device firmware silently discarded bytes — v1.1 D-02 violation):** Mitigated by Phase 13 VERIFY-03 explicitly verifying the exclamation-glyph + no-toast behaviour; this ADR establishes the architectural ground truth that VERIFY-03 enforces visually.

## Threat Flags

No new threat surface introduced beyond the threat-model-documented entries above. Section omitted.

## Next Phase Readiness

- **Phase 10 (AKP03 variant_3004 Promotion) unblocked architecturally** — DEVICES-05 plan can now specify the exact `notes:` line text for `docs/_data/devices.yaml` against a citeable ADR. The plan MUST reference `.planning/phases/09-research-captures-hygiene/ARCH-05.md` in its `<context>` block and cite the conditional status.
- **Phase 12 (AK980 PRO Promotion) unblocked architecturally** — DEVICES-06 plan has the same shape as DEVICES-05 plus the additional binding to DISPLAY-05 (host-rendered `TftClockWidget` via cmd `0x72`, deferred to v1.2.x). The plan MUST reference this ADR and cite the conditional status.
- **Phase 13 (VERIFY-01/03) unblocked architecturally** — real-hardware visual verification of Sync-button visibility on `hasClock=false` rows + exclamation glyph on `NotImplemented` rows now has its architectural ground truth ratified.
- **Phase 10/12 execution gated** on Phase 9.x finalization run per D-05 honesty contract. STATE.md `pending_todos` continues to carry an ARCH-04 + ARCH-05 (+ pending ARCH-06) finalization entry so the gate is structurally visible to the planner.
- **No downstream blockers** for further Phase 9 partial-scope plans (09-07 ARCH-06) — same ADR shape established here applies.
- **No new C++ link-time dependency, no PyPI dependency, no runtime code change.** COD-031 boundary preserved (`grep -rn nlohmann src/core/include/` remains 0).

## Known Stubs

None. The ADR is a complete, ratifiable architectural decision document at default verdict; the conditionality is documented, not stubbed.

## Self-Check: PASSED

- `.planning/phases/09-research-captures-hygiene/ARCH-05.md` — FOUND (317 lines, all verification grep counts pass).
- `.planning/PROJECT.md` — modified (ARCH-05 row added to Key Decisions table; row format matches surrounding rows including ARCH-04 sibling).
- Commit `5410c2a` — FOUND in `git log` (`docs(arch-05): ratify per-device setTime outcome at default verdict (ARCH-05)`).

______________________________________________________________________

*Phase: 09-research-captures-hygiene*
*Completed: 2026-05-15*
