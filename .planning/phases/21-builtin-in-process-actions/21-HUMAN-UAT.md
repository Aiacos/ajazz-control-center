---
status: partial
phase: 21-builtin-in-process-actions
source: [21-VERIFICATION.md]
started: '2026-05-24T19:00:00Z'
updated: '2026-05-24T19:00:00Z'
deferred_to_phase: 25
---

## Current Test

[awaiting live-environment integration of built-in actions — deferred to Phase 25]

## Tests

### 1. Live OS key synthesis (system.hotkey / multimedia / volume)

expected: On a platform that permits synthesis (build with AJAZZ_FEATURE_INPUT_SYNTH), a bound hotkey/media/volume key actuates the OS; on Wayland (uinput restricted) the action degrades gracefully (logged, no crash) rather than silently failing.
result: [pending]

### 2. Live OBS action (obsstudio, auth-on)

expected: With OBS + obs-websocket v5 (auth enabled) running, a bound scene-switch/record/stream-toggle drives OBS with the correct password; a wrong/missing password yields authFailed, refuses to connect unauthenticated, and does not retry on every press.
result: [pending]

## Summary

total: 2
passed: 0
issues: 0
pending: 2
skipped: 0
blocked: 0

## Gaps
