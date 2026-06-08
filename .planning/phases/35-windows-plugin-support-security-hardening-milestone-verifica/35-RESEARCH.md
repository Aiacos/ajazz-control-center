# Phase 35: Windows Plugin Support + Security Hardening + Milestone Verification - Research

**Researched:** 2026-06-08
**Domain:** Brownfield extend-existing-code — Stream Deck plugin verification/security, Windows-plugin classification, milestone modularity audit
**Confidence:** HIGH (every load-bearing symbol grep-verified at file:line in this session)

\<user_constraints>

## User Constraints (from 35-CONTEXT.md)

### Locked Decisions

**WINPLG-01/02 (classification + native run):**

- Classification heuristic = inspect manifest `CodePath`/`CodePathWin` suffix (.exe/.dll) AND scan the bundle for PE binaries (magic bytes `MZ` / `PE\0\0`) — robust against a mislabeled manifest. Produces per-plugin: **WS-only-IPC** vs **vendor-DLL**.
- Classification runs at install/scan time, cached on the plugin model (NOT lazily at launch).
- WS-only win plugins run natively on Linux/macOS via a `supportsCurrentPlatform()` helper + manifest OS-filter — no Wine.
- Chip states (objectName-addressed): "Runs natively" / "Requires Wine" / "Unsupported on this OS". All reachable via `scripts/ajazz-debug qml.get` (VERIF-01).
- Produce a WINPLG-01 ADR-style classification decision doc.

**WINPLG-03 — CHIP-ONLY STUB THIS PHASE (launch DEFERRED):**

- Implement vendor-DLL **detection + status chips ONLY**. The actual Wine launch wiring (`QStandardPaths::findExecutable("wine")`, per-plugin `WINEPREFIX`, spawning the DLL host under Wine) is **DEFERRED** — no Wine/Windows hardware to live-test here. Building untestable launch code is rejected.
- WINPLG-03 lands **partial**: classification + chip + "this would need Wine" UX done and live-verifiable; launch path is a documented HUMAN-UAT/future-phase follow-up. Record partial status honestly.
- **Never bundle Wine** (locked anti-decision).

**PLGSEC-01 (Tampered vs Unsigned):**

- Tampered (signature present but invalid) = **hard-refused even when `userConfirmedUnsigned=true`** (CR-01 invariant); `installFromFile` aborts. Catch2 test asserts the tampered verdict + install abort.
- Unsigned (no sig) requires explicit per-plugin consent + amber "Unsigned — requires consent" chip (objectName-addressed).

**PLGSEC-02 (consent persistence):**

- Per-plugin unsigned-consent stored in `QSettings` under explicit org/app scope (registry-safe), keyed per plugin UUID, surviving the launch-sweep + app restart. Verified by grant→restart→`plugin.list` shows loaded without re-prompt.

**PLGSEC-03 (loopback):**

- Add the `grep QHostAddress::Any src/` → 0 CI grep gate to `.github/workflows/ci.yml` (extends Phase-30 invariant) AND assert WS server binds loopback-only in code; catalog never phones home. `scripts/ajazz-debug state`/`ping` confirms loopback.

**VERIF-01/02 (milestone audit):**

- Scripted grep-gate set + audit doc: `mirajazz` in core/bridge = 0; `#include.*nlohmann` in core/include = 0 (meaningful gate is the include, not the word in comments); `akp05.cpp|makeAkp05|makeAkp03|makeAkp153` = 0; objectName-coverage grep over new v2.0 QML.
- Honestly document which phases have live debug-channel + screenshot evidence vs deferred HUMAN-UAT — **NEVER fabricate screenshots**.
- VERIF grep-gates + audit doc are Phase 35 deliverables; the milestone lifecycle (`gsd-audit-milestone → complete → cleanup`) runs AFTER as the final gate.

### Claude's Discretion

- Exact `VerifyVerdict` enum shape (note: already exists — see below), classification function/helper names, QSettings key layout (note: already chosen — `plugins/allowed/<uuid>`), chip QML placement, audit-doc format, WINPLG-01 ADR structure.

### Deferred Ideas (OUT OF SCOPE)

- Wine launch wiring (WINPLG-03 launch path) — chips only this phase.
- PE loader for win-only `.exe` plugins — permanently rejected; Wine path instead.
- Any new device/protocol work — out of v2.0 scope.
  \</user_constraints>

\<phase_requirements>

## Phase Requirements

| ID                     | Description                                                                    | Research Support                                                                                                                                                                                                                                                              |
| ---------------------- | ------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| WINPLG-01              | Feasibility spike: classify WS-only-IPC vs vendor-DLL, document ADR            | `plugin_manifest.{hpp,cpp}` already parses `codePath`/`codePathWin`/`codePathMac` + `os[]`. New work: a classification helper (suffix + PE-magic scan) + ADR doc.                                                                                                             |
| WINPLG-02              | WS-only win plugins run natively (OS-filter + `supportsCurrentPlatform()`)     | `manifestRunnableHere()` (plugin_manifest.cpp:316) + the LOCKED Linux-accept policy ALREADY accept these. Gate hook is `plugin_manager.cpp:278`. New work: `supportsCurrentPlatform()` helper that special-cases WS-only-IPC regardless of declared OS.                       |
| WINPLG-03 (partial)    | Vendor-DLL detection + status chips; launch DEFERRED                           | Classification helper output → new model role → new QML chip. NO Wine launcher.                                                                                                                                                                                               |
| PLGSEC-01              | Tampered hard-refused even with consent; Unsigned consent-gated                | **ALREADY IMPLEMENTED** (`VerifyVerdict::Refused` branch at plugin_catalog_model.cpp:907; CR-01 test at test_plugin_install_from_file.cpp:581). New work: verify + (if gaps) tighten the test lock.                                                                           |
| PLGSEC-02              | Per-plugin consent persists in QSettings, survives launch-sweep + restart      | **ALREADY IMPLEMENTED** (FIX-CONSENT block plugin_catalog_model.cpp:1063-1076; launch-sweep reads `perPluginAllowed` at :173). New work: verify restart round-trip via debug channel + (if missing) a persistence test.                                                       |
| PLGSEC-03              | WS server loopback-only, CI-gated; catalog never phones home                   | **CI GATE ALREADY EXISTS** (ci.yml:67-83) and server binds `QHostAddress::LocalHost` (sd_plugin_server.cpp:93). Catalog network is opt-in gated (plugin_catalog_model.cpp:521-535). New work: verify; the success-criterion grep gate is already present — confirm or extend. |
| VERIF-01               | New v2.0 controls debug-addressable; live debug-channel + screenshot per phase | objectName-coverage scan over v2.0 QML; honest reconciliation of live-evidence vs HUMAN-UAT (Phases 32/33/34 have deferred walks).                                                                                                                                            |
| VERIF-02               | mirajazz unmodified; no revived C++ AKP backends                               | Grep gates run NOW: all pass except naive matches hit COMMENTS only (see audit below).                                                                                                                                                                                        |
| \</phase_requirements> |                                                                                |                                                                                                                                                                                                                                                                               |

## Summary

**This phase is ~70% verification and ~30% new code.** The grep-verified reality: the entire PLGSEC security model is already implemented, CR-fixed, and unit-tested. The verify-gate splits `Trusted / SelfSigned / Unsigned / Refused` (Refused == tampered), and the CR-01 invariant — *tampered is quarantined even with `userConfirmedUnsigned=true`* — is live in code (plugin_catalog_model.cpp:907-927) AND locked by a Catch2 test ("PluginInstallFromFile tampered plugin refused even with consent", test_plugin_install_from_file.cpp:581). Per-plugin consent already persists in `QSettings` at `plugins/allowed/<uuid>` (the FIX-CONSENT block, :1063-1076) and the launch-sweep already honors it (:173). The loopback CI grep gate already exists in ci.yml (:67-83), and the server binds `QHostAddress::LocalHost`.

**The genuinely new code is small and confined to WINPLG-01/02/03(chip):** a classification helper (CodePath suffix + PE-magic-byte scan), a `supportsCurrentPlatform()` helper that lets WS-only-IPC win plugins through the OS gate, a new model role carrying the classification, a new QML status chip ("Runs natively" / "Requires Wine" / "Unsupported on this OS"), and an ADR doc. Plus deliverable artifacts: the milestone audit doc, the objectName-coverage scan, and the honest screenshot/HUMAN-UAT reconciliation.

**Primary recommendation:** Treat PLGSEC-01/02/03 as "verify + add the one missing test/live-check"; do NOT redesign the verdict enum or consent store (they exist and are correct). Spend implementation budget on the WINPLG classification helper + chip (the only net-new feature) and on producing honest audit/ADR deliverables. Run the VERIF greps verbatim as scripted gates — they pass today (comment-only matches), and the `QHostAddress::Any` CI gate already exists.

## Architectural Responsibility Map

| Capability                           | Primary Tier                                               | Secondary Tier                  | Rationale                                                                            |
| ------------------------------------ | ---------------------------------------------------------- | ------------------------------- | ------------------------------------------------------------------------------------ |
| Manifest CodePath/OS parsing         | App / Backend (`plugin_manifest.cpp`)                      | —                               | Already parses os[], codePath, codePathWin/Mac                                       |
| WS-only vs vendor-DLL classification | App / Backend (new helper near `plugin_manifest`)          | —                               | Pure manifest+bundle inspection; no device I/O                                       |
| OS/platform gate                     | App / Backend (`manifestRunnableHere` / PluginManager)     | —                               | Spawn-eligibility decision; LOCKED Linux-accept lives here                           |
| Signature verify (Ed25519)           | Plugins tier (`manifest_signer` via Python verifier)       | App gate (`plugin_verify_gate`) | COD-031: actual verify is PRIVATE to ajazz_plugins; app collapses to `VerifyVerdict` |
| Consent persistence                  | App / Storage (`QSettings`, registry-safe)                 | —                               | Org/app scope set in main.cpp; key `plugins/allowed/<uuid>`                          |
| Classification/verdict chip          | QML / Client (`LoadedPluginsPage.qml` / `PluginStore.qml`) | model role                      | Display only; backed by a model role                                                 |
| Loopback invariant                   | App / Backend (`sd_plugin_server.cpp`)                     | CI (`ci.yml` grep gate)         | Bind site + a permanent CI assertion                                                 |
| Milestone audit                      | Tooling / CI (scripts + grep) + docs                       | —                               | Scripted gates + an audit doc artifact                                               |

## Standard Stack

No new external packages. This phase extends existing in-tree C++/QML/CMake/GitHub-Actions assets only.

- Qt 6.7+ (`QSettings`, `QStandardPaths`, `QJsonDocument`, `QVersionNumber`) — already used.
- Catch2 — existing test framework (ctest `--preset linux-release`).
- Python Ed25519 verifier — already wired via `AJAZZ_PLUGIN_VERIFIER_SCRIPT` (fail-closed when absent).

**Package Legitimacy Audit:** N/A — no external packages installed this phase. (Confirmed: WINPLG-03 anti-decision forbids bundling Wine/node; all detection-only.)

## Architecture Patterns

### System data flow (Phase 35 touch points)

```
.sdPlugin install/scan
   │
   ├─ extractSdPluginArchive (staging, zip-slip guarded)
   │
   ├─ parsePluginManifest  ──►  PluginManifest{ os[], codePath, codePathWin, codePathMac }
   │                                   │
   │                                   ├─ NEW: classifyWindowsPlugin()  ─► WS-only-IPC | vendor-DLL
   │                                   │        (CodePath suffix .exe/.dll  +  scan bundle for MZ/PE magic)
   │                                   │
   │                                   └─ manifestRunnableHere() / NEW supportsCurrentPlatform()
   │                                            (OS gate + LOCKED linux-accept; WS-only passes regardless of OS)
   │
   ├─ verifyStagedPlugin  ──►  VerifyOutcome{ verdict ∈ {Trusted,SelfSigned,Unsigned,Refused} }
   │        │
   │        ├─ Refused (tampered)  ─► QUARANTINE unconditionally (CR-01) — even userConfirmedUnsigned=true
   │        ├─ Unsigned + !consent ─► abort, await confirm
   │        └─ Trusted/SelfSigned/consented ─► promote
   │                                              │
   │                                              └─ FIX-CONSENT: QSettings plugins/allowed/<uuid>=true
   │
   └─ model role (trustLevel + NEW classification) ──► QML chip (LoadedPluginsPage / PluginStore)
                                                          objectName-addressed (VERIF-01)

App start ─► launch-sweep: re-verify every *.sdPlugin; honor perPluginAllowed() (consent survives restart)

WS server start ─► m_server->listen(QHostAddress::LocalHost, port)   [CI grep gate enforces ::Any absence]
```

### Pattern 1: Reuse the existing four-way verdict, do not invent a new enum

**What:** `VerifyVerdict { Trusted, SelfSigned, Unsigned, Refused }` already exists.
**Where:** `src/app/src/plugin_verify_gate.hpp:41-46` [VERIFIED: grep]. `Refused` == tampered (sig present, Ed25519-invalid). The trust-level vocabulary maps to `"trusted"/"self-signed"/"unsigned"/"tampered"` via `verdictToTrustLevel()` (plugin_verify_gate.hpp:89).
**Implication for PLGSEC-01:** The "split" the requirement asks for is **already done**. The task is to verify it and ensure a test asserts the tampered-with-consent abort (it does — see Validation Architecture). Do NOT add a new enum value or rename existing ones.

### Pattern 2: Default-constructed QSettings resolves to the named scope (registry-safe)

**What:** `QApplication::setOrganizationName(AJAZZ_VENDOR_NAME)` / `setOrganizationDomain(...)` / `setApplicationName(AJAZZ_PRODUCT_NAME)` are set in `src/app/src/main.cpp:69-71` [VERIFIED: grep]. Defs: `AJAZZ_VENDOR_NAME="Aiacos"`, `AJAZZ_PRODUCT_NAME="AJAZZ Control Center"` in `src/app/CMakeLists.txt:705-719` [VERIFIED: grep].
**Implication for PLGSEC-02:** A bare `QSettings settings;` already writes under the named org/app scope, which round-trips on the Windows registry backend. The consent key `plugins/allowed/<uuid>` (plugin_catalog_model.cpp:1072) is already correct. No new scope plumbing needed.

### Pattern 3: Classification heuristic (the one net-new algorithm)

**What:** Decide WS-only-IPC vs vendor-DLL from a parsed manifest + bundle.
**When:** at install/scan time (cache on the model).
**Sketch (pure, testable; place near `manifestRunnableHere`):**

```cpp
// Source pattern (new): mirror plugin_manifest.cpp free-function style.
// WS-only-IPC: CodePath* points to a .js/.html/.cjs (or no native binary) AND
//   no PE binary (MZ / "PE\0\0") is found anywhere in the bundle.
// vendor-DLL:  CodePath*/CodePathWin ends .exe/.dll OR a PE-magic file exists.
enum class WinPluginClass { WsOnlyIpc, VendorDll, NotWindowsOnly };
WinPluginClass classifyWindowsPlugin(PluginManifest const& m, QString const& bundleDir);
```

- PE-magic scan: read first bytes of each candidate code file; `MZ` (0x4D 0x5A) at offset 0, optional `PE\0\0` at the e_lfanew offset. Cap reads (reuse the existing bounded-read discipline from installFromFile, plugin_catalog_model.cpp:843).
- `supportsCurrentPlatform()` = `WsOnlyIpc → true regardless of OS` ; `VendorDll → (currentPlatform==windows) || wineDetected` (wineDetected is DEFERRED → always false this phase → chip "Requires Wine"/"Unsupported on this OS").

### Pattern 4: Reuse the existing trust-chip delegate idiom for the new classification chip

**What:** `LoadedPluginsPage.qml:168-220` has a `Rectangle{ id: trustChip; visible: trustLevel !== "trusted"; ... }` with Theme.chipBg{Error,Warning} coloring and per-state ToolTip. Phase 34 added an amber capability chip with the same idiom.
**Implication:** Add a sibling chip keyed on a new `classification`/`platformStatus` role. **GAP:** the existing `trustChip` has NO `objectName` (grep returned zero objectNames in LoadedPluginsPage.qml) — the new chip MUST set `objectName` (VERIF-01), and consider adding `objectName` to the existing trustChip while here.

### Anti-Patterns to Avoid

- **Inventing a new verdict enum / renaming `Refused`** — it already means tampered and is referenced in 30+ sites + tests. [VERIFIED: grep]
- **Re-plumbing QSettings org/app scope** — already set in main.cpp; a bare `QSettings` is correct.
- **Building a Wine launcher** — locked anti-decision; detection + chip only.
- **A PE loader for `.exe` plugins** — permanently rejected.
- **Lazy classification at launch** — locked: classify at install/scan, cache on the model.
- **Adding interactive QML without `objectName`** — VERIF-01 failure; the existing trustChip is the cautionary example.

## Don't Hand-Roll

| Problem                    | Don't Build         | Use Instead                                                                         | Why                                                                |
| -------------------------- | ------------------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| Signature verification     | A new Ed25519 path  | Existing `verifyStagedPlugin` / `verifyManifest` (Python verifier, COD-031 PRIVATE) | Already fail-closed, three-way `SignatureState`, tested            |
| Verdict states             | New enum            | `VerifyVerdict` (plugin_verify_gate.hpp:41)                                         | Exists, mapped, tested                                             |
| Consent store              | New file/db         | `QSettings plugins/allowed/<uuid>`                                                  | Registry-safe, launch-sweep already honors it                      |
| OS gate                    | New platform filter | `manifestRunnableHere()` + LOCKED linux-accept                                      | Exists at plugin_manifest.cpp:316; wired at plugin_manager.cpp:278 |
| Manifest CodePath/OS parse | New parser          | `parsePluginManifest` (codePath/codePathWin/codePathMac/os[] already parsed)        | plugin_manifest.hpp:91-97                                          |
| Loopback CI gate           | New workflow        | Extend/confirm ci.yml:67-83 (`QHostAddress::Any` gate already present)              | Comment-aware grep already merged                                  |
| Zip-slip / staging         | New extractor guard | `extractSdPluginArchive` + staging-before-promote                                   | Phase-13 CR-01 guard                                               |

**Key insight:** The "security hardening" in this phase's name is mostly *already shipped*. The risk is duplicating or regressing it. Grep first, extend minimally.

## Runtime State Inventory

> Phase 35 is brownfield but NOT a rename/refactor. The relevant runtime state is **persisted consent** — verify it survives, do not migrate it.

| Category            | Items Found                                                                                                                                                                                                                                             | Action Required                                                                                                          |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| Stored data         | `QSettings` key `plugins/allowed/<uuid>=true` (per-plugin consent); `plugins/allowUnsignedPlugins` (global toggle); `plugins/onlineCatalogEnabled`                                                                                                      | Verify round-trip on restart (PLGSEC-02). No migration.                                                                  |
| Live service config | WS server binds `QHostAddress::LocalHost` (sd_plugin_server.cpp:93); port runtime-assigned                                                                                                                                                              | Verify loopback via `ajazz-debug state`/`ping` (PLGSEC-03). No change.                                                   |
| OS-registered state | None — verified by grep (no Task Scheduler / launchd / systemd state set by plugin layer).                                                                                                                                                              | None.                                                                                                                    |
| Secrets/env vars    | `AJAZZ_PLUGIN_VERIFIER_SCRIPT`, `AJAZZ_PLUGIN_TRUST_ROOTS` (CMake compile defs); `AJAZZ_ALLOW_UNTRUSTED_PLUGINS` (env override for consent)                                                                                                             | None — code reads them already; do not rename.                                                                           |
| Build artifacts     | Catalog default URL `https://space.key123.vip/...` (streamdock_catalog_fetcher.cpp:49) is the vendor Mirabox endpoint, but gated off unless `onlineCatalogEnabled` AND fired only via the `disabled` sentinel guard (plugin_catalog_model.cpp:521-535). | Document in the audit doc that "never phones home" = network is opt-in/disabled-by-sentinel, NOT that the URL is absent. |

## Common Pitfalls

### Pitfall 1: Assuming PLGSEC needs greenfield design

**What goes wrong:** Planning tasks that re-implement the verdict split / consent store, causing churn or regression of CR-01.
**Why:** The requirement text reads as "do X" but X is already shipped + CR-fixed.
**How to avoid:** Frame PLGSEC-01/02/03 tasks as "verify + lock with a test + live-drive", not "implement".
**Warning signs:** A task that defines a new enum, a new settings key, or a new bind site.

### Pitfall 2: VERIF greps flagging COMMENTS as violations

**What goes wrong:** Naive `grep -rn "akp05.cpp"` / `grep -rn QHostAddress::Any` return non-zero because the strings appear in **explanatory comments**, not live code. Live results today: `akp05` = 2 hits (both comments, stream_dock_control_service.cpp:193 + sidecar_stream_dock_device.hpp:16); `QHostAddress::Any` = 2 hits (both comments, sd_plugin_server.cpp). `mirajazz` in core/bridge = 0; `#include.*nlohmann` in core/include = 0. [VERIFIED: grep, this session]
**Why:** The meaningful gate is the *include* / *call site*, not the lexical token.
**How to avoid:** Use the comment-aware filter the CI gate already uses: `grep -vP ':[0-9]+:\s*(//|[*])'` (ci.yml:73). For nlohmann use `#include.*nlohmann`. For akp05 prefer `makeAkp05|makeAkp03|makeAkp153` (function symbols) over `akp05.cpp` (filename, appears in comments).
**Warning signs:** A gate that reports "FAIL: 2 hits" when both hits are `// ...`.

### Pitfall 3: New chip invisible to the debug channel

**What goes wrong:** New classification chip added without `objectName` → invisible to `qml.get`/`qml.set`/screenshot → VERIF-01 fails.
**Why:** The existing `trustChip` already lacks `objectName` (grep: zero objectNames in LoadedPluginsPage.qml) — easy to copy the bad pattern.
**How to avoid:** Set `objectName` on the new chip; live-verify with `scripts/ajazz-debug qml.get`. The CLAUDE.md harness gap applies (qml.invoke fires onClicked but not Switch.toggled) — use plain controls / dedicated Q_INVOKABLE for any action.

### Pitfall 4: `.sdPlugin` vs Python-host plugin list confusion

**What goes wrong:** `LoadedPluginsPage.qml` backs the **Python OOP-host** plugin list (the comment at :222-227 says the per-plugin "Allow" was a no-op for `.sdPlugin`). The `.sdPlugin` install/consent UX lives in `PluginStore.qml`.
**Why:** Phase 27 mis-placed `.sdPlugin` trust UX here (documented at LoadedPluginsPage.qml:76). This exact trap was caught live in Phase 27 (memory: 700+ tests passed, button was a no-op for `.sdPlugin`).
**How to avoid:** Decide which surface the WINPLG classification chip belongs on (the `.sdPlugin` catalog → `PluginStore.qml` / `PluginCatalogModel` role, since win-only plugins are `.sdPlugin`). Live-drive the REAL surface, not just unit tests.

### Pitfall 5: Fabricating screenshot evidence in the VERIF-01 reconciliation

**What goes wrong:** Claiming live-walk coverage that was actually deferred.
**Why:** Phases 32/33/34 each have user-deferred HUMAN-UAT walks (32-HUMAN-UAT.md, 33-HUMAN-UAT.md, 34-HUMAN-UAT.md all exist [VERIFIED: ls]).
**How to avoid:** The audit doc must state per-phase: headless/automated live evidence vs deferred HUMAN-UAT. Never claim a walk happened.

## Code Examples

### Existing CR-01 invariant (do not regress) — installFromFile Refused branch

```cpp
// Source: src/app/src/plugin_catalog_model.cpp:907-927 [VERIFIED: read this session]
if (vout.verdict == VerifyVerdict::Refused) {
    // CR-01: tampered (sig present + Ed25519-invalid) — quarantine
    // unconditionally, even when userConfirmedUnsigned==true.
    QDir(QDir(stagingParent).filePath(archiveName)).removeRecursively();
    QDir(stagingParent).rmdir(QStringLiteral("."));
    emit installFinished(localPath, false,
        tr("Plugin signature verification failed: %1").arg(reason));
    return false;
}
```

### Existing consent persistence (PLGSEC-02 already done)

```cpp
// Source: src/app/src/plugin_catalog_model.cpp:1070-1076 [VERIFIED]
if (userConfirmedUnsigned && vout.verdict != VerifyVerdict::Trusted) {
    QSettings settings;  // resolves to AJAZZ_VENDOR_NAME / AJAZZ_PRODUCT_NAME scope
    settings.setValue(QStringLiteral("plugins/allowed/") + candidateUuid, true);
}
// Launch-sweep honors it: plugin_catalog_model.cpp:173
//   if (verdict==Unsigned && (consentToUnsigned() || perPluginAllowed(entry))) KEEP;
```

### Existing loopback bind + CI gate (PLGSEC-03 already enforced)

```cpp
// Source: src/app/src/sd_plugin_server.cpp:93 [VERIFIED]
auto const bound = m_server->listen(QHostAddress::LocalHost, port);
```

```yaml
# Source: .github/workflows/ci.yml:67-83 [VERIFIED] — comment-aware QHostAddress::Any gate ALREADY EXISTS
- name: Enforce loopback-only + SIGPIPE invariants (Phase 30)
  run: |
    if grep -rn 'QHostAddress::Any' src/ | grep -vP ':[0-9]+:\s*(//|[*])'; then
      echo "::error::QHostAddress::Any found in non-comment source — loopback-only violated."
      exit 1
    fi
    if ! grep -n 'SIGPIPE' src/app/src/main.cpp | grep -q 'SIG_IGN'; then ... fi
```

## State of the Art

| Old (assumed by CONTEXT)                        | Current (grep-verified)                                            | Impact                                               |
| ----------------------------------------------- | ------------------------------------------------------------------ | ---------------------------------------------------- |
| Verify-gate "needs splitting" Tampered/Unsigned | Four-way `VerifyVerdict` already splits them; Refused==tampered    | PLGSEC-01 = verify + test lock, not implement        |
| Consent is "one-shot, launch-sweep deletes it"  | FIX-CONSENT persists `plugins/allowed/<uuid>`; sweep honors it     | PLGSEC-02 = verify restart round-trip, not implement |
| `QHostAddress::Any` CI gate "needs adding"      | Gate exists (ci.yml:67-83), comment-aware                          | PLGSEC-03 CI gate = confirm/extend, not add          |
| Manifest doesn't carry CodePath/OS              | `parsePluginManifest` parses os[]/codePath/codePathWin/codePathMac | WINPLG classification builds on parsed fields        |
| OS gate needs `supportsCurrentPlatform()`       | `manifestRunnableHere()` + LOCKED linux-accept exist               | New helper only special-cases WS-only-IPC            |

**Note on the one-shot consent memory:** the auto-memory note "unsigned consent is one-shot — launch-sweep deletes it" was the PRE-FIX state. The FIX-CONSENT block (plugin_catalog_model.cpp:1063-1076) and the launch-sweep `perPluginAllowed` read (:173) resolved it. PLGSEC-02 should still **live-verify** the restart round-trip (a real grant→restart→`plugin.list` check) because the memory flags this as historically fragile.

## Assumptions Log

| #   | Claim                                                                                                                                                                                                   | Section                 | Risk if Wrong                                                                                                                                                                                                   |
| --- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | The WINPLG classification chip belongs on the `.sdPlugin` catalog surface (`PluginStore.qml` / `PluginCatalogModel`), since win-only plugins are `.sdPlugin` — not the Python-host `LoadedPluginsPage`. | Pitfall 4 / Pattern 4   | If placed on the wrong surface, the chip is a no-op for real win plugins (the Phase-27 trap). Planner should confirm which model row-type carries win-only plugins.                                             |
| A2  | PE-magic detection (`MZ` / `PE\0\0`) over the bundle's code files is sufficient to distinguish vendor-DLL from WS-only-IPC without a full PE parse.                                                     | Pattern 3               | A win plugin that ships an unused `.dll` could be misclassified vendor-DLL. Acceptable per CONTEXT ("robust against mislabeled manifest"); the CodePath suffix is the primary signal, PE-scan the corroborator. |
| A3  | No PLGSEC-02 *persistence-specific* test exists yet (the existing tests cover tampered/unsigned/promote but I did not find a grant→reload-instance→still-allowed test).                                 | Validation Architecture | If a persistence test is missing, add one; if it exists under a different name, no new test needed. Planner/executor should grep `perPluginAllowed`/`plugins/allowed` in tests before adding.                   |
| A4  | The catalog "never phones home" requirement is satisfied by the opt-in `onlineCatalogEnabled` gate + `disabled` sentinel, NOT by removing the vendor URL.                                               | Runtime State Inventory | If the requirement intends URL removal, this is wrong — but REQUIREMENTS.md "host-owned catalog only" + the existing T-22-phonehome guard support the gated interpretation.                                     |

## Open Questions

1. **Which surface carries win-only `.sdPlugin` plugins in the UI?**

   - Known: `LoadedPluginsPage.qml` = Python-host list; `PluginStore.qml` = `.sdPlugin` catalog.
   - Unclear: whether installed win-only `.sdPlugin` plugins also appear in a loaded-list that needs the chip.
   - Recommendation: add the classification role to `PluginCatalogModel` and render the chip in `PluginStore.qml`; live-verify the real surface before declaring done (A1).

1. **Is there an existing consent-persistence test, or is a new one needed (A3)?**

   - Recommendation: grep `tests/unit` for `plugins/allowed` / `perPluginAllowed`; add a round-trip test only if absent. A two-instance test (write via instance 1, construct instance 2, assert sweep keeps it) is the cleanest lock for PLGSEC-02.

1. **WINPLG-01 ADR scope** — feasibility-spike doc only; no implementation beyond the classifier + chip.

   - Recommendation: ADR documents the heuristic (CodePath suffix + PE-magic), the WS-only-IPC-runs-native decision, and the explicit Wine-launch DEFERRAL with rationale (no hardware).

## Environment Availability

| Dependency                          | Required By         | Available                                        | Version               | Fallback                                                            |
| ----------------------------------- | ------------------- | ------------------------------------------------ | --------------------- | ------------------------------------------------------------------- |
| CMake + Ninja + Qt 6                | Build/test          | ✓ (project baseline)                             | Qt 6.7+               | —                                                                   |
| `ctest --preset linux-release`      | Validation          | ✓                                                | ~785 cases (STATE.md) | —                                                                   |
| `scripts/ajazz-debug` debug channel | VERIF-01 live-drive | ✓ (in-tree, `AJAZZ_DEBUG_CONTROL=1`)             | —                     | —                                                                   |
| Wine                                | WINPLG-03 launch    | ✗                                                | —                     | **DEFERRED — chip-only this phase (locked)**                        |
| Windows runtime                     | WINPLG-03 native    | ✗ (Linux host)                                   | —                     | classification + chip live-verifiable on Linux; launch is HUMAN-UAT |
| Python Ed25519 verifier             | verify-gate         | ✓ (when `AJAZZ_PLUGIN_VERIFIER_SCRIPT` compiled) | —                     | fail-closed → Refused (no crash)                                    |

**Missing dependencies with no fallback:** none that block this phase (Wine/Windows absence is by-design deferred).
**Missing dependencies with fallback:** Wine/Windows → chip-only stub + HUMAN-UAT note (locked decision).

## Validation Architecture

### Test Framework

| Property                                                        | Value                                                                                   |
| --------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Framework                                                       | Catch2 (via ctest)                                                                      |
| Config                                                          | CMake presets; `tests/unit/CMakeLists.txt` registers suites                             |
| Quick run                                                       | `ctest --preset linux-release -R "plugin-install\|plugin-verify-gate\|plugin-manifest"` |
| Full suite                                                      | `ctest --preset linux-release` (~785 cases)                                             |
| ASCII-only test names + `-R` (NOT `--test-regex`) per CLAUDE.md | required                                                                                |

### Highest-risk behaviors → proof

| Risk (behavior that must hold)                           | Test / live-check                                                                                                                                                                     | Status                                      |
| -------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------- |
| **Tampered refused even with consent** (PLGSEC-01 CR-01) | Catch2 "PluginInstallFromFile tampered plugin refused even with consent" (test_plugin_install_from_file.cpp:581, passes `userConfirmedUnsigned=true`, asserts not-installed)          | EXISTS — verify still green; do not regress |
| Tampered verdict classification                          | "PluginVerifyGate tampered manifest -> Refused" (test_plugin_verify_gate.cpp:265) + "unsigned -> Unsigned" (:305)                                                                     | EXISTS                                      |
| Unsigned consent-gated (not auto-installed)              | "self-signed without confirm -> not promoted" (:309); unsigned branch at installFromFile:929                                                                                          | EXISTS — confirm an Unsigned-specific case  |
| **Consent survives restart** (PLGSEC-02)                 | NEW (if absent, A3): write `plugins/allowed/<uuid>` via one model instance, construct a second instance, assert launch-sweep KEEPS the plugin (perPluginAllowed path)                 | LIKELY NEW — grep first                     |
| **Loopback invariant** (PLGSEC-03)                       | `bindAddress()`/`listen` asserts LocalHost (test_sd_plugin_server.cpp) + CI grep gate (ci.yml:67) + live `ajazz-debug state`/`ping`                                                   | CI gate EXISTS; confirm a code-level assert |
| **Classification correctness** (WINPLG-01/02)            | NEW: unit cases — CodePath `.html`/`.js` + no PE → WsOnlyIpc; CodePath `.exe`/`.dll` or PE-magic present → VendorDll; `supportsCurrentPlatform()` returns true for WsOnlyIpc on linux | NEW                                         |
| **WS-only win plugin runs native on Linux** (WINPLG-02)  | NEW: `manifestRunnableHere`/`supportsCurrentPlatform` accepts an OS=["windows"] WS-only manifest on linux; live-drive: install a WS-only win plugin, confirm `plugin.list` loads it   | NEW + live                                  |
| **Chip states addressable** (VERIF-01)                   | objectName on each chip; `scripts/ajazz-debug qml.get` returns the chip + label ("Runs natively"/"Requires Wine"/"Unsupported on this OS"); screenshot read                           | NEW + live                                  |

### Sampling rate

- **Per task commit:** the quick run above (plugin-install + verify-gate + manifest + any new classification suite).
- **Per wave merge:** full `ctest --preset linux-release`.
- **Phase gate:** full suite green + live debug-channel drive of the new chip(s) + the consent restart round-trip + `ajazz-debug state` loopback confirm, BEFORE `/gsd:verify-work`.

### Wave 0 gaps

- [ ] Classification helper unit suite (`tests/unit/test_win_plugin_classification.cpp` or extend `test_plugin_manifest.cpp`) — WINPLG-01/02.
- [ ] Consent-persistence round-trip test — PLGSEC-02 (only if grep shows none — A3).
- [ ] (No framework install needed — Catch2 + ctest present.)

## Security Domain

> `security_enforcement` is enabled (not set false in config). This phase IS the security-hardening phase.

### Applicable ASVS Categories

| ASVS Category                      | Applies | Standard Control (existing)                                                                                 |
| ---------------------------------- | ------- | ----------------------------------------------------------------------------------------------------------- |
| V1 Architecture / Trust boundaries | yes     | COD-031 (nlohmann PRIVATE to ajazz_plugins); plugin OOP isolation                                           |
| V5 Input Validation                | yes     | `parsePluginManifest` (never throws, bounded), `validateDownloadedArchive` (size+zip-magic), zip-slip guard |
| V6 Cryptography                    | yes     | Ed25519 verify (`verifyManifest` / Python verifier) — **never hand-roll**; fail-closed                      |
| V10 Malicious Code / Integrity     | yes     | Tampered = unconditional quarantine (CR-01); launch-sweep re-verify; per-plugin consent                     |
| V13 / API & comms                  | yes     | WS server loopback-only (`QHostAddress::LocalHost`); catalog opt-in (no phone-home)                         |

### Known threat patterns for this stack

| Pattern                           | STRIDE                | Standard Mitigation (existing)                                                      |
| --------------------------------- | --------------------- | ----------------------------------------------------------------------------------- |
| Tampered signed plugin            | Tampering / EoP       | `Refused` verdict → quarantine even with consent (plugin_catalog_model.cpp:907)     |
| Unsigned sideload abuse           | Spoofing / EoP        | Per-plugin consent + amber chip; default-deny                                       |
| Zip-slip / path traversal         | Tampering             | `sdplugin_extractor` guard (Phase-13 CR-01) + staging-before-promote                |
| Decompression bomb                | DoS                   | Bounded read `kMaxPluginDownloadBytes` (installFromFile:843)                        |
| WS server exposure                | Info disclosure / EoP | LocalHost bind + CI grep gate (ci.yml:67)                                           |
| Catalog phone-home                | Info disclosure       | `onlineCatalogEnabled` opt-in + `disabled` sentinel (plugin_catalog_model.cpp:521)  |
| Native code via win `.exe`/`.dll` | EoP                   | NOT loaded in-process (PE loader rejected); Wine path detection-only, never bundled |

## Project Constraints (from CLAUDE.md)

- **COD-031:** no `nlohmann::json` in `src/core/include/` (grep = 0; the verify gate keeps nlohmann PRIVATE via `ajazz::plugins`). Any new code stays clear.
- **Debug-channel verification MANDATORY:** ctest green is necessary but NOT sufficient — every UI/behavior change live-driven via `scripts/ajazz-debug` + screenshot before "done" (the Phase-27 no-op-button lesson; VERIF-01).
- **Every new interactive control MUST set `objectName`** (debug-addressable).
- **Test names ASCII-only**; ctest filter is `--tests-regex`/`-R` (NOT `--test-regex`).
- **VERIF-02:** never modify the `mirajazz` crate; never reintroduce `makeAkp05/03/153` / `akp05.cpp` wire backends.
- **No system-level mutations** from project tooling.
- **GitFlow-lite:** topic branch → PR into `develop`; never push direct.
- Cross-platform strictness: MSVC `/W4 /WX` (C4996 → use `_s` variants); Apple Clang `-Werror`.

## Sources

### Primary (HIGH confidence — grep/read this session)

- `src/app/src/plugin_verify_gate.hpp:41-89` — `VerifyVerdict` enum + `verdictToTrustLevel`.
- `src/app/src/plugin_catalog_model.cpp:130-208` (launch-sweep), `:821-1089` (installFromFile + FIX-CONSENT), `:521-535` (phone-home gate), `:61-66` (perPluginAllowed).
- `src/app/src/plugin_manifest.{hpp:66-197, cpp:316-372}` — manifest fields + `manifestRunnableHere` + `currentPlatformString`.
- `src/app/src/sd_plugin_server.cpp:86-148` — loopback bind.
- `src/app/src/main.cpp:69-71` + `src/app/CMakeLists.txt:705-719` — org/app scope defs.
- `src/app/qml/LoadedPluginsPage.qml:100-228` — trust-chip delegate (no objectName — GAP).
- `.github/workflows/ci.yml:48-83` — existing grep-gate invariants (hid_open, QHostAddress::Any, SIGPIPE).
- `tests/unit/test_plugin_install_from_file.cpp` (21 TEST_CASEs incl. CR-01 tampered-with-consent), `test_plugin_verify_gate.cpp` (9), `test_plugin_manifest.cpp` (24).
- `src/plugins/include/ajazz/plugins/manifest_signer.hpp:66-128` — `SignatureState` + `verifyManifest`.
- `.planning/REQUIREMENTS.md`, `35-CONTEXT.md`, `STATE.md` (read this session).

### Secondary

- CLAUDE.md project memory; auto-memory notes (`project_plugin_install_demo_working`, `reference_plugin_os_version_gate`, PR #80 CI green-up).

## Metadata

**Confidence breakdown:**

- PLGSEC-01/02/03 (already implemented): HIGH — read the exact code + tests + CI gate.
- WINPLG classification design: MEDIUM-HIGH — fields parsed are verified; the helper + chip are net-new but small and pattern-matched.
- VERIF audit: HIGH — greps run this session; results are comment-only matches.
- Surface placement of the new chip (A1): MEDIUM — needs a planner/executor confirm.

**Research date:** 2026-06-08
**Valid until:** 2026-07-08 (stable in-tree code; re-grep if other Phase-35 work lands first).
