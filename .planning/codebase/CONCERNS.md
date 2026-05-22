# Codebase Concerns

**Analysis Date:** 2026-05-22

## Overview

This document catalogs technical debt, known issues, security considerations, and fragile areas in the AJAZZ Control Center codebase. Items are organized by category, with severity tiers and resolution status. Four recent code-review phases (09–13, all completed 2026-05-22) provided baseline findings; this audit cross-references those reviews and adds platform-specific constraints from CLAUDE.md.

**Key resolution statuses:**

- **🟢 RESOLVED**: Fixed and verified; no longer actionable
- **🔴 OPEN BLOCKER**: Prevents release or breaks core functionality
- **🟡 DEFERRED**: Known issue, documented, deliberately not fixed (e.g., pending hardware verification)
- **⚪ OPEN**: Actionable quality/maintainability concern, not blocking

______________________________________________________________________

## Resolved Issues (Verified Fixed)

These blockers were confirmed resolved in phases 09–13 and are retained for traceability only.

### CR-01: Profile serialization data loss — `mouseButtons` + `KeyState`

**Status: 🟢 RESOLVED (commit b361596, phase 09)**

**File:** `src/core/src/profile.cpp:207-218` (writer), `:692-708` (reader)

**What was wrong:** `Profile::mouseButtons` and `Binding::state` (KeyState) were never serialized on a save → load cycle, causing silent data loss.

**How it was fixed:** (commit b361596) Added `writeBinding`/`readBinding` round-trip for `mouseButtons` using wire key `"mouseButtons"` per schema (`docs/protocols/PROFILE_SCHEMA.md:41`). The escaping/unescaping is tested at `test_profile_serialization.cpp:95-124` with a JSON-escaped button key (`dpi"shift`) and an empty binding, validating the symmetric reader loop.

**Verification:** `profileToJson` emits `,"mouseButtons":{<escaped>:<binding>}` and `profileFromJson` parses the branch with a string-keyed inline loop. No comma/structure regression. **Note:** KeyState serialization (the visual state — image, overlay, RGB, fontSize) is still unserializable and remains WR-01 (see below).

______________________________________________________________________

### CR-02: Device-yank uncaught writes

**Status: 🟢 RESOLVED (commit 02d03b5, phase 11)**

**File:** `src/devices/mouse/src/aj_series.cpp:197-203, 210-216, 808-814, 817-830`

**What was wrong:** Four void-setter write paths (`setLiftOffDistanceMm`, `setButtonBinding`, `uploadDpiTableAtomic`, `emitLedPacket`) did not guard `m_transport->write()` against throws on device yank.

**How it was fixed:** All four paths now wrap the write in `try { … } catch (std::exception const&)` block with `AJAZZ_LOG_WARN`, matching sibling setters. No unguarded `->write` / `->writeFeature` / `->read` / `->readFeature` call remains on the AJ series configuration path.

**Verification:** Regression test `test_aj_series_mock_transport.cpp:239-257` exercises all four sites (two direct, one via `setActiveDpiStage`, one via `setRgbBrightness`) and asserts `CHECK_NOTHROW`.

______________________________________________________________________

### CR-03: AKP05 / Stream Dock image upload report-id framing

**Status: 🟢 RESOLVED (branch `feat/linux-device-support`, hardware-confirmed 2026-05-22)**

**File:** `src/core/src/hid_transport.cpp` (transport layer); `src/devices/streamdeck/src/akp05.cpp` (backend flag)

**What was wrong:** Stream Deck packets omitted the leading HID Report-ID byte that hidraw expects. Windows `WriteFile` tolerated the missing byte; Linux hidraw was strict and treated the first data byte as the Report ID, misaligning all packets.

**How it was fixed:** `makeHidTransport` gained a `prependReportIdPosix` flag (default false) that the four streamdeck constructors set to `true`. `HidTransport::write()` prepends a single `0x00` report-id byte **only under `#ifndef _WIN32`**, keeping Windows byte-for-byte unchanged. Fedora hardware confirm: a `CRT LIG` brightness probe with the prepend made the panel respond correctly; every write ACKed full-length.

**Verification:** Windows MSVC build + tests pass (byte count unchanged); GCC `-Werror` clean. Fedora confirmation complete (2026-05-22).

______________________________________________________________________

### CR-04: AJ-series mouse usage-page fallback on Linux hidraw

**Status: 🟢 RESOLVED (branch `feat/linux-device-support`, commit db21686, pending hardware confirm)**

**File:** `src/core/src/hid_transport.cpp:open()` match loop

**What was wrong:** On Linux hidraw, the mouse control collection usage can be unpopulated (0), causing `HidTransport::open` to skip it and fall back to the first interface (boot mouse), where battery/clock features don't exist.

**How it was fixed:** `HidTransport::open()` now does a two-pass match: pass 1 `usage_page`+`usage` (strict), pass 2 `usage_page`-only fallback for hidraw's unpopulated `usage`. Logs distinguish "usage+page filtered" / "usage-page filtered" / "first-interface" / "default" so future triage is one grep. Windows unaffected (it reports `usage`).

**Verification:** Windows MSVC build + 365 tests; GCC `-std=c++20 -Wall -Wextra` clean. Fedora hardware confirmation pending (test plan in TODO.md).

______________________________________________________________________

### CR-05: Stream Dock zip-slip archive extraction

**Status: 🟢 RESOLVED (commit in phase 13)**

**File:** `src/app/src/sdplugin_extractor.cpp:51-71`

**What was wrong:** No path-traversal guard on plugin archive extraction; `../../escape.txt` entries could write outside the staging directory.

**How it was fixed:** Computes `rootCanon = QDir::cleanPath(tmpPath) + "/"` and rejects entries that are absolute, scheme-prefixed (`:/` or `:\`), or don't satisfy `outPath.startsWith(rootCanon)`. Symlinks skipped. Trailing `/` defeats sibling-prefix attacks.

**Verification:** Regression test `test_sdplugin_extractor.cpp:190-243` drives a Python-built malicious zip (base64-embedded) and asserts the escape file is never created.

______________________________________________________________________

### CR-06: Download-size cap on plugin store

**Status: 🟢 RESOLVED (64 MiB cap + validation, phase 13)**

**File:** `src/app/src/plugin_catalog_model.cpp:388, 415-427, 508-512, 545-550`

**What was wrong:** Unbounded plugin download could exhaust disk/memory.

**How it was fixed:** 64 MiB cap (`kMaxPluginDownloadBytes`) enforced at three layers: (1) up-front abort in `downloadProgress` when `received` or `total` exceed cap; (2) `validateDownloadedArchive()` called in `finished` handler before any disk write, re-checking size + `PK\x03\x04` magic; (3) `finished` handler maps `OperationCanceledError` to clear error message.

**Verification:** Unit test covers valid magic, empty body, non-zip body, and 64 MiB+1 body with valid magic.

______________________________________________________________________

### CR-07: SdPluginServer dead connection slots

**Status: 🟢 RESOLVED (phase 13)**

**File:** `src/app/src/sd_plugin_server.cpp:140-161`

**What was wrong:** `onClientDisconnected` nulled slots instead of erasing them, creating `{uuid,nullptr}` rows that caused linear-scan bugs on reconnect.

**How it was fixed:** `onClientDisconnected` now erases the matching slot entirely (no nulling), then `deleteLater()`s the socket. No dead rows survive in the vector.

**Verification:** Regression test asserts count returns to 0 after disconnect, exactly 1 after same-UUID reconnect.

______________________________________________________________________

## Hard Project Constraints

**COD-031 Boundary (Release Blocker):**

- **Constraint:** No `nlohmann::json` in `ajazz_core` or public headers. PRIVATE-linked to `ajazz_plugins` only.
- **Verification:** `grep -rn nlohmann src/core/include/` must return 0.
- **Status:** ✅ PASS all phases (09–13). Public boundary intact.

**Schema as Source of Truth (Load-Bearing Contract):**

- **Constraint:** When C++ field name and JSON wire key differ (`Profile::deviceCodename` ⇄ `"device"`), the schema documentation wins.
- **Status:** ✅ PASS. CR-01 fix uses `"mouseButtons"` (schema key), not C++ field name.

**RE Provisional Values (Hardware Wins):**

- **Constraint:** RE docs flag some values as "provisional"/"unconfirmed" — treat as hypotheses, not facts. Hardware (live capture/probe) is the ground truth. When RE and hardware disagree, hardware wins.
- **Examples:** `0x0300:0x3004` was mis-filed as AKP03 until live `CRT VER` handshake proved AKP05E; AK980 control interface was 0xFF00 in notes but real device uses 0xFF13.
- **Status:** ✅ Project memory aligned. Phase 11–12 findings respect this rule.

**Cross-Platform -Werror Strictness:**

- **Constraint:** Each platform catches different warnings. All three must land.
  - GCC/Linux-Clang: most permissive
  - Apple Clang (macOS): catches `-Wunused-const-variable` on `inline constexpr` at file scope
  - MSVC (windows-2022): `/W4 /WX`, C4996 deprecation errors, prefer `_s` variants
- **Test names:** ASCII-only (no em-dash/right-arrow; Win32 CMD codepage mangles them)
- **Status:** ✅ PASS. Phases 09–13 verified; IN-04 (phase 11, single-use `kDefaultProfile`) noted but not blocking.

**systemd ≥258 uaccess Regression (Linux-Only, Known Gap):**

- **Constraint:** Even with correct udev rule ordering (`70-ajazz.rules` before `73-seat-late.rules`), systemd ≥258 applies the `uaccess` ACL only on physical replug or boot, NOT on `udevadm trigger` or synthetic re-enumeration.
- **Impact:** After a USB hub re-enumeration storm, a device can silently lose its ACL. `udevadm trigger` will NOT restore it.
- **Workaround (transient):** `sudo setfacl -m u:$(id -u):rw /dev/hidraw*` (recovers until next replug)
- **Status:** ⚪ OPEN — unfixable at project level (systemd regression). Documented in CLAUDE.md for end-user awareness.

______________________________________________________________________

## Wire-Format & Hardware Verification Gaps (Deferred, Pending Device Capture)

These issues CANNOT be fixed without physical hardware or live USB capture. They are documented, not regressions, and have no live callers today.

### DFR-01: AK980 PRO lighting + settings envelope report-id framing

**Status: 🟡 DEFERRED (documented in code + RE doc, phase 12)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:936-987, 1001-1038`

**Issue:** The shipped lighting + settings envelope packets use report-id `0x04` with opcode at byte 1. The corrected deep-RE (`ak980pro_vendor.md` §13.1) says report-id `0x00` with frame-magic `0x04` at byte 1 and opcode at byte 2. This is the same "off-by-one" framing the time-sync path was hardware-proven to require on the same device.

**Why deferred:** (1) No live AK980 PRO hardware witness for these two paths either way. (2) Report-id-0x04 framing was a silent no-op on this device when tested. (3) RE doc is internally inconsistent (§10 recommends `{0x04, 0xF0, …}`; §4/§13.1 say 0x00 / byte-2 opcode). (4) Tests lock in the unverified layout, so a hardware correction would look like a test regression rather than an expected fix.

**Fix path:** Re-derive lighting + settings envelopes against §13.1/§13.4 (report id 0x00, frame-magic 0x04 at byte 1, opcode at byte 2) **OR** record in proprietary.md that report-id-0x04 layout was hardware-verified for these opcodes. Do NOT leave the tests pinning the unverified layout as ground truth.

**Tracking:** Phase 12, WR-01. No live caller; no regression-test for these opcodes.

______________________________________________________________________

### DFR-02: AK980 PRO settings-batch DATA packet framing

**Status: 🟡 DEFERRED (documented, phase 12)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:243-257`

**Issue:** `buildSettingsBatch` calls `makeReport(CmdSettingsBatch)` yielding `byte0 = 0x04` (should be 0x00 per §13.2), never writes the fixed `0x01` at byte 5, and omits the disable_winkey / disable_alt_f4 / disable_alt_tab bytes (6/7/8) the corrected map documents.

**Why deferred:** The same device proved it needs report id 0x00 for time-sync (hardware-confirmed). The 0x04 report-id + missing fixed byte is a likely silent no-op against real hardware. No live caller; no regression test for `CmdSettingsBatch`.

**Fix path:** Reconcile `buildSettingsBatch` against §13.2 — set byte 0 to 0x00 (or confirm hardware accepts 0x04), write the fixed `0x01` at byte 5. Update test to assert the documented layout. **Schema wins**: where C++ field name and schema differ, the schema wins (CLAUDE.md).

**Tracking:** Phase 12, WR-02.

______________________________________________________________________

### DFR-03: AK980 PRO `setRgbBuffer` 0x0A off-by-two

**Status: 🟡 DEFERRED (documented, no live caller, phase 12)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:663-718`

**Issue:** `RgbBufferChunk = 60` but the per-LED RGB report's header is 6 bytes, leaving only 58 payload bytes per report. The loop sets `pkt[5] = take` (up to 60) and advances `offset += take`, while the `memcpy` clamps to `min(take, 58)` — dropping 2 bytes/chunk and claiming a length the report does not carry.

**Why deferred by deliberate decision (commit d70503d):** (1) `setRgbBuffer` (the `IRgbCapable` surface) has **zero live callers and zero unit tests** in production. (2) The whole `0x0A` zone-buffer path is legacy; the corrected RE shows it should unify onto the per-key protocol `0x20/sub-0x04`, already implemented as `buildPerKeyRgbWriteHeader`. (3) Patching the constant in isolation would polish a superseded path against competing/provisional RE. (4) KNOWN-ISSUE comment is present and accurate in the source.

**Fix path (future):** Unify on `0x20/0x04` and verify the RE-flagged-unconfirmed wired LED-to-byte mapping against a physical AK980 PRO, rather than fixing `RgbBufferChunk` in place.

**Tracking:** Phase 12, CR-01 (DEFERRED).

______________________________________________________________________

### DFR-04: Touch-strip X coordinate clamped at 640 (rightmost 20% dead input)

**Status: 🟡 DEFERRED (hardware capture pending, phase 10)**

**File:** `src/devices/streamdeck/src/akp05.cpp:285-294`, `src/devices/streamdeck/src/akp05_protocol.hpp:76-84`

**Issue:** `parseInputReport` discards any X ≥ 640 (`akp05::TouchStripRangeX`), but the panel is 800px wide (`TouchStripWidthPx = 800`) and the encoder zone is X ∈ [600, 799]. Input landing in X ∈ [640, 799] is silently dropped as "malformed," losing roughly 20% of the touch surface to the rightmost encoder zone. The header flags `TouchStripRangeX` as "preserved for backwards-compat tests; capture pending" — acknowledged-provisional and unverified against hardware.

**Why deferred:** Per CLAUDE.md, provisional RE values are hypotheses. The code is internally consistent with `TouchStripRangeX`, but the hypothesis directly contradicts the device's own reported geometry (800px). Only a hardware capture can settle the real on-wire X scale vs display space.

**Fix path:** (a) Confirm the real on-wire X scale against a physical AKP05E (v3 capture required). (b) If 0..639 is the true digitiser scale, gate on `TouchStripWidthPx` and rescale X to display space. (c) If the X range is correct, document the asymmetry and drop the provisional flag.

**Tracking:** Phase 10, WR-01. No regression test pins the X clamping; the `test_akp05_touch_strip.cpp` suite uses full-range synthetic frames.

______________________________________________________________________

### DFR-05: AKP05 v3 protocol framing (1024-byte packets)

**Status: 🟡 DEFERRED (hardware capture pending, TODO.md)**

**File:** `src/devices/streamdeck/src/akp05_protocol.hpp:PacketSize` (currently 512)

**Issue:** Per `[mirajazz]` taxonomy, the AKP05 family is protocol_version 3 with **1024-byte packets**, but our backend hardcodes `PacketSize = 512`. When the first real v3 capture lands, the packet size must be gated on detected protocol version (like `Kind::is_v2_api()` in `[ajazz-sdk]`) and `buildCmdHeader` adapted accordingly.

**Why deferred:** No v3 USB capture yet. Changing the packet size breaks every chunk loop in `akp05.cpp` without hardware validation.

**Tracking:** TODO.md, "AKP05 v3 framing migration". Tests must cover both 512- and 1024-byte paths once resolved.

______________________________________________________________________

### DFR-06: AKP03 v2 protocol framing (1024-byte packets)

**Status: 🟡 DEFERRED (hardware capture pending, TODO.md)**

**File:** `src/devices/streamdeck/src/akp03_protocol.hpp:PacketSize` (currently 512)

**Issue:** AKP03 is also a v2_api device shipping 1024-byte packets per `[ajazz-sdk]`. Our backend still hardcodes `PacketSize = 512`.

**Why deferred:** No USB capture confirms the packet size. Change requires widening every chunk loop in `akp03.cpp`.

**Tracking:** TODO.md, "AKP03 v2 framing migration".

______________________________________________________________________

### DFR-07: AKP05 placeholder VID:PID retirement

**Status: 🟡 DEFERRED (vendor contact pending, TODO.md)**

**File:** `src/devices/streamdeck/src/register.cpp:255`

**Issue:** The `0x0300:0x5001` pair we shipped is a placeholder with no public source. The canonical Mirabox N4 ID (`0x6603:0x1007`) is now registered in parallel. Once a capture confirms the AJAZZ-branded AKP05's real VID:PID, delete the placeholder.

**Why deferred:** Vendor pages do not list the real AJAZZ SKU ID yet.

**Tracking:** TODO.md, "AKP05 placeholder VID:PID retirement".

______________________________________________________________________

## Open Quality/Maintainability Concerns

### QC-01: `Binding::state` (KeyState) silently dropped on profile round-trip

**Status: ⚪ OPEN (pre-existing gap, phase 09, WR-01)**

**File:** `src/core/src/profile.cpp:143-162` (writers), `:550-600` (readers); schema `docs/protocols/PROFILE_SCHEMA.md:64,74,96`

**Issue:** `writeBinding`/`writeEncoderBinding` never emit the `state` object (image path, overlay text, background/foreground RGB, fontSize). The schema declares `state: {$ref: KeyState}` on both Binding and EncoderBinding, but the writers omit it entirely, causing silent data loss on save → load. The profile.hpp doc claims "every field that the writer emits is round-tripped" — technically true because the writer omits `state`, but a caller expecting persistence of key visuals gets unexpected data loss.

**Impact:** Users cannot persist custom key visuals (icons, labels, colors). Every profile load strips the visual state.

**Fix approach:** Either (a) emit/parse a `"state"` object in `writeBinding`/`readBinding` (and encoder variants) using optional-aware RGB/text fields, or (b) if KeyState is deliberately persisted elsewhere, document that explicitly and remove `state` from the wire schema so the contract is honest. Add a round-trip assertion on `Binding::state`.

**Tracking:** Phase 09, WR-01. Not covered by the new round-trip test (test only asserts action chains, never `state`).

______________________________________________________________________

### QC-02: `writeRgb` is dead code

**Status: ⚪ OPEN (pre-existing, phase 09, IN-01)**

**File:** `src/core/src/profile.cpp:36-39`

**Issue:** `writeRgb` is defined and marked `[[maybe_unused]]` but has no callers — RGB/KeyState are never serialized (see QC-01). It exists in anticipation of KeyState serialization that was never wired up. Dead helpers marked `[[maybe_unused]]` invite drift.

**Fix:** Remove it once QC-01 is resolved (either delete or wire into the KeyState fix).

**Tracking:** Phase 09, IN-01.

______________________________________________________________________

### QC-03: `readUInt` accepts negative numbers, wrapping to huge unsigned values

**Status: ⚪ OPEN (pre-existing, phase 09, WR-04)**

**File:** `src/core/src/profile.cpp:401-419, 614-620`

**Issue:** `readUInt` consumes an optional leading `-` or `+` (lines 404-406) and parses with `std::stoul`, casting to `std::uint32_t`. For an edited input like `"delayMs":-5`, the parser wraps to a huge unsigned value (~4.29 billion) instead of rejecting, turning a Sleep action into a multi-year hang. The `readUintKeyedMap` key parse has the same exposure. As a defensive parser fed user-edited profile files, it should reject negatives.

**Impact:** Malformed profiles (or hostile edits) can cause infinite hangs on Sleep actions.

**Fix:** Reject a leading `-` outright, or range-check the `std::stoul` result against `UINT32_MAX` before cast. Apply same guard to map-key parse.

**Tracking:** Phase 09, WR-04.

______________________________________________________________________

### QC-04: `\uXXXX` decoder does not handle UTF-16 surrogate pairs

**Status: ⚪ OPEN (low impact, pre-existing, phase 09, IN-02)**

**File:** `src/core/src/profile.cpp:357-390`

**Issue:** The `\u` escape handler decodes a single BMP code point. A surrogate pair (astral-plane char) is decoded as two separate halves, each emitted as invalid UTF-8. The doc comment acknowledges "Real Unicode payloads are not expected," and the writer never emits `\u` escapes, so this only bites if an external editor produces a `\u`-escaped profile with non-BMP characters.

**Impact:** Very low — astral-plane character support is not currently expected in profiles.

**Fix:** Detect a high surrogate and combine with following `\uDCxx`, or document the limitation alongside the existing ASCII-only note.

**Tracking:** Phase 09, IN-02.

______________________________________________________________________

### QC-05: `actionKind` unknown-value fallback degrades silently to `Plugin`

**Status: ⚪ OPEN (pre-existing, phase 09, IN-03)**

**File:** `src/core/src/profile.cpp:78-96, 482-502`

**Issue:** Both `actionKindName` and `actionKindFromString` default unknown inputs to `Plugin`/`"plugin"`. This is intentional forward-compat, but a typo'd `kind` value (`"openfolder"` vs `"openFolder"`) silently degrades rather than surfacing the mismatch.

**Impact:** Low — not a correctness bug for the writer's own consistent output, but action-kind drift would fail open.

**Fix:** Debug-log on the unknown-kind fallback path if action-kind drift becomes a support issue.

**Tracking:** Phase 09, IN-03.

______________________________________________________________________

### QC-06: `parseVidPid` (Win32 hot-plug) uses `std::wcstoul` with no error check

**Status: ⚪ OPEN (pre-existing, phase 09, IN-04)**

**File:** `src/core/src/hotplug_monitor.cpp:219-232`

**Issue:** `std::wcstoul` on a non-hex tail returns 0 without signaling error, so a device path with malformed `VID_`/`PID_` produces `vid=0,pid=0`. The event is still dispatched, but the debouncer/registry won't match a backend for (0,0), so practical impact is a no-op — but it's not flagged as a parse failure. The Linux `parseHex16` is stricter.

**Impact:** Silent, unlogged parse failures on malformed device paths. Harder to diagnose device-enumeration issues.

**Fix:** Validate that `wcstoul` advanced past the expected 4 hex chars (or that vid/pid are non-zero) before dispatching, mirroring `parseHex16` strictness.

**Tracking:** Phase 09, IN-04.

______________________________________________________________________

### QC-07: `isAlive()` STILL_ACTIVE==259 collision (process-exit-code aliasing)

**Status: ⚪ OPEN (low risk, documented, phase 09, IN-05)**

**File:** `src/plugins/src/out_of_process_plugin_host_win32.cpp:708-724`

**Issue:** `GetExitCodeProcess` returning 259 (`STILL_ACTIVE`) is ambiguous — a child that legitimately exits with code 259 is reported alive forever. The comment notes the child only uses 0/127, so this is safe for the current wire protocol. Latent footgun if a future child path exits with 259.

**Impact:** Low — current protocol is safe. Latent risk on protocol evolution.

**Fix:** Pair `GetExitCodeProcess` check with non-blocking `WaitForSingleObject(h, 0)` to disambiguate.

**Tracking:** Phase 09, IN-05.

______________________________________________________________________

### QC-08: macOS hot-plug `IOServiceMatching` result unchecked for null

**Status: ⚪ OPEN (platform-specific, phase 09, WR-03)**

**File:** `src/core/src/hotplug_monitor.cpp:349-350`

**Issue:** `IOServiceMatching(kIOUSBDeviceClassName)` can return `nullptr` (memory pressure). The code immediately `CFRetain(matching)` without guarding, causing undefined behavior (crash). Linux and Windows guard their primary handles; macOS path does not.

**Impact:** Potential crash on memory pressure during macOS hot-plug initialization. Untestable on non-macOS platforms.

**Fix:** Guard before retain: `if (!matching) { IONotificationPortDestroy(port); AJAZZ_LOG_WARN(…); return; }`.

**Tracking:** Phase 09, WR-03. Platform-specific, cannot be reproduced here.

______________________________________________________________________

### QC-09: `notify-send` / `osascript` shell-out resolves via PATH (PATH-hijack surface)

**Status: ⚪ OPEN (pre-existing, phase 09, WR-02)**

**File:** `src/core/src/notification_service.cpp:125,153`

**Issue:** Linux and macOS notification back-ends call `execvp("notify-send", ...)` / `execvp("osascript", ...)`, resolving the binary against inherited `PATH`. If the process launches with an attacker-influenced `PATH` (a `.desktop` launcher, wrapper script, or test harness), a malicious `notify-send`/`osascript` on `PATH` runs with user privileges every time a notification fires. Title/body are passed as separate argv entries (no shell injection), so the only exposure is the PATH lookup of the helper itself.

**Impact:** Notification feature becomes a privilege-escalation surface if the app's launch context is untrusted.

**Fix:** Prefer absolute paths (`/usr/bin/notify-send`, `/usr/bin/osascript`) or use `execv`/`posix_spawn` against a vetted candidate list, falling back across known install locations. At minimum, document the trust assumption that the process launches with a clean `PATH`.

**Tracking:** Phase 09, WR-02.

______________________________________________________________________

### QC-10: AKP05/AKP03/AKP153/AKP815 — no bounds-check on keyIndex/encoderIndex before write

**Status: ⚪ OPEN (pre-existing, phase 10, WR-02)**

**File:** `src/devices/streamdeck/src/akp05.cpp:544-568, 601-609`; `akp03.cpp:438-461`; `akp153.cpp:334-359`; `akp815.cpp:176-198`

**Issue:** `setKeyImage`, `setKeyColor`, `setEncoderImage`, `clearKey` accept an index and place it verbatim into the packet header with no range validation. The parser side correctly range-checks (key `tag <= KeyCount`; encoder `>= EncoderCount` → nullopt), making the write side asymmetric. `clearKey(0xff)` is a deliberate broadcast sentinel and must stay, but other values should be validated.

**Impact:** Out-of-range indices ship silently to firmware, potentially corrupting device state or causing unexpected behaviour.

**Fix:** Validate and reject (WARN + early return) before building the header, mirroring the existing `setTouchStripImage` location-range guard (akp05.cpp:684-690):

```cpp
if (keyIndex == 0 || keyIndex > akp05::KeyCount) {
    AJAZZ_LOG_WARN("akp05", "setKeyImage: keyIndex {} out of range 1..{}",
                   static_cast<int>(keyIndex), static_cast<int>(akp05::KeyCount));
    return;
}
```

**Tracking:** Phase 10, WR-02.

______________________________________________________________________

### QC-11: AKP05 `m_firmwareVersion` read/write data race

**Status: ⚪ OPEN (pre-existing, phase 10, WR-03)**

**File:** `src/devices/streamdeck/src/akp05.cpp:434, 783-797, 854, 856`

**Issue:** `firmwareVersion()` reads `m_firmwareVersion` without lock, while `probeFirmwareVersion()` (called from `open()`) writes it via `std::move`. The `m_mutex` guards only `m_callback`, not `m_firmwareVersion`. If a UI thread calls `firmwareVersion()` concurrently with the I/O thread running `open()`, a data race on `std::string` causes UB (torn read). Unit tests only exercise the single-threaded path, so this is not surfaced.

**Impact:** Potential data race on concurrent access to firmware version string. May manifest as garbage version on high-contention systems.

**Fix:** Guard `m_firmwareVersion` with `m_mutex` (made `mutable`) on both read and write:

```cpp
[[nodiscard]] std::string firmwareVersion() const override {
    std::lock_guard const lock(m_mutex);
    return m_firmwareVersion;
}
```

**Tracking:** Phase 10, WR-03.

______________________________________________________________________

### QC-12: AKP05 DRA header advertises BE32 size but `sendImage` caps at 0xFFFF

**Status: ⚪ OPEN (pre-existing, phase 10, WR-04)**

**Issue:** `setSecondaryScreenImage` builds the DRA header with a BE32 JPEG-size field (4 bytes, max 0xFFFFFFFF), but `sendImage()` caps payload at 0xFFFF and refuses anything larger. The DRA path can never transmit a JPEG larger than 64 KB even though its header reserves 32 bits. A full-panel 800×480 JPEG at quality 85 can exceed 64 KB, causing `clearTouchStrip` / `setTouchStripImage` to WARN-and-fail.

**Impact:** Large JPEG uploads silently fail on the secondary/touch-strip screen, despite the wire format supporting larger sizes.

**Fix:** Either (a) parameterise `sendImage` with the protocol size-field width so the DRA path allows up to its true BE32 limit (bounded by HID-rate cap), or (b) if hardware genuinely only accepts ≤ 64 KB on DRA, document that and stop advertising BE32. Confirm against the RE corpus / hardware before picking.

**Tracking:** Phase 10, WR-04.

______________________________________________________________________

### QC-13: AKP153/AKP815 — `setKeyColor` voids keyIndex then uses it

**Status: ⚪ OPEN (pre-existing, phase 10, IN-04)**

**File:** `src/devices/streamdeck/src/akp153.cpp:347-353`; `akp815.cpp:188-192`

**Issue:** Both devices' `setKeyColor` do `(void)keyIndex; (void)color; clearKey(keyIndex);` — the cast is misleading because `keyIndex` IS used on the next line. Neither device renders the requested color (both fall back to clear). This is a known stub, but the AKP05 backend already renders color correctly via `encodeSolid`, so the siblings are strictly worse.

**Impact:** `setKeyColor` does not work on AKP153/AKP815; falls back to clear instead of rendering. Inconsistent with AKP05 capability.

**Fix:** Drop the spurious `(void)keyIndex` cast, and route AKP153/AKP815 `setKeyColor` through `encodeSolid` + `sendImage` as AKP05 does (image_pipeline is linked into the same module).

**Tracking:** Phase 10, IN-04.

______________________________________________________________________

### QC-14: Mouse battery — stale 0x83 dead-code path with contradictory comments

**Status: ⚪ OPEN (pre-existing, phase 11, WR-01)**

**File:** `src/devices/mouse/src/aj_series.cpp:267-315`, `aj_series_protocol.cpp:73-79`, `aj_series_protocol.hpp:59-60,130-134`

**Issue:** The implemented battery read uses the 0xF7 status poll built inline. The older 0x83 path is now dead: `buildGetBattery()` has zero call sites and is referenced only in comments. The `IBatteryCapable` doc block still describes the superseded "SET_FEATURE 0x83 GET_BATTERY poke ... then GET_FEATURE that report ... read charge at byte 2" handshake. The actual code sends 0xF7, reading charge at byte 3 (Windows) / byte 2 (Linux). Comments contradict the code.

**Impact:** Misleading documentation. A maintainer reading the block could re-introduce the 0x83 poke. CLAUDE.md mandates the RE doc as source of truth; stale comments undermine that.

**Fix:** Either delete `buildGetBattery()` + `FeaCmd::GetBattery` if 0x83 is retired, or annotate "superseded by 0xF7 — see `batteryPercent()`". Rewrite the `batteryPercent()` doc block to describe the 0xF7 poll and the byte-3/byte-2 auto-detect, removing the 0x83 language.

**Tracking:** Phase 11, WR-01. Verified fixed in hardware (2026-05-22, Fedora).

______________________________________________________________________

### QC-15: Mouse settings push zeroes cached LED sub-blocks on the wire

**Status: ⚪ OPEN (pre-existing, phase 11, WR-02)**

**File:** `src/devices/mouse/src/aj_series.cpp:484-491`, `aj_series_protocol.cpp:309-310`, `aj_series_protocol.hpp:301-307`

**Issue:** `setMouseSettings` calls `buildMouseSettings(...)` which intentionally leaves `ledBlock` and `logoLedBlock` zero. The builder comment and header doc promise that "the AjSeriesMouse setter wires the cached blocks back in before send" to "keep the LED state coherent across commits." But `setMouseSettings` never injects the cached LED blocks before `write()` — the mirror-back at lines 507-535 feeds INTO `m_options` for *future* re-emits, not the just-built packet. Result: every settings push transmits all-zero LED sub-blocks (bytes 24..39), which firmware reads as "LED off / black". A user who sets RGB and then changes any unrelated setting (sleep timer, LOD, sensitivity) silently loses their lighting.

**Impact:** RGB lighting is lost on every settings change, frustrating for users with custom lighting.

**Fix:** Before the `write(pkt)` in `setMouseSettings`, populate the LED sub-blocks from cached state by injecting `m_lastLed` into `m_options.ledBlock`/`logoLedBlock` before the transmit. Alternatively, if clearing LED blocks on every omnibus push is the intended firmware behaviour, fix the contradictory docs — the current code+doc pair cannot both be right.

**Tracking:** Phase 11, WR-02.

______________________________________________________________________

### QC-16: Mouse macro `lastNonZeroPos` is 0-based but documented as 1-based

**Status: ⚪ OPEN (pre-existing, phase 11, WR-03)**

**File:** `src/devices/mouse/src/aj_series.cpp:686-698`, `aj_series_protocol.hpp:419-427`

**Issue:** `uploadMacro` computes `lastNonZeroPos` as the **0-based** index of the last non-zero byte. But three places document it as **1-based** (header param doc per §3.11 `56*(u-1)+s` vendor formula; inline comment for empty macro; class-level comment). For an empty macro the loop yields `lastNonZeroPos = 0` (index of `0x01`), NOT 1. The code is off-by-one relative to its own documented vendor formula. There is no round-trip test pinning the convention.

**Impact:** Firmware may truncate the final macro byte, or macros may be silently corrupted on-wire depending on the real vendor convention.

**Fix:** Reconcile against `aj_series_opcode_table.md` §3.11 line 491. If 1-based, use `lastNonZeroPos = static_cast<std::uint8_t>(i + 1)` and confirm empty-macro case lands `1`. If 0-based, correct the three comments. Add unit test pinning `lastNonZeroPos` for empty-macro and multi-event payload.

**Tracking:** Phase 11, WR-03.

______________________________________________________________________

### QC-17: Mouse `setRgbBrightness` percent→scale conversion has no upper clamp

**Status: ⚪ OPEN (pre-existing, phase 11, WR-04)**

**File:** `src/devices/mouse/src/aj_series.cpp:352-357`

**Issue:** Comment says "Clamp 0..100% → vendor scale 0..5", but there is no clamp. The `percent` param is `std::uint8_t` (range 0..255); passing `percent > 100` yields `(255 * 5) / 100 = 12`, far outside the documented vendor 0..5 range. Sibling code clamps defensively everywhere (sensitivity/LOD in `buildMouseSetOption0`, profile slot in `setActiveOnboardProfile`), so this is inconsistent.

**Impact:** Out-of-spec brightness byte sent to firmware on unclamped input. Firmware behaviour undefined.

**Fix:**

```cpp
std::uint8_t const pct = std::min<std::uint8_t>(percent, 100);
m_lastLed.brightness = static_cast<std::uint8_t>((pct * 5u) / 100u);
```

**Tracking:** Phase 11, WR-04.

______________________________________________________________________

### QC-18: Mouse — `setActiveDpiStage` clamps while `setDpiStage` throws (asymmetric error handling)

**Status: ⚪ OPEN (pre-existing, phase 11, IN-01)**

**File:** `src/devices/mouse/src/aj_series.cpp:161-179`

**Issue:** `setDpiStage(index, ...)` throws `std::out_of_range` for out-of-range index, but `setActiveDpiStage(index)` silently clamps via `std::min<std::uint8_t>(index, 7)`. Two adjacent `IMouseCapable` index setters handle out-of-range differently — a caller cannot predict whether a bad index throws or is clamped.

**Impact:** API contract is ambiguous. Callers cannot write portable error handling.

**Fix:** Pick one policy. Given the file clamps defensively elsewhere, prefer clamping in `setDpiStage` too (or document the divergence in the interface).

**Tracking:** Phase 11, IN-01.

______________________________________________________________________

### QC-19: Mouse battery — `parseBatteryCharge` auto-detect can misclassify on Linux

**Status: ⚪ OPEN (pre-existing, phase 11, IN-02, low risk)**

**File:** `src/devices/mouse/src/aj_series.cpp:249-265`

**Issue:** The Windows-vs-Linux offset is auto-detected purely by `frame[0] == kBatteryStatusReportId (0x05)`. On Linux (unnumbered frame) the charge sits at `frame[2]`; if a Linux frame's `frame[0]` ever equals 0x05, the parser takes the Windows branch (chargeIndex=3) and reads the wrong byte. The captured Linux frame is `00 00 64 ...` (charge at index 2) so risk is low — but detection is value-based, not transport-based.

**Impact:** Very low on current hardware. Latent risk if a future Linux frame format changes to have 0x05 at byte 0.

**Fix:** Low priority. If a Linux frame with a non-zero leading byte is ever observed, switch to a transport-supplied "report-id present" flag instead of sniffing the value.

**Tracking:** Phase 11, IN-02.

______________________________________________________________________

### QC-20: AK980 `firmwareVersion()` swallows exceptions with no log

**Status: ⚪ OPEN (pre-existing, phase 12, WR-03)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:528-542`

**Issue:** The `catch (...)` block (line 539) discards the error entirely with no log, unlike `batteryPercent` and `setTime` which both `AJAZZ_LOG_WARN`. On device yank or transport failure, the function returns `"unknown"` indistinguishably from a device that genuinely reports unparsable version. A short/garbage reply can produce a plausible-looking but bogus `"x.y.z"`. Operationally this hides I/O failures that every sibling method records.

**Impact:** Silent I/O failures on version reads, harder to diagnose device issues.

**Fix:** Catch `std::exception const& e` and `AJAZZ_LOG_WARN("keyboard.ak980", "firmwareVersion: HID I/O failed: {}", e.what())` before falling through to `"unknown"`.

**Tracking:** Phase 12, WR-03.

______________________________________________________________________

### QC-21: AK980 `batteryPercent()` treats 0% charge as "no battery"

**Status: ⚪ OPEN (pre-existing, phase 12, WR-04)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:781-783`

**Issue:** `if (pct == 0) return std::nullopt;` conflates two distinct states: a wired keyboard with no battery (the intended suppression) and a wireless keyboard genuinely at 0% / critically drained. A real near-empty battery shows "unknown" in the UI instead of "0%", exactly when the user most needs the warning. The comment references `resp[1]` opcode echo as a sanity check, but the code only validates the opcode echo — it does not use it to disambiguate drained-wireless from no-battery.

**Impact:** Critically drained wireless keyboards show "unknown" battery instead of a warning-critical "0%".

**Fix:** Disambiguate "wired, no battery" from wireless 0% using a device/echo signal (e.g. gate the `nullopt` on the descriptor's battery/wireless state, or on a distinct echo byte), rather than on the percent value alone.

**Tracking:** Phase 12, WR-04.

______________________________________________________________________

### QC-22: AK980 `buildSetTimeData` high-year wrap above 2255 is unguarded

**Status: ⚪ OPEN (pre-existing, phase 12, WR-05)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:196`

**Issue:** `pkt[4] = (year >= 2000) ? static_cast<std::uint8_t>(year - 2000) : 0;` guards the low end (pre-2000 saturates to 0) but not the high end: `year = 2256` gives `256`, which truncates to `0` — 2256 silently encodes as 2000. The test suite pins 2255 → 0xFF but never exercises the 2256 wrap. Not reachable from a real `system_clock` today, but latent silent-corruption path.

**Impact:** Very low on current platforms. Latent risk for future systems with extended time values.

**Fix:** Clamp the high end too:

```cpp
year >= 2255 ? 0xFF : (year >= 2000 ? year - 2000 : 0)
```

Add test for 2256-saturates-to-2000.

**Tracking:** Phase 12, WR-05.

______________________________________________________________________

### QC-23: AK980 `setFirmwareLightingMode` comment claims FINISH is "not yet shipped" — but it ships

**Status: ⚪ OPEN (pre-existing, phase 12, WR-07)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:900-908`

**Issue:** The banner comment says "4-packet envelope … the 5th packet CMD_FINISH (0xF0) … our project does not yet ship it (Phase 3 P3.6 pending)". The code immediately below (lines 968-975) DOES emit the FINISH packet, and the test asserts a 5-packet envelope ending in 0xF0. The comment directly contradicts the shipped behaviour — a regression introduced when FINISH was wired in (issue #58) without updating this banner.

**Impact:** Documentation-vs-code contradiction. Future readers will believe FINISH is absent and may introduce regressions.

**Fix:** Rewrite the banner to describe the shipped 5-packet envelope (START → MODE_BEGIN → DATA → SAVE → FINISH); remove the "does not yet ship it / P3.6 pending" sentence.

**Tracking:** Phase 12, WR-07.

______________________________________________________________________

### QC-24: AK980 Lighting DATA trailer byte order inverted relative to settings/time

**Status: ⚪ OPEN (pre-existing, phase 12, IN-02)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:436-437`

**Issue:** `buildSetRgbModeData` writes `pkt[14]=0x55, pkt[15]=0xaa` (matching `ak980pro_vendor.md` §3.4), but settings batch and time-sync trailers are `0xAA 0x55` (bytes 18/19 and 63/64). §13 of the same doc lists the lighting trailer as `0xAA 0x55`, contradicting §3.4. The code is self-consistent with §3.4, but the intra-doc conflict means only a hardware witness can settle which order opcode 0x13 actually wants.

**Impact:** Low — intra-doc conflict in the RE, shipped code is self-consistent. Latent risk on hardware divergence.

**Fix:** Record in `proprietary.md` which trailer order was hardware-verified for opcode 0x13, and reconcile §3.4 vs §13.

**Tracking:** Phase 12, IN-02.

______________________________________________________________________

### QC-25: AK980 Streaming TFT checksum and output-report transport PROVISIONAL but tests pin it as ground truth

**Status: ⚪ OPEN (pre-existing, phase 12, IN-03)**

**File:** `src/devices/keyboard/src/proprietary_keyboard.cpp:283-301, 1044-1067`, `tests/unit/test_ak980_tft_chunked.cpp:106-108, 140-141`

**Issue:** The chunked TFT path (byte-32 checksum + `write()` output reports vs `writeFeature()`) is explicitly unverified ("whether it accepts output reports for image upload is UNVERIFIED — no USB/Frida capture exists yet"). The tests assert exact checksum values as if they were ground truth, so a future hardware-driven correction will look like a test regression rather than an expected change.

**Impact:** False confidence in unverified wire format. Hardware fix will break tests in a confusing way.

**Fix:** Annotate the checksum `REQUIRE`s in `test_ak980_tft_chunked.cpp` as PROVISIONAL, mirroring the source comment, so a hardware fix isn't mistaken for a defect.

**Tracking:** Phase 12, IN-03.

______________________________________________________________________

### QC-26: App — RgbPicker fires unsolicited HID writes on tab open / device swap

**Status: ⚪ OPEN (pre-existing, phase 13, WR-05)**

**File:** `src/app/qml/RgbPicker.qml:88-95, 110-116`

**Issue:** The brightness and speed `Slider`s call `LightingService.setMode(...)` from `onValueChanged`. `onValueChanged` fires not only on user drag but also on the programmatic seed and whenever `root.firmwareBrightnessMax` re-resolves because device changed. Merely opening the RGB tab (or switching the bound device) emits an unsolicited HID `setMode` write to hardware the user never touched. This can flicker device lighting and burns HID I/O.

**Impact:** Unsolicited device writes on UI navigation. Visual artifacts and unnecessary HID traffic.

**Fix:** Gate the slider handlers on user interaction using `onMoved` (fires only on user drag, not programmatic assignment) instead of `onValueChanged`, or set a `seeded` flag and early-return while `!seeded`:

```qml
Slider {
    onMoved: {  // user-drag only
        if (firmwareModeBox.currentValue === undefined) return
        LightingService.setMode(…)
    }
}
```

**Tracking:** Phase 13, WR-05.

______________________________________________________________________

### QC-27: App — SettingsRow sleep ComboBox silently maps unknown values to "Never"

**Status: ⚪ OPEN (pre-existing, phase 13, WR-06)**

**File:** `src/app/qml/SettingsRow.qml:99-108, 320-326`

**Issue:** `_sleepIndexFor(minutes)` returns `0` ("Never") for any `sleepMinutes` value not in `_sleepValues [0,1,3,5,10,30]`. If `SettingsService.currentSettings()` reports a firmware default like `2` or `15`, the ComboBox silently snaps to "Never". On the next "Apply" the device is reprogrammed to disable sleep without the user ever choosing that — a silent destructive write.

**Impact:** User's device settings silently overwritten on sync. Data loss.

**Fix:** When `_sleepIndexFor` finds no match, append the actual value as a custom entry or disable Apply until the user explicitly picks a known value.

**Tracking:** Phase 13, WR-06.

______________________________________________________________________

### QC-28: App — update-banner re-fires for dismissed tag on non-304 re-check

**Status: ⚪ OPEN (pre-existing, phase 13, WR-08)**

**File:** `src/app/src/app_update_service.cpp:316-321, 406-435`

**Issue:** The dismissed-tag suppression in `applyRelease` (lines 426-431) only holds while the server returns a full body. The 304 fast-path at 316-321 restores `Status::UpdateAvailable` purely from `m_latestVersion.isEmpty()` — it does NOT re-consult the persisted `dismissedTag`. So a sequence {check → banner → user dismisses (Idle) → next auto-check returns 304} flips the banner back to `UpdateAvailable` even though the user dismissed that tag.

**Impact:** Update banner re-appears after user dismissal on subsequent checks.

**Fix:** Mirror the dismissed-tag check in the 304 branch (add the same `m_latestVersion != dismissed` guard before setting `Status::UpdateAvailable`).

**Tracking:** Phase 13, WR-08.

______________________________________________________________________

### QC-29: App — StreamdockCatalogFetcher Loading re-entry guard has no watchdog

**Status: ⚪ OPEN (pre-existing, phase 13, WR-09)**

**File:** `src/app/src/streamdock_catalog_fetcher.cpp:502-510, 581-657`

**Issue:** `refresh()` early-returns whenever `m_state == State::Loading`. The only exits from `Loading` live inside `onPageFinished`. A per-request timeout means the common case (stalled socket) unblocks — but there's no watchdog: if a reply is never delivered (NAME torn down, future code path drops the connection), `m_state` stays `Loading` forever and every later `reload()`/Retry no-ops, while the QML Retry button is disabled.

**Impact:** Stuck "Loading" state that cannot be recovered without an app restart.

**Fix:** Arm a single-shot watchdog QTimer when entering `Loading` that, on expiry without a terminal result, forces the state back to `Cached`/`Offline` so the guard self-heals and Retry becomes usable.

**Tracking:** Phase 13, WR-09.

______________________________________________________________________

### QC-30: App — BatteryIndicator keeps stale percent across undetected offline transition

**Status: ⚪ OPEN (pre-existing, phase 13, WR-10)**

**File:** `src/app/qml/components/BatteryIndicator.qml:58, 168-199`

**Issue:** The chip self-hides only on an explicit `batteryUnavailable` signal or when `percent < 0`. If a device goes offline without `BatteryService` emitting `batteryUnavailable` for that codename, the chip keeps showing the last-known percent. The header comment claims the parent gates `visible` on "connected", but inside the component `visible: percent >= 0 && !unavailable` does not consider connection state. Also no `onCodenameChanged` reset, so a recycled delegate can inherit a prior device's charge until the first signal arrives.

**Impact:** Stale battery indicator for offline devices. Confusing UI.

**Fix:** (a) Clear `percent = -1; unavailable = false` in `onCodenameChanged` so recycled delegates don't inherit stale charge. (b) Have the parent row bind a `connected` property the component honours.

**Tracking:** Phase 13, WR-10.

______________________________________________________________________

### QC-31: AKP03 protocol version upgrade pending

**Status: ⚪ OPEN (deferred until capture, TODO.md)**

**File:** `src/devices/streamdeck/src/akp03_protocol.hpp:17`

**Issue:** `[ajazz-sdk]/info.rs::Kind::Akp03::is_v2_api()` is true, so AKP03 is a v2 protocol device sending 1024-byte packets. Our backend hardcodes `PacketSize = 512`. The change requires bumping `PacketSize` plus widening every chunk loop in `akp03.cpp`. Must be verified against a USB capture before flipping.

**Impact:** Potential packet-size mismatch with real devices. May cause incomplete image uploads or hangs.

**Fix:** Confirm against a USB capture, then gate packet size on protocol version detection (mirroring AKP05 v3 approach once that lands).

**Tracking:** TODO.md, "AKP03 v2 framing migration".

______________________________________________________________________

## Performance & Scalability Notes

### PERF-01: Test suite growth (365 tests as of 2026-05-18)

**Status:** ℹ️ INFORMATIONAL

**File:** CMake test suite; ctest via `--preset linux-release`

**Observation:** The test suite has grown from 178 tests at v1.1 close to 365 tests (roughly doubled) through Phase 9 captures, vendor-RE work, AK980 clock-sync, OOP plugin host, SdPluginServer MVP, and the bulk audit follow-up. Full suite runs in ~30 s on a modern CPU.

**Impact:** None today; latent concern if the suite grows another 3x without optimization. Monitor runtime on each major milestone.

______________________________________________________________________

## Missing or Incomplete Features

### FEAT-01: Per-LED RGB buffer on VIA keyboards

**Status:** ⚪ OPEN (source-level stub, TODO.md)

**File:** `src/devices/keyboard/src/via_keyboard.cpp:185`

**Issue:** `throw std::runtime_error("per-LED RGB buffer: TODO (requires QMK_RGB_MATRIX path)")`. Today we speak `qmk_rgblight` (brightness/effect/color). Per-LED keying via `qmk_rgb_matrix` is a different VIA surface with a variable channel ID.

**Impact:** Per-LED RGB not supported on VIA boards.

**Fix approach:** Probe the supported VIA channels at device-open time; flip a `KeyboardCapabilities::hasRgbMatrix` flag based on the probe result; implement the write path.

**Tracking:** TODO.md, "via_keyboard — per-LED RGB matrix path". ≈ 1 day.

______________________________________________________________________

### FEAT-02: MacroRecorder real backends (all platforms)

**Status:** ⚪ OPEN (stub returns, TODO.md)\*\*

**File:** `src/core/include/ajazz/core/macro_recorder.hpp:14-15`, `src/core/src/macro_recorder.cpp:10-12`

**Issue:** `makeDefaultMacroRecorder()` returns a `StubRecorder` on every platform — `start()` / `stop()` just log. No real event capture.

**Impact:** Macro recording is non-functional.

**Fix approach:** Implement platform-specific capture backends:

- **Linux**: evdev (`/dev/input/eventN`) reader thread. Requires `input` group membership.
- **macOS**: `CGEventTap` via Accessibility permissions. Translate `CGEventFlags` + keycode.
- **Windows**: `SetWindowsHookExW(WH_KEYBOARD_LL)` + `WH_MOUSE_LL` low-level hooks in a dedicated thread.

Also wire the CMake option `AJAZZ_FEATURE_MACRO_RECORDER` (currently not wired).

**Tracking:** TODO.md, "MacroRecorder — real native back-ends on all three OSes". ≈ 1 day per platform + 0.25 day for CMake wiring.

______________________________________________________________________

### FEAT-03: Autostart service (macOS + Windows)

**Status:** ⚪ OPEN (Linux only, TODO.md)\*\*

**File:** `src/app/src/autostart_service.cpp:163`

**Issue:** Only Linux launches via XDG `.desktop` autostart. macOS and Windows stubs missing.

**Impact:** Autostart cannot be enabled on macOS or Windows.

**Fix approach:**

- **macOS**: Write LaunchAgent plist to `~/Library/LaunchAgents/<appId>.plist` with `RunAtLoad = true` and (for start-minimised) `ProgramArguments` array with `--minimized`.
- **Windows**: Write `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` registry value via `QSettings(QSettings::NativeFormat)`.

**Tracking:** TODO.md, "macOS + Windows AutostartService backends". ≈ 0.5 day per platform.

______________________________________________________________________

### FEAT-04: Stream Dock firmware update via QtSerialPort

**Status:** ⚪ OPEN (deferred, TODO.md)\*\*

**File:** (no implementation yet)

**Issue:** Vendor `FirmwareUpgradeTool.exe` is a separate process linked against `Qt5SerialPort.dll`, suggesting a USB-CDC bootloader handoff. We have no firmware-update path yet.

**Impact:** Stream Deck devices cannot be updated via the app.

**Fix approach:** Wire-capture the boot-into-bootloader command + the subsequent serial flash protocol.

**Tracking:** TODO.md, "Stream Dock firmware update via QtSerialPort handoff".

______________________________________________________________________

## Summary: Concern Tiers

| Tier                                                         | Count | Examples                                                                                                     |
| ------------------------------------------------------------ | ----- | ------------------------------------------------------------------------------------------------------------ |
| **🔴 BLOCKER** (release-preventing)                          | 0     | (all resolved or deferred by design)                                                                         |
| **🟡 DEFERRED** (known, documented, pending hardware/vendor) | 7     | AK980 envelope framing (WR-01/02), Touch-strip X clamp, v3 protocol, placeholder PIDs, off-by-two RGB buffer |
| **⚪ OPEN** (actionable quality/maintainability)             | 24    | KeyState serialization (QC-01), data races (QC-11), macro encoding (QC-16), app UI glitches (QC-26–30), etc. |
| **ℹ️ INFORMATIONAL** (monitoring)                            | 1     | Test suite growth                                                                                            |

______________________________________________________________________

*Codebase concerns audit: 2026-05-22*
*References: phases 09–13 code-review reports, CLAUDE.md hard rules, TODO.md open work.*
