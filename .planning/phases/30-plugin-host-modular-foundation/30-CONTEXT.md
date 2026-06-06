# Phase 30: Plugin-Host Modular Foundation - Context

**Gathered:** 2026-06-06
**Status:** Ready for planning
**Mode:** Smart discuss (autonomous)

<domain>
## Phase Boundary

The plugin layer gets a clean, modular contract: all runtimes (Node, HTML/QWebEngine,
native, Python) share one spawn / lifecycle / IPC interface; a plugin crash mid-handshake
(before its contexts register) never brings down the app; device SKU details never bleed
into plugin code. Delivers HOST-01, HOST-02, HOST-03 plus the IPluginHost2 unification ADR.

In scope: `IPluginHost` contract design, `PluginManager` dispatch refactor, pre-registration
crash isolation, SKU/wire decoupling of the plugin layer, lifecycle behaviours
(3-in-30s disable+notify, restart, exitApp), the ADR.

Out of scope: ActionInstance model (Phase 31), binding/wire fixes (Phase 32),
Property Inspector (Phase 33).

</domain>

<decisions>
## Implementation Decisions

### Plugin-Host Contract Shape

- **IPluginHost2 unification scope: FULL UNIFICATION NOW.** Collapse the Node, HTML,
  native, and Python runtimes onto a single shared host implementation in Phase 30 — not
  document-and-defer. `PluginManager` dispatch is refactored onto the one contract.
  - ⚠️ **This is an explicit user override of the v2.0 research verdict** (STATE.md /
    research SUMMARY recommended "keep `.sdPlugin` WS path and Python OOP path separate
    in v2.0"). The ADR (success criterion #5) must therefore document a **unify** decision
    with rationale, NOT a defer decision. The planner must scope for the larger refactor.
  - **Regression budget:** because all four runtime paths move onto one host, each runtime
    (Node, HTML, native, Python) needs an explicit no-regression check, not just the
    disconnect-before-register Catch2 test. Success criterion #1 ("no behavioural regression
    vs current per-runtime paths") applies per runtime.
- **Pre-registration crash tracking: SINGLE MAP + SENTINEL UUID.** Insert a freshly
  connected socket into the live connection map under a synthetic sentinel UUID until
  `registerPlugin` arrives, then rekey to the real UUID. Pre-registration exits must NOT
  count toward the 3-crashes-in-30s disable window. Guard the rekey path against races
  (socket dying during rekey) — this is the known subtlety of this approach.
- **SKU-decoupling enforcement (HOST-03): CODE-REVIEW ONLY.** Document the boundary in the
  ADR / CLAUDE.md; do NOT add a CI grep gate for `akp05e|akp03|akp153` in
  `plugin_manager.cpp`. Success criterion #2's grep is a one-time manual verification at
  audit time, not a permanent CI test. (Note: the *separate* QHostAddress::Any + SIGPIPE
  CI gate from success criterion #3 is unaffected by this and still applies.)

### Claude's Discretion

- Exact interface method names / file layout for the unified `IPluginHost` contract.
- Internal data-structure choices beyond the sentinel-UUID decision above.
- ADR filename and section structure (must land in `.planning/phases/30-*/`).

</decisions>

\<code_context>

## Existing Code Insights

### Reusable Assets

- `src/plugins/include/ajazz/plugins/i_plugin_host.hpp` — an `IPluginHost` interface
  already exists; the unification builds on / replaces this rather than greenfielding.
- `src/plugins/include/ajazz/plugins/out_of_process_plugin_host.hpp` — existing OOP host.
- `src/app/src/plugin_manager.{cpp,hpp}` (~37 KB / ~18 KB) — the dispatch layer to refactor.
- `src/app/src/sd_plugin_server.{cpp,hpp}` — the `.sdPlugin` WebSocket server; already
  documents the security delta from vendor's `QHostAddress::Any` bind.
- `loaded_plugins_model.{cpp,hpp}`, `application.hpp` — reference `IPluginHost`.

### Established Patterns

- SIGPIPE is already ignored process-wide in `main.cpp:55` (`::signal(SIGPIPE, SIG_IGN)`)
  — success criterion #3's SIGPIPE half is already satisfied; just needs a CI assertion.
- `QHostAddress::Any` appears ONLY in comments in `sd_plugin_server.{cpp,hpp}` (documenting
  the security delta), not in any actual bind — success criterion #3's grep half is already
  satisfied; needs the CI gate added.
- COD-031 boundary: no `nlohmann::json` in `ajazz_core` or installed headers (plugin layer
  may use it PRIVATE-linked).

### Integration Points

- Device I/O from plugins must route ONLY through
  `PluginDeviceBridge` → `StreamDockControlService` → mirajazz sidecar edge (HOST-03).
- Lifecycle: 3-in-30s disable+notify, restart, `exitApp` already exist and must be preserved.

\</code_context>

<specifics>
## Specific Ideas

- The disconnect-before-register Catch2 test must verify BOTH zero crash AND zero dangling
  pointer, and `scripts/ajazz-debug plugin.list` must return 0 connected (not a crash) in
  that scenario.
- Per CLAUDE.md debug-channel rule: verify the lifecycle behaviours live via the debug
  channel before declaring done, not just via ctest.

</specifics>

<deferred>
## Deferred Ideas

- Adding a permanent CI grep gate for SKU strings in the plugin layer — user chose
  code-review-only for now; could be revisited in Phase 35 (security hardening) if desired.

</deferred>
