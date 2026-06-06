# Phase 30: Plugin-Host Modular Foundation - Research

**Researched:** 2026-06-06
**Domain:** Qt 6 / C++ plugin-host architecture — lifecycle, IPC, crash isolation, interface unification
**Confidence:** HIGH

## Summary

The plugin system currently has two entirely separate host implementations living side-by-side in `Application`. The first is `PluginManager` (`src/app/src/plugin_manager.{cpp,hpp}`), which orchestrates Elgato `.sdPlugin` plugins (Node.js, HTML/QWebEngine, and native processes) over a WebSocket IPC layer via `SdPluginServer`. The second is `OutOfProcessPluginHost` (`src/plugins/include/ajazz/plugins/out_of_process_plugin_host.hpp`), which runs Python plugins in a sandboxed child process over line-delimited JSON IPC via stdin/stdout. These two systems share an `IPluginHost` interface in name only — `PluginManager` does NOT implement `IPluginHost`; `OutOfProcessPluginHost` does. The user's locked decision (CONTEXT.md) is to collapse all four runtimes onto a single unified host contract in Phase 30.

The pre-registration crash hazard exists specifically in `SdPluginServer`: when a `QWebSocket` connects and then disconnects before sending `registerPlugin`, the connection slot's UUID is empty and `pluginDisconnected` is not emitted. The crash tracker and `PluginManager` never learn about this connection. However, `PluginManager::onProcessFailed` fires on the `QProcess` side for the process that corresponds to that socket — and if `m_live` contains that process entry, it attempts to access it while the WebSocket-side tracking is already cleaned up. The sentinel-UUID approach inserts the connection into an augmented map under a synthetic UUID immediately on connect, so the teardown path always has a valid entry to rekey or remove.

The SKU decoupling (HOST-03) audit reveals that `plugin_manager.cpp` is already clean (`grep -rn "akp05e|akp03|akp153" src/app/src/plugin_manager.cpp` → 0 hits), but `plugin_device_bridge.cpp` has two `"akp05e"` string fallbacks that constitute the remaining coupling. The `QHostAddress::Any` grep gate and SIGPIPE handler are already satisfied in the code; only the CI assertion is missing.

**Primary recommendation:** Phase 30 introduces `IPluginHost2` as a thin, unified spawn/lifecycle/IPC contract implemented by a single `UnifiedPluginHost` class that wraps both the WebSocket path (delegating to `SdPluginServer` + `PluginManager`) and the OOP Python path (delegating to `OutOfProcessPluginHost`). The sentinel-UUID approach adds a `QHash<QWebSocket*, QString> m_pendingConnections` (or augments `SdPluginServer::PluginConnection`) so pre-registration disconnects are tracked without entering the crash-disable window.

______________________________________________________________________

\<user_constraints>

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **IPluginHost2 unification scope: FULL UNIFICATION NOW.** Collapse the Node, HTML, native, and Python runtimes onto a single shared host implementation in Phase 30 — not document-and-defer. `PluginManager` dispatch is refactored onto the one contract. The ADR (success criterion #5) must document a **unify** decision with rationale, NOT a defer decision. The planner must scope for the larger refactor.
  - **Regression budget:** because all four runtime paths move onto one host, each runtime (Node, HTML, native, Python) needs an explicit no-regression check, not just the disconnect-before-register Catch2 test.
- **Pre-registration crash tracking: SINGLE MAP + SENTINEL UUID.** Insert a freshly connected socket into the live connection map under a synthetic sentinel UUID until `registerPlugin` arrives, then rekey to the real UUID. Pre-registration exits must NOT count toward the 3-crashes-in-30s disable window. Guard the rekey path against races (socket dying during rekey).
- **SKU-decoupling enforcement (HOST-03): CODE-REVIEW ONLY.** Document the boundary in the ADR / CLAUDE.md; do NOT add a CI grep gate for `akp05e|akp03|akp153` in `plugin_manager.cpp`. Success criterion #2's grep is a one-time manual verification at audit time, not a permanent CI test. (Note: the *separate* `QHostAddress::Any` + SIGPIPE CI gate from success criterion #3 is unaffected and still applies.)

### Claude's Discretion

- Exact interface method names / file layout for the unified `IPluginHost` contract.
- Internal data-structure choices beyond the sentinel-UUID decision above.
- ADR filename and section structure (must land in `.planning/phases/30-*/`).

### Deferred Ideas (OUT OF SCOPE)

- Adding a permanent CI grep gate for SKU strings in the plugin layer — user chose code-review-only for now; could be revisited in Phase 35 (security hardening) if desired.

\</user_constraints>

\<phase_requirements>

## Phase Requirements

| ID      | Description                                                                                                                                                                                                                               | Research Support                                                                 |
| ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| HOST-01 | A single `IPluginHost` interface unifies the Node, HTML, native, and Python runtimes behind one spawn/lifecycle/IPC contract; `PluginManager` dispatch is refactored onto it with no behavioural regression vs current per-runtime paths. | Section: Standard Stack, Architecture Patterns, Code Examples                    |
| HOST-02 | Plugin crash isolation is pre-registration-safe — a plugin that crashes mid-handshake never crashes the app; 3-crashes-in-30s disable+notify, restart, and exitApp shutdown behaviours are preserved.                                     | Section: Architecture Patterns (sentinel-UUID), Don't Hand-Roll, Common Pitfalls |
| HOST-03 | The plugin layer has zero compile/link coupling to specific device SKUs or wire/sidecar internals; all device I/O is reached only through `PluginDeviceBridge` → `StreamDockControlService` → the mirajazz sidecar edge.                  | Section: Architecture Patterns (SKU audit), Common Pitfalls                      |

\</phase_requirements>

______________________________________________________________________

## Architectural Responsibility Map

| Capability                                              | Primary Tier                             | Secondary Tier                                | Rationale                                                                                                                             |
| ------------------------------------------------------- | ---------------------------------------- | --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| Plugin process spawn + lifecycle                        | `PluginManager` (app layer)              | `IPluginHost2` (contract)                     | Spawning is runtime-specific (QProcess for Node/native, QWebEnginePage for HTML, subprocess for Python); the contract abstracts this. |
| WebSocket IPC (registerPlugin handshake, event routing) | `SdPluginServer` (app layer)             | `PluginManager` (orchestrates)                | The WS server owns the wire; PluginManager coordinates lifecycle around it.                                                           |
| Python IPC (line-delimited JSON over stdin/stdout)      | `OutOfProcessPluginHost` (plugins layer) | `IPluginHost2` (contract)                     | Existing, working, tested. Wrapped by the new unified host.                                                                           |
| Pre-registration crash tracking (sentinel-UUID)         | `SdPluginServer::PluginConnection`       | `PluginManager::m_live`                       | The WS connection slot is where the sentinel UUID lives; PluginManager reads from it to decide crash-window eligibility.              |
| Crash-window (3-in-30s) + disable policy                | `PluginCrashTracker` + `PluginManager`   | `IPluginHost2` (surfaces via signal)          | `PluginCrashTracker` is a pure value type; stays as-is.                                                                               |
| SKU/device I/O routing                                  | `PluginDeviceBridge`                     | `StreamDockControlService` → mirajazz sidecar | Bridge owns the device abstraction; plugin layer MUST NOT reach below it.                                                             |
| CI security assertions (grep gates)                     | `.github/workflows/ci.yml` Linux step    | —                                             | Grep gates run on Linux only (source-identical across runners).                                                                       |
| ADR / lifecycle documentation                           | `.planning/phases/30-*/`                 | `CLAUDE.md` (boundary note)                   | ADR is the authoritative decision record for downstream phases.                                                                       |

______________________________________________________________________

## Standard Stack

### Core (existing, verified in codebase)

| Library/Component                      | Version/Location                                                   | Purpose                                                       | Why Standard                                           |
| -------------------------------------- | ------------------------------------------------------------------ | ------------------------------------------------------------- | ------------------------------------------------------ |
| `QWebSocketServer` / `QWebSocket`      | Qt 6.8.3 (CI: `qtwebsockets`)                                      | WebSocket IPC for `.sdPlugin` Node/HTML/native plugins        | Already in use; `SdPluginServer` wraps it              |
| `QProcess`                             | Qt 6 Core                                                          | Subprocess spawn and lifecycle for Node.js and native plugins | Already in use in `PluginManager::spawn()`             |
| `QWebEnginePage` / `QWebEngineProfile` | Qt 6 `AJAZZ_HAVE_WEBENGINE`                                        | In-process Chromium for HTML plugins                          | Already in use in `PluginManager::spawn()` HTML branch |
| `OutOfProcessPluginHost`               | `src/plugins/include/ajazz/plugins/out_of_process_plugin_host.hpp` | Python subprocess + line-delimited JSON IPC                   | Already wired into `Application::initPluginHost()`     |
| `PluginCrashTracker`                   | `src/app/src/plugin_crash_tracker.hpp`                             | Pure 3-in-30s crash-window logic (clock-injectable)           | Already in use; unit-tested; must be preserved         |
| `SdPluginServer`                       | `src/app/src/sd_plugin_server.hpp`                                 | Elgato v6 WebSocket plugin server                             | Already in use; loopback-only invariant enforced       |
| `PluginDeviceBridge`                   | `src/app/src/plugin_device_bridge.hpp`                             | Device I/O abstraction for plugins                            | HOST-03 boundary owner                                 |

### No New External Packages Required

Phase 30 is a pure internal refactor. No new libraries are installed. All required infrastructure already exists.

### Package Legitimacy Audit

No external packages are installed in this phase. Section not applicable.

______________________________________________________________________

## Architecture Patterns

### System Architecture Diagram

```
Plugin Process (Node/HTML/native) ──WS JSON──> SdPluginServer
                                                     |
                                                     | onNewConnection → sentinel UUID inserted
                                                     | registerPlugin → rekey to real UUID
                                                     | pluginRegistered signal
                                                     v
IPluginHost2 (unified contract) <── PluginManager ──+── IPluginHost2::spawn()
         |                                           |     .js → QProcess(node)
         | (wraps)                                   |     .html → QWebEnginePage
         |                                           |     else → QProcess(native)
         v                                           |
OutOfProcessPluginHost ──stdin/stdout JSON──> Python child process
         |
         | IPluginHost2::dispatch(), plugins(), loadAll()
         v
Application (owns both via IPluginHost2 pointer or dual members)
         |
         v
PluginDeviceBridge → StreamDockControlService → mirajazz sidecar → hardware
```

**Data flow for pre-registration crash (HOST-02 fix):**

```
QWebSocket connects
    → SdPluginServer::onNewConnection()
        → insert PluginConnection{uuid = sentinel_uuid, socket = client}
    → QProcess::finished(CrashExit) fires (process died before sending registerPlugin)
        → PluginManager::onProcessFailed(pluginId)
            → check: is pluginId in pendingConnections (sentinel)?
                YES → erase sentinel entry, do NOT record crash, do NOT count toward window
                NO → normal crash-window logic (existing PluginCrashTracker path)
```

### Recommended Project Structure (changes from Phase 30)

```
src/app/src/
├── plugin_manager.{cpp,hpp}         # refactored: implements IPluginHost2 or is owned by it
├── sd_plugin_server.{cpp,hpp}       # modified: sentinel-UUID tracking in PluginConnection
├── i_plugin_host2.hpp               # NEW: unified spawn/lifecycle/IPC contract
├── unified_plugin_host.{cpp,hpp}    # NEW (optional): aggregates Manager + OOP host
└── plugin_crash_tracker.{cpp,hpp}   # unchanged: pure value type

.planning/phases/30-plugin-host-modular-foundation/
├── 30-ADR-plugin-host-unification.md  # NEW: IPluginHost2 decision + rationale
└── ...
```

### Pattern 1: Sentinel-UUID Pre-Registration Tracking

**What:** On `QWebSocket` connect, `SdPluginServer` inserts the new connection under a synthetic UUID (e.g. `"__pending__" + QUuid::createUuid().toString(QUuid::WithoutBraces)`) rather than waiting for `registerPlugin`. On `registerPlugin`, rekey the entry to the real UUID. On disconnect-before-register, erase the sentinel entry and do NOT emit `pluginDisconnected`.

**When to use:** Every time a WebSocket client connects but before it sends `registerPlugin`.

**Implementation sketch:**

```cpp
// Source: sd_plugin_server.cpp — SdPluginServer::onNewConnection()
void SdPluginServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QWebSocket* client = m_server->nextPendingConnection();
        if (!client) continue;
        client->setParent(this);
        connect(client, &QWebSocket::textMessageReceived, this, &SdPluginServer::onClientTextMessage);
        connect(client, &QWebSocket::disconnected, this, &SdPluginServer::onClientDisconnected);
        // SENTINEL UUID: marks pre-registration connections without exposing them
        // to pluginDisconnected or the crash-window. Rekey in registerPlugin handler.
        QString const sentinelUuid = QStringLiteral("__pending__")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_connections.push_back({sentinelUuid, client, QString{}, 0, false});
        AJAZZ_LOG_INFO("plugin-server", "client connected (sentinel {})", sentinelUuid.toStdString());
    }
}
```

**Rekey in registerPlugin:**

```cpp
// In dispatchClientMessage, registerPlugin branch:
// Find entry by socket pointer (not by UUID — it's a sentinel).
auto it = std::find_if(m_connections.begin(), m_connections.end(),
    [client](auto const& c) { return c.socket == client; });
if (it != m_connections.end()) {
    // Guard: socket-dying-during-rekey race — check socket is still valid.
    if (it->socket == nullptr) { return; } // already cleaned up
    // Impersonation guard (existing CR-03): reject if real UUID already held.
    // Then rekey:
    it->uuid = uuid; // overwrite sentinel with real UUID
    // ... proceed with existing passHello logic ...
}
```

**Disconnect-before-register handling:**

```cpp
// In onClientDisconnected:
QString const uuid = uuidForClient(client); // returns sentinel or real UUID
m_connections.erase(/* by socket pointer */);
client->deleteLater();
// Only emit pluginDisconnected for real (non-sentinel) UUIDs:
if (!uuid.isEmpty() && !uuid.startsWith(QStringLiteral("__pending__"))) {
    emit pluginDisconnected(uuid);
} else {
    AJAZZ_LOG_INFO("plugin-server", "pre-registration client disconnected (sentinel)");
}
```

**Crash-window guard in PluginManager:**

```cpp
// In onProcessFailed:
// Pre-registration exits must not count toward the disable window.
// The process failed before its socket sent registerPlugin.
// The sentinel UUID means no PluginConnection entry with the real pluginId exists.
// If m_live does not contain uuid, this is a pre-registration exit -> skip crash credit.
if (m_live.find(uuid) == m_live.end()) {
    AJAZZ_LOG_INFO("plugin-manager", "pre-registration exit for {} — not counted", ...);
    return; // sentinel path: no crash credit, no respawn
}
// ... existing crash-window logic ...
```

**[VERIFIED: codebase]** — `SdPluginServer::m_connections` is a `std::vector<PluginConnection>` (line 230 sd_plugin_server.hpp); `PluginConnection::uuid` is a `QString` initialized empty on connect (line 171 sd_plugin_server.cpp). The sentinel replaces the empty-UUID convention with a unique-per-connection identifier.

### Pattern 2: IPluginHost2 Unified Contract

**What:** A new abstract interface that subsumes both `PluginManager`'s `.sdPlugin` surface and `OutOfProcessPluginHost`'s Python surface under one type.

**Recommended minimal method surface:**

```cpp
// Source: to be created as src/app/src/i_plugin_host2.hpp
class IPluginHost2 {
public:
    virtual ~IPluginHost2() = default;
    // Spawn a plugin from its manifest. Runtime (Node/HTML/native/Python) is
    // determined by the manifest's CodePath extension or type discriminator.
    virtual void spawn(PluginManifest const& manifest) = 0;
    // Graceful shutdown: send exitApp, wait, kill.
    virtual void shutdown() = 0;
    // Lifecycle signals (bridge via Qt signal or callback registration).
    // Returns count of currently connected (registered) plugins.
    [[nodiscard]] virtual int connectedPluginCount() const noexcept = 0;
    // Returns list of live plugin metadata (for LoadedPluginsModel).
    [[nodiscard]] virtual std::vector<PluginInfo> plugins() = 0;
    // Dispatch: route a plugin action to the appropriate handler.
    virtual bool dispatch(QString const& pluginUuid,
                          QString const& actionId,
                          QJsonObject const& payload) = 0;
    // Access the underlying server for sendEvent (host->plugin outbound).
    [[nodiscard]] virtual SdPluginServer* pluginServer() const noexcept = 0;
};
```

**[ASSUMED]** — The exact method surface is Claude's discretion per CONTEXT.md. The above reflects what PluginManager and OutOfProcessPluginHost currently expose combined.

**Unification approach (recommended):** Rather than rewriting `PluginManager` from scratch, introduce `IPluginHost2` as a thin wrapper that `PluginManager` satisfies (add the interface as a base class). For Python, `OutOfProcessPluginHost` continues to be owned as a sub-host. A single `UnifiedPluginHost` class aggregates both and presents the `IPluginHost2` surface to `Application`. The Python path's `plugins()` call is merged with the `.sdPlugin` path's live manifest inventory.

### Pattern 3: CI Grep Gate (QHostAddress::Any + SIGPIPE)

**What:** A CI step on Linux that runs `grep -rn "QHostAddress::Any" src/` and fails if any hits come from code (not comments). A second assertion verifies `grep -n "SIGPIPE" src/app/src/main.cpp` returns a match.

**Current state (verified in codebase):**

- `grep -rn "QHostAddress::Any" src/` → only 2 comment hits in `sd_plugin_server.cpp` and `sd_plugin_server.hpp` [VERIFIED: codebase]
- `grep -n "SIGPIPE" src/app/src/main.cpp` → line 55: `::signal(SIGPIPE, SIG_IGN)` [VERIFIED: codebase]
- No CI step currently asserts either. [VERIFIED: .github/workflows/ci.yml]

**Gate implementation (to add to ci.yml Linux-only step):**

```yaml
- name: Enforce loopback-only + SIGPIPE invariants (Phase 30)
  if: runner.os == 'Linux'
  run: |
    # QHostAddress::Any must appear only in comments, never in live code.
    if grep -rn 'QHostAddress::Any' src/ | grep -v '^\s*//' | grep -v '^\s*\*'; then
      echo "::error::QHostAddress::Any found in non-comment source — loopback-only violated."
      exit 1
    fi
    # SIGPIPE handler must be present in main.cpp.
    if ! grep -n 'SIGPIPE' src/app/src/main.cpp | grep -q 'SIG_IGN'; then
      echo "::error::SIGPIPE SIG_IGN handler missing from main.cpp."
      exit 1
    fi
    echo "Loopback-only + SIGPIPE invariants OK."
```

**[VERIFIED: codebase]** — Existing CI gate pattern confirmed in `.github/workflows/ci.yml` lines 52-62 (hid_open invariant step).

### Pattern 4: IPluginHost2 ADR Structure

**What:** An Architecture Decision Record committed to `.planning/phases/30-plugin-host-modular-foundation/30-ADR-plugin-host-unification.md`.

**Required sections:**

1. Context: two separate host implementations existed (PluginManager for .sdPlugin, OutOfProcessPluginHost for Python)
1. Decision: UNIFY (document the user override of the research verdict)
1. Rationale: single lifecycle, single crash-window, single shutdown path, simplifies Phase 31+ work
1. Consequences: regression risk for each of the four runtime paths; mitigation = per-runtime no-regression tests
1. SKU boundary rule: `PluginManager` dispatch must have zero SKU strings; device I/O only via `PluginDeviceBridge` → `StreamDockControlService` → sidecar
1. Rejected alternative: "keep separate" (the v2.0 research verdict) — rejected because it leaves two lifecycle systems to maintain

### Anti-Patterns to Avoid

- **Destroying QProcess inside its own signal handler.** `PluginManager::onProcessFailed` already defers teardown via `QTimer::singleShot(0, ...)` to avoid this. The sentinel-UUID path must follow the same deferred-cleanup pattern. [VERIFIED: codebase — plugin_manager.cpp lines 640-643]
- **Caching raw QWebSocket pointers.** `SdPluginServer::sendEvent` re-resolves the socket on every call via `socketForUuid()`. The sentinel-UUID rekey must not cache the socket pointer between the connect and the registerPlugin message. [VERIFIED: codebase — sd_plugin_server.cpp line 472-485]
- **Double-firing onProcessFailed.** Qt emits both `errorOccurred(FailedToStart)` AND `finished(-2, CrashExit)` for a FailedToStart event. The existing CR-02 guard (plugin_manager.cpp line 420-428) routes only from `finished`, not `errorOccurred(FailedToStart)`. The new sentinel path must not break this guard.
- **SKU strings in plugin_manager.cpp.** Already zero hits; must stay zero after the refactor. `plugin_device_bridge.cpp` has two `"akp05e"` fallback strings at lines 1242 and 1251 — these are in the bridge (correct tier); do not move them into `plugin_manager.cpp`.
- **nlohmann::json in ajazz_core or installed headers.** COD-031. The unified host lives in `src/app/src/` and may use QJson (already the pattern). [VERIFIED: CLAUDE.md + plugin_manager.cpp line 14]
- **Counting pre-registration exits as plugin crashes.** A plugin process that exits before `registerPlugin` is a startup failure, not a runtime crash. The crash-disable window must not fire for it.

______________________________________________________________________

## Don't Hand-Roll

| Problem                       | Don't Build                                | Use Instead                                    | Why                                                                  |
| ----------------------------- | ------------------------------------------ | ---------------------------------------------- | -------------------------------------------------------------------- |
| Unique sentinel ID generation | Custom UUID string builder                 | `QUuid::createUuid()`                          | Qt standard, collision-free, already used in codebase                |
| Process exit deferral         | Manual signal/slot dance to defer teardown | `QTimer::singleShot(0, this, ...)` lambda      | Already proven pattern in `onProcessFailed` (plugin_manager.cpp:642) |
| Crash-window timing           | Custom timestamp logic                     | `PluginCrashTracker` with injected clock       | Already exists, fully tested, clock-injectable                       |
| WebSocket server              | Custom TCP server                          | `QWebSocketServer` (existing `SdPluginServer`) | Already implements the full Elgato v6 protocol surface               |
| Python subprocess IPC         | New line-protocol implementation           | `OutOfProcessPluginHost` (existing)            | Already implements the full slice-3a/3b/3c/3d contract               |

**Key insight:** Phase 30 is an architecture refactor of existing machinery, not new construction. The crash tracker, WS server, Python host, and spawn dispatch all exist and pass 694 tests. The work is wiring them under a unified contract, patching the sentinel-UUID gap, and adding CI assertions.

______________________________________________________________________

## Runtime State Inventory

Phase 30 is a code-only refactor with no runtime state migration needed. The plugin directory layout, QSettings persisted-disabled set, and WebSocket protocol wire format are unchanged. Omitting this section.

______________________________________________________________________

## Common Pitfalls

### Pitfall 1: Socket-Dying-During-Rekey Race

**What goes wrong:** Between `onNewConnection()` inserting the sentinel and `registerPlugin` arriving, the WebSocket could disconnect. If `dispatchClientMessage` then tries to rekey a now-erased slot, it accesses stale memory or panics.

**Why it happens:** Qt's `QWebSocket::disconnected` fires on the event loop, which could interleave with `textMessageReceived` processing (both are Qt signals dispatched from the event queue).

**How to avoid:** In the `registerPlugin` handler, find the connection slot by socket pointer. After finding it, check `it->socket != nullptr` before accessing `it->uuid`. The `onClientDisconnected` nulls the socket pointer and calls `erase`, so the find-by-socket-pointer will simply not find the slot after disconnect.

**Warning signs:** Test: `disconnect-before-register` Catch2 case fails with SIGSEGV or fails the `connectedPluginCount() == 0` assertion.

### Pitfall 2: Double-Fire from QProcess FailedToStart (Existing CR-02)

**What goes wrong:** Qt fires both `QProcess::errorOccurred(FailedToStart)` AND `QProcess::finished(-2, CrashExit)` when a process cannot start. Without the CR-02 guard, `onProcessFailed` fires twice, recording two crash credits instead of one — allowing a plugin that fails to start twice to hit the 3-crash disable threshold prematurely.

**Why it happens:** Qt signal-slot machinery emits both. The existing fix (plugin_manager.cpp:420-428) routes `FailedToStart` exclusively through `finished`. The sentinel-UUID refactor must not break this routing.

**How to avoid:** Keep the existing CR-02 guard. Do not add a new `errorOccurred` → `onProcessFailed` connection for the sentinel path. The sentinel pre-registration exit is handled purely at the `m_live.find()` check.

**Warning signs:** `PluginManagerTest FailedToStart fires onProcessFailed only once` test fails.

### Pitfall 3: Sentinel UUID Leaking into Crash Window

**What goes wrong:** If `PluginManager::onProcessFailed` is called for a plugin that exits before `registerPlugin`, and the crash tracker records a crash credit for the sentinel UUID, the same plugin will be blocked from starting again (wrong UUID in `m_disabled`).

**Why it happens:** The process exits before `registerPlugin` — `m_live` contains the process entry (under the plugin's `pluginId`), but `SdPluginServer::m_connections` has the sentinel. The crash window check must use `m_live` as the authority, not the WS connection map.

**How to avoid:** In `onProcessFailed(uuid)`, check `m_live.find(uuid) == m_live.end()` before recording the crash. If the entry is absent, it means the process failed before the WebSocket registered — treat as a pre-registration startup failure, not a crash. Log it, do not increment the crash counter.

**Warning signs:** Catch2 test for `disconnect-before-register` shows `isDisabled()` returns true after one exit.

### Pitfall 4: HTML Plugin Process is nullptr in m_live (Existing WR-02)

**What goes wrong:** HTML plugins run in-process via `QWebEnginePage` and have `process == nullptr` in the `LivePlugin` struct. If `handleProcessFailure` tries to restart them by calling `spawn()`, it would re-inject the Mirabox shim into an already-live page.

**Why it happens:** `QWebEnginePage` crash signals are handled differently from `QProcess`; the existing code guards this with `if (it->second.process != nullptr)` in `handleProcessFailure`.

**How to avoid:** The guard already exists (plugin_manager.cpp:662). Ensure the IPluginHost2 unification does not remove this guard.

**Warning signs:** `PluginManagerTest HTML plugin is not re-spawned on failure` fails.

### Pitfall 5: Pre-existing ASan Failures in Plugin Crash Tests

**What goes wrong:** Two tests (`test_plugin_lifecycle.cpp` ~line 806, `test_plugin_concurrency.cpp`) exhibit SIGSEGV under ASan-instrumented dev builds. These are pre-existing and not caused by Phase 30 changes.

**Why it happens:** Suspected heap-use-after-free in crash-tracker state cleanup under ASan, not reproducible in release/CI. Documented in `.planning/codebase/CONCERNS.md`.

**How to avoid:** Do not confuse Phase 30 ASan failures with pre-existing ones. Run `ctest --preset linux-release` (not ASan) as the baseline. If new ASan failures appear on tests that previously passed, those ARE Phase 30 regressions.

**Warning signs:** ASan failures on tests that pass in release mode — verify they were pre-existing before blaming Phase 30.

### Pitfall 6: IPluginHost (old) vs IPluginHost2 Name Confusion

**What goes wrong:** The existing `IPluginHost` in `src/plugins/include/ajazz/plugins/i_plugin_host.hpp` is the Python host interface, implemented only by `OutOfProcessPluginHost`. `PluginManager` does NOT implement this interface. Confusing the two leads to a plan that tries to make `PluginManager` conform to the OLD Python interface instead of introducing the new unified one.

**Why it happens:** The names are similar; the documentation comment in `i_plugin_host.hpp` does not explicitly say "Python-only".

**How to avoid:** The new unified interface should be named `IPluginHost2` (or `IUnifiedPluginHost`) and live in `src/app/src/` (not in `src/plugins/include/`), since it depends on Qt types (`QJsonObject`, `QString`) that the `ajazz_plugins` library layer should not import.

______________________________________________________________________

## Code Examples

### Existing Crash-Window Lifecycle (verified, preserve exactly)

```cpp
// Source: src/app/src/plugin_manager.cpp — onProcessFailed()
void PluginManager::onProcessFailed(QString const& uuid) {
    qint64 const now = m_clock();
    m_crashTracker.recordCrash(uuid, now);
    if (m_crashTracker.shouldDisable(uuid, now)) {
        disableWithNotice(uuid, QStringLiteral("crashed 3 times within 30 seconds"));
    }
    // Deferred teardown to avoid destroying QProcess from within its own signal.
    if (!m_failurePending.contains(uuid)) {
        m_failurePending.insert(uuid);
        QTimer::singleShot(0, this, [this, uuid]() { handleProcessFailure(uuid); });
    }
}
```

### Existing SdPluginServer Connection Table (verified, base for sentinel extension)

```cpp
// Source: src/app/src/sd_plugin_server.hpp — SdPluginServer::PluginConnection
struct PluginConnection {
    QString uuid;              // Empty until registerPlugin; sentinel after onNewConnection
    QWebSocket* socket{nullptr};
    QString salt;
    int authAttempts{0};
    bool authenticated{false};
};
std::vector<PluginConnection> m_connections;
```

### Existing sendEvent Pattern (verified, T-17-UAF guard)

```cpp
// Source: src/app/src/sd_plugin_server.cpp — socketForUuid()
// Re-resolve socket on every call; never cache raw pointer between calls.
QWebSocket* SdPluginServer::socketForUuid(QString const& uuid) const {
    auto it = std::find_if(m_connections.begin(), m_connections.end(),
        [&uuid](auto const& c) { return c.uuid == uuid && c.socket != nullptr; });
    if (it == m_connections.end()) return nullptr;
    return it->socket;
}
```

### Existing Loopback-Only Bind (verified, satisfies HOST-03 partial)

```cpp
// Source: src/app/src/sd_plugin_server.cpp:92
// SECURITY-CRITICAL: always LocalHost, never Any.
auto const bound = m_server->listen(QHostAddress::LocalHost, port);
```

### Existing SIGPIPE Handler (verified, satisfies criterion #3 half)

```cpp
// Source: src/app/src/main.cpp:55
::signal(SIGPIPE, SIG_IGN);
```

______________________________________________________________________

## State of the Art

| Old Approach                                                               | Current Approach                                           | When Changed | Impact                                                  |
| -------------------------------------------------------------------------- | ---------------------------------------------------------- | ------------ | ------------------------------------------------------- |
| Two separate host pointers in Application (m_pluginManager + m_pluginHost) | Unified behind IPluginHost2                                | Phase 30     | Single lifecycle surface; ADR documents the decision    |
| No pre-registration tracking (empty UUID in m_connections)                 | Sentinel UUID inserted at connect, rekey at registerPlugin | Phase 30     | Pre-registration exits no longer cause dangling state   |
| No CI gate for QHostAddress::Any / SIGPIPE                                 | CI grep gate on Linux step                                 | Phase 30     | Permanent regression protection for security invariants |
| PluginManager dispatch branches per runtime without contract               | IPluginHost2 contract routes all four runtimes             | Phase 30     | HOST-01 satisfied                                       |

**Deprecated/outdated:**

- The "keep separate" research verdict in STATE.md (line 79: "IPluginHost unification — Phase 30 ADR to decide... research verdict is 'keep separate in v2.0'") is superseded by the user's CONTEXT.md override. The ADR must document "UNIFY" with rationale.

______________________________________________________________________

## Assumptions Log

| #   | Claim                                                                                                                                                             | Section               | Risk if Wrong                                                                                                                                                                                              |
| --- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | `IPluginHost2` is the correct name for the new unified interface.                                                                                                 | Standard Stack        | Low — name is Claude's discretion per CONTEXT.md; plannable under any name.                                                                                                                                |
| A2  | `plugin_device_bridge.cpp` lines 1242/1251 `"akp05e"` fallbacks are the ONLY SKU strings in the plugin layer (outside plugin_manager.cpp which is already clean). | Architecture Patterns | Medium — if more SKU strings exist in other plugin-layer files, HOST-03 code-review audit would catch them; risk is they silently remain. Run: `grep -rn "akp05e\|akp03\|akp153" src/app/src/` to confirm. |
| A3  | The existing ASan failures in `test_plugin_lifecycle.cpp` and `test_plugin_concurrency.cpp` are pre-existing and not caused by Phase 30.                          | Common Pitfalls       | Low — documented in CONCERNS.md; release CI does not trigger them.                                                                                                                                         |
| A4  | `UnifiedPluginHost` should live in `src/app/src/` (not in `src/plugins/`), since it depends on Qt types incompatible with the `ajazz_plugins` layer.              | Architecture Patterns | Low — if the decision reverses, the COD-031 boundary still holds because `ajazz_core` is the install-time concern, not `ajazz_plugins` itself.                                                             |

______________________________________________________________________

## Open Questions

1. **Python host IPluginHost2 surface mismatch**

   - What we know: `OutOfProcessPluginHost::dispatch()` takes `std::string_view pluginId, actionId, settingsJson`, while `SdPluginServer` routes by `QString pluginUuid` and `QJsonObject` payload.
   - What's unclear: whether the unified `IPluginHost2::dispatch()` should use Qt types (QString, QJsonObject) or STL types (std::string_view). Using Qt types means the Python host wrapper does a conversion; using STL types means the WS path does a conversion.
   - Recommendation: Use Qt types in `IPluginHost2` (the app-layer interface) and have the Python wrapper convert at the boundary. Keeps the interface consistent with the rest of `src/app/src/`.

1. **Python host's `loadAll()` and `addSearchPath()` have no `.sdPlugin` equivalent**

   - What we know: `OutOfProcessPluginHost` has `loadAll()` / `addSearchPath()` for directory scanning; `PluginManager` has `discover()` + `spawn()` + `rediscover()`.
   - What's unclear: whether `IPluginHost2` should expose `discover()` semantics or `loadAll()` semantics.
   - Recommendation: Expose `discover() + spawn()` on the unified interface; add a `loadAllPython()` internal call that wraps `OutOfProcessPluginHost::loadAll()` and `addSearchPath()`. The Python path's plugin list folds into `plugins()`.

______________________________________________________________________

## Environment Availability

| Dependency                          | Required By                             | Available                   | Version            | Fallback       |
| ----------------------------------- | --------------------------------------- | --------------------------- | ------------------ | -------------- |
| `ctest --preset linux-release`      | All test validation                     | Yes                         | CMake 3.x (system) | —              |
| `scripts/ajazz-debug` debug channel | Live behaviour verification             | Yes                         | In-tree script     | —              |
| Node.js 20+                         | Node plugin spawn (test probe injected) | Not required for unit tests | —                  | Fake NodeProbe |
| Python 3                            | Python host tests                       | Yes                         | 3.11 (CI)          | —              |

No missing dependencies block Phase 30 execution.

______________________________________________________________________

## Validation Architecture

### Test Framework

| Property           | Value                                      |
| ------------------ | ------------------------------------------ |
| Framework          | Catch2 (existing)                          |
| Config file        | CMakePresets.json (`linux-release` preset) |
| Quick run command  | `ctest --preset linux-release -R plugin`   |
| Full suite command | `ctest --preset linux-release`             |

### Phase Requirements → Test Map

| Req ID  | Behavior                                                                                  | Test Type        | Automated Command                                                 | File Exists?                              |
| ------- | ----------------------------------------------------------------------------------------- | ---------------- | ----------------------------------------------------------------- | ----------------------------------------- |
| HOST-01 | Node plugin spawns and registers via WS without SKU branches                              | unit             | `ctest --preset linux-release -R PluginManagerTest`               | Yes (test_plugin_lifecycle.cpp)           |
| HOST-01 | HTML plugin loads via QWebEnginePage without SKU branches                                 | unit             | `ctest --preset linux-release -R PluginManagerTest`               | Yes                                       |
| HOST-01 | Native plugin spawns without SKU branches                                                 | unit             | `ctest --preset linux-release -R PluginManagerTest`               | Yes                                       |
| HOST-01 | Python plugin dispatches via OutOfProcessPluginHost                                       | unit             | `ctest --preset linux-release -R OutOfProcess`                    | Yes (test_out_of_process_plugin_host.cpp) |
| HOST-02 | Disconnect-before-register: zero app crash, zero dangling pointer, connectedCount stays 0 | unit             | `ctest --preset linux-release -R disconnect.before.register`      | No — Wave 0 gap                           |
| HOST-02 | 3-in-30s crashes cause disable+notify, not restart                                        | unit             | `ctest --preset linux-release -R "crash 3 in 30s"`                | Yes (test_plugin_lifecycle.cpp)           |
| HOST-02 | Pre-registration exit does NOT count toward crash window                                  | unit             | `ctest --preset linux-release -R pre.registration.exit`           | No — Wave 0 gap                           |
| HOST-02 | exitApp is sent before QProcess::terminate on shutdown                                    | unit             | `ctest --preset linux-release -R "shutdown sends exitApp"`        | Yes                                       |
| HOST-03 | grep -rn "akp05e\|akp03\|akp153" src/app/src/plugin_manager.cpp returns 0                 | audit (one-time) | `grep -rn "akp05e\|akp03\|akp153" src/app/src/plugin_manager.cpp` | N/A (manual at audit)                     |
| HOST-03 | QHostAddress::Any not in live code                                                        | CI grep gate     | `grep -rn 'QHostAddress::Any' src/`                               | No — Wave 0 gap (CI step)                 |
| HOST-03 | SIGPIPE SIG_IGN confirmed in main.cpp                                                     | CI grep gate     | `grep -n 'SIGPIPE' src/app/src/main.cpp`                          | No — Wave 0 gap (CI step)                 |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R plugin`
- **Per wave merge:** `ctest --preset linux-release`
- **Phase gate:** Full suite green + `scripts/ajazz-debug plugin.list` returns `connectedCount: 0` (not a crash) in the disconnect-before-register scenario

### Wave 0 Gaps

- [ ] `tests/unit/test_plugin_host2.cpp` — covers `disconnect-before-register` (HOST-02), pre-registration exit not in crash window (HOST-02), IPluginHost2 interface conformance for all four runtimes (HOST-01)
- [ ] CI step in `.github/workflows/ci.yml` — `QHostAddress::Any` grep gate + SIGPIPE presence check (HOST-03)
- [ ] `.planning/phases/30-plugin-host-modular-foundation/30-ADR-plugin-host-unification.md` — IPluginHost2 UNIFY decision (criterion #5)

______________________________________________________________________

## Security Domain

### Applicable ASVS Categories

| ASVS Category         | Applies               | Standard Control                                         |
| --------------------- | --------------------- | -------------------------------------------------------- |
| V2 Authentication     | no                    | —                                                        |
| V3 Session Management | no                    | —                                                        |
| V4 Access Control     | yes (loopback-only)   | `QHostAddress::LocalHost` binding (existing)             |
| V5 Input Validation   | yes (UUID validation) | `isSafeUuidComponent()` in plugin_manager.cpp (existing) |
| V6 Cryptography       | no                    | —                                                        |

### Known Threat Patterns

| Pattern                                                | STRIDE                            | Standard Mitigation                                                                         |
| ------------------------------------------------------ | --------------------------------- | ------------------------------------------------------------------------------------------- |
| Pre-registration socket impersonation                  | Spoofing                          | Sentinel UUID + CR-03 impersonation guard (existing)                                        |
| Plugin UUID collision (two plugins claiming same UUID) | Spoofing                          | CR-03 guard in registerPlugin handler (existing)                                            |
| QHostAddress::Any exposure                             | Information Disclosure            | Loopback-only bind; CI-gated grep (Phase 30 addition)                                       |
| SIGPIPE on broken plugin IPC pipe killing host         | Denial of Service                 | `::signal(SIGPIPE, SIG_IGN)` in main.cpp (existing); CI-gated assertion (Phase 30 addition) |
| SKU strings in plugin dispatch layer                   | Information Disclosure / coupling | CODE-REVIEW-ONLY per CONTEXT.md (HOST-03)                                                   |

______________________________________________________________________

## Sources

### Primary (HIGH confidence)

- [VERIFIED: codebase] `src/app/src/sd_plugin_server.{cpp,hpp}` — SdPluginServer implementation, PluginConnection struct, sentinel-UUID design point
- [VERIFIED: codebase] `src/app/src/plugin_manager.{cpp,hpp}` — PluginManager spawn dispatch, crash lifecycle, m_live structure
- [VERIFIED: codebase] `src/plugins/include/ajazz/plugins/i_plugin_host.hpp` — existing IPluginHost interface (Python-only)
- [VERIFIED: codebase] `src/plugins/include/ajazz/plugins/out_of_process_plugin_host.hpp` — Python OOP host interface
- [VERIFIED: codebase] `src/app/src/plugin_crash_tracker.hpp` — 3-in-30s crash tracker
- [VERIFIED: codebase] `src/app/src/application.{cpp,hpp}` — how both hosts are wired into Application
- [VERIFIED: codebase] `.github/workflows/ci.yml` — existing grep gate pattern (hid_open invariant)
- [VERIFIED: codebase] `src/app/src/main.cpp:55` — SIGPIPE handler
- [VERIFIED: codebase] `tests/unit/test_plugin_lifecycle.cpp` — existing test patterns to follow
- [VERIFIED: codebase] `tests/unit/test_sd_plugin_server.cpp` — SdPluginServer test patterns
- [VERIFIED: codebase] `.planning/codebase/CONCERNS.md` — pre-existing ASan failure documentation

### Secondary (MEDIUM confidence)

- [CITED: .planning/phases/30-plugin-host-modular-foundation/30-CONTEXT.md] — user decisions, locked constraints
- [CITED: .planning/REQUIREMENTS.md] — HOST-01/02/03 requirement text
- [CITED: CLAUDE.md] — COD-031 boundary, debug-channel verification rule

______________________________________________________________________

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — all components are in-codebase verified
- Architecture (sentinel-UUID pattern): HIGH — directly derived from existing SdPluginServer code structure
- Architecture (IPluginHost2 surface): MEDIUM — method signatures are Claude's discretion; shape is recommended but not locked
- Pitfalls: HIGH — directly derived from existing code comments, test names, and CONCERNS.md
- CI gate implementation: HIGH — follows existing ci.yml pattern exactly

**Research date:** 2026-06-06
**Valid until:** Stable — this is a codebase analysis, not ecosystem research; valid until the source files change.
