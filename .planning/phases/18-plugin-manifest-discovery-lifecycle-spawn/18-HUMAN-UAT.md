---
status: partial
phase: 18-plugin-manifest-discovery-lifecycle-spawn
source: [18-VERIFICATION.md]
started: '2026-05-24T15:30:00Z'
updated: '2026-05-24T15:30:00Z'
deferred_to_phase: 19
---

## Current Test

[WebEngine-path runtime confirmation + HTML-plugin page-load wiring — deferred to Phase 19/20]

## Tests

### 1. HTML-plugin in-process page-load (PLUGIN-08, WebEngine path)

expected: An HTML plugin's index.html actually loads into a Chromium/WebEngine view with the Mirabox shim injected. NOTE: Phase 18 honestly DEFERS the page-load to the Phase 19/20 bridge/PI WebEngine surface — the plugin is tracked in m_live (lifecycle active) and the shim is injected into the profile, but the page-load itself is logged as deferred. This item closes when Phase 19/20 wires the plugin WebEngine view.
result: [pending — deferred to Phase 19/20]

### 2. Real system-node end-to-end spawn

expected: With a real system node >=20 installed, a real node-runtime .sdPlugin is discovered, validated, extracted, spawned, connects back to SdPluginServer, and the crash/restart/exitApp lifecycle behaves. (Code is proven hardware-free with injected fakes; this is the live-node integration smoke.)
result: [pending]

### 3. WebEngine-gated shim-injection test under app build

expected: The "mirabox shim injects at document creation" test (compiled out of the unit build, which lacks AJAZZ_HAVE_WEBENGINE) passes when run against an AJAZZ_HAVE_WEBENGINE build.
result: [pending]

## Notes

- Verifier item "ctest returns No tests found" was a STALE-REGISTRATION false alarm: `cmake --build build/linux-release && ctest --preset linux-release` reports 484/484 passing (confirmed by orchestrator). Not a gap.

## Summary

total: 3
passed: 0
issues: 0
pending: 3
skipped: 0
blocked: 0

## Gaps
