---
phase: VERIFY-2026-05-22
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 8
files_reviewed_list:
  - src/devices/keyboard/src/proprietary_keyboard.cpp
  - src/devices/streamdeck/src/akp05.cpp
  - src/core/src/notification_service.cpp
  - src/app/qml/components/BatteryIndicator.qml
  - src/app/qml/components/DeviceRow.qml
  - src/devices/mouse/src/aj_series.cpp
  - src/devices/mouse/src/aj_series_protocol.cpp
  - src/devices/streamdeck/src/akp153.cpp
findings:
  critical: 0
  warning: 0
  info: 1
  total: 1
status: clean
---

# VERIFICATION Re-review: Code-review fixes (c43980d..HEAD)

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 8 (plus 4 supporting: 2 protocol headers + 2 test files)
**Status:** clean

## Summary

This was a verification re-review of 7 commits (`c43980d..HEAD`) that fixed 5
code-review warnings and removed 2 dead functions. I reviewed only the changes
in the diff, traced each fix against its claimed intent, checked for new bugs
or regressions, and confirmed the dead-code removals left no dangling
references.

**All five fixes are correct and introduce no new bugs, regressions, or
quality defects. Both dead-function removals are clean. The changes are
backed by appropriate new unit tests.** One INFO-level note is recorded below
for the documented-and-intentional behavioral narrowing in the Linux
notification path — it is not a defect.

### Per-item verification results

1. **proprietary_keyboard.cpp year clamp (WR-05) — CORRECT.**
   `pkt[4] = (year >= 2255) ? 0xFF : (year >= 2000) ? year-2000 : 0`.
   No off-by-one: 2255 encodes to 0xFF (it would naturally too, since
   2255-2000 = 255 = 0xFF — the clamp is harmless at the boundary), and 2256
   is the first value that would have wrapped (256 truncates to 0) and is now
   correctly capped at 0xFF. New tests at
   `test_proprietary_keyboard_protocol.cpp` assert 2256 and 9999 both → 0xFF.
   Lower bound (pre-2000 → 0) preserved.

1. **proprietary_keyboard.cpp batteryPercent() 0% (WR-04) — CORRECT.**
   The `resp[1] != CmdBatteryQuery` echo guard at line 784 still rejects
   non-battery / not-yet-ready replies (and an all-zero buffer, since
   `0 != 0x20`, falls through to `continue`, not a spurious 0%). Removing the
   `pct == 0 -> nullopt` suppression is sound: a confirmed-battery reply with
   charge 0 now surfaces as a genuine 0% instead of "unknown". New tests cover
   both the genuine-0% case and the wrong-echo rejection.

1. **akp05.cpp m_firmwareVersion race (WR-03) — CORRECT, no deadlock.**
   `m_mutex` is now `mutable`, `firmwareVersion()` reads under
   `lock_guard`, `probeFirmwareVersion()` writes under `lock_guard`. No double
   lock: `probeFirmwareVersion()` has exactly one caller (`open()`), the write
   lock (line 833) is not nested inside any other `m_mutex` hold, and the
   `poll()`/`onEvent()` locks guard only `m_callback` — disjoint critical
   sections, never nested. The unguarded read at line 449 (`open()`'s log
   line) is benign: it is sequenced immediately after the same thread's
   `probeFirmwareVersion()` write on the I/O thread (single-threaded entry to
   `open()`), so it is not a concurrent read/write — it is a same-thread
   read-after-write. No remaining racing read.

1. **BatteryIndicator.qml / DeviceRow.qml connected gating (WR-10) — CORRECT.**
   `connected` is a declared `bool` (default `true`, preserving prior behavior
   for standalone/unbound instances). `visible: connected && percent >= 0 && !unavailable` is a pure declarative binding with no self-reference → no
   binding loop. `DeviceRow` binds `connected: root.deviceConnected`, and
   `deviceConnected` is a declared `bool` property (default `false`) → no
   null/undefined deref. `_seedFromCache()` is invoked only from imperative
   handlers (`onCodenameChanged`, `Component.onCompleted`) and assigns plain
   properties — no binding loop, and `BatteryService` is a registered singleton
   used as a `Connections` target elsewhere → no null-deref. Reset-then-reseed
   on codename change correctly prevents a recycled ListView delegate from
   inheriting a stale charge.

1. **notification_service.cpp execv (WR-02) — CORRECT.**
   `argv[0]` retains the conventional basename (`"notify-send"` /
   `"osascript"`); the candidate-path loop execs each absolute path and, since
   `execv` returns only on failure, falls through to `std::_Exit(127)` when
   none succeed. On a clean PATH `/usr/bin/notify-send` (tried first) and
   `/usr/bin/osascript` are the standard locations → no behavior regression in
   the normal case. argv array and null-termination unchanged.

1. **Dead-builder removals (buildGetBattery, buildShowLogo) — CLEAN.**
   `grep` across `src/`, `tests/`, `python/` confirms zero remaining
   references to either symbol after removal from both `.cpp` and `.hpp`. The
   `FeaCmd::GetBattery = 0x83` enum entry is intentionally retained, and the
   updated aj_series.cpp comment accurately describes it as a catalogued
   earlier theory superseded by the live-confirmed 0xF7 status poll — matches
   the project's RE-corpus / battery-resume memory notes. The akp153
   `buildShowLogo` doc comment was removed alongside the declaration; comment
   edits are accurate.

## Narrative Findings (AI reviewer)

## Info

### IN-01: Linux notification path no longer honors a non-standard install prefix

**File:** `src/core/src/notification_service.cpp:132-136`
**Issue:** The switch from `execvp("notify-send", ...)` to a fixed candidate
list (`/usr/bin`, `/bin`, `/usr/local/bin`) means a distro or packaging layout
that installs `notify-send` outside those three directories (some Nix/Guix
profiles, unusual symlink farms) will silently drop notifications where the
old PATH-based lookup would have found it. This is a deliberate, documented
security tradeoff (PATH-hijack hardening, WR-02) and is consistent with the
project's Flatpak-first deployment where `/usr/bin/notify-send` is the portal
convention — so it is **not a defect**. Recorded only so the narrowing is
visible to future maintainers.
**Fix:** No change required. If broader coverage is ever wanted without
reintroducing PATH-hijack risk, resolve a portal D-Bus
`org.freedesktop.Notifications` call instead of execing a helper at all — but
that is out of scope for this fix.

______________________________________________________________________

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
