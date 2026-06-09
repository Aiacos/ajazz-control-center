# Phase 35: Windows Plugin Support + Security Hardening + Milestone Verification - Context

**Gathered:** 2026-06-08
**Status:** Ready for planning

<domain>
## Phase Boundary

The FINAL v2.0 phase. Three themes:

1. **Windows-only plugin support (WINPLG):** classify win-only `.sdPlugin` plugins into
   **WS-only-IPC** (runnable natively cross-platform) vs **vendor-DLL** (needs Wine); run WS-only
   win plugins natively on Linux/macOS via the existing host; surface vendor-DLL plugins with a
   status chip. Wine LAUNCH is **deferred** this phase (chip-only stub — see decisions).
1. **Plugin security hardening (PLGSEC):** split the verify-gate into Tampered (hard-refused even
   with consent) vs Unsigned (per-plugin consent); persist consent in QSettings (registry-safe);
   keep the plugin WebSocket server loopback-only (CI grep-gated); catalog never phones home.
1. **Verification & modularity gate (VERIF):** the milestone modularity audit — CI/scripted grep
   gates (no mirajazz in core/bridge, no nlohmann in core headers, no revived akp05 backends, no
   QHostAddress::Any), objectName-coverage over new v2.0 QML, and an honest audit doc; then the
   milestone lifecycle (gsd-audit-milestone → complete → cleanup) runs as the final gate.

Satisfies WINPLG-01/02/03 (03 partial — chip-only), PLGSEC-01/02/03, VERIF-01/02. Closes v2.0.

</domain>

<decisions>
## Implementation Decisions

### Windows plugin classification + status chips (WINPLG-01/02)

- **Classification heuristic:** inspect the manifest `CodePath`/`CodePathWin` suffix (.exe/.dll)
  AND scan the bundle for PE binaries (magic bytes `MZ`/`PE\0\0`) — robust against a mislabeled
  manifest. Produces a per-plugin classification: WS-only-IPC vs vendor-DLL.
- **When:** classification runs at install/scan time and is cached on the plugin model (not lazily
  at launch).
- **WS-only win plugins run natively** on Linux/macOS: a `supportsCurrentPlatform()` helper +
  manifest OS-filter accept WS-only-IPC plugins regardless of declared OS — no Wine.
- **Chip states (objectName-addressed):** "Runs natively" (WS-only) / "Requires Wine" (vendor-DLL,
  Wine present) / "Unsupported on this OS" (vendor-DLL, Wine absent). All `objectName` + reachable
  via `scripts/ajazz-debug qml.get` (VERIF-01).
- Document the classification + heuristic ADR-style (WINPLG-01 is a feasibility spike → produce the
  classification decision doc before/with the implementation).

### Wine integration depth (WINPLG-03) — CHIP-ONLY STUB THIS PHASE

- **Scope reduced (user decision):** implement vendor-DLL **detection + the status chips ONLY** this
  phase. The actual Wine launch wiring (system-Wine detection via
  `QStandardPaths::findExecutable("wine")`, per-plugin `WINEPREFIX` isolation, spawning the vendor
  DLL host under Wine) is **DEFERRED** to a later Windows/Wine-host session — there is no Wine or
  Windows hardware here to live-test a launch path, and building untestable launch code is rejected.
- WINPLG-03 therefore lands **partial**: the classification + chip + "this would need Wine" UX is
  done and live-verifiable; the launch path is explicitly a documented follow-up (HUMAN-UAT / future
  phase). Record this partial status honestly in the SUMMARY + parity/audit doc.
- **Never bundle Wine** (locked anti-decision) — detection-only when the launch path is eventually
  built.

### Security hardening (PLGSEC-01/02/03)

- **Tampered vs Unsigned (PLGSEC-01):** split `VerifyVerdict` — **Tampered** (signature present but
  invalid/corrupted) is **hard-refused even when `userConfirmedUnsigned=true`** (the CR-01
  invariant); `installFromFile` aborts. **Unsigned** (no signature) requires explicit per-plugin
  consent and shows an amber "Unsigned — requires consent" chip (objectName-addressed). Catch2 test
  asserts `VerifyVerdict::Tampered` + install abort.
- **Consent persistence (PLGSEC-02):** per-plugin unsigned-consent stored in `QSettings` under an
  explicit org/app scope (so it round-trips on the Windows registry backend), keyed per plugin UUID,
  surviving the launch-sweep + an app restart. Verified by grant→restart→`plugin.list` shows loaded
  without re-prompt.
- **Loopback enforcement (PLGSEC-03):** add the `grep QHostAddress::Any src/` → 0 CI grep gate to
  `.github/workflows/ci.yml` (extends the Phase-30 SIGPIPE/Any invariant) AND assert the WS server
  binds loopback-only in code; the catalog never phones home. `scripts/ajazz-debug state`/`ping`
  confirms loopback.

### Milestone verification & modularity audit (VERIF-01/02)

- **Form:** a scripted grep-gate set + an audit doc (`VERIF`/milestone-audit artifact) enumerating
  results: `grep -rn mirajazz src/core/ src/app/src/plugin_manager.cpp src/app/src/plugin_device_bridge.cpp`
  = 0; `grep -rn nlohmann src/core/include/` = 0 (the meaningful gate is `#include.*nlohmann`, since
  boundary COMMENTS legitimately contain the word); `grep -rn "akp05\.cpp\|makeAkp05\|makeAkp03\|makeAkp153" src/`
  = 0; objectName-coverage grep over new v2.0 QML files.
- **Screenshot/VERIF-01 reconciliation:** honestly document which phases have live debug-channel +
  screenshot evidence vs deferred HUMAN-UAT — **NEVER fabricate screenshots**. Note the true coverage
  (Phases 32/33/34 each have user-deferred live walks in their HUMAN-UAT files).
- **Where it lives:** the VERIF grep-gates + audit doc are Phase 35 deliverables; the milestone
  lifecycle `gsd-audit-milestone → complete-milestone → cleanup` runs AFTER as the final gate.

### Claude's Discretion

- Exact `VerifyVerdict` enum shape, the classification function/helper names, the QSettings key
  layout, the chip QML placement, the audit-doc format, and the WINPLG-01 ADR structure are at the
  executor's discretion, consistent with existing patterns.

</decisions>

\<code_context>

## Existing Code Insights

### Reusable Assets

- The existing plugin verify-gate (sdplugin verifier; the macOS verifier path was hardened in PR #80)
  — PLGSEC-01 splits its verdict. See `src/app/src/` plugin verification + `sdplugin_extractor.cpp`
  (zip-slip guard, Phase 13 CR-01).
- `PluginManager` / `UnifiedPluginHost` (Phase 30) — owns plugin registration + the OS/version gate
  (`reference_plugin_os_version_gate`: the LOCKED linux-accept exception lives here). WINPLG-02
  `supportsCurrentPlatform()` extends this gate.
- `plugin_manifest.{hpp,cpp}` — manifest parsing (already parses OS fields + ApplicationsToMonitor in
  Phase 34); add CodePath/CodePathWin classification here.
- The plugin WS server (`SdPluginServer`) — Phase 30 already enforced QHostAddress loopback + SIGPIPE
  CI invariants; PLGSEC-03 extends the CI grep gate.
- QSettings is already used for app settings — PLGSEC-02 consent reuses it under the named org/app
  scope (the app already sets QCoreApplication org/app name).
- The unsigned-consent one-shot flow exists but is fragile (`project_plugin_install_demo_working`:
  "unsigned consent is one-shot — launch-sweep deletes it") — PLGSEC-02 must make it persist.

### Established Patterns

- Platform gates via manifest OS-filter + a helper (mirror the Phase-34 watcher platform split where
  relevant). Compile-guarded per-OS code where needed.
- CI grep-gate invariants in `.github/workflows/ci.yml` (Phase 30 added QHostAddress::Any/SIGPIPE).
- Every new interactive control needs objectName; live debug-channel verification mandatory (VERIF-01).
- nlohmann::json PRIVATE to ajazz_plugins only; never in src/core/include/ (COD-031).

### Integration Points

- Manifest classification → plugin model → QML status chip (LoadedPluginsPage / the plugin list).
- VerifyVerdict (Tampered/Unsigned) → installFromFile abort path + consent chip + QSettings consent.
- CI grep gates → .github/workflows/ci.yml.
- Milestone audit doc → consumed by gsd-audit-milestone at lifecycle.

\</code_context>

<specifics>
## Specific Ideas

- WINPLG-01 is explicitly a **feasibility spike** — produce the classification ADR/decision doc as a
  deliverable (JS-bundled/WS-only vs vendor-DLL PE), do NOT implement a PE loader (locked
  anti-decision: vendor-DLL plugins use Wine, and even that launch is deferred this phase).
- The success criteria reference exact grep gates (success criterion 5) — implement them verbatim as
  CI/scripted checks.
- Consent must survive the launch-sweep (the known one-shot bug) AND an app restart.
- Tampered-even-with-consent is the hard CR-01 security invariant — a unit test must lock it.

</specifics>

<deferred>
## Deferred Ideas

- **Wine launch wiring (WINPLG-03 launch path)** — detection + chips only this phase; the actual
  `findExecutable("wine")` + per-plugin WINEPREFIX + DLL-host-under-Wine launch is deferred to a
  Windows/Wine-host session (no hardware to live-test here).
- PE loader for win-only `.exe` plugins — permanently rejected (not production-viable; Wine path
  instead).
- Any new device/protocol work — out of v2.0 scope.

</deferred>
