---
status: human_needed
phase: 24-family-coverage-akp03-153-815
score: 9/9
verified: '2026-05-24T21:00:00Z'
reverification: false
requirement_ids: [DEVICES-10]
human_verification:
  - test: 'Live family coverage: with a physical AKP03, AKP153, and AKP815 connected (one at a time), confirm assign-image-and-press works on each via the capability-generic control service  -  image appears on a key, a key press fires the bound action; on AKP03 the 3 encoders rotate/press; AKP153/815 (no encoders) behave correctly. Per-family image format/rotation (incl. AKP815 Rot180) renders correctly.'
    expected: Each family paints keys and routes input through the generic services; no AKP05 geometry leaks (AKP153/815 key 15 paints; AKP03 6 LCD keys + 3 encoders); AKP815 strip rotation correct.
    why_human: All proven hardware-free with per-family MockTransport byte tests (632/632 incl. the 20-case StreamDockFamily suite); the physical AKP03/153/815 are not the connected device (only the AKP05E is). Live family witnesses are Phase 25, and several wire values (AKP815 strip 800x480, akp153 release encoding) remain RE-provisional pending hardware capture.
---

# Phase 24: Family Coverage AKP03/153/815 - Verification Report

**Phase Goal:** The same assign-image-and-press flow works across the AKP03/153/815 families via the capability-generic control service.

**Status:** human_needed (9/9 automated must-haves verified; 1 live-family item + RE-provisional values deferred to Phase 25)
**Verified by:** orchestrator inline (verifier subagent conserved - weekly usage limit). Evidence by code inspection + RE cross-check + the full app+qml+tests build/test gate.

## Must-Haves Verified (9/9)

1. **DEVICES-10 (family coverage):** REQUIREMENTS.md DEVICES-10 = Complete. `makeAkp03 / makeAkp153 / makeAkp815 WithTransport` DI seams (thin wrappers, no wire change - RE cross-checked); the control + input services drive all four families.
1. **Descriptor-driven geometry (no AKP05 hardcode):** `m_encoderCount` from `descriptor.encoderCount` (AKP03=3, AKP05=4, AKP153/815=0) with a `std::vector` accumulator; paint dims from `displayInfo().widthPx/heightPx` (not 85x85); touch dispatch gated on `m_hasTouchStrip`. The AKP05 path is not regressed.
1. **Capability honesty:** encoder/touch routing guarded by descriptor presence - AKP153/815 (no encoders/touch) never receive encoder/touch dispatch; encoder index bounded by `m_encoderCount`.
1. **CR-01 (akp03 LCD key count) FIXED:** `setKeyImage`/`clearKey` guard on `DisplayKeyCount` (6), not total `KeyCount` (9) - no image burst to the 3 non-LCD side buttons (RE: akp03.md "6 LCD keys"). Regression tests: keys 7-9 rejected, 1-6 paint.
1. **CR-02 (akp03 wire docs) FIXED:** doc/param names corrected to RE truth (60x60 JPEG, 3 encoders); no wire bytes changed.
1. **WR-05 (AKP03 encoder release) documented + tested:** the misleading "dormant" comment corrected; AKP03 v3 real `EncoderReleased` passes through dispatch without crash (full routing to `onRelease` is a tracked follow-up - `EncoderBinding` has no `onRelease` field yet).
1. **RE-doc integrity (WR-01/02/03):** AKP815 strip-size matrix contradiction reconciled to 800x480 (marked provisional); akp153 wrong USB-ID constant names `@deprecated`-annotated; akp153 release-encoding honestly left as a TODO referencing the akp153.md gap (NOT invented).
1. **WR-04 plus IN doc fixes:** input-service docstring de-AKP05E-ified; stale geometry comments updated.
1. **Family byte tests + build:** 20-case `test_stream_dock_family.cpp` (per-family BAT+ULEND, key-15 no-clamp, AKP03 3-encoder, 0-encoder inert path). COD-031; device-yank try/catch preserved. app+qml+unit clean under -Werror; ctest 632/632.

## Human Verification Required (1 - Phase 25)

See `human_verification` frontmatter: live AKP03/153/815 assign-image-and-press + RE-provisional value confirmation.
