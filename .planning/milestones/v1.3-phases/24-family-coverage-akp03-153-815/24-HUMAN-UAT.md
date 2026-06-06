---
status: partial
phase: 24-family-coverage-akp03-153-815
source: [24-VERIFICATION.md]
started: '2026-05-24T21:00:00Z'
updated: '2026-05-24T21:00:00Z'
deferred_to_phase: 25
---

## Current Test

[awaiting live AKP03/153/815 family hardware - deferred to Phase 25]

## Tests

### 1. Live family assign-image-and-press (DEVICES-10)

expected: With a physical AKP03, AKP153, and AKP815 (one at a time), the capability-generic control service paints a key image and a key press fires the bound action on each; AKP03's 3 encoders rotate/press; AKP153/815 (no encoders) behave correctly with no spurious encoder/touch routing; per-family image format/rotation (incl. AKP815 Rot180) renders correctly.
result: [pending]

### 2. RE-provisional wire values confirmation

expected: Hardware capture confirms (or corrects) the provisional values flagged in Phase 24 - AKP815 touch-strip size (in-code 800x480 vs an earlier 854x480 matrix entry) and the AKP153 encoder/key release-byte encoding (currently undocumented in akp153.md -> release events not yet dispatched).
result: [pending]

## Summary

total: 2
passed: 0
issues: 0
pending: 2
skipped: 0
blocked: 0

## Gaps
