# Phase 27: Plugin Install/Trust/Persistence Hardening - Context

**Gathered:** 2026-05-31
**Status:** Ready for planning
**Source:** Orchestrator-authored from grep-verified reconciliation (see STATE.md "2026-05-31 — Plugin install/run epic reconciliation"). Equivalent to discuss-phase output; every file:line below is confirmed in code, not assumed.

<domain>
## Phase Boundary

Close the gap between "Stream Dock `.sdPlugin` plugins register over the WebSocket" (shipped 2026-05-30 in 8 ad-hoc commits) and "plugins are fully installable, concurrent, and persistent **from the GUI**." Five concrete deliverables:

1. **rediscover-after-install** — a freshly installed `.sdPlugin` spawns live with NO app restart.
1. **GUI unsigned-install-with-consent** — split the verifier's `Refused` verdict so a tampered plugin ALWAYS refuses but an unsigned one installs with explicit consent.
1. **In-app trust UX** — a settings toggle + a per-plugin "allow" action, replacing the `AJAZZ_ALLOW_UNTRUSTED_PLUGINS` env var as the primary path.
1. **Persisted per-plugin enable/disable** — a user-disabled plugin stays disabled across restart.
1. **Concurrency regression guard** — pin that one plugin's crash disables only itself.

**IN scope:** `src/app/src/` (PluginManager, PluginCatalogModel, plugin_verify_gate, plugin_crash_tracker), the Ed25519 verifier in `src/plugins/`/`ajazz::plugins::verifyManifest`, `src/app/qml/` trust UX, and `tests/unit/`.

**OUT of scope (do NOT touch):**

- **Wire/protocol/RE** — no opcode, packet, HID, or BAT/LIG/CLE/ULEND changes. COD-031 boundary (no `nlohmann::json` in `ajazz_core` or installed public headers) stays intact.
- **The Python OOP host** (`src/plugins/` `OutOfProcessPluginHost` / bwrap / `_host_child.py`, SEC-003) — it is the separate AJAZZ-Python ecosystem, NOT the Stream Dock runtime. Untouched.
- **plugin→device `setImage` round-trip on physical hardware** — that is **Phase 25 live-debt** (the unmet VERIFY-05/06 witness, blocked on the `0x3004` demo unit's input gap). Phase 27 proves nothing on real hardware.
- The runtime concurrency model itself — it is ALREADY done (node/native = separate `QProcess`; HTML = per-plugin `QWebEnginePage`; `SdPluginServer` `std::vector<PluginConnection> m_connections`; 4 plugins proven concurrent). This phase adds a *regression guard*, not new concurrency.

</domain>

<decisions>
## Implementation Decisions (locked)

### D-27-1 — Tampered ALWAYS refuses; only unsigned can be consented

The verifier (`plugin_verify_gate.cpp:36-82` → `ajazz::plugins::verifyManifest`) currently collapses both "no signature" and "signature present but Ed25519-invalid" into `VerifyVerdict::Refused`. Split it: add an **Unsigned** outcome (manifest carries no signature block) distinct from **Tampered** (signature block present but cryptographically invalid). `installFromFile` then promotes Unsigned-with-consent but quarantines Tampered **even when `userConfirmedUnsigned==true`**. This is the security-critical invariant — a tampered package is an attack, an unsigned one is a developer sideload. (CR-01; named as the follow-up in commit `90d97e2`'s body.)

### D-27-2 — `AJAZZ_ALLOW_UNTRUSTED_PLUGINS` survives only as a CI/dev override

The env var (`plugin_catalog_model.cpp:59-64`, `untrustedPluginsAllowed()`) stays functional for headless/CI runs, but the GUI gains a first-class trust affordance: a persisted "Allow unsigned plugins" setting (QSettings, same pattern as `plugins/onlineCatalogEnabled`) + a per-plugin "Allow this plugin" action in `LoadedPluginsPage.qml` (which today shows only read-only trust chips at `:153-198`). When the setting or the per-plugin allow is set, `installFromFile` accepts Unsigned with consent. Tampered is never consentable (D-27-1).

### D-27-3 — rediscover spawns ONLY newly added plugins (idempotent)

`PluginManager::rediscover()` re-scans the plugins dir, diffs against the already-live set (`m_live`, keyed by `.sdPlugin` dir name per `5725cb0`), and spawns ONLY plugins not already running. Already-live plugins are NOT torn down or double-spawned. Wire it to `PluginCatalogModel::installFinished` (or an equivalent signal emitted after promote). No restart required.

### D-27-4 — Persisted enable/disable is a positive disabled-set in QSettings

Persist a per-plugin disabled flag (QSettings list/group, e.g. `plugins/disabled/<pluginId>`). `discover()` (`plugin_manager.cpp:204-262`) consults it and does NOT spawn disabled plugins. The runtime-only `m_disabled` map (`plugin_manager.cpp:545-557`, set on 3-in-30s crash) stays as-is for the crash path; the new persisted set is the *user-intent* disable. Distinguish the two: a crash-disable is session-scoped and recovers on restart; a user-disable persists.

### D-27-5 — Concurrency regression test guards the `5725cb0` failure mode

Add a Catch2 test that spawns ≥3 plugins keyed by distinct `.sdPlugin` dir names, then drives one to the 3-in-30s crash threshold and asserts (a) only that plugin's entry leaves `m_live` / is disabled, (b) the sibling sockets in `SdPluginServer::m_connections` stay connected, (c) `connectedPluginCount()` drops by exactly one. Guards the shared-key collision `5725cb0` fixed and the `WR-02` HTML-no-respawn guard.

### D-27-6 — Hostile-zip tests use QZipWriter (Python not required)

Per `reference_qzipreader_sanitization`, QZipWriter sanitizes on write, so build the tampered/unsigned test `.sdPlugin` fixtures in C++/Catch2 with QZipWriter (or a committed fixture) rather than shelling to Python. The tampered fixture has a valid-looking signature block with a flipped byte; the unsigned fixture has no signature block.

### Claude's Discretion

- Exact signal name/shape for the rediscover trigger (reuse `installFinished` if its signature suffices).
- Whether the trust setting is one global toggle, per-plugin only, or both (the success criteria require both an app-level toggle AND a per-plugin action — implement both).
- Test file placement under `tests/unit/` following the existing `test_plugin_*` naming.
- Whether `rediscover()` is also exposed as a debug-channel RPC (`plugin.rediscover`) for the optional smoke — nice-to-have, not required.

</decisions>

\<code_context>

## Existing Code Insights (grep-verified 2026-05-31)

- **PluginManager** `src/app/src/plugin_manager.{hpp,cpp}` — `discover()` at `:204-262`; spawn dispatch node `:333-393`, HTML `:399-442`, native `:456-510`; `onProcessFailed`/restart `:517-539`; `m_disabled` session map `:545-557`; live set `m_live` keyed by `.sdPlugin` dir name. Instantiated in `application.cpp:799-806` (discover+spawn at launch, after `m_pluginServer->start(0)`). **No `rediscover()` exists** (`grep -rn rediscover src/app/` = 0).
- **PluginCatalogModel** `src/app/src/plugin_catalog_model.cpp` — `installFromFile` at `:668`; `Refused` hard-quarantine at `:739-759`; `userConfirmedUnsigned` only gates `SelfSigned` at `:761`; env-var launch-sweep at `:163`; `untrustedPluginsAllowed()` at `:59-64`; `onlineCatalogEnabled` default `true` at `:135`; emits `installFinished`. Plugins dir = `QStandardPaths::AppDataLocation/plugins`.
- **Verifier** `plugin_verify_gate.cpp:36-82` → `ajazz::plugins::verifyManifest` returns `Trusted | SelfSigned | Refused`; unsigned and tampered both map to `Refused` (the split target).
- **Crash tracker** `plugin_crash_tracker.cpp:21-37` — 3-in-30s window, `kWindowMs=30000`, `kDisableThreshold=3`.
- **Server** `sd_plugin_server.hpp:200` `std::vector<PluginConnection> m_connections`; `connectedPluginCount()` `:109`; `socketForUuid` re-resolves per-call (Pitfall 4).
- **Settings persistence (per-action/global)** `pi_bridge.cpp:133,142` — atomic JSON under `AppDataLocation/plugins/<uuid>/`; QSettings used for `plugins/onlineCatalogEnabled` (`plugin_catalog_model.cpp:135,499`).
- **Trust UI (read-only today)** `LoadedPluginsPage.qml:153-198` — chips: trusted (none), self-signed (amber), unsigned (red). No per-plugin allow action.
- **Test patterns** — `tests/unit/test_plugin_*`; QSettings hermetic via `QStandardPaths::setTestModeEnabled`; QZipReader/Writer sanitization per `reference_qzipreader_sanitization` (zip-slip guard in `sdplugin_extractor.cpp`).

\</code_context>

<specifics>
## Specific Ideas

- The five deliverables map 1:1 to PLUGIN-15 (rediscover + restart-survival), PLUGIN-16 (verifier split + GUI consent + trust UX), PLUGIN-17 (concurrency regression guard). Group plans so each requirement ID is fully covered.
- Suggested wave shape (max 2 concurrent per CLAUDE.md cap): Wave 1 — verifier Unsigned/Tampered split + hostile-zip tests (PLUGIN-16 core, no UI). Wave 2 (parallel) — `rediscover()` + install→spawn wiring + restart-survival test (PLUGIN-15) ‖ persisted enable/disable in `discover()` (PLUGIN-15). Wave 3 — trust UX QML (toggle + per-plugin allow, depends on Wave-1 verdict surface) ‖ concurrency regression test (PLUGIN-17). Planner may refine.
- Every plan must keep `ctest --preset linux-release -E qml` green (current ≈645 unit pass; trust the live count).

</specifics>

<deferred>
## Deferred Ideas

- Plugin→device `setImage` via a bound profile action on the **physical** AKP05E — stays Phase 25 live-debt (demo-unit input gap).
- Debug-channel `plugin.installFromFile` + `plugin.rediscover` RPCs — optional convenience for shell-driven smoke; not a phase gate.
- A full plugin "store ratings / update" surface — out of milestone scope.

</deferred>
