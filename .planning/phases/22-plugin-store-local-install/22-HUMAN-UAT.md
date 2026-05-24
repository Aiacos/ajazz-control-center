---
status: partial
phase: 22-plugin-store-local-install
source: [22-VERIFICATION.md]
started: '2026-05-24T20:00:00Z'
updated: '2026-05-24T20:00:00Z'
deferred_to_phase: 25
---

## Current Test

[awaiting live local install of a real signed .sdPlugin through the UI — deferred to Phase 25]

## Tests

### 1. Live local install + signature gate + offline silence (PLUGIN-14)

expected: Through PluginStore's FileDialog, picking a real SIGNED .sdPlugin/.zip extracts, passes the Ed25519 verify gate, lands in installedPlugins/, and appears in the catalog; a TAMPERED/UNSIGNED package is Refused and not installed; with the online-catalog opt-in OFF, no outbound vendor (Mirabox/Aliyun) network request fires, including on Refresh.
result: [pending]

## Summary

total: 1
passed: 0
issues: 0
pending: 1
skipped: 0
blocked: 0

## Gaps
