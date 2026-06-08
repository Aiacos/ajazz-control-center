---
phase: 35-windows-plugin-support-security-hardening-milestone-verifica
verified: 2026-06-08T22:40:00Z
status: human_needed
score: 8/8 must-haves verified (automated portions); WINPLG-03 accepted partial (chip-only) per locked CONTEXT
overrides_applied: 0
human_verification:
  - test: WINPLG-03 live status-chip render + qml.get (VERIF-01 closing walk)
    expected: In a windowed GUI session, open the Loaded-plugins drawer with a WS-only-IPC win .sdPlugin (os=[windows], CodePath .js) and a vendor-DLL win .sdPlugin (CodePath .exe/.dll or MZ-magic file) installed. platformStatusChip.statusLabel reads 'Runs natively' for the WS-only plugin and 'Unsupported on this OS' for the vendor-DLL plugin (Wine absent). qml.get on platformStatusChip/unsignedConsentChip/trustChip returns the locked labels; a screenshot shows the chips rendered (not blank).
    why_human: The loadedPluginsDrawer is a modal Drawer/Popup opened via the navLoaded ToolButton onClicked — the documented CLAUDE.md harness gap means qml.invoke clicked / qml.invoke open do not fire for ToolButton/Popup, so the delegate chips are not realized headlessly. The CR-01 wiring (application.cpp:1120-1122) and the production population path are now in code + unit-covered (WR-02 test), but the live render needs a real plugin + a windowed click.
  - test: PLGSEC-02 consent restart round-trip (live UI)
    expected: Install an unsigned .sdPlugin and grant consent; plugin.list shows it loaded. Kill + relaunch the app; plugin.list shows it loaded again WITHOUT a re-prompt (consent persisted in QSettings plugins/allowed/<uuid>, survived the launch-sweep).
    why_human: Requires a real app restart in a windowed session. The persistence is already test-locked (test_plugin_install_from_file.cpp:942 launch-sweep survival, green); this is the live UI confirmation of the no-re-prompt behavior.
  - test: WINPLG-03 Wine launch of a VendorDll plugin (DEFERRED by locked CONTEXT — milestone-level)
    expected: A vendor-DLL win-only .sdPlugin actually launches under detected system Wine with per-plugin WINEPREFIX isolation.
    why_human: Explicitly DEFERRED this phase (chip-only) — no Wine launcher ships this milestone, no Wine/Windows hardware. The supportsCurrentPlatform VendorDll path is the documented WINPLG-03 deferral point (plugin_manifest.cpp:500). Not a gap; a future hardware-gated phase.
  - test: WR-04 (Phase 34) Win32 per-app-profile dedup — Windows host walk
    expected: Per-app profile event-parity behaves correctly on a real Windows host.
    why_human: Carried Windows-host deferral from Phase 34; no Windows hardware on the dev machine.
---

# Phase 35: Windows Plugin Support + Security Hardening + Milestone Verification — Verification Report

**Phase Goal:** Windows-only plugins are correctly classified and surfaced to the user; security invariants are CI-enforced; the milestone closes with a modularity audit confirming all VERIF-01/02 criteria.
**Verified:** 2026-06-08T22:40:00Z
**Status:** human_needed
**Re-verification:** No — initial verification

## Goal Achievement

This is the FINAL phase of the v2.0 milestone. The goal decomposes into three
strands — (1) Windows-plugin classification + surfacing, (2) CI-enforced security
invariants, (3) a milestone modularity audit (VERIF-01/02) — across 8 requirement
IDs. Every automated/codebase-verifiable portion is VERIFIED in the actual source
(not from SUMMARY claims). The remaining items are genuinely human/hardware-gated
(windowed-GUI chip render, app-restart, Wine launch) and route to human_verification.

### Observable Truths

| #   | Truth                                                                                                                                                                                                                   | Status   | Evidence                                                                                                                                                                                                                                                                                                                                           |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | A win-only .sdPlugin (CodePath .js, no PE) classifies as WsOnlyIpc; (.exe/.dll OR MZ-magic file) classifies as VendorDll                                                                                                | VERIFIED | `plugin_manifest.cpp:465-483` classifyWindowsPlugin: suffix primary (`.exe`/`.dll`→VendorDll :473), bounded MZ-magic corroborator (`:455-457` 0x4D 0x5A, 2-byte read, kMaxFilesScanned=4096 :438) overrides mislabeled manifest :479, else WsOnlyIpc :483. 13-case `[win-plugin-classification]` suite green (ctest WinPluginClassification PASS). |
| 2   | A WS-only-IPC win-only manifest (os=[windows]) passes the platform gate and runs natively on Linux; VendorDll stays strict-rejected; LOCKED linux-accept preserved; Software.MinimumVersion floor still applied (WR-01) | VERIFIED | `plugin_manager.cpp:305-310` native-run OR-override: \`baseRunnable                                                                                                                                                                                                                                                                                |
| 3   | PluginInfo carries the classification verdict so the model renders a chip without re-scanning; COD-031-safe                                                                                                             | VERIFIED | `i_plugin_host.hpp:89` `int winClass{0}` (plain int, no Qt/nlohmann). Stamped at scan time `plugin_manager.cpp:285` (classify in discover) + `:974` (`info.winClass = static_cast<int>(livePlugin.manifest.winClass)` in plugins()).                                                                                                               |
| 4   | The WINPLG-01 ADR documents the heuristic, the WS-only-runs-native decision, and the explicit Wine-launch deferral                                                                                                      | VERIFIED | 35-ADR-windows-plugin-classification.md §3.3 (WS-only runs native), §3.4 ("Wine launch — EXPLICITLY DEFERRED (WINPLG-03)", :103-106 with QStandardPaths/WINEPREFIX deferred + never-bundle-Wine).                                                                                                                                                  |
| 5   | LoadedPluginsModel exposes a platformStatus role derived from winClass; chip reads 'Runs natively'/'Requires Wine'/'Unsupported on this OS'                                                                             | VERIFIED | `loaded_plugins_model.cpp:90-91` data() case, `:107` roleNames entry, `:161-176` platformStatusOf (1→"native", 2→"unsupported" wine-false, 0→""); QML labels LoadedPluginsPage.qml:244-250. test_loaded_plugins_model.cpp cases green.                                                                                                             |
| 6   | Tampered plugin refused even with userConfirmedUnsigned=true (CR-01); tampered != unsigned in the verdict enum                                                                                                          | VERIFIED | `plugin_verify_gate.hpp:41-46` 4-way {Trusted,SelfSigned,Unsigned,Refused}; Refused→"tampered" :35. `plugin_catalog_model.cpp:907-913` Refused branch quarantines unconditionally. Tests :581, :854, :660 PASS.                                                                                                                                    |
| 7   | Per-plugin unsigned consent persists in QSettings plugins/allowed/<uuid> and survives the launch-sweep                                                                                                                  | VERIFIED | `plugin_catalog_model.cpp:644` setValue plugins/allowed/<uuid>, read by perPluginAllowed() :61-66, launch-sweep :173. test :942 "per-plugin allow survives launch-sweep with global toggle OFF" PASS.                                                                                                                                              |
| 8   | WS server binds QHostAddress::LocalHost; CI grep gate for QHostAddress::Any active + comment-aware; VERIF-02 gates all pass; mirajazz untouched; no akp05 revival                                                       | VERIFIED | `sd_plugin_server.cpp:93` listen(QHostAddress::LocalHost,port). `scripts/verif-milestone-gates.sh` → all 4 gates PASS exit 0 (run live). ci.yml:93-95 wires it + :70-74 Phase-30 gate retained. mirajazz crate edits all pre-Phase-35 (Slices).                                                                                                    |

**Score:** 8/8 truths verified (automated/codebase-verifiable portions). WINPLG-03
accepted partial (chip-only, Wine launch deferred) per locked CONTEXT.

### Required Artifacts

| Artifact                                              | Expected                                                                      | Status                      | Details                                                                                         |
| ----------------------------------------------------- | ----------------------------------------------------------------------------- | --------------------------- | ----------------------------------------------------------------------------------------------- |
| `src/app/src/plugin_manifest.{hpp,cpp}`               | WinPluginClass enum + classifyWindowsPlugin + supportsCurrentPlatform         | VERIFIED                    | enum :41, decls :268/:290, impls :465/:490; bounded 2-byte MZ scan + 4096 cap                   |
| `src/app/src/plugin_manager.cpp`                      | scan-time classify + native-run gate + winClass stamp                         | VERIFIED                    | :285 classify, :305-310 gate (WR-01 versionOk), :974 stamp                                      |
| `src/plugins/include/ajazz/plugins/i_plugin_host.hpp` | PluginInfo.winClass plain int (COD-031)                                       | VERIFIED                    | :89 `int winClass{0}`; 0 nlohmann #include, 0 Qt include                                        |
| `src/app/src/loaded_plugins_model.{hpp,cpp}`          | PlatformStatusRole + platformStatusOf + host2 wiring                          | VERIFIED                    | role :107, derive :161-176, setPluginHost2 :122, refresh prefers host2 :134-135                 |
| `src/app/qml/LoadedPluginsPage.qml`                   | 3 objectName'd chips + readable labels                                        | VERIFIED                    | trustChip :174, platformStatusChip :241, unsignedConsentChip :303; grep -c objectName = 5 (>=3) |
| `tests/unit/test_win_plugin_classification.cpp`       | classification + support-matrix cases                                         | VERIFIED                    | registered, 13 cases, ctest PASS                                                                |
| `tests/unit/test_loaded_plugins_model.cpp`            | derive cases + WR-02 production-path (FakeMergedHost2→setPluginHost2→refresh) | VERIFIED                    | :64 FakeMergedHost2, :97+ cases; ctest PASS                                                     |
| `scripts/verif-milestone-gates.sh`                    | 4 comment-aware VERIF-02 gates                                                | VERIFIED                    | runs, exit 0, all PASS                                                                          |
| `.github/workflows/ci.yml`                            | VERIF-02 gates CI-wired + Phase-30 gate retained                              | VERIFIED                    | :93-95 script step, :70-74 Phase-30 QHostAddress::Any gate intact                               |
| `docs/milestone-v2.0-modularity-audit.md`             | gate results + honest live-evidence reconciliation                            | VERIFIED (drift noted WR-A) | exists; §1 gates, §2 objectName, §3 PLGSEC, §4 per-phase 30-35 honest table                     |
| `35-ADR-windows-plugin-classification.md`             | WINPLG-01 decision + Wine deferral                                            | VERIFIED                    | §3.3 native, §3.4 deferral                                                                      |

### Key Link Verification

| From                             | To                                            | Via                                             | Status | Details                                                          |
| -------------------------------- | --------------------------------------------- | ----------------------------------------------- | ------ | ---------------------------------------------------------------- |
| plugin_manager.cpp               | classifyWindowsPlugin/supportsCurrentPlatform | scan-time gate (opt->sourceDir)                 | WIRED  | :285 + :308-309                                                  |
| plugin_manager.cpp               | PluginInfo.winClass                           | plugins() stamp                                 | WIRED  | :974                                                             |
| application.cpp                  | LoadedPluginsModel                            | setPluginHost2 + setPlugins(merged) — CR-01 fix | WIRED  | :1120-1122 (UnifiedPluginHost merged inventory) after spawn loop |
| loaded_plugins_model.cpp         | UnifiedPluginHost merged plugins              | refresh prefers m_host2                         | WIRED  | :134-135                                                         |
| LoadedPluginsPage.qml            | platformStatus role                           | delegate row.platformStatus binding             | WIRED  | :244, :251, :258                                                 |
| scripts/verif-milestone-gates.sh | ci.yml                                        | CI invokes script                               | WIRED  | ci.yml:95 `bash scripts/verif-milestone-gates.sh`                |

### Data-Flow Trace (Level 4)

| Artifact                          | Data Variable | Source                                                                                                        | Produces Real Data                                        | Status                                                      |
| --------------------------------- | ------------- | ------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- | ----------------------------------------------------------- |
| LoadedPluginsModel.platformStatus | info.winClass | PluginManager scan-time classify → manifest.winClass → plugins() stamp → UnifiedPluginHost merge → setPlugins | Yes (real classifier verdict, merged .sdPlugin inventory) | FLOWING (code+unit) — live windowed render deferred (human) |

The CR-01 fix closed the previously-dead data path: the model is now fed the
MERGED `.sdPlugin`+Python inventory (the only source carrying a nonzero winClass).
The WR-02 unit test exercises this exact production path (FakeMergedHost2 →
setPluginHost2 → refresh asserts a win-only row surfaces non-empty platformStatus
and the Python row remains present). The remaining gap to a *rendered* chip is the
modal-Drawer harness gap (human walk), not a data-flow break.

### Behavioral Spot-Checks

| Behavior                                      | Command                                                                               | Result                 | Status |
| --------------------------------------------- | ------------------------------------------------------------------------------------- | ---------------------- | ------ |
| WINPLG classification suite                   | `ctest -R WinPluginClassification`                                                    | included in 62/62 PASS | PASS   |
| platformStatus derive + WR-02 production path | `ctest -R "loaded-plugins\|LoadedPlugins"`                                            | PASS                   | PASS   |
| PLGSEC tamper/consent/loopback                | `ctest -R "PluginInstallFromFile\|PluginCatalog\|SdPluginServer\|plugin-verify-gate"` | 62/62 PASS             | PASS   |
| VERIF-02 modularity gates                     | `bash scripts/verif-milestone-gates.sh`                                               | exit 0, all 4 PASS     | PASS   |
| COD-031 strict                                | `grep -rn '#include.*nlohmann' src/core/include/`                                     | 0                      | PASS   |

### Probe Execution

| Probe                              | Command                                 | Result                                                                          | Status |
| ---------------------------------- | --------------------------------------- | ------------------------------------------------------------------------------- | ------ |
| `scripts/verif-milestone-gates.sh` | `bash scripts/verif-milestone-gates.sh` | mirajazz=0, nlohmann=0, akp-symbols=0, qhostaddress-any=0; RESULT: PASS; exit 0 | PASS   |

### Requirements Coverage

| Requirement | Source Plan | Description                                                                    | Status                           | Evidence                                                                                                    |
| ----------- | ----------- | ------------------------------------------------------------------------------ | -------------------------------- | ----------------------------------------------------------------------------------------------------------- |
| WINPLG-01   | 35-01       | Classify WS-only-IPC vs vendor-DLL + ADR                                       | SATISFIED                        | classifyWindowsPlugin + ADR (truths 1,4)                                                                    |
| WINPLG-02   | 35-01       | WS-only win plugins run natively, no Wine                                      | SATISFIED                        | native-run gate + WR-01 version floor (truth 2)                                                             |
| WINPLG-03   | 35-03       | Vendor-DLL chip + (full) Wine launch                                           | SATISFIED (partial — chip-only)  | platformStatus chip done (truth 5); Wine launch DEFERRED per locked CONTEXT + ADR §3.4 → human_verification |
| PLGSEC-01   | 35-02       | Tampered≠Unsigned; tampered refused even with consent                          | SATISFIED                        | verdict enum + CR-01 branch (truth 6)                                                                       |
| PLGSEC-02   | 35-02       | Consent persists in QSettings + survives launch-sweep                          | SATISFIED (live restart→human)   | :644 write + :942 test (truth 7); live UI restart deferred                                                  |
| PLGSEC-03   | 35-02       | WS loopback-only, CI-gated; no phone-home                                      | SATISFIED                        | LocalHost bind + CI gate + opt-in catalog (truth 8)                                                         |
| VERIF-01    | 35-03       | Every new v2.0 control objectName-addressable; honest per-phase reconciliation | SATISFIED (live chip walk→human) | trustChip gap closed (objectName count 5); audit §4 honest 30-35 table; live render deferred                |
| VERIF-02    | 35-02       | mirajazz unmodified; no akp05 revival; gates pass                              | SATISFIED                        | 4 gates PASS + CI-wired; mirajazz pre-Phase-35 only                                                         |

No orphaned requirements: all 8 IDs declared in plan frontmatter and all map to
Phase 35 in REQUIREMENTS.md (lines 126-134).

**Note on WINPLG-03 vs REQUIREMENTS.md:** REQUIREMENTS.md:57 describes the FULL
Wine launch (`QStandardPaths::findExecutable("wine")` + WINEPREFIX) and marks it
"Complete". The phase delivered WINPLG-03 as a documented PARTIAL — the
classification + status chip ("requires Wine"/"unsupported on this OS") are done;
the Wine launch run-path is explicitly deferred (ADR §3.4, audit §4 "Requirement
partials"). This is an accepted partial per the locked CONTEXT, not a silent miss.
The "Complete" marking in the requirements table slightly overstates the run-path;
the audit doc and ADR are the accurate source of truth on the deferral.

### Anti-Patterns Found

| File   | Line | Pattern                                          | Severity | Impact                                                                                                                                                   |
| ------ | ---- | ------------------------------------------------ | -------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| (none) | —    | No TBD/FIXME/XXX in any phase-35 modified source | —        | Clean. The 1 akp05 + 7 nlohmann naive grep hits are all DOC COMMENTS (verified) and are excluded by the comment-aware gate; COD-031 strict #include = 0. |

### Documentation Drift (Warning — informational, not a gap)

**WR-A: Audit doc + 35-03-SUMMARY frontmatter carry the pre-CR-01-fix narrative.**
`docs/milestone-v2.0-modularity-audit.md` §2 (lines 102-113) and §4, plus the
35-03-SUMMARY "Threat Flags"/"Deviations" sections, state that LoadedPluginsModel
"is never wired to .sdPlugin inventory" / the chip data source "is not wired."
That was true when those docs were authored (commit dd55ad7) but was FIXED
afterward by the code-review follow-up commit `12acd2f` ("wire LoadedPluginsModel
to UnifiedPluginHost so .sdPlugin plugins surface (CR-01)") — confirmed live in
`application.cpp:1120-1122` + `loaded_plugins_model.cpp:122-137`. The code is
correct; only the prose lags. Recommend a one-line refresh of audit §2 to reflect
that CR-01 is resolved (the wiring is in place; only the windowed render walk
remains deferred). This does NOT block the goal — the deliverable's substantive
claims (objectName coverage, gate results, honest per-phase reconciliation) hold.

### Human Verification Required

See frontmatter `human_verification`. Four items, all genuinely
human/hardware-gated (not gaps), per the locked CONTEXT:

1. **WINPLG-03 live status-chip render + qml.get** — CR-01 wiring is in code +
   WR-02 unit-covered; the live render needs a windowed click (modal-Drawer
   harness gap) + a real win-only .sdPlugin.
1. **PLGSEC-02 consent restart round-trip** — persistence is test-locked (:942);
   live no-re-prompt confirmation needs a real app restart.
1. **WINPLG-03 Wine launch** — DEFERRED by locked CONTEXT (no Wine launcher this
   milestone; no Wine/Windows hardware).
1. **WR-04 (Phase 34) Win32 dedup** — Windows-host walk, no Windows hardware.

### Gaps Summary

No blocking gaps. All 8 requirement IDs have real, grep-verified code in the
actual codebase plus automated coverage for their non-gated portions:

- WINPLG-01/02: classifier + native-run gate (incl. WR-01 version-floor fix) —
  verified in source, 13-case suite + plugin-manager tests green.
- WINPLG-03: chip + derive done (accepted partial — Wine launch deferred per
  locked CONTEXT); CR-01 wiring (the no-op-chip review finding) is FIXED in code
  and WR-02-unit-covered.
- PLGSEC-01/02/03: verify-only, confirmed unchanged at cited file:line, 62/62
  security tests green, no security source drift.
- VERIF-01: trustChip objectName gap closed (count 5 ≥ 3); honest per-phase audit.
- VERIF-02: 4 comment-aware gates PASS + CI-wired; mirajazz crate untouched in
  Phase 35; no akp05 wire-symbol revival; COD-031 strict clean.

One informational documentation-drift WARNING (WR-A): the audit doc/SUMMARY prose
predates the CR-01 fix and should be refreshed, but the code is correct.

### Milestone-Level Readiness (FINAL phase)

Phase 35 closes v2.0. The milestone modularity/security boundary (VERIF-01/02) is
CI-enforced and documented. The honest per-phase reconciliation (audit §4) cleanly
catalogues the deferred HUMAN-UAT walks for Phases 32/33/34/35 — these are tracked
in their respective `*-HUMAN-UAT.md` files and are live/hardware-gated, NOT
regressions. The milestone is ready for the lifecycle audit, with the windowed-GUI
and Wine/Windows-hardware walks (above) as the outstanding human-confirmation
backlog and the one-line audit-doc prose refresh (WR-A) as a documentation tidy-up.

______________________________________________________________________

_Verified: 2026-06-08T22:40:00Z_
_Verifier: Claude (gsd-verifier)_
