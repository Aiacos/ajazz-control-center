---
phase: 09-research-captures-hygiene
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 18
files_reviewed_list:
  - src/core/include/ajazz/core/capabilities.hpp
  - src/core/include/ajazz/core/device.hpp
  - src/core/include/ajazz/core/profile.hpp
  - src/core/src/device_registry.cpp
  - src/core/src/hid_transport.cpp
  - src/core/src/hotplug_monitor.cpp
  - src/core/src/notification_service.cpp
  - src/core/src/profile.cpp
  - src/plugins/src/manifest_signer_win32.cpp
  - src/plugins/src/out_of_process_plugin_host_win32.cpp
  - src/plugins/src/win32_env_block.cpp
  - python/ajazz_plugins/_host_child.py
  - tests/unit/fixtures/mock_transport.hpp
  - tests/unit/test_hotplug_harness.cpp
  - tests/unit/test_win32_env_block.cpp
  - tests/unit/test_profile_serialization.cpp
  - tests/unit/test_manifest_signer.cpp
  - docs/protocols/PROFILE_SCHEMA.md
findings:
  critical: 0
  warning: 4
  info: 5
  total: 9
status: issues_found
---

# Phase 09: Code Review Report (RE-REVIEW / verification pass)

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 18
**Status:** issues_found

## Summary

This is a verification re-review of the Phase 09 core + plugin-host + infra code
following the CR-01 fix (commit b361596). The prior critical finding —
`Profile::mouseButtons` never serialized by `profileToJson` nor read by
`profileFromJson` (data loss on round-trip) — is **confirmed resolved**. No new
critical issues were found, and the hard checks (COD-031, schema-as-source-of-truth,
Win32EnvBlock sort/move ordering, OOP env scrubbing, `_host_child.py` import safety)
all pass.

Hard-check verdicts:

- **COD-031 (RELEASE BLOCKER): PASS.** `grep -rn nlohmann src/core/include/` returns
  zero matches. The hand-rolled writer/reader in `profile.cpp` keeps `ajazz_core`
  free of `nlohmann::json`; the only nlohmann users are the plugin-side manifest
  signer TUs (PRIVATE-linked).
- **Schema-as-source-of-truth: PASS.** The new `mouseButtons` writer/reader uses
  the wire key `"mouseButtons"` (per `docs/protocols/PROFILE_SCHEMA.md` line 41,
  `additionalProperties: {$ref: Binding}` — a string-keyed Binding map), NOT a
  C++ field name. `deviceCodename` still correctly serializes to `"device"`
  (schema line 14/27). No writer was aligned to a C++ identifier.
- **CR-01 round-trip: PASS.** `profileToJson` emits `,"mouseButtons":{<escaped name>:<binding>}` (profile.cpp:207-218); `profileFromJson` reads the
  `mouseButtons` branch with a string-keyed inline loop (profile.cpp:692-708).
  Round-trip is exercised by `test_profile_serialization.cpp:95-124`, including a
  JSON-escaping button key (`dpi"shift`) and an empty binding. No comma/structure
  regression: the block is appended with a leading comma after `encoders` and
  before `pages`, structurally symmetric with the surrounding maps. The reader
  branch mirrors the `pages` inline string-key loop (since `readUintKeyedMap` is
  uint16-only), and the escape/readString symmetry is exercised by the escaped key.
- **Win32EnvBlock: PASS.** The `size()+1` advance is captured BEFORE
  `std::move(entry)` (win32_env_block.cpp:79, with the moved-from-size hazard
  documented). Final sort is by KEY substring via `_wcsnicmp` on `wstring_view` up
  to first `=` (lines 141-157), not by full entry — matches the CLAUDE.md rule and
  the Win32 name-ordering contract. Double-null terminator invariant guarded
  (lines 181-189).
- **OOP host env scrubbing: PASS.** The Win32 host builds a per-spawn
  `Win32EnvBlock` with only `PYTHONDONTWRITEBYTECODE`/`PYTHONUNBUFFERED`/`PYTHONPATH`
  overrides and passes it as `lpEnvironment` with `CREATE_UNICODE_ENVIRONMENT`
  to both `CreateProcessW` and `CreateProcessAsUserW` (lines 489-495, 514, 576, 587).
  No `_putenv_s` parent-env mutation remains; the block's lifetime spans both spawn
  branches (declared on the function stack, not inside an if/else).
- **`_host_child.py` import safety: PASS.** Top-level code only captures stdout and
  rebinds `sys.stdout`; all op handling is behind the `if __name__ == "__main__"`
  guard (line 444). Helpers (`_load_one_plugin`, `_dirname_rejection`, `_emit`) are
  importable without side effects, matching the project's test convention. The
  SEC-S2 stdlib-shadowing guard (lines 153-173) is present and applied in
  `_load_one_plugin` via `_dirname_rejection`.

The remaining findings are pre-existing warnings/info carried over from the prior
review; none are regressions introduced by the CR-01 fix.

## Warnings

### WR-01: KeyState (`Binding::state` / `EncoderBinding::state`) is silently dropped on round-trip

**File:** `src/core/src/profile.cpp:143-162` (writers); `:550-600` (readers); schema
`docs/protocols/PROFILE_SCHEMA.md:64,74,96`
**Issue:** `writeBinding`/`writeEncoderBinding` never emit the `state` object
(image path, overlay text, background/foreground RGB, fontSize), and the readers
never parse it. The schema declares `state: {$ref: KeyState}` on both Binding and
EncoderBinding. Any profile with per-key visual state (icon, label, color) loses
that data on a save → load cycle — the restored `Binding::state` is always
default-constructed. This is the same class of defect as the original CR-01
(`mouseButtons`) but for a different field, and it is NOT covered by the new
round-trip test (the test asserts only action chains, never `state`). The
profile.hpp doc (lines 188-196) claims "every field that the writer emits is
round-tripped" — technically true, but the writer silently omits `state`, so a
caller reasonably expecting persistence of key visuals gets data loss. Pre-existing
gap, not a regression, but the highest-value remaining correctness issue.
**Fix:** Either (a) emit/parse a `"state"` object in `writeBinding`/`readBinding`
(and the encoder variants) using the optional-aware RGB/text fields, or (b) if
KeyState is deliberately persisted elsewhere (e.g. the app layer), document that
explicitly in profile.hpp and remove `state` from the wire schema so the contract
is honest. Add a round-trip assertion on `Binding::state` once persistence is
decided.

### WR-02: `notify-send` / `osascript` shell-out resolves the helper via PATH (`execvp`) — PATH-hijack surface

**File:** `src/core/src/notification_service.cpp:125,153`
**Issue:** The Linux and macOS notification back-ends call
`execvp("notify-send", ...)` / `execvp("osascript", ...)`, which resolves the
binary against the inherited `PATH`. If the AJAZZ process is ever launched with an
attacker-influenced `PATH` (a `.desktop` launcher, a wrapper script, or a test
harness that prepends a writable dir), a malicious `notify-send`/`osascript` on
`PATH` runs with the user's privileges every time a notification fires. Title/body
are passed as separate `argv` entries (good — no argv/shell injection), so the only
exposure is the PATH lookup of the helper itself.
**Fix:** Prefer absolute paths (`/usr/bin/notify-send`, `/usr/bin/osascript`) or
use `execv`/`posix_spawn` against a vetted candidate list, falling back across
known install locations rather than trusting `PATH`. At minimum document the trust
assumption that the process launches with a clean `PATH`.

### WR-03: macOS hot-plug — `IOServiceMatching` result unchecked for null before `CFRetain`

**File:** `src/core/src/hotplug_monitor.cpp:349-350`
**Issue:** `IOServiceMatching(kIOUSBDeviceClassName)` can return `nullptr` (memory
pressure / unexpected IOKit state). The code immediately `CFRetain(matching)`
(line 350) and passes it to `IOServiceAddMatchingNotification` twice.
`CFRetain(nullptr)` is undefined behavior (crash). The Linux and Windows branches
guard their primary handles (`udev_new`, `CreateWindowExW`), but the macOS
matching-dictionary path is not. `iokitCb` correctly guards `vidRef`/`pidRef`
before `CFNumberGetValue`, so per-event null handling is fine — the gap is only the
matching dictionary itself.
**Fix:** Guard before the retain: `if (!matching) { IONotificationPortDestroy(port); AJAZZ_LOG_WARN("hotplug", "IOServiceMatching failed; hotplug disabled"); return; }`.
Re-verify the `CFRetain` + double-`IOServiceAddMatchingNotification` reference
accounting if either registration can fail early.

### WR-04: `readUInt` accepts a leading `-`/`+` then casts to unsigned, wrapping instead of erroring

**File:** `src/core/src/profile.cpp:401-419` (and the map-key parse at `:614-620`)
**Issue:** `readUInt` consumes an optional leading `-` or `+` (lines 404-406) and
parses the digits with `std::stoul`, casting to `std::uint32_t`. For an edited
input like `"delayMs":-5`, `std::stoul("-5")` itself accepts the minus and returns a
huge wrapped unsigned value, then truncates to `uint32_t` — `delayMs` silently
becomes ~4.29 billion instead of being rejected, turning a Sleep action into a
multi-year hang. As a defensive parser fed user-edited profile files it should
reject negatives. The `readUintKeyedMap` key parse uses a separate `std::stoul`
that has the same wrap exposure on a negative key.
**Fix:** Reject a leading `-` outright (`if (!eof && src_[pos_]=='-') fail("negative integer")`), or range-check the `std::stoul` result against
`UINT32_MAX` before the cast. Apply the same guard to the map-key parse.

## Info

### IN-01: `writeRgb` is dead code (`[[maybe_unused]]`, never called)

**File:** `src/core/src/profile.cpp:36-39`
**Issue:** `writeRgb` is defined and marked `[[maybe_unused]]` but has no caller —
RGB/KeyState are not serialized anywhere (see WR-01). It exists in anticipation of
KeyState serialization that was never wired up.
**Fix:** Remove it, or wire it into the WR-01 KeyState fix. Dead `[[maybe_unused]]`
helpers invite drift.

### IN-02: `\uXXXX` decoder does not handle UTF-16 surrogate pairs

**File:** `src/core/src/profile.cpp:357-390`
**Issue:** The `\u` escape handler decodes a single BMP code point and emits 1-3
UTF-8 bytes. A surrogate pair (astral-plane char) is decoded as two separate
high/low surrogate code points, each emitted as invalid UTF-8 (encoded surrogate
halves). The doc comment acknowledges "Real Unicode payloads are not expected", and
the writer never emits `\u` escapes, so this only bites if an external editor
produces a `\u`-escaped profile with non-BMP characters. Low impact given the
upstream-validation assumption (profile.cpp:14-16).
**Fix:** If non-BMP labels become a concern, detect a high surrogate and combine it
with a following `\uDCxx`; otherwise document the limitation alongside the existing
ASCII-only note.

### IN-03: `actionKind` unknown-value fallback degrades silently to `Plugin`

**File:** `src/core/src/profile.cpp:78-96, 482-502`
**Issue:** Both `actionKindName` and `actionKindFromString` default unknown inputs
to `Plugin`/`"plugin"`. This is intentional forward-compat, but a typo'd or future
`kind` value (`"openfolder"` vs `"openFolder"`) silently degrades to a Plugin
action rather than surfacing the mismatch. Not a correctness bug for the writer's
own consistent output, but action-kind drift would fail open to Plugin dispatch.
**Fix:** None required for the current schema; consider a debug-log on the
unknown-kind fallback path if action-kind drift becomes a support issue.

### IN-04: `parseVidPid` (Win32 hot-plug) uses `std::wcstoul` with no error check

**File:** `src/core/src/hotplug_monitor.cpp:219-232`
**Issue:** `std::wcstoul` on a non-hex tail returns 0 without signaling error, so a
device path with a malformed `VID_`/`PID_` segment produces `vid=0,pid=0` and the
event is still dispatched (the function returns true once both substrings are
found). `parseDevicePathW` (the test helper) has the same shape. Downstream the
debouncer/registry simply won't match a backend for (0,0), so the practical impact
is a no-op event — but it is not flagged as a parse failure, unlike the Linux
`parseHex16` which fully validates.
**Fix:** Validate that `wcstoul` advanced past the 4 hex chars (or that vid/pid are
non-zero) before dispatching, mirroring `parseHex16` strictness.

### IN-05: `isAlive()` STILL_ACTIVE==259 collision is documented but relies on child exit-code discipline

**File:** `src/plugins/src/out_of_process_plugin_host_win32.cpp:708-724`
**Issue:** `GetExitCodeProcess` returning 259 (`STILL_ACTIVE`) is ambiguous: a child
that legitimately exits with code 259 is reported alive forever. The comment notes
the child only uses 0/127, so this is safe for the current wire protocol — a latent
footgun if a future child path (or a Python crash with exit 259) is introduced.
Documented, low risk.
**Fix:** None required now. If robustness is wanted, pair the `GetExitCodeProcess`
check with a non-blocking `WaitForSingleObject(h, 0)` to disambiguate a running
process from one that exited with 259.

______________________________________________________________________

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
