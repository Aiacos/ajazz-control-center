---
status: human_needed
phase: 19-device-plugin-bridge
score: 10/10
verified: '2026-05-24T17:00:00Z'
reverification: false
requirement_ids: [PLUGIN-10]
human_verification:
  - test: 'Live plugin↔device round-trip on a physical AKP05E: a real .sdPlugin paints a key (setImage) and a physical key press/encoder turn/touch reaches the plugin (keyDown/dialRotate/touchTap).'
    expected: The plugin's image appears on the physical key within ~1s; pressing that key delivers a keyDown event to the plugin over its loopback socket.
    why_human: Proven hardware-free via MockTransport (device) + loopback WebSocket (plugin) with 32+ e2e tests; the live demoable round-trip on real hardware is the Phase 25 witness (VERIFY-05/06).
---

# Phase 19: Device ↔ Plugin Bridge — Verification Report

**Phase Goal:** A plugin paints a key and a physical press/turn reaches the plugin — the convergence of the device slice and the SDK. First demoable plugin↔device round-trip.

**Status:** human_needed (10/10 automated must-haves verified; 1 physical-demo item deferred to Phase 25 by design)
**Verified by:** orchestrator inline (verifier subagent unavailable — weekly usage limit). Evidence gathered by direct code inspection + the build/test gate.

## Design context

HARDWARE-FREE by design: the bidirectional round-trip is proven with MockTransport (device side) + loopback `QWebSocket` (plugin side) — 32+ e2e tests. The live demoable round-trip on a physical AKP05E with a real `.sdPlugin` is the Phase 25 witness. "No physical device" is therefore not a gap.

## Must-Haves Verified (10/10)

1. **PLUGIN-10 inbound (plugin → device):** `onSetImage` strips `data:` URI → decode → scale → JPEG q85 → routes through the Phase-14 `StreamDockControlService` to the correct 1-based key. Proven by the control-service-spy loopback e2e test. REQUIREMENTS.md PLUGIN-10 = Complete.
1. **PLUGIN-10 outbound (device → plugin):** physical `keyDown`/`keyUp`, `dialRotate`/`dialDown`/`dialUp`, `touchTap` map to the §4.4 envelope and reach the bound plugin via `SdPluginServer::sendEvent`. Proven by loopback e2e tests.
1. **Lifecycle:** `willAppear`/`willDisappear`/`deviceDidConnect` fire on context/page/device transitions.
1. **CR-01 (image OOM bound):** `decodeDataUriImage` enforces `kMaxBase64Bytes` (512 KB) on the base64 body AND `kMaxRawBytes` (384 KB) on decoded bytes before `QImage::loadFromData`; oversize → logged no-op/placeholder, no crash. Security test present.
1. **CR-02 (cross-device isolation):** `ContextRegistry::coordKey` includes `deviceId` as its first component; `onDeviceEvent` routes by `deviceId` (no `Q_UNUSED`). Two-device isolation test asserts no cross-routing.
1. **WR-01:** `onDeviceDisconnected` routes through `retirePageContexts` → `willDisappear` per context.
1. **WR-02:** `StreamDockControlService::pageNavigated` is emitted in `navigatePage` and wired to `PluginDeviceBridge::onActivePageChanged` in `application.cpp:418-420` — page changes now fire willAppear/willDisappear.
1. **WR-03:** hardcoded `"akp05e"` removed; `m_activeDeviceId` threaded through register/disconnect.
1. **Security invariants intact:** Phase-17 auth gate (only authenticated plugin drives device); T-19-xplugin cross-plugin ownership check (`ctx.pluginUuid` must match sender — plugin A cannot paint plugin B's key); T-19-leak outbound `sendEvent` targets only the owning plugin (no broadcast); Phase-14 device-yank try/catch on paint. COD-031 (QJsonObject, no nlohmann leak).
1. **Build + tests:** app + qml + unit targets build clean under -Werror (independently confirmed); `ctest --preset linux-release` = 519/519.

## Human Verification Required (1 — Phase 25)

See `human_verification` frontmatter: the live plugin↔device round-trip demo on physical AKP05E hardware (VERIFY-05/06).
