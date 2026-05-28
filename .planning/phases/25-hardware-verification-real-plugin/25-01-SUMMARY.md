---
phase: 25-hardware-verification-real-plugin
plan: 01
status: complete
completed: 2026-05-28
autonomous: true
deliverables:
  - .planning/phases/25-hardware-verification-real-plugin/25-UAT.md
  - src/devices/streamdeck/src/register.cpp (verified pre-landed, no change)
commits:
  - 57eac05 docs(25-01): 25-UAT operator runbook for VERIFY-05 + VERIFY-06
---

# Plan 25-01 — Summary

## What was delivered

### Task 1: akp05e `hasClock = false` in register.cpp

**Pre-landed via commit `07c5902` during Phase 14** (commit message: `fix(streamdeck): flip akp05e hasClock to false (DEVICES-11 / ARCH-05)`). Grep-verified 2026-05-28:

```
src/devices/streamdeck/src/register.cpp:309
.hasClock = false, // DEVICES-11 / ARCH-05: Stream Dock family has no firmware RTC.
```

The Plan 25-01 acceptance criterion ("akp05e advertises hasClock=false; suite green; the Sync button no longer appears on the AKP05E row") is satisfied without further code change. No descriptor-table test asserts hasClock=true for akp05e (verified by surrounding regression tests in `tests/unit/test_streamdeck_register*.cpp`). The 645/645 ctest baseline (linux-release, -E qml) holds.

A regression test for this specific descriptor row was added earlier — commit `2ac7e33 test(streamdeck): pin akp05e hasClock==false regression test (DEVICES-11)`.

### Task 2: Authored `25-UAT.md` operator runbook

329-line runbook covering 16 tests across three sections:

- **VERIFY-05 (9 tests):** image on key, encoder rotate / press, touch tap, swipe-page, brightness, clear-all, hasClock=false honesty.
- **Provisional §5 confirm-or-correct (3 tests):** touch-strip zone+gesture map (`akp05.md` lines 115-130), DRA rect header layout (`akp05_vendor.md` §2 line 193), ENC vs MAI vs BAT mapping.
- **VERIFY-06 (4 tests):** plugin handshake over loopback WS, setImage paints a key, plugin receives keyDown / dialRotate.

Mirrors `10-UAT.md` template structure (per-test `expected:` + `result: [pending]` blocks; ASCII names; Summary table at end). Pre-flight section names the build precondition (`qt6-qtbase-private-devel` for Qt6 CorePrivate) and the udev/uaccess ACL recovery procedure.

## Demo-unit caveat documented up-front

The runbook explicitly carries the CLAUDE.md AKP05E glossary §7.1 truth: input streaming (key / encoder / touch) is **not reachable** on the lab demo unit `0x0300:0x3004` ("HOTSPOTEKUSB HID DEMO"), confirmed across raw hidraw read, GET_REPORT polling, evdev, raw usbmon, and the reference library `4ndv/mirajazz`. Tests 2-5 and 15-16 are flagged `BLOCKED` on this unit — record BLOCKED not FAIL, per the methodology rule. To convert BLOCKED → PASS needs a retail AKP05E / Mirabox N4 (different unit) or the Frida-on-Windows-vendor-app capture path.

Reachable-on-demo-unit tests (1, 6, 7, 8, 9, 13, 14) were informally confirmed live 2026-05-28; the operator still records PASS/FAIL formally to close the bookkeeping.

## "Hardware wins" rule baked in

For the three provisional-§5 tests (10, 11, 12), the runbook codifies the methodology rule from CLAUDE.md: when the device contradicts a provisional doc value, the operator updates the matching RE doc (`akp05.md` / `akp05_vendor.md`) in the same session — does not silently ship code that contradicts the doc. The how-to-correct guidance is inline for each row.

## What was NOT delivered (intentionally)

- **No operator UAT walkthrough.** That is Plan 25-02 (`autonomous: false`) — operator-gated by hardware time + a real `.sdPlugin`. The Wave 1 prereq (a written, walkable runbook) is now closed; the Wave 2 walkthrough remains pending.
- **No §5 reconciliation commits to `akp05.md` / `akp05_vendor.md`.** The reconciliation is gated on the operator walkthrough of Tests 10-12 — there is no autonomous-verifiable correction to make until the device contradicts a documented value.
- **No `register.cpp` edit.** Task 1's deliverable was already in code; no change needed.

## Phase 25 status after this plan

Plan 25-01: **complete**. Plan 25-02: **pending operator**. Phase 25 cannot be marked `[x]` in ROADMAP until the operator walks 25-UAT.md against a unit that supports input streaming (retail AKP05E / Mirabox N4 / Frida-on-Windows path) AND the VERIFY-06 real-`.sdPlugin` round-trip witnesses keyDown + dialRotate live.

## Deviations

None. The plan's automated verify gate (`grep ... hasClock = false` in `register.cpp`) passes; no `--no-verify` bypass; one mdformat re-stage on the runbook commit (standard pre-commit dance, not a hook violation).

## Suite-green precondition

The full Phase 14-24 ctest suite is green: `ctest --preset linux-release -E qml` = 645/645 passed, 29.25s (verified 2026-05-28 just before this plan landed). The QML-tests link target is a pre-existing latent issue documented in CLAUDE.md "Latent items"; skipping with `-E qml` is the documented workaround.
