# ADR: IPluginHost2 Unification

**Status:** Accepted (2026-06-06)

______________________________________________________________________

## 1. Status

Accepted (2026-06-06). This ADR supersedes the v2.0 research verdict documented in STATE.md
("keep separate in v2.0"). That verdict is explicitly OVERRIDDEN by a user decision recorded in
CONTEXT.md ("IPluginHost2 unification scope: FULL UNIFICATION NOW").

______________________________________________________________________

## 2. Context

The plugin system currently contains two entirely separate host implementations inside
`Application`:

1. **PluginManager** (`src/app/src/plugin_manager.{cpp,hpp}`) — orchestrates Elgato `.sdPlugin`
   plugins (Node.js, HTML/QWebEngine, native processes) over WebSocket IPC via `SdPluginServer`.
   It owns process lifecycle, the 3-in-30s crash disable window (`PluginCrashTracker`), and spawn
   dispatch per runtime (JS, HTML, native).

1. **OutOfProcessPluginHost** (`src/plugins/include/ajazz/plugins/out_of_process_plugin_host.hpp`)
   — runs Python plugins in a sandboxed child process over line-delimited JSON IPC via
   stdin/stdout. It implements the existing `IPluginHost` interface
   (`src/plugins/include/ajazz/plugins/i_plugin_host.hpp`).

**Name confusion (Pitfall 6):** The existing `IPluginHost` in `src/plugins/include/` is the
Python-only host interface. `PluginManager` does NOT implement it. The new unified contract
is named `IPluginHost2` and lives in `src/app/src/` (not `src/plugins/include/`) so it can
depend on Qt types (`QJsonObject`, `QString`) without violating the COD-031 boundary.

**State before Phase 30:**

- `Application` owns two separate host objects: `m_pluginManager` and `m_pluginHost`.
- There is no single dispatch entry point; callers must know which runtime a plugin UUID
  belongs to before routing.
- Pre-registration crash tracking is absent: a plugin that exits before `registerPlugin`
  has its crash credited against the 3-in-30s window, even though it never registered.

______________________________________________________________________

## 3. Decision: UNIFY

**UNIFY** — introduce `IPluginHost2` as the single spawn/lifecycle/IPC contract for all
four plugin runtimes (Node.js, HTML/QWebEngine, native, Python), implemented by a
`UnifiedPluginHost` aggregator class.

### UNIFY details

`IPluginHost2` is an app-layer interface (`src/app/src/i_plugin_host2.hpp`) that defines:

- `spawn(PluginManifest const&)` — runtime-agnostic spawn entry point
- `shutdown()` — graceful shutdown (exitApp + terminate + kill)
- `connectedPluginCount() const noexcept` — count of registered (post-handshake) plugins
- `plugins()` — unified plugin inventory (all runtimes)
- `dispatch(QString, QString, QJsonObject)` — route an action to any runtime by UUID
- `pluginServer() const noexcept` — access SdPluginServer for sendEvent

`UnifiedPluginHost` (new class in `src/app/src/`) owns both concrete sub-hosts:

- `PluginManager` — the `.sdPlugin` WebSocket sub-host (refactored to satisfy `IPluginHost2`)
- `OutOfProcessPluginHost` — the Python sub-host (wrapped at the aggregator boundary)

Dispatch routes by UUID INTERNALLY inside `UnifiedPluginHost::dispatch()`; `Application`
sees only the single `IPluginHost2` interface and never routes by runtime type.

**Override of research verdict:** The v2.0 research verdict ("keep `.sdPlugin` WS path and Python
OOP path separate in v2.0", documented in STATE.md line 79) is superseded by the CONTEXT.md
user override. This ADR records the UNIFY decision as the authoritative record for
downstream phases (31–35).

______________________________________________________________________

## 4. Rationale

- **One lifecycle, one crash-window.** A single crash tracker under `IPluginHost2` means the
  3-in-30s disable+notify and restart behaviours are the same code path for all runtimes.
  No per-runtime divergence in crash accounting.

- **One shutdown path.** `UnifiedPluginHost::shutdown()` calls both the `.sdPlugin` (exitApp +
  QProcess::terminate/kill) and Python (SIGTERM + pipe close) shutdown paths. There is one
  place to audit and test.

- **One dispatch entry point.** `pluginHost2()->dispatch(uuid, actionId, payload)` reaches
  every runtime. No facade, no caller-side routing by runtime type. Phase 31+ work
  (`ActionInstance` model, binding, Property Inspector) can address the unified surface
  without knowing whether a UUID belongs to a Node plugin or a Python plugin.

- **Simplifies Phase 31+ work.** The `ActionInstance` model (Phase 31) needs a stable UUID →
  host interface for action routing. A unified `IPluginHost2` provides this without branching.

- **User override authority.** The CONTEXT.md user decision is explicit and final: "This is
  an explicit user override of the v2.0 research verdict." The rationale of that override is
  that the marginal refactor cost in Phase 30 is lower than the ongoing maintenance cost of
  two separate lifecycle systems.

______________________________________________________________________

## 5. Consequences

### Positive

- Single lifecycle seam for crash-isolation fix (HOST-02 sentinel-UUID pattern, Plan 30-02).
- Downstream plans (31–35) address one surface without runtime-specific branches.
- `Application` is simpler: one `IPluginHost2*` replaces two separate host members.

### Regression risk

Because all four runtime paths move onto one contract, each runtime needs an explicit
no-regression check:

| Runtime         | Regression check                                                     |
| --------------- | -------------------------------------------------------------------- |
| Node.js         | `ctest -R PluginManagerTest` (existing, must stay green)             |
| HTML/QWebEngine | `ctest -R PluginManagerTest` HTML branch (existing, must stay green) |
| Native process  | `ctest -R PluginManagerTest` native branch (existing)                |
| Python OOP      | `ctest -R OutOfProcess` (existing, must stay green)                  |

A per-runtime no-regression check is mandatory before declaring Phase 30 complete — not
just the `disconnect-before-register` Catch2 test added in Plan 30-01.

### Migration note

The `Application` member rename (`m_pluginManager` + `m_pluginHost` → single
`m_pluginHost2`) is part of Plan 30-03. Plans 30-01 and 30-02 are preparatory (scaffold
and sentinel-UUID). No `Application`-visible API change before Plan 30-03.

______________________________________________________________________

## 6. SKU Boundary Rule

Plugin dispatch carries **zero SKU strings** (`akp05e`, `akp03`, `akp153`). Device I/O from
plugins reaches hardware ONLY via the following chain:

```
PluginDeviceBridge -> StreamDockControlService -> mirajazz sidecar -> hardware
```

`PluginManager::dispatch()` and `IPluginHost2::dispatch()` must not contain SKU-specific
branches. The `plugin_device_bridge.cpp` fallback strings at lines 1242/1251 (`"akp05e"`) are
in the bridge tier (correct tier) and must NOT be moved into `plugin_manager.cpp`.

**Enforcement is CODE-REVIEW-ONLY** per the locked CONTEXT.md decision. No permanent CI grep
gate for `akp05e|akp03|akp153` in `plugin_manager.cpp` is added. The one-time audit command
is: `grep -rn "akp05e|akp03|akp153" src/app/src/plugin_manager.cpp` (expect 0 hits).

Note: the **separate** `QHostAddress::Any` + SIGPIPE CI grep gate (added in ci.yml by Plan
30-01 Task 2) is unaffected by this code-review-only decision and remains a permanent
CI assertion.

______________________________________________________________________

## 7. Rejected Alternative: Keep Separate

**Alternative:** Keep `PluginManager` and `OutOfProcessPluginHost` as two independent
host objects in `Application` (the v2.0 research verdict).

**Rejected because:** It leaves two lifecycle systems to maintain indefinitely — two crash
windows, two shutdown paths, two dispatch entry points. The Phase 31+ `ActionInstance` model
would need to branch on runtime type. The sentinel-UUID crash-isolation fix (HOST-02) would
need to be applied twice. The marginal cost in Phase 30 of unifying under `UnifiedPluginHost`
is lower than the compounding maintenance cost across Phases 31–35.

**Authority for override:** CONTEXT.md locked decision ("IPluginHost2 unification scope: FULL
UNIFICATION NOW. This is an explicit user override of the v2.0 research verdict.").

______________________________________________________________________

## 8. COD-031 Note

`IPluginHost2` and `UnifiedPluginHost` live in `src/app/src/` and may use `QJsonObject`,
`QString`, and other Qt types. They must **NOT** pull `nlohmann::json` into `ajazz_core` or
any installed public header in `src/core/include/` or `src/plugins/include/`.

The Qt ↔ STL type conversion for the Python path (which uses `std::string_view`/STL
arguments in `OutOfProcessPluginHost::dispatch()`) lives at the `UnifiedPluginHost`
aggregator boundary — inside `src/app/src/unified_plugin_host.cpp`. The conversion never
crosses into `ajazz_core` or `ajazz_plugins`'s public header surface.

Audit command (enforced at review time): `grep -rn nlohmann src/core/include/` must return 0.
