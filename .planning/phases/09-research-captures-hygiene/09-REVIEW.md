---
phase: 09-research-captures-hygiene
reviewed: 2026-05-22T00:00:00Z
depth: standard
files_reviewed: 27
files_reviewed_list:
  - src/core/include/ajazz/core/capabilities.hpp
  - src/core/include/ajazz/core/device.hpp
  - src/core/include/ajazz/core/device_registry.hpp
  - src/core/include/ajazz/core/hid_transport.hpp
  - src/core/include/ajazz/core/hotplug_monitor.hpp
  - src/core/include/ajazz/core/profile.hpp
  - src/core/src/device_registry.cpp
  - src/core/src/hid_transport.cpp
  - src/core/src/hotplug_monitor.cpp
  - src/core/src/notification_service.cpp
  - src/core/src/profile.cpp
  - src/plugins/src/manifest_signer_win32.cpp
  - src/plugins/src/out_of_process_plugin_host_win32.cpp
  - src/plugins/src/win32_env_block.cpp
  - src/plugins/src/win32_python_resolve.hpp
  - python/ajazz_plugins/_host_child.py
  - python/ajazz_plugins/examples/hello/plugin.py
  - python/ajazz_plugins/tests/test_host_child_safety.py
  - python/ajazz_plugins/tests/test_manifest_signing.py
  - python/ajazz_plugins/tests/test_plugin_api.py
  - tests/unit/fixtures/mock_transport.hpp
  - tests/unit/test_hex_to_cpparray.py
  - tests/unit/test_hotplug_harness.cpp
  - tests/unit/test_hotplug_win32_smoke.cpp
  - tests/unit/test_load_trust_roots.cpp
  - tests/unit/test_manifest_signer.cpp
  - tests/unit/test_win32_env_block.cpp
  - tests/unit/test_sd_plugin_server.cpp
  - tests/unit/test_sdplugin_extractor.cpp
findings:
  critical: 1
  warning: 6
  info: 5
  total: 12
status: issues_found
---

# Phase 9: Code Review Report

**Reviewed:** 2026-05-22
**Depth:** standard
**Files Reviewed:** 27 (file list passed by workflow; 3 listed test files — `test_host_child_safety.py`, `test_manifest_signing.py`, `test_plugin_api.py`, `test_hotplug_win32_smoke.cpp`, `test_load_trust_roots.cpp`, `test_manifest_signer.cpp`, `test_sd_plugin_server.cpp` — were spot-read but are not the source of any finding below)
**Status:** issues_found

## Summary

Project-specific gates pass:

- **COD-031 boundary holds.** `grep -rn nlohmann src/core/include/` returns 0. None of the six core public headers pull in `nlohmann::json`; `profile.cpp` uses a hand-rolled writer/reader exactly as required.
- **Schema-as-source-of-truth holds** for the keys that *are* serialized: `Profile::deviceCodename` ⇄ `"device"` in both the writer (`profile.cpp:175`) and reader (`profile.cpp:668`).
- **Win32EnvBlock** correctly snapshots `entry.size() + 1` *before* `std::move(entry)` (`win32_env_block.cpp:79`) and sorts **by key only** via `_wcsnicmp` over the substring up to `=` (`win32_env_block.cpp:141-157`) — both CLAUDE.md gotchas satisfied.
- **OOP host env scrubbing** is correct: no `_putenv_s`, per-spawn `Win32EnvBlock` passed as `lpEnvironment` to both spawn branches with `CREATE_UNICODE_ENVIRONMENT`.

The one BLOCKER is a silent data-loss bug in the Profile JSON round-trip: `Profile::mouseButtons` is declared, populated by the UI for every mouse device, and **never serialized or deserialized**. The remaining findings are robustness / correctness concerns in the hotplug monitor, transport init, and notification shell-out.

## Critical Issues

### CR-01: `Profile::mouseButtons` is silently dropped on save and load

**File:** `src/core/src/profile.cpp:166-250` (writer) and `src/core/src/profile.cpp:654-712` (reader); field declared at `src/core/include/ajazz/core/profile.hpp:156`

**Issue:** `Profile` carries `std::unordered_map<std::string, Binding> mouseButtons` (button name → binding), the canonical store for mouse-button action chains. `profileToJson()` emits `id`, `name`, `device`, `keys`, `encoders`, `pages`, and `applicationHints` — but **never `mouseButtons`**. Symmetrically, `profileFromJson()` has reader branches for every key *except* `mouseButtons`, so an inbound blob would hit the `else { r.skipValue(); }` fallthrough even if one were present. The header for `profileFromJson` explicitly promises "Round-trip safe with profileToJson(): every field that the writer emits is round-tripped back to the same value." For a mouse profile this contract is broken end-to-end: a user binds actions to mouse buttons, saves the profile, and on reload every mouse-button binding is gone. This is data loss on the primary configuration surface for the entire Mouse device family.

**Fix:** Serialize and deserialize `mouseButtons` (a string-keyed `Binding` map). Confirm the wire key against `docs/protocols/PROFILE_SCHEMA.md` before naming it (do NOT default to the C++ field name — per the schema-as-source-of-truth rule). Add a writer block mirroring `encoders`:

```cpp
// writer, after the encoders block:
out << ",\"mouseButtons\":{";   // verify exact key against PROFILE_SCHEMA.md
first = true;
for (auto const& [name, binding] : profile.mouseButtons) {
    if (!first) { out << ","; }
    escape(out, name);
    out << ":";
    writeBinding(out, binding);
    first = false;
}
out << "}";
```

```cpp
// reader, new else-if branch in profileFromJson:
} else if (key == "mouseButtons") {   // same verified key
    r.expect('{');
    if (!r.tryConsume('}')) {
        while (true) {
            std::string const btn = r.readString();
            r.expect(':');
            profile.mouseButtons.emplace(btn, readBinding(r));
            if (r.tryConsume(',')) { continue; }
            r.expect('}');
            break;
        }
    }
}
```

Add a round-trip unit test that populates `mouseButtons` and asserts equality after `profileFromJson(profileToJson(p))`.

## Warnings

### WR-01: Windows hotplug `stop()` can race the worker's `threadId` write — thread never woken, join hangs

**File:** `src/core/src/hotplug_monitor.cpp:480-485` (start) and `499-523` (stop)

**Issue:** On Windows the worker thread sets `impl->threadId = GetCurrentThreadId()` from *inside* the spawned lambda (`hotplug_monitor.cpp:482`). `start()` returns immediately after launching the thread without waiting for that assignment. If `stop()` is called quickly after `start()` (e.g. a fast start/stop in a test, or app teardown during startup), `stop()` reads `p_->threadId` while it is still 0, the `if (p_->threadId)` guard at `:513` is false, `PostThreadMessageW` is never sent, and `GetMessageW` in `runWindows` blocks forever — `worker.join()` at `:522` deadlocks. `threadId` is also a plain `DWORD` written on the worker thread and read on the caller thread with no synchronization (data race / torn-read UB). The same shape applies to `impl->hidden` (HWND) which `runWindows` writes via `outHwnd` but `stop()` does not need.

**Fix:** Either (a) make `threadId` a `std::atomic<DWORD>` and have `stop()` spin/wait briefly until it is non-zero before posting, or (b) capture the thread id synchronously in `start()` before the worker enters its loop (e.g. use a `std::promise<DWORD>`/`future` the worker fulfills, and have `start()` wait on it). Option (b) also removes the data race.

### WR-02: macOS hotplug `IOServiceMatching` dictionary leaks one reference on early-return paths

**File:** `src/core/src/hotplug_monitor.cpp:349-383`

**Issue:** `matching = IOServiceMatching(...)` returns a +1 dict; `CFRetain(matching)` bumps it to +2 to feed two `IOServiceAddMatchingNotification` calls (each consumes one). The reference accounting is correct on the happy path. But there is no release on the early-return at `:340-343` (`if (!port) return;`) — that path returns *before* the matching dict is created, so it is actually fine — however if `IOServiceMatching` itself returns null (allocation failure) the two `IOServiceAddMatchingNotification` calls are invoked with a null dict and `CFRetain(nullptr)` is undefined. There is no null check on `matching`. Lower-frequency than WR-01 but still a crash-on-OOM path with no guard.

**Fix:** Add `if (!matching) { IONotificationPortDestroy(port); AJAZZ_LOG_WARN(...); return; }` immediately after the `IOServiceMatching` call and before `CFRetain`.

### WR-03: `makeHidTransport` leaks a hidapi init refcount if allocation throws

**File:** `src/core/src/hid_transport.cpp:375-377`

**Issue:** `hidLibrary().acquire()` increments the refcount (and may call `hid_init()`), then `std::make_unique<GuardedHidTransport>(...)` allocates. If the allocation throws `std::bad_alloc`, the `acquire()` is never balanced by a `release()` (the destructor that calls `release()` only runs for a successfully-constructed object). The refcount stays elevated for the process lifetime, so `hid_exit()` never runs — a leaked global init. Rare, but it is an unbalanced-RAII bug in the one factory that is supposed to guarantee the `hid_init`/`hid_exit` balance.

**Fix:** Wrap the construction so a failure releases the guard:

```cpp
hidLibrary().acquire();
try {
    return std::make_unique<GuardedHidTransport>(
        vid, pid, std::move(serial), usagePage, usage, prependReportIdPosix);
} catch (...) {
    hidLibrary().release();
    throw;
}
```

### WR-04: `profileFromJson` integer reader silently wraps negative values instead of rejecting

**File:** `src/core/src/profile.cpp:385-403` (`readUInt`)

**Issue:** `readUInt` accepts a leading `-` or `+` (`:388`), accumulates the digit run, then returns `static_cast<std::uint32_t>(std::stoul(tok))`. For input like `"delayMs":-5`, `std::stoul("-5")` does **not** throw — libstdc++/libc++ parse it as a wrapped huge unsigned (`std::stoul` applies `strtoul`, which negates modulo 2^N). So a negative `delayMs` round-trips to ~4.29 billion ms instead of being rejected, contradicting the file's stated contract ("rejects malformed input with a descriptive std::runtime_error"). The same wrap applies to any unsigned field. Map keys go through `std::stoul` too (`:599`) with the identical hazard.

**Fix:** Reject a leading `-` explicitly in `readUInt` (a JSON unsigned field has no business being negative), and reject a leading `+` too if strict round-trip with the writer is desired (the writer never emits a sign):

```cpp
if (pos_ < src_.size() && src_[pos_] == '-') {
    fail("negative value where unsigned expected");
}
```

### WR-05: `notify-send` / `osascript` child uses `execvp` with an attacker-influenced PATH and no `_Exit` flush guard concern

**File:** `src/core/src/notification_service.cpp:105-130` (Linux), `146-157` (macOS)

**Issue:** The Linux/macOS notification path `fork()`s and calls `execvp("notify-send", ...)` / `execvp("osascript", ...)`. `execvp` resolves the bare program name against `$PATH`. The host process's `$PATH` is inherited; if it contains a writable or attacker-controlled directory ahead of `/usr/bin`, a malicious `notify-send`/`osascript` runs with the app's privileges every time a notification fires (e.g. on every device hot-plug toast). `title`/`body` themselves are passed as separate `argv` entries (good — no shell, so no interpolation injection), so the residual risk is PATH-hijack, not content injection. This is a defense-in-depth gap rather than a direct exploit, but a desktop app shelling out by bare name on a per-event basis is a recurring local-priv-esc vector.

**Fix:** Prefer absolute paths (`/usr/bin/notify-send`, `/usr/bin/osascript`) with `execv`, or resolve the binary once at startup via a sanitized PATH and cache the absolute path. At minimum document the assumption that `$PATH` is trusted.

### WR-06: macOS `iokitCb` initial-drain runs before the run loop and can re-enter the user callback on the wrong thread contract

**File:** `src/core/src/hotplug_monitor.cpp:354-370`

**Issue:** After registering each notification, `iokitCb(implPtr, addedIter, HotplugAction::Arrived)` is called synchronously to "drain the initial set" (`:361`, `:370`). This invokes `impl->snapshotCallback()(ev)` for every already-connected USB device at `start()` time. That is a behavioral surprise: the documented contract (`hotplug_monitor.hpp:46-60`) is that the callback fires for *changes*; here every device present at startup is reported as an `Arrived` event. Downstream `Application::onHotplug` may double-count devices it already enumerated, or emit spurious "device connected" toasts on launch. On Linux/Windows there is no equivalent initial drain, so the cross-platform behavior diverges.

**Fix:** Either suppress the initial drain (let the run loop deliver only real changes) or document and align all three platforms to the same "report initial set" semantics so `Application` handles it uniformly.

## Info

### IN-01: `urgencyArg` / `escapeForAppleScript` marked `[[maybe_unused]]` but always used on their platform

**File:** `src/core/src/notification_service.cpp:45` (`urgencyArg`)

**Issue:** `urgencyArg` is `[[maybe_unused]]` yet is referenced on the Linux path (`:118`). The attribute is only needed because the function sits outside the `#if` guards and is unused on Windows. This is fine, but the macOS/Windows builds will see it as genuinely unused — the attribute is doing real work only there. No action required; noted to avoid a future "remove dead function" mistake. The Windows branch never calls it (it builds its own PowerShell), so on Windows it is truly dead.

**Fix:** None required. Optionally move `urgencyArg` inside `#if defined(__linux__)` to make the dead-on-Windows fact explicit.

### IN-02: PowerShell command-line built from `title`/`body` with quote-doubling that is not the correct PowerShell escape

**File:** `src/core/src/notification_service.cpp:76-89` (`quoteForCmd`), used at `:176,183`

**Issue:** `quoteForCmd` wraps in double quotes and doubles embedded `"`. That is the `cmd.exe` / `CommandLineToArgvW` convention, but the strings are interpolated into a PowerShell `-Command` script body where the correct in-string escape for a double-quote inside a double-quoted PowerShell literal is a backtick-quote (`` `" ``) or doubling only inside single quotes. A `title`/`body` containing `"` could prematurely terminate the PowerShell string. Since `title`/`body` are app-authored (device names, status strings) the practical exploitability is low, but a device that reports a crafted name string into a notification could in principle break the script. Classified Info because the inputs are not currently attacker-controlled end-to-end.

**Fix:** Pass title/body to PowerShell via environment variables or a here-string, or use single-quoted PowerShell literals with `'` doubled, rather than embedding them into a double-quoted `-Command` body.

### IN-03: `parseVidPid` / `parseDevicePathW` use `std::wcstoul` without validating the parse consumed hex digits

**File:** `src/core/src/hotplug_monitor.cpp:229-230` and `433-434`

**Issue:** `std::wcstoul(s.c_str() + v + 4, nullptr, 16)` is called on the substring after `VID_`. If the substring is malformed (no hex digits), `wcstoul` returns 0 and the event is built with `vid==0`/`pid==0`. The Win32 production `parseVidPid` returns `true` regardless (it only checks that `VID_`/`PID_` substrings *exist*, not that digits followed), so a `vid==0` event can be dispatched to subscribers. The test helper `parseDevicePathW` documents "vid==0 && pid==0 signals parse failure" but `parseVidPid` does not enforce that invariant before returning true.

**Fix:** After `wcstoul`, treat `vid==0 || pid==0` as a parse failure in `parseVidPid` (return false) for consistency with the documented sentinel.

### IN-04: `closeOpenDevicesInFamily` counts only handles it actually closed, but logs may misrepresent already-closed live backends

**File:** `src/core/src/device_registry.cpp:188-220`

**Issue:** The function collects live backends matching `family`, then increments `closed` only when `dev->isOpen()` was true and `close()` succeeded. A backend that is live-but-already-closed contributes 0, which is correct for the return value. No bug — but the doc comment ("Number of devices whose handle was closed") and the test (`test_hotplug_harness.cpp:401`) both assume `ensureTransportOpen` left every opened device `isOpen()==true`, which is only guaranteed when the real transport open succeeds. With a failing transport (logged, not thrown — `device_registry.cpp:114`) the device is returned not-open and the count would silently be 0. Acceptable given D-02, noted for awareness.

**Fix:** None required; behavior matches D-02 (honest failure). Consider logging at INFO when a matched backend was found already-closed.

### IN-05: `_crash_for_test` relies on `ctypes.string_at(0)` which is technically platform-dependent UB but adequate for the test contract

**File:** `python/ajazz_plugins/_host_child.py:380-407`

**Issue:** The crash trigger first attempts `os.kill(getpid(), SIGSEGV)` (suppressed on Windows where it raises), then falls back to `ctypes.string_at(0)` to force a null-deref. On some hardened platforms a null read may not fault (e.g. if address 0 is mapped), in which case neither path crashes and the test that asserts child death would hang/timeout rather than fail cleanly. This is test-only code and the documented contract is "deterministic SIGSEGV"; the fallback is a reasonable belt-and-braces. Noted only because a non-crashing platform would surface as a confusing timeout in `crashChildForTest`.

**Fix:** None required for production. If the test ever flakes on a new platform, add a final `os._exit(139)` after `ctypes.string_at(0)` so the child always dies non-zero.

______________________________________________________________________

_Reviewed: 2026-05-22_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
