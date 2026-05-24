---
status: human_needed
phase: 20-property-inspector-settings
score: 9/9
verified: '2026-05-24T18:00:00Z'
reverification: false
requirement_ids: [PLUGIN-09, PLUGIN-13]
human_verification:
  - test: 'Visual PI render: load a real .sdPlugin with a Property Inspector HTML; confirm the PI renders in the WebEngine view (sdpi.css applied), settings widgets work, and getSettings/setSettings round-trip through the live PI UI.'
    expected: The PI HTML renders styled with sdpi.css; changing a setting in the PI persists and is restored on reload; sendToPlugin/sendToPropertyInspector relay correctly.
    why_human: The C++/JS bridge, persistence, shims, URL policy, size caps and the object-payload relay are all proven hardware-free (538/538 incl. loopback + persistence + sendToPlugin tests); the actual visual rendering of a real plugin's PI in WebEngine is a human/visual check deferred to Phase 25.
---

# Phase 20: Property Inspector + Settings — Verification Report

**Phase Goal:** Each action has its settings UI (Property Inspector) and settings persist.

**Status:** human_needed (9/9 automated must-haves verified; 1 visual-render item deferred to Phase 25)
**Verified by:** orchestrator inline (verifier subagent conserved due to weekly usage limit). Evidence by direct code inspection + the full app+qml+tests build/test gate.

## Must-Haves Verified (9/9)

1. **PLUGIN-09 (PI renders in WebEngine):** `loadInspector` creates a per-plugin `QQuickWebEngineProfile`, injects `makeCefQueryShim()` + `makeMiraboxShim()` via `profile->userScripts()->insert(...)`, and exposes the `$SD` `PIBridge` over a `QQmlWebChannel` (Qt6 WebChannelQuick — the project's documented gotcha). `Inspector.qml`/`PIWebView.qml` drive load/close on action selection. REQUIREMENTS.md PLUGIN-09 = Complete.
1. **PLUGIN-13 (settings persist):** `getSettings`/`setSettings` (per-context) + `getGlobalSettings`/`setGlobalSettings` (plugin-wide) persist to disk and survive restart — proven by the 20-01 round-trip tests. REQUIREMENTS.md PLUGIN-13 = Complete.
1. **sdpi.css served from built-in URL:** bundled qrc resource + interceptor redirect (`isSdpiCssRequest`).
1. **Per-plugin profile isolation:** each plugin gets its own profile (cookie/cache/storage isolation); context-isolation test asserts ctx-A settings invisible to ctx-B.
1. **CR-01 (object payload relay) FIXED:** `sendToPlugin` re-serializes object payloads via `QJsonDocument(v.toObject()).toJson(Compact)` instead of dropping them with `.toString()`. Regression test present.
1. **WR-01 (nav escape) FIXED:** `PIWebView.qml onNavigationRequested` denies-by-default — only `activeUrl` allowed, all other navigations `IgnoreRequest` (defense-in-depth over the C++ interceptor).
1. **WR-02 (relay/log DoS) FIXED:** `sendToPlugin` capped at `kMaxRelayBytes` (1 MiB), `logMessage` at `kMaxLogMessageBytes` (64 KiB).
1. **WR-03 (read DoS) FIXED:** `readJsonOrEmpty` checks `in.size()` against the cap before `readAll()`.
1. **Path-traversal refusal + ASCII UUID:** settings path-component sanitization rejects `../` (20-01 tests) and (IN-01) non-ASCII UUID components. COD-031 (QJsonObject, no nlohmann leak). Build: app+qml+unit clean under -Werror; ctest 538/538.

## Human Verification Required (1 — Phase 25)

See `human_verification` frontmatter: visual render of a real plugin's PI in the WebEngine view.
