---
status: partial
phase: 16-device-controls-binding-persistence-pages
source: [16-VERIFICATION.md]
started: '2026-05-24T14:00:00Z'
updated: '2026-05-24T14:00:00Z'
deferred_to_phase: 25
---

## Current Test

[awaiting human testing on physical AKP05E — deferred to Phase 25 (VERIFY-05)]

## Tests

### 1. Brightness slider live response (DISPLAY-09)

expected: On a real AKP05E, dragging the brightness slider visibly dims/brightens the panel (debounced, no flooding).
result: [pending]

### 2. Clear-all live blanking (DISPLAY-09)

expected: Clicking "Clear all keys" blanks all physical keys on the panel.
result: [pending]

### 3. Binding persistence across real restart (PROFILE-01)

expected: Assign key/encoder/touch bindings, quit the app, relaunch with the device attached — bindings are restored and keys repaint.
result: [pending]

### 4. Touch-swipe page navigation (PROFILE-02)

expected: On a multi-page profile, swiping the touch strip navigates pages and the panel repaints the new page's keys.
result: [pending]

## Summary

total: 4
passed: 0
issues: 0
pending: 4
skipped: 0
blocked: 0

## Gaps
