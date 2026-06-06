---
status: testing
phase: 10-akp05e-3004-promotion
source: [derived from ROADMAP Phase 10 goal — no SUMMARY.md (implemented out-of-band, ahead of schedule)]
started: 2026-05-22T19:30:00Z
updated: 2026-05-22T19:30:00Z
---

## Current Test

<!-- OVERWRITE each test - shows where we are -->

number: 1
name: Push image to an LCD key
expected: |
With the AKP05E (0300:3004) connected, launch the app, select the AKP05E in
the sidebar, and assign/push an image to one of the 10 LCD keys. The image
appears on that physical key (and the write lands without needing a manual
flush).
awaiting: user response

## Tests

### 1. Push image to an LCD key

expected: Selecting the AKP05E and assigning an image to an LCD key shows that image on the physical key.
result: [pending]

### 2. Encoder rotate / press / release events

expected: Rotating one of the 4 endless encoders and pressing it produces live rotate/press/release feedback in the app (e.g., a value changes / event is reflected).
result: [pending]

### 3. Set per-key colour

expected: Setting a key's colour (solid colour fill) makes that key display the chosen colour on the device.
result: [pending]

### 4. Set global brightness

expected: Changing the global brightness control visibly changes the LCD brightness across the device's screens.
result: [pending]

### 5. Clear the device

expected: Triggering "clear" blanks all key LCDs / screens on the device.
result: [pending]

### 6. No false `clock` capability

expected: The AKP05E does NOT expose a time-sync / clock control in the UI (it has no RTC; the row must not advertise `clock`).
result: [pending]

## Summary

total: 6
passed: 0
issues: 0
pending: 6
skipped: 0

## Gaps

[none yet]
