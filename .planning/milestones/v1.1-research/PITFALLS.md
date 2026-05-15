# Pitfalls Research — v1.1 Device Lifecycle Hardening + Scaffolding-to-Functional

**Domain:** Qt 6 / QML 6 + hidapi cross-platform device control-center (Linux primary, Windows + macOS supported)
**Researched:** 2026-05-13
**Confidence:** HIGH (most pitfalls verified against existing repo code, official MS / Qt / hidapi documentation, or first-hand v1.0 retrospective notes)

This file is **specific to adding** the v1.1 feature set to **this** brownfield repo. Generic Qt / hidapi best practices were filtered out unless they intersect a concrete v1.1 work item. Every entry below has either a repo-anchor (file/line or commit) or an authoritative external source.

______________________________________________________________________

## Critical Pitfalls

### Pitfall 1: `IDevice` use-after-free during disconnect-while-in-use

**What goes wrong:**
A hot-plug `Removed` event arrives mid-`setTime()` (or any HID I/O). The `IDevice*` returned by `DeviceRegistry::open(deviceId)` is unique-owned by the registry; if `refresh()` runs on the GUI thread (`Application::onHotplug` → `QMetaObject::invokeMethod(deviceModel, refresh, QueuedConnection)`, `src/app/src/application.cpp:193-194`) while the `TimeSyncService` background sync still holds a raw `IDevice*` snapshot, the underlying `unique_ptr` may be reset between the `dynamic_cast<IClockCapable*>` and the `setTime(now)` call. The dangling cast pointer is then dereferenced.

**Why it happens:**
The current `Application::onHotplug` only refreshes the model — it does NOT yet tear down per-device transports. Adding `TimeSyncService` introduces the **first** consumer that holds an `IDevice*` across an `await`-ish boundary (300 ms debounce QTimer per the time-sync design, `docs/superpowers/specs/2026-05-13-time-sync-design.md:243-249`). The "thread-affine: must be called from the device's I/O thread" comment on `IClockCapable::setTime` (design doc line 128) does NOT guarantee the registry hasn't dropped the device between thread hops.

**How to avoid:**

- **Always** look up `IDevice*` via the registry **immediately before** each HID call, never cache across event loop turns.
- **Never** pass raw `IDevice*` across `QTimer::singleShot` boundaries. Capture the `DeviceId` (string) and re-resolve at the firing point.
- **Always** make the registry's per-device `open()` return a `std::shared_ptr<IDevice>` (or a lease handle) so a removal can `reset()` the registry slot while outstanding callers still hold a valid pointer for the duration of their call. (Existing `enumerate()` returns descriptors by value, which is fine; the danger is `open()`-returned interface pointers.)
- **Build-break check:** add a clang-tidy / cppcheck rule (`cppcoreguidelines-owning-memory` / `bugprone-dangling-handle`) for any function in `time_sync_service.cpp` and `device_registry.cpp` that returns or stores `IDevice*` or `IClockCapable*`.

**Warning signs:**

- ASAN reports `heap-use-after-free` originating in `setTime()` stack frames during the hot-plug stress test.
- Intermittent SIGSEGV under the multi-device test (Phase 1 SC) that disappears under `--single-device` runs.
- Crash dumps with `0xdead...` or `0xfeee...` (Windows debug heap fill values) inside `IClockCapable::setTime`.

**Phase to address:**
**Phase 1 (Hot-plug hardening)** — must land the shared-ownership change **before** Phase 2 (time-sync) wires the first cross-event-loop consumer. If Phase 2 ships against today's `unique_ptr<IDevice>` registry, the use-after-free is essentially guaranteed by the 300 ms debounce.

______________________________________________________________________

### Pitfall 2: `dynamic_cast` returning `nullptr` for the device that just disappeared

**What goes wrong:**
`TimeSyncService::setSystemTimeOn(deviceId)` does `clk = dynamic_cast<IClockCapable*>(device)`. Between the descriptor lookup (`descriptor.hasClock == true`) and the cast, the device disconnects; the registry now returns either `nullptr` or a different device (same VID/PID but new serial, since the OS recycled the device path). The cast either returns `nullptr` (no crash, but `setTime` silently no-ops with no UI feedback) or succeeds against a **different** physical device (correct cast, wrong target).

**Why it happens:**

- `hasClock` is a **static** descriptor flag (`docs/superpowers/specs/2026-05-13-time-sync-design.md:186-191`); it does not imply the device is currently connected, only that the model row claims the capability.
- `dynamic_cast` failure on a polymorphic non-pointer-to-base path is silent — there is no exception, no log, just `nullptr`. The repo's design doc currently assumes `setTime` runs unconditionally after the cast.
- Stream Dock devices commonly enumerate twice (HID interface + composite control) — same VID/PID, different `hid_open` paths — so "same VID/PID after reconnect" is **not** a unique-identity check.

**How to avoid:**

- **Always** treat `dynamic_cast` result as `nullptr`-possible:
  ```cpp
  auto* clk = dynamic_cast<IClockCapable*>(device);
  if (!clk) {
      emit syncFailed(deviceId, tr("Device no longer connected or capability lost"));
      return;
  }
  ```
- **Never** rely on VID/PID equality to confirm "same device after reconnect." Always include the USB serial (which `HotplugEvent::serial`, `hotplug_monitor.hpp:42`, already carries) in the equality check, falling back to bus/port path on Linux when serial is empty.
- **Auto-sync rule:** the 300 ms debounce QTimer must re-validate connectedness **and** capability at firing time, not at scheduling time.
- **Code-review checklist:** every `dynamic_cast<I*Capable*>` site must be paired with a non-null branch in the same function — grep-able rule: `dynamic_cast<I[A-Z]\w+Capable\*>` → next 3 lines must contain `if (!` or `!= nullptr`.

**Warning signs:**

- "Sync time" button click produces no toast (success or failure) — silent no-op.
- Logs show `setTime` was never called even though the user-visible click landed.
- "Auto-sync triggered immediately on disconnect" — reverse-direction race; the schedule fired against a removal event that was misclassified.

**Phase to address:**
**Phase 2 (Time-sync scaffolding)** — both the manual-sync and auto-sync code paths in `TimeSyncService`.

______________________________________________________________________

### Pitfall 3: Toast-flood / stuck-toast UX during device shuffle

**What goes wrong:**
A user unplugs and re-plugs a USB hub (or a flaky cable produces 4-8 rapid `Removed`/`Arrived` events in \<500 ms). The current toast surface (`src/app/qml/Main.qml:136`, `src/app/qml/components/Toast.qml`) is a single instance — each new `Toast.show(...)` either replaces the previous mid-animation (looks like a flicker) or queues forever (the 4th hot-plug event's toast may still be on screen 8 seconds later, after the user already saw the visual result). With auto-sync enabled, every arrival also tries to `setTime` → another toast for the `NotImplemented` failure.

**Why it happens:**

- Hot-plug events are **debounced by the OS but not by the app**. Linux udev typically delivers `add` for each USB interface (composite Stream Dock = 2 interfaces = 2 `add` events per physical connect). Windows `DBT_DEVICEARRIVAL` fires per interface GUID match.
- `TimeSyncService` auto-sync proposes a 300 ms debounce **per device** but does not coalesce across devices.
- The repo's `Toast` component (per `Main.qml:16-17` comment) was built for "profile save / load events" — single-shot, not high-frequency.

**How to avoid:**

- **Always** coalesce hot-plug events for the same `(vid, pid, serial)` tuple inside `Application::onHotplug` with a 250-500 ms trailing-edge debounce **before** any consumer (DeviceModel, TimeSyncService) sees them. Reference implementation: `QTimer` per tuple, restart on each event, fire once.
- **Never** show a toast for every `setTime` call in auto-sync mode. Auto-sync failures should log WARN and surface only via the per-row glyph (exclamation), not the global toast queue. Reserve the toast for **user-initiated** "Sync now" clicks.
- **Always** cap the toast queue to 1-2 simultaneously visible; new toasts beyond the cap replace the oldest non-error toast, never an error.
- **Stuck-toast prevention:** explicit `dismiss()` on `Toast` should be a no-op if the toast is already hidden; the timer-driven hide must be `restart()`-safe.

**Warning signs:**

- Toast text changes mid-fade (visual artefact, hard to read).
- Toast still visible 5+ seconds after the user's last click.
- Multiple identical toasts queue up after a single hub-replug.
- Auto-sync log volume scales linearly with number of connected devices instead of being bounded by user action.

**Phase to address:**
**Phase 1 (Hot-plug hardening)** introduces the coalescing debounce in `Application::onHotplug`. **Phase 2 (Time-sync UI)** must consume the *coalesced* event stream and respect the user-initiated vs auto-initiated toast distinction.

______________________________________________________________________

### Pitfall 4: QML_SINGLETON dual-instantiation strikes `TimeSyncService`

**What goes wrong:**
`TimeSyncService` is registered as a QML singleton (per design doc lines 165-167). Without the precise pattern the v1.0 sweep enforced, Qt 6 silently creates a **second** instance: the QML side reads stale state (`autoSync = false`) while the C++-owned instance receives the `HotplugMonitor::deviceArrived` hook and reads `autoSync = true`. Result: auto-sync silently does nothing, settings page toggle does nothing visible, both compile and run.

**Why it happens:**
Documented in this repo's user-memory (`reference_qt_qml_gotchas.md`) and reinforced by `src/app/src/branding_service.hpp:60-71` + `:166-171`: `QML_SINGLETON` + default-constructible class → Qt 6 SFINAE picks `SingletonConstructionMode::Constructor` (default-construct via `new T`) over `Factory` (call our static `create()`), **even if a static `create(QQmlEngine*, QJSEngine*)` exists**. The light-theme bug (commit `d7f932f`) surfaced it; the preventive sweep (`e221b21`) added `static_assert(!std::is_default_constructible_v<T>, ...)` to BrandingService, DeviceModel, ProfileController, ThemeService, TrayController, LoadedPluginsModel, PluginCatalogModel, PropertyInspectorController, AutostartService (verified via grep at research time).

**How to avoid:**

- **Always** make the `TimeSyncService` constructor take a non-defaulted parameter (e.g. `explicit TimeSyncService(QObject* parent)` — no `= nullptr` default arg, no `= default` declarations).
- **Always** add the `static_assert(!std::is_default_constructible_v<TimeSyncService>, "see ctor @note")` immediately after the class declaration. This is the load-bearing invariant lock the v1.0 sweep established.
- **Always** register via `qmlRegisterSingletonInstance` (or the `create(QQmlEngine*, QJSEngine*)` static returning the `Application`-owned instance with `CppOwnership` set) — never rely on `QML_SINGLETON` alone.
- **Build-break check:** the static_assert IS the build break. The v1.0 sweep also added a `tests/unit/test_qml_singleton_sweep.cpp`-style assertion sweep; the new `TimeSyncService` must be added to it.
- **Code-review checklist:** for any new `QML_SINGLETON` class, search `git grep "static_assert.*is_default_constructible.*<ClassName>"` before merge.

**Warning signs:**

- The settings toggle visibly changes but auto-sync does nothing on connect.
- `qDebug() << this` inside `TimeSyncService::setAutoSync` and inside `TimeSyncService::onDeviceArrived` print **different** pointer values.
- A breakpoint on the constructor fires twice during app startup.

**Phase to address:**
**Phase 2 (Time-sync scaffolding)** — the `TimeSyncService` definition itself.

______________________________________________________________________

### Pitfall 5: Win32 env-block UTF-16 layout — missing the second null terminator

**What goes wrong:**
The CR-01 fix builds a per-spawn UTF-16 environment block to pass to `CreateProcessW` with `CREATE_UNICODE_ENVIRONMENT`. The naive implementation builds `L"KEY1=val1\0KEY2=val2\0KEY3=val3\0"` (single trailing `\0`). `CreateProcessW` then reads past the buffer end looking for the second `\0\0` block terminator. Result: child process inherits garbage env vars (best case: spurious "key=value" parsed from adjacent heap memory; worst case: access violation in `kernelbase.dll`).

**Why it happens:**
Per Microsoft docs (verified at `learn.microsoft.com/.../CreateProcessW`): _"A Unicode environment block is terminated by four zero bytes: two for the last string, two more to terminate the block."_ This is **two `wchar_t` nulls in a row** (`L"...\0\0"` — i.e. the last string's NUL **plus** a separate block-terminator NUL). The block-terminator NUL is easy to forget because:

- `std::wstring` operators (`+=`, `push_back('\0')`) add exactly one terminator at a time.
- Visual debuggers display `L"...\0"` and `L"...\0\0"` identically (both render as the same trailing-null indication).
- POSIX `envp` is a null-terminated array of pointers — no equivalent block-terminator concept — so reviewers from a POSIX background often "fix" the perceived redundancy.

**Additional traps in the same code path:**

- **Lifetime:** `lpEnvironment` must remain valid until `CreateProcessW` **returns** (it copies the block into the new process). A `std::vector<wchar_t>` local in the same scope as the `CreateProcessW` call is fine; a `wchar_t*` returned from a helper that lets the underlying vector go out of scope is undefined behaviour.
- **Sort order:** Windows requires env-block entries to be **sorted alphabetically** (case-insensitive, by-character, with `=`-drive-letter entries at the front). Inserting the three Python overrides in unsorted order against a `GetEnvironmentStringsW` snapshot that **was** sorted produces a block the kernel treats as truncated at the first out-of-order entry.
- **`=`-drive-letter entries:** `GetEnvironmentStringsW` returns entries like `L"=C:=C:\\Users\\...\0"` — leading-`=` entries are current-directory-per-drive records, not user env. They MUST be preserved (or current-directory inheritance breaks). The CR-01 design doc (`.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md:34-40`) correctly says "snapshot from `GetEnvironmentStringsW`" — but the build helper must not filter `=`-prefixed entries.
- **`bInheritHandles` interaction:** the current `out_of_process_plugin_host_win32.cpp:421-449` flow `SetHandleInformation(..., HANDLE_FLAG_INHERIT, 0)` for the read end (correct) but **also passes `bInheritHandles = TRUE`** (line ~540 typically). When the env block is added, the inheritance interaction is unchanged — but reviewers often "tidy up" inheritance flags when touching the spawn site, breaking pipe wiring. **Do not** modify `bInheritHandles` while landing CR-01.

**How to avoid:**

- **Always** terminate the env block with `block.push_back(L'\0'); block.push_back(L'\0');` after the last `KEY=VALUE\0` entry. Verify with `assert(block.size() >= 2 && block[block.size()-1] == 0 && block[block.size()-2] == 0)`.
- **Always** sort entries case-insensitively (`_wcsicmp` on the key portion up to the first `=`) **before** appending the block terminator.
- **Always** preserve `=`-prefixed entries verbatim from `GetEnvironmentStringsW`.
- **Always** free the snapshot with `FreeEnvironmentStringsW` (not `delete[]`, not `LocalFree`).
- **Never** reuse the `std::wstring` heap of a temporary as the env block — keep a `std::vector<wchar_t>` owned by the same stack frame that calls `CreateProcessW`.
- **Test on Windows.** This is the v1.0 lesson the milestone-context explicitly calls out as "canonical example" — the Linux dev box can compile this code via cross-toolchain but cannot exercise it. CR-01 closure **requires** a Windows CI job (or local VM smoke-test) before the deferred status flips.

**Warning signs:**

- Child process starts but `os.environ['PYTHONPATH']` in the Python plugin host is empty / unset / wrong.
- Child process **inherits** some env vars but **not** the three overrides, in a non-deterministic pattern (corruption is alignment-dependent).
- AppVerifier or the Windows debug heap reports "block terminator missing" or "buffer overrun by N bytes" at the `CreateProcessW` call site.
- The pipe stdio works fine but the child crashes in `Py_Initialize` because PYTHONPATH points at random heap bytes.
- A second `OutOfProcessPluginHost` constructed concurrently no longer leaks env vars (the CR-01 ctor-counter `std::atomic<int>` from the fallback design also doesn't fire) — but Python plugin behaviour is still wrong. → env block malformed.

**Phase to address:**
**Phase 4 (CR-01 closure)** — Windows-specific phase. Must include a Windows CI job exercising the spawn path (even a dummy plugin host with a trivial Python entrypoint that prints `os.environ['PYTHONPATH']` and exits would catch every variant of this bug).

______________________________________________________________________

### Pitfall 6: `_putenv_s` left in place "for safety" — defeats the entire fix

**What goes wrong:**
The CR-01 fix introduces the per-spawn env block but the reviewer (understandably nervous) leaves the existing three `_putenv_s` calls (`out_of_process_plugin_host_win32.cpp:463-467`) in place as a "belt and braces" measure. The env-block path is correct in isolation, but the parent's `PYTHONPATH` is still polluted, **and** the cross-instance-pollution race the deferred review specifically called out (`.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md:14-20`) is unmitigated. Worse, the sibling `manifest_signer_win32.cpp` verifier subprocess still inherits the polluted parent env.

**Why it happens:**
`_putenv_s` is a `CRT`-level call that mutates the **parent's** process env. The whole point of CR-01 is that the **per-spawn block** removes the need to mutate the parent. Removing the `_putenv_s` calls feels scary if reviewers don't 100% trust the new block is correct — but keeping them is strictly worse than no fix at all (it makes the new env-block code dead while preserving the bug).

**How to avoid:**

- **Always** delete the three `_putenv_s` calls in the same commit that adds the env-block builder. Atomicity matters: the env-block path is only correct **iff** the `_putenv_s` mutations are gone.
- **Always** add a unit test that asserts `GetEnvironmentVariableW(L"PYTHONPATH", ...)` returns the **parent's** unmutated value after `OutOfProcessPluginHost` construction. This test fails today (because `_putenv_s` mutates the parent) and must pass after the fix.
- **Code-review checklist:** in the CR-01 PR, `git diff` must show three `-_putenv_s(...)` removals **and** ~40-60 lines of env-block code added. If `_putenv_s` survives the diff, request changes.

**Warning signs:**

- The new env-block unit test passes (env block is correct) but the parent-pollution test still fails (`_putenv_s` survived).
- Two concurrently-constructed hosts still produce cross-pollution under the race test the v1.0 review wanted closed.

**Phase to address:**
**Phase 4 (CR-01 closure)** — same phase that adds the env block.

______________________________________________________________________

### Pitfall 7: `loadTrustRoots` parser DoS via deeply nested / pathological JSON

**What goes wrong:**
The current mini-grep parser (`src/plugins/src/manifest_signer.cpp:102-153`) is `O(n²)` worst-case (each `find("\"key\"")` is a forward scan, then `rfind('{', keyPos)` is a backward scan, repeated `cursor`-times). A pathological trust-roots file — 50 MB of nested `{{{{...}}}}` braces with no actual `"key"` field — burns CPU until the user kills the process. The current 512-byte window cap helps with the inner scan but does not bound the outer cursor advance.

**Why it happens:**
The partial fix shipped in v1.0 (`.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md:60-95`) explicitly widened the window to fix the name-before-key order bug, **but did not address DoS resistance**. Trust-roots files are typically \<10 KB, so this is a low-probability path — but it's a **security-sensitive** file (the manifest verifier's trust store), and security-sensitive parsers should fail closed on malformed input within a bounded budget, not loop until killed.

**How to avoid:**

- **Always** impose a hard byte-cap on `readFile(jsonPath)` (e.g. 1 MB — trust-roots beyond that size are not legitimate use cases). Fail closed on oversize.
- **Always** impose an entry-count cap on the parser loop (e.g. 1024 entries — beyond which "trust roots" is misnamed).
- **Always** measure parser CPU under a pathological-input fuzz corpus before declaring WR-01 closed.
- **Replacement decision (per design doc):** the three options are documented in `01-FIX-DEFERRED.md:74-96`. `nlohmann::json` violates nothing structural but adds a dep; `QJsonDocument` violates `COD-031` (Qt-free `ajazz_plugins`); five-state scanner is the in-house option. **Recommendation:** five-state scanner is the right tradeoff (~80-100 LOC, no new deps, lives in `wire_protocol.hpp` next to `findStringField`, fully fuzzable). Reject the `nlohmann::json` option even though it's tempting — it widens the trusted parsing surface inside the plugin sandbox boundary, which is the opposite of the WR-01 goal.

**Warning signs:**

- Fuzz harness (libFuzzer / AFL) reports >1 second runtime on inputs under 100 KB.
- Memory profiler shows linear growth in string-view backing buffer during parser run (shouldn't — `string_view` is non-owning).

**Phase to address:**
**Phase 5 (WR-01 closure)** — owns the architectural decision plus the implementation.

______________________________________________________________________

### Pitfall 8: TOCTOU between `loadTrustRoots` read and `verifyManifest` use

**What goes wrong:**
`loadTrustRoots(config.trustedPublishersFile)` reads the file at verifier-startup time and caches the result (or re-reads at every `verifyManifest` call — current behaviour at `manifest_signer.cpp:183` is "re-read at every call", which is one of the few cases where this is a feature, not a bug). Between `readFile` and the actual `dispatch_to_verifier`, an attacker with write access to the trust-roots file can swap in attacker-controlled publishers, then swap back before any audit notices. Mitigation depends on **who** has write access to the file — typically `~/.config/ajazz/trust-roots.json`, mode 0600, owned by the user. The user themselves can swap freely; an attacker who can write that file has already lost the security boundary.

**Why it happens:**
The classic TOCTOU shape is read-A, use-B-where-B-≠-A. The current code does `readFile`-then-walk-the-blob, which is in fact TOCTOU-safe by accident — the **blob** is read into memory once and walked there. The TOCTOU exists only across separate `verifyManifest` calls.

**How to avoid:**

- **Always** load the blob into an `std::string` **once** per `verifyManifest` call (the current code does this — keep it that way; don't "optimise" by caching `trustRoots` across calls without an invalidation mechanism).
- **Always** document the file-permissions assumption in the public API doc for `ManifestSignerConfig::trustedPublishersFile` ("must be 0600 / user-only-writable; behaviour is undefined if the file is concurrently writable by another principal").
- **Defence in depth:** the file's mtime + size + SHA-256 could be cached as a fast-path; full re-parse only if any change. Out of scope for WR-01; tracked as future-work.
- **Never** trust a path supplied at runtime by an unprivileged caller. The path is configured at host construction (root-owned config), not per-verify.

**Warning signs:**

- Race-stress test (1000 concurrent `verifyManifest` calls against a file being rewritten) produces a verification result that doesn't match any committed file state at any single point in time. → cached state survived past file mutation.

**Phase to address:**
**Phase 5 (WR-01 closure)** — same phase that hardens the parser; the TOCTOU documentation goes in the same header comment.

______________________________________________________________________

## Moderate Pitfalls

### Pitfall 9: HID Report ID byte 0 confusion across feature/output reports

**What goes wrong:**
The repo's existing AKP153 / AKP03 / AKP05 backends use `hid_write` for output reports and `hid_send_feature_report` for feature reports (`src/core/src/hid_transport.cpp:86, 109`). When a new device wire format gets implemented (Phase 3 scaffolding-to-functional), the implementer copy-pastes a packet-builder pattern from `akp153.cpp` that uses `hid_write` and gets `hid_write failed` with no obvious reason — because the new device's command requires a feature report.

A separate flavour: the AKP-family backends prepend the Report ID byte (typically `0x00` or `0x02`) at position `[0]` of the packet (visible in `buildCmdHeader` — bytes 0-2 are `"CRT"` which **includes** the Report ID position). A new backend that does NOT prepend a Report ID byte will see its first protocol byte stripped silently on some platforms (Linux hidraw strips byte 0 if it doesn't match a declared Report ID; Windows hidapi treats byte 0 as Report ID unconditionally — verified via hidapi docs at libusb/hidapi master, hidapi.h header).

**Why it happens:**

- hidapi convention: byte 0 of the buffer passed to `hid_write`/`hid_send_feature_report` is **always** the Report ID, even on devices with a single (Report ID = 0) report. The wrapper functions in `hid_transport.cpp` do **not** prepend it for the caller — the caller's `data.data()[0]` is interpreted as the Report ID.
- Stream Dock-family packets are 512 bytes including the Report ID; the protocol header `"CRT"` at bytes 0-2 means byte 0 (`0x43` = `'C'`) is being treated as Report ID 0x43, which works only because Stream Dock firmware accepts any byte 0 value. A device that strictly validates Report ID will reject the packet.
- Feature reports vs output reports use different USB control transfers; mixing them produces `STALL` from the device, surfacing as `hid_write failed` / `hid_send_feature_report failed` with no further diagnostic.

**How to avoid:**

- **Document explicitly** in each device-protocol .md (`docs/protocols/<family>/<device>.md`) whether byte 0 is a Report ID or part of the protocol header. Stream Dock family: byte 0 is the "Report ID" used by hidapi but happens to coincide with the protocol's `'C'` prefix.
- **Code-review checklist** for new device backends: the first call to `transport.write(...)` or `transport.writeFeature(...)` must have a comment naming the Report ID convention used.
- **Test pattern:** every new device's unit test must include a "round-trip a packet built by `buildCmdHeader` and assert byte 0 is preserved" check — catches accidental Report-ID stripping early.

**Phase to address:**
**Phase 3 (Scaffolded-device wiring)** — each device backend promotion.

______________________________________________________________________

### Pitfall 10: Endianness on packed protocol structs

**What goes wrong:**
A new device wire format defines a 32-bit Unix timestamp at bytes 8-11 of a command packet. The implementer writes:

```cpp
auto* ts = reinterpret_cast<std::uint32_t*>(&pkt[8]);
*ts = static_cast<std::uint32_t>(epoch_seconds);
```

This is host-byte-order on x86 (little-endian) — correct on Intel Win/Linux/macOS. The repo also targets macOS-on-Apple-Silicon (ARM64) which is also little-endian today. But: any future Linux-on-RISC-V or BE-MIPS embedded target silently sends big-endian bytes to a device that expects little-endian. Existing examples in the repo use `std::array<std::uint8_t, N>` with byte-by-byte assignment (`pkt[10] = percent;`), which sidesteps the issue — but a new contributor copying patterns from an external HID example may not.

**Why it happens:**
USB HID is a byte protocol; multi-byte fields have device-defined endianness. The C++ way to write multi-byte fields portably is byte-by-byte (`pkt[8] = (value >> 0) & 0xFF; pkt[9] = (value >> 8) & 0xFF; ...`) or via `std::endian` + `std::byteswap` (C++23). `reinterpret_cast` to a multi-byte pointer is host-endian and is wrong on principle even when accidentally correct on x86/ARM.

**How to avoid:**

- **Always** write multi-byte fields byte-by-byte in protocol code, or wrap in a `pack_le16` / `pack_le32` / `pack_be32` helper. The existing `akp153.cpp` patterns (`pkt[10] = std::min<std::uint8_t>(percent, 100);`) are the model.
- **Never** `reinterpret_cast<uint32_t*>` into a packed protocol buffer. clang-tidy `cppcoreguidelines-pro-type-reinterpret-cast` catches it.
- **Document endianness in protocol .md** for every multi-byte field.

**Phase to address:**
**Phase 3 (Scaffolded-device wiring)** — when a real timestamp / counter / coordinate becomes a protocol field.

______________________________________________________________________

### Pitfall 11: hidapi blocking-mode regression after device close/reopen

**What goes wrong:**
The repo sets `hid_set_nonblocking(m_handle, 1)` once in `HidTransport::open()` (`hid_transport.cpp:70`). After a device disconnect + reconnect, `open()` runs again, sets non-blocking again — fine. But if a future change adds a `reset()` method that calls `hid_close` followed by a quick `hid_open` (without going through `HidTransport`), the `hid_set_nonblocking` call gets skipped. Subsequent `hid_read_timeout` calls block for the full timeout even when no data is available, making the I/O thread unresponsive to shutdown.

**Why it happens:**
hidapi's per-handle non-blocking flag does not persist across `hid_close` / `hid_open` cycles. Verified via libusb/hidapi `hidapi.h` documentation and the signal11/hidapi issues thread.

**How to avoid:**

- **Always** route every `hid_open` through `HidTransport::open()`. No ad-hoc `hid_open` calls elsewhere in the codebase. Add a grep CI check: `grep -r "hid_open(" src/ | grep -v hid_transport.cpp` must return zero hits.
- **Always** call `hid_set_nonblocking(handle, 1)` immediately after every successful `hid_open` in `HidTransport::open()`.

**Phase to address:**
**Phase 1 (Hot-plug hardening)** — any reconnect/reset paths added must respect the open-pairs-with-nonblocking invariant.

______________________________________________________________________

### Pitfall 12: System-time-set permission denied (red herring — out of scope but the design hides it)

**What goes wrong:**
A reviewer or new contributor reads "time sync" and assumes the feature can mutate the **host** clock (`settimeofday` / `SetSystemTime`). On non-root Linux / non-admin Windows, those calls return `EPERM` / `ERROR_PRIVILEGE_NOT_HELD`. They then ask "why does Time Sync fail on a non-root user?" — wasting cycles before the design doc's **non-goal** ("We do NOT manipulate the host's clock from the device — one-way push only", design doc lines 47-49) is re-discovered.

**Why it happens:**
The feature name "Time Sync" connotes bidirectional sync in NTP / chrony parlance. The AJAZZ feature is device-set-only.

**How to avoid:**

- **UI label:** the QML button is "Sync time **to device**" (or "Send host time to device"), never just "Sync time."
- **Settings page label:** "Auto-sync **device clock** on connect" (the design doc already has this wording, line 174). Phase 2 must NOT shorten it during UI polish.
- **README + CHANGELOG language** must be explicit that the feature is host→device.

**Phase to address:**
**Phase 2 (Time-sync UI)** — labels and documentation framing.

______________________________________________________________________

### Pitfall 13: Auto-sync persisted setting silently survives a backend that loses `Capability::Clock`

**What goes wrong:**
A user toggles "Auto-sync time on device connect" on. Setting persists in `QSettings("Time/AutoSync") = true`. Later, the user updates the app; the new release ships a backend regression where `Capability::Clock` is no longer advertised by any connected device (e.g. the bit accidentally renumbered). The auto-sync hook still fires on every connect, the capability check fails silently (no `IClockCapable*`), and the setting toggle in the UI is greyed-out (because no connected device has the capability) but the underlying `true` value is unchanged. User then connects a different device that **does** still advertise the capability — auto-sync fires unexpectedly because the persisted value was never invalidated.

**Why it happens:**
Persistent settings outlive code versions. Capability bit renumbering is a real risk in v1.1 (the design doc explicitly adds `Clock = 1u << 15` as the next free bit, so reordering above it is dangerous).

**How to avoid:**

- **Never** renumber existing capability bits. Always append (the design's `1u << 15` rule). Lock this with a `static_assert` on the existing bits' values.
- **Validate persisted state at load time:** if the persisted `autoSync` is `true` but no registered device advertises `Capability::Clock`, log INFO ("auto-sync persisted ON but no capable device") — do not silently disable, do not silently fire.
- **CHANGELOG check on bit renumber:** any PR that touches `enum class Capability` requires a "no values reordered" review note.

**Phase to address:**
**Phase 2 (Time-sync scaffolding)** — both the bit-numbering lock and the load-time validation.

______________________________________________________________________

## Minor Pitfalls

### Pitfall 14: `Result::NotImplemented` log spam under auto-sync

**What goes wrong:**
Auto-sync fires on every device arrival → every connected `IClockCapable`-advertising device returns `Result::NotImplemented` → AJAZZ_LOG_WARN emits one line per device per connect event. With 7 scaffolded devices and a single hub-replug, that's 14+ WARN lines in \<1 second. CI log artefacts get larger, and the warnings train users to ignore the WARN level.

**How to avoid:**

- Backend stubs log WARN **once per process lifetime** for the `NotImplemented` case (gate on a `std::once_flag` per backend). Subsequent calls return `Result::NotImplemented` silently.
- The `TimeSyncService` does NOT log `NotImplemented` itself — that's the backend's responsibility (per design doc, line 202-204).

**Phase to address:** Phase 2.

______________________________________________________________________

### Pitfall 15: Hot-plug refresh thrash when `DeviceModel::refresh()` rebuilds the whole list

**What goes wrong:**
`DeviceModel::refresh()` (`src/app/src/device_model.cpp:105-110`) does `beginResetModel()` / `endResetModel()` — full list rebuild. Each hot-plug event triggers a full reset; QML rebuilds the entire DeviceList, drops selection state, loses scroll position. With a coalesced event stream (Pitfall 3) this is fine; without coalescing, the UX is jittery on USB hubs.

**How to avoid:**

- Coalesce per Pitfall 3 (the same mitigation).
- **Future-work** (NOT v1.1): switch `DeviceModel` to fine-grained `dataChanged` / `beginInsertRows` / `beginRemoveRows`. Out of scope for v1.1 unless multi-device baseline tests reveal it as a blocker.

**Phase to address:** Phase 1.

______________________________________________________________________

### Pitfall 16: `nlohmann::json` "while you're in there" dep creep on WR-01

**What goes wrong:**
While closing WR-01 the implementer notices that the app layer already uses `QJsonDocument` and the plugin layer would benefit from `nlohmann::json` for the verifier helper script's input parsing. They add `nlohmann::json` as a project dep "just for this fix," forgetting that the COD-031 boundary (`ajazz_plugins` must be Qt-free, but also: minimal-dep) was deliberately set to keep the plugin library distributable as a thin shared library.

**How to avoid:**

- **Architectural decision must be made before Phase 5 starts**, not during it. Per `01-FIX-DEFERRED.md:74-83`, the options are `nlohmann::json` (adds dep), `QJsonDocument` (breaks COD-031), five-state scanner (no new deps). **Pick during the phase-planning pass, not during the implementation slice.**
- If `nlohmann::json` is chosen: update `vcpkg.json`, CMakeLists link list, CI pin manifests, and the COD-031 charter in the same commit.

**Phase to address:** Phase 5 (decision in planning, implementation in execution).

______________________________________________________________________

## Cross-Cutting Pitfall: "Silently passes CI on Linux while broken on Windows"

The canonical v1.0 example: CR-01 was discoverable only on Windows; the Linux-only CI green-lit the original `_putenv_s`-based code. v1.1 has **at least three** features that re-create this risk shape:

| Feature                | Linux-CI-blind failure mode                                                                                                                                                                              |
| ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CR-01 env block        | Wrong block terminator / sort order — only matters when `CreateProcessW` actually parses it.                                                                                                             |
| Hot-plug Win32 backend | `WM_DEVICECHANGE` message-only window has subtle thread-affinity rules; `PostThreadMessageW` (`hotplug_monitor.cpp:457-458`) on a thread that hasn't yet pumped messages **loses** the WM_QUIT silently. |
| HID Report ID byte 0   | Linux hidraw and Windows hidapi handle Report ID byte 0 differently; a device working on Linux may STALL on Windows.                                                                                     |

**Prevention strategy (apply to every v1.1 phase):**

- **Always** spin up a Windows CI job for any phase touching `*_win32.cpp`, hot-plug, hidapi, or process spawn. Even a smoke-build is better than nothing.
- **Always** add a "Tested-on: linux-x86_64, windows-x86_64" trailer to commit messages for these phases. If the trailer says linux-only, the PR must justify why.
- **Code-review checklist:** any new `#if defined(_WIN32)` block requires at least one Windows-platform assertion or a `TODO(windows-validation): ...` comment naming the unvalidated path.

**Phase to address:** Phases 1, 3, 4 (and Phase 5 if `nlohmann::json` is chosen — its Windows build matrix needs validation).

______________________________________________________________________

## Technical Debt Patterns

| Shortcut                                                                                                                   | Immediate Benefit                               | Long-term Cost                                                                               | When Acceptable                                                                                                                            |
| -------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------- | -------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------ |
| Use `unique_ptr<IDevice>` registry slots, look up raw `IDevice*` per call                                                  | No API change in v1.1                           | Use-after-free under disconnect-during-use (Pitfall 1)                                       | Never once any consumer crosses an event-loop turn — i.e. never in v1.1+. Was acceptable in v1.0 because nothing crossed event-loop turns. |
| Keep `_putenv_s` in CR-01 fix "for safety"                                                                                 | No risk of "what if my new code is wrong"       | Defeats the entire fix (Pitfall 6)                                                           | Never. The new env-block path is correct **iff** `_putenv_s` is removed.                                                                   |
| Cache `loadTrustRoots` result across `verifyManifest` calls                                                                | Faster verification (negligible — \<10 KB file) | TOCTOU expansion (Pitfall 8), invalidation complexity                                        | Never until file-change detection is in place.                                                                                             |
| Skip the QML_SINGLETON static_assert on `TimeSyncService` "because we already register via `qmlRegisterSingletonInstance`" | Slightly less ceremony                          | Dual-instantiation bug re-emerges the day someone adds a default arg to the ctor (Pitfall 4) | Never — the static_assert IS the regression test.                                                                                          |
| `reinterpret_cast<uint32_t*>` for multi-byte protocol fields                                                               | One line shorter                                | Endianness regression on any non-LE host (Pitfall 10)                                        | Never — byte-wise is the project standard (`akp153.cpp` is the model).                                                                     |
| Ship CR-01 fix without a Windows CI job                                                                                    | "We'll validate locally"                        | Same shape as v1.0 CR-01 itself (Cross-cutting)                                              | Never — Windows CI is the canonical lesson learned.                                                                                        |

______________________________________________________________________

## Integration Gotchas

| Integration                            | Common Mistake                                                 | Correct Approach                                                                                                             |
| -------------------------------------- | -------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `HotplugMonitor` callback → GUI thread | Touching `QAbstractListModel` directly from the monitor thread | `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` — repo already does this at `application.cpp:193-194`, do not regress |
| `IClockCapable*` from `dynamic_cast`   | Assume non-null after the descriptor `hasClock` check          | Always test `if (!clk)` and emit `syncFailed` (Pitfall 2)                                                                    |
| `CreateProcessW` env block             | Single `\0` terminator                                         | `\0\0` block terminator + sorted entries (Pitfall 5)                                                                         |
| `hidapi` byte 0                        | Treat as first protocol byte                                   | Treat as Report ID; document the convention per device (Pitfall 9)                                                           |
| `QML_SINGLETON` registration           | Macro alone                                                    | `static_assert` + `qmlRegisterSingletonInstance` (Pitfall 4)                                                                 |
| `QSettings`-persisted booleans         | Trust on read                                                  | Validate against current capability surface at load (Pitfall 13)                                                             |

______________________________________________________________________

## "Looks Done But Isn't" Checklist

- [ ] **Phase 1 (Hot-plug):** disconnect-during-use stress test runs without ASAN errors AND without intermittent crashes across 1000 iterations on **both** Linux and Windows.
- [ ] **Phase 1 (Hot-plug):** event coalescing fires exactly once per `(vid,pid,serial)` debounce window — verified by a timer-mocked unit test, not just visual inspection.
- [ ] **Phase 2 (Time-sync):** `static_assert(!std::is_default_constructible_v<TimeSyncService>)` is present at the bottom of `time_sync_service.hpp`. (Grep check, not eyeball.)
- [ ] **Phase 2 (Time-sync):** `dynamic_cast<IClockCapable*>` null-check exists at every call site (grep + manual review).
- [ ] **Phase 2 (Time-sync):** auto-sync persisted-flag load-time validation is in `TimeSyncService` ctor, not deferred.
- [ ] **Phase 2 (Time-sync):** UI labels say "to device" / "device clock" — host-clock-set framing nowhere in product surface.
- [ ] **Phase 3 (Device wiring):** every promoted backend's first `transport.write`/`writeFeature` call site carries a Report ID convention comment.
- [ ] **Phase 3 (Device wiring):** every multi-byte protocol field is written byte-wise (no `reinterpret_cast<uint16/32/64_t*>`).
- [ ] **Phase 4 (CR-01):** `git diff` shows three `-_putenv_s(...)` lines AND the env-block builder added. No "belt-and-braces."
- [ ] **Phase 4 (CR-01):** Windows CI job exercises the spawn path. Linux-only validation is **not** sufficient.
- [ ] **Phase 4 (CR-01):** env-block fuzzer / unit test asserts the `\0\0` block terminator and sort order.
- [ ] **Phase 5 (WR-01):** parser architectural decision (`nlohmann::json` vs scanner) was made **before** implementation started, with a written rationale in the phase plan.
- [ ] **Phase 5 (WR-01):** parser has hard byte-cap and entry-count-cap with explicit fail-closed behaviour.
- [ ] **Phase 5 (WR-01):** trust-roots file-permission requirement is documented in the public API header.
- [ ] **Cross-cutting:** every phase that touches `*_win32.cpp` has at least one Windows CI run before merge.

______________________________________________________________________

## Recovery Strategies

| Pitfall                                       | Recovery Cost                    | Recovery Steps                                                                                                                                                                          |
| --------------------------------------------- | -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Use-after-free in TimeSyncService (Pitfall 1) | MEDIUM                           | Migrate registry slots to `shared_ptr<IDevice>`. Surfaces as crashes; no data corruption. Hotfix-able.                                                                                  |
| `dynamic_cast` null silent no-op (Pitfall 2)  | LOW                              | Add the null-check; ship as a point release. No regression test gap once the check exists.                                                                                              |
| Toast flood (Pitfall 3)                       | LOW                              | Add coalescing debounce in `Application::onHotplug`; bounded UI impact, no data risk.                                                                                                   |
| QML_SINGLETON dual-instance (Pitfall 4)       | LOW                              | Add the `static_assert`, fix the ctor. Build break makes regression impossible once landed.                                                                                             |
| Win32 env-block malformed (Pitfall 5)         | HIGH                             | Child plugin host crashes / runs with wrong env → silent plugin misbehaviour. Requires Windows reproduction to debug; can corrupt plugin sandbox boundary. **Prevent, do not recover.** |
| `_putenv_s` left in (Pitfall 6)               | LOW                              | Delete the calls; verifiable diff. The risk is **catching** it before merge, not recovering after.                                                                                      |
| Trust-roots parser DoS (Pitfall 7)            | MEDIUM                           | Add byte/entry caps, ship as a security patch. CVE-grade if exploited.                                                                                                                  |
| Trust-roots TOCTOU (Pitfall 8)                | LOW (in current code)            | Documented as a permissions assumption; no code change unless caching is added.                                                                                                         |
| Report ID byte 0 wrong (Pitfall 9)            | MEDIUM per device                | Device-specific; requires firmware-side experimentation. Document and unit-test.                                                                                                        |
| Endianness wrong (Pitfall 10)                 | LOW until shipped to a BE target | Byte-wise rewrite; mechanical fix.                                                                                                                                                      |
| Capability bit renumber (Pitfall 13)          | HIGH                             | Persisted settings now mean something different. Requires a settings-migration step on next launch. **Prevent via static_assert lock.**                                                 |

______________________________________________________________________

## Pitfall-to-Phase Mapping

| Pitfall                                        | Prevention Phase                         | Verification                                                                              |
| ---------------------------------------------- | ---------------------------------------- | ----------------------------------------------------------------------------------------- |
| 1. `IDevice` UAF during disconnect             | Phase 1 (Hot-plug hardening)             | ASAN-clean hot-plug stress test, 1000 cycles, both platforms                              |
| 2. `dynamic_cast` nullptr silent no-op         | Phase 2 (Time-sync)                      | Grep `dynamic_cast<I.*Capable\*>` → all matches have null-check within 3 lines            |
| 3. Toast-flood / stuck-toast                   | Phase 1 (Hot-plug) + Phase 2 (Time-sync) | Hub-replug test produces ≤2 toasts; auto-sync produces zero toasts (only glyphs)          |
| 4. QML_SINGLETON dual-instance                 | Phase 2 (Time-sync)                      | `static_assert(!is_default_constructible_v<TimeSyncService>)` compiles                    |
| 5. Win32 env-block layout                      | Phase 4 (CR-01)                          | Windows smoke test prints expected `os.environ['PYTHONPATH']` in child; AppVerifier clean |
| 6. `_putenv_s` left in alongside env block     | Phase 4 (CR-01)                          | PR diff shows three `_putenv_s` deletions; parent-pollution unit test passes              |
| 7. Trust-roots parser DoS                      | Phase 5 (WR-01)                          | libFuzzer / AFL corpus runs to completion under 1 s on 100 KB inputs                      |
| 8. Trust-roots TOCTOU                          | Phase 5 (WR-01)                          | Header doc-comment names the 0600 permissions assumption                                  |
| 9. HID Report ID byte 0 confusion              | Phase 3 (Device wiring)                  | Per-device protocol .md documents byte 0 convention; round-trip test                      |
| 10. Endianness on packed structs               | Phase 3 (Device wiring)                  | clang-tidy `cppcoreguidelines-pro-type-reinterpret-cast` passes                           |
| 11. hidapi blocking-mode regression            | Phase 1 (Hot-plug)                       | Grep check: `hid_open` only in `hid_transport.cpp`                                        |
| 12. System-time-set permission red herring     | Phase 2 (Time-sync)                      | UI labels reviewed; no "Sync time" without "to device"                                    |
| 13. Persisted setting outliving capability     | Phase 2 (Time-sync)                      | `static_assert(static_cast<unsigned>(Capability::Clock) == (1u << 15))` lock              |
| 14. `Result::NotImplemented` log spam          | Phase 2 (Time-sync)                      | Multi-device hub-replug log line count is bounded by `O(devices)`, not `O(events)`        |
| 15. Hot-plug refresh thrash                    | Phase 1 (Hot-plug)                       | Selection / scroll position survives 1 Hz event coalescing                                |
| 16. Dep creep on WR-01                         | Phase 5 planning                         | Decision recorded in phase-plan before implementation begins                              |
| Cross-cutting: Linux-CI-blind Windows breakage | Phases 1, 3, 4 (5 if applicable)         | Windows CI job exists and ran green on the merge commit                                   |

______________________________________________________________________

## Sources

**Repo-internal (HIGH confidence):**

- `.planning/PROJECT.md` — milestone goals, v1.0 key decisions, QML_SINGLETON sweep history
- `.planning/milestones/v1.0-phases/01-sec-003-plugin-host/01-FIX-DEFERRED.md` — CR-01 + WR-01 deferred-fix paths (canonical context)
- `docs/superpowers/specs/2026-05-13-time-sync-design.md` — five-layer slice, `dynamic_cast` lazy-cast pattern, 300 ms debounce
- `docs/superpowers/plans/2026-05-13-time-sync.md` — file-level implementation plan
- `src/core/src/hotplug_monitor.cpp` — Linux/Win32/macOS hot-plug implementation (already-shipped patterns)
- `src/core/src/hid_transport.cpp` — hidapi wrapper, non-blocking mode handling
- `src/plugins/src/manifest_signer.cpp:102-153` — current WR-01 parser
- `src/plugins/src/out_of_process_plugin_host_win32.cpp:440-470` — current CR-01 `_putenv_s` site
- `src/app/src/branding_service.hpp:60-71, 166-171` — canonical QML_SINGLETON static_assert pattern
- `src/app/src/application.cpp:180-195` — hot-plug callback marshalling

**External (HIGH-MEDIUM confidence):**

- [CreateProcessW — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) — verified: `lpEnvironment` block layout, `\0\0` Unicode block terminator, CREATE_UNICODE_ENVIRONMENT flag, sort order, `=`-prefixed entries
- [Environment Variables — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/procthread/environment-variables) — sort order, `GetEnvironmentStringsW` / `FreeEnvironmentStringsW`
- [Changing Environment Variables — Microsoft Learn](https://learn.microsoft.com/en-us/windows/win32/procthread/changing-environment-variables) — parent-can-mutate-child-env-only-at-spawn
- [libusb/hidapi — hidapi.h](https://github.com/libusb/hidapi/blob/master/hidapi/hidapi.h) — Report ID byte-0 convention, `hid_set_nonblocking` per-handle scope
- [Singletons in QML — Qt 6 docs](https://doc.qt.io/qt-6/qml-singleton.html) — `SingletonConstructionMode::Constructor` vs `Factory` SFINAE behaviour
- [QQmlEngine — qmlRegisterSingletonInstance](https://doc.qt.io/qt-6/qqmlengine.html) — instance registration

**User-memory references (cited but not re-read; HIGH confidence by reputation):**

- `reference_qt_qml_gotchas.md` — QML_SINGLETON dual-instance pattern, MultiEffect mask Item type requirements, `Qt6::WebChannelQuick` naming
- `feedback_no_system_mutations.md` — no writes to `~/.config/<other-app>/` (constrains the trust-roots / settings-persistence pitfalls)

**Confidence summary:**

- Repo-anchored claims (Pitfalls 1, 2, 4, 5, 6, 7, 9, 14, 15, 16): HIGH (verified against current source at named line ranges).
- Win32 / hidapi behavioural claims (Pitfalls 5, 9, 11): HIGH (verified against Microsoft Learn and libusb/hidapi authoritative sources at research time).
- Future-facing predictions (Pitfalls 3, 10, 12, 13): MEDIUM (extrapolation from current code + general domain knowledge; no direct repo evidence yet because the code doesn't exist).
- TOCTOU framing (Pitfall 8): MEDIUM (analysis of current code path; depends on threat model the project hasn't formally documented).
