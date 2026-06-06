# Phase 27: Plugin Install/Trust/Persistence Hardening - Research

**Researched:** 2026-05-31
**Source:** Orchestrator parallel-subagent sweep (runtime/concurrency, install/persistence, GSD divergence) + grep verification of every load-bearing claim. No external dependencies introduced — this is internal hardening of an existing subsystem.

## Summary

The Stream Dock plugin runtime is **already concurrent and persistent at the process/settings level**. The remaining work is five well-scoped internal changes. No new frameworks, no protocol/RE changes, no `nlohmann::json` in core. The risk surface is the **verifier split** (security-critical) and the **rediscover idempotency** (must not double-spawn or tear down live plugins).

## D-1: Verifier Unsigned-vs-Tampered split (PLUGIN-16, security-critical)

**Current:** `verifyStagedPlugin` (`plugin_verify_gate.cpp:36-82`) calls `ajazz::plugins::verifyManifest(manifestPath, config)` which returns `result.valid == false` for BOTH "no signature block" and "signature present but Ed25519 mismatch", mapping both to `VerifyVerdict::Refused`. `installFromFile` (`plugin_catalog_model.cpp:739`) then quarantines unconditionally.

**Approach:**

1. In `ajazz::plugins::verifyManifest` (the Ed25519 gate, locate via `grep -rn "verifyManifest" src/plugins/ src/app/`), distinguish the two cases: a manifest with NO signature field → new `Unsigned` result; a manifest WITH a signature field that fails Ed25519 verification → `Tampered`/invalid. Surface this as a new field on the result (e.g. `enum class SignatureState { None, Valid, Invalid }`) or a new `VerifyVerdict::Unsigned` distinct from `Refused`.
1. `plugin_verify_gate.cpp` maps: `Valid + trusted key` → `Trusted`; `Valid + untrusted key` → `SelfSigned`; `None` → `Unsigned`; `Invalid` → `Refused` (tampered).
1. `installFromFile` (`:739`): `Refused` (tampered) → quarantine ALWAYS, even with `userConfirmedUnsigned`. `Unsigned` → promote IF `userConfirmedUnsigned || untrustedPluginsAllowed() || <trust-setting>`. Keep the re-verify-after-cross-fs-copy path (`:846`) consistent with the same rule.

**Test (D-27-6):** QZipWriter-built fixtures — a tampered `.sdPlugin` (valid-shaped signature, one flipped byte) MUST refuse even with `userConfirmedUnsigned=true`; an unsigned `.sdPlugin` (no signature block) MUST install with consent and MUST refuse without consent. Pattern: `reference_qzipreader_sanitization` (QZipWriter sanitizes on write; build hostile zips in-process). See existing `sdplugin_extractor` zip-slip tests for the harness shape.

**Landmine:** do NOT let `userConfirmedUnsigned` leak into the Tampered path — that is the whole point of CR-01. The test that asserts "tampered refused even with consent" is the gate.

## D-2: rediscover-after-install (PLUGIN-15)

**Current:** `discover()` (`plugin_manager.cpp:204-262`) runs once at launch (`application.cpp:801`); installing a plugin lands it on disk but nothing re-scans → restart required. `grep -rn rediscover src/app/` = 0.

**Approach:**

1. Add `PluginManager::rediscover()`: re-run the dir scan, build the runnable set, diff against `m_live` keys (`.sdPlugin` dir name), and `spawn()` ONLY the manifests whose key is not already in `m_live`. Do NOT touch already-live plugins (D-27-3 idempotency). Respect the persisted disabled-set from D-3.
1. Wire `PluginCatalogModel::installFinished(path, ok, err)` → `PluginManager::rediscover()` in `Application` (near the `:799` instantiation; the manager already holds the server pointer for the port). Guard on `ok==true`.
1. Optional: a `plugin.rediscover` debug-channel RPC for shell-driven smoke (see `project_debug_control_epic`).

**Test:** hermetic — point the plugins dir at a temp dir (`QStandardPaths::setTestModeEnabled`), spawn 1 plugin, assert `connectedPluginCount`/live==1, drop a second plugin dir, call `rediscover()`, assert it spawns ONLY the new one (live==2) and did not re-spawn the first (no duplicate in `m_live`).

## D-3: Persisted per-plugin enable/disable (PLUGIN-15)

**Current:** `discover()` returns all runnable manifests; the caller spawns every one. `m_disabled` (`:545-557`) is session-only (crash path).

**Approach:** a persisted disabled-set in QSettings (e.g. group `plugins/disabled`, key = pluginId). `discover()` (or the spawn loop) skips plugins whose id is in the persisted disabled-set. Add `PluginManager::setPluginEnabled(id, bool)` that writes the setting and (when disabling) tears the live plugin down; (when enabling) spawns it. Keep the crash-disable `m_disabled` semantics separate: crash-disable is NOT persisted (recovers on restart); user-disable IS persisted.

**Test:** hermetic QSettings (`setTestModeEnabled`) — disable a plugin, simulate restart (fresh PluginManager over same dir+settings), assert `discover()`/spawn does NOT start it; re-enable, assert it starts.

## D-4: In-app trust UX (PLUGIN-16)

**Current:** `LoadedPluginsPage.qml:153-198` shows read-only chips (trusted=none, self-signed=amber, unsigned=red). Allowing unsigned is env-var-only.

**Approach:**

1. App-level "Allow unsigned plugins" toggle persisted to QSettings (mirror `onlineCatalogEnabled` at `plugin_catalog_model.cpp:135,499`; expose `Q_PROPERTY` + `Changed` signal). `untrustedPluginsAllowed()` (`:59-64`) becomes `env-var OR setting`.
1. Per-plugin "Allow this plugin" action in `LoadedPluginsPage.qml` for unsigned/refused-unsigned rows → calls a `Q_INVOKABLE` that records consent for that pluginId (persisted) and triggers a re-verify/promote or a `rediscover()`. Reuse the existing chip layout; add an action button/menu next to the red "unsigned" chip.
1. Tampered rows are NEVER allowable from the UI (D-27-1) — surface a distinct "tampered/invalid signature" state, not the same as "unsigned".

**Note (UI gate):** this is an additive affordance on an existing page (toggle + action), not a new screen — no separate UI-SPEC required; follow `reference_qt_qml_gotchas` (Material attached props inside popup contentItem; singleton via `qmlRegisterSingletonInstance` if a new singleton is introduced).

## D-5: Concurrency regression guard (PLUGIN-17)

**Approach:** Catch2 test — register ≥3 plugins keyed by distinct `.sdPlugin` dir names; drive one to the 3-in-30s crash threshold (`plugin_crash_tracker.cpp:21-37`); assert only that plugin is disabled/removed from `m_live`, sibling `m_connections` entries (`sd_plugin_server.hpp:200`) stay connected, `connectedPluginCount()` drops by exactly one, and the `WR-02` HTML-no-respawn guard holds (HTML plugin with `process==nullptr` is not re-spawned). Guards the `5725cb0` shared-key collision regression.

## Cross-cutting constraints (from CLAUDE.md / memories)

- COD-031: no `nlohmann::json` in `ajazz_core` or installed public headers (verifier/catalog are app/plugin-linked — fine).
- Cross-platform `-Werror`: MSVC `/W4/WX` C4996 (prefer `_s` variants), Apple-Clang `-Wunused-const-variable` on file-scope `inline constexpr`, ASCII-only Catch2 test names.
- mdformat/gitleaks pre-commit (`reference_gitleaks_doc_false_positive`): reword `Word=value` doc tokens; re-add after mdformat reflow; never `--no-verify`.
- HW-free: no physical AKP05E needed; the optional install→spawn smoke runs via the debug channel + offscreen QPA.

## Validation Architecture

**Framework:** Catch2 (v3) via ctest, preset `linux-release`. Existing infra covers all Phase 27 requirements — no Wave 0 framework install. New tests are added as `tests/unit/test_plugin_*.cpp` registered in the unit-test `CMakeLists`. Hermetic state via `QStandardPaths::setTestModeEnabled(true)`; hostile/unsigned `.sdPlugin` fixtures built in-process with QZipWriter (no Python). Quick run: `ctest --preset linux-release -R "Plugin|Verify|Crash" -E qml`. Full suite: `ctest --preset linux-release -E qml` (the `-E qml` skips the pre-existing QML-tests link issue per the latent-items note in CLAUDE.md). The QML trust-UX (D-4) is offscreen-QPA testable but its primary proof is the C++ verdict surface + a manual affordance check.

Every success criterion has an automated proof:

- **PLUGIN-16 verifier split** → a Catch2 case asserting tampered-refused-even-with-consent AND unsigned-installs-with-consent / refused-without (the security gate).
- **PLUGIN-15 rediscover** → a hermetic case asserting only-new-plugins-spawn, no double-spawn.
- **PLUGIN-15 persisted disable** → a hermetic case asserting a disabled plugin does not spawn across a simulated restart.
- **PLUGIN-17 concurrency** → a case asserting one crash disables only itself, siblings stay connected.
- **No regressions** → full suite green (≈645, trust the live count).
