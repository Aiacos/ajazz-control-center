---
status: partial
phase: 32-binding-layer-fix-multi-toggle-action-device-editor
source: [32-VERIFICATION.md]
started: 2026-06-08
updated: 2026-06-08
---

## Current Test

[awaiting human / a binding-authoring path]

## Tests

### 1. Multi Action live sequencing (BIND-04)

expected: Bind a 2-child Multi Action to a key; `scripts/ajazz-debug input.key` on that key shows both children firing in order in `plugin.protocolLog` with the per-child inter-step delay between them.
result: [pending — needs a debug-channel instance-commit RPC or a hand-authored profile JSON to create a Multi Action binding; logic is unit-covered (test_multiaction_dispatch.cpp) and the input->dispatch pipeline is live-confirmed]

### 2. Toggle Action live cycle + persistence (BIND-05/07)

expected: Bind a 3-state Toggle to a key; `input.key` x3 cycles currentState 0->1->2->0 with the key face image changing per `screenshot`; relaunch the app and confirm the key reopens at the last `currentState` (persisted to profile JSON).
result: [pending — same authoring-path gap; cycle-mod-N + persistence are unit-covered (test_toggle_dispatch.cpp incl. reload-from-disk); renderToggleState coordinate conversion verified by code analysis vs the working populateContextsForActivePage path]

## Summary

total: 2
passed: 0
issues: 0
pending: 2
skipped: 0
blocked: 0

## Gaps

- Both items require a way to CREATE a Multi/Toggle ActionInstance binding from outside C++ (a debug-channel `profile.commitInstance`-style RPC, or the binding UI). That authoring path is not yet wired. Until then these live walks cannot be performed; the underlying logic is fully unit-tested and the dispatch + EDIT-01 surfaces were live-confirmed via the debug channel (see 32-REVIEW-DISPOSITION.md).
- Follow-up coverage: add a `renderToggleState` coordinate-conversion test in `test_plugin_device_bridge.cpp` (the current toggle test stubs the render hook).
