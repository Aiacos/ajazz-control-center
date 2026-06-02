# Codebase Concerns

**Analysis Date:** 2026-06-02

## Tech Debt

### QML Test Target Link Gaps

**Area:** QML smoke test executable `ajazz_qml_tests`

**Issue:** The `tests/qml/CMakeLists.txt` (line 186–190) historically omitted `plugin_device_bridge.cpp` from the target sources, causing undefined-reference link errors for all `PluginDeviceBridge::on*` virtual method slots. The gap was **fixed in Phase 26 Plan 26-05** via commit references in the file (see `tests/qml/CMakeLists.txt:186–190`), but the comment documents it as a "pre-existing latent issue."

**Files:** `tests/qml/CMakeLists.txt`

**Impact:** If regression occurs (e.g., a CMake refactor omits the `.cpp`), the test fails to link, blocking the entire CI-gate QML smoke test. The failure is a link-time error, not a runtime issue, so it surfaces during build.

**Prevention:** The CMake comments explicitly call out the requirement; a code review catching "remove a .cpp source" from this target is the primary guard. No automated check exists (the build will catch it).

______________________________________________________________________

### Streamdeck Report-ID Framing for Linux/hidraw

**Area:** Streamdeck (AKP03 / AKP05 / AKP153 / AKP815) image upload on Linux

**Issue:** The AKP-series devices send commands with the ASCII `CRT` prefix at **byte 0** (no HID Report-ID prepended). This works on **Windows WriteFile** (tolerates missing report-id) but **Linux hidraw is strict**: it interprets `buffer[0]` as the Report ID, so the panel receives misaligned packets and renders nothing. The mouse/keyboard backends **already carry** their report-id bytes, so a blanket transport fix would double-prefix them and break those devices.

**Files:**

- `src/devices/streamdeck/src/akp03.cpp` / `akp05.cpp` / `akp153.cpp` / `akp815.cpp` (output builders)
- `src/core/src/hid_transport.cpp` (transport layer)

**Hardware Status:** ✅ **FIXED in commit `cc04a54` + `037bd8d`** via `prependReportIdPosix` flag set by streamdeck constructors; `HidTransport::write()` prepends `0x00` on Linux/macOS only. Verified live on Fedora 2026-05-22 with the AKP05E (`0x0300:0x3004`). If a future refactor removes the flag or the conditional, the Linux path regresses silently (devices detected but images fail to render).

**Impact:** Linux-only platform. On Windows the app renders keys correctly; on Linux the same build shows blank panels. High user-visible impact if regression occurs.

______________________________________________________________________

### Mouse Battery & Clock Feature Report Collection Selection on Linux/hidraw

**Area:** AJ-series mouse battery status + OLED clock on Linux

**Issue:** The mouse exposes **two** `0xFFFF` vendor-usage collections (usage 2 = control, usage 1 = other). On **Windows hidapi**, `hid_enumerate` reliably populates `usage` so the app selects the correct control collection. On **Linux hidraw**, `hid_enumerate` may return `usage=0` (unpopulated) for non-primary collections, causing the transport to **fall back to the first interface** (the boot mouse) where battery/clock feature reports do not exist → silent no-op.

**Files:** `src/core/src/hid_transport.cpp` (line ~200, the interface-selection match loop)

**Hardware Status:** ✅ **FIXED in commit `db21686`** via a two-pass match strategy: (1) try `usage_page` + `usage`, (2) if no match and no enumerated entry reported non-zero usage, fall back to `usage_page`-only match. Verified on Windows (preserves existing behavior); Linux implementation compiles under GCC/Clang. Pending Fedora hardware confirmation.

**Impact:** Linux-only; mouse battery reads as ∅ instead of a percentage. Users see "battery unavailable" instead of "100%". Affects only multi-interface devices (AJ-series mice, AK980 PRO keyboard). AK980's control collection `0xFF13` is a single collection (no usage disambiguation needed) so that device is unaffected.

______________________________________________________________________

## Known Bugs & Test Failures

### Plugin Manager Crash-Disable Test Gaps (Pre-existing)

**Area:** Plugin lifecycle and crash tracking

**Files:**

- `tests/unit/test_plugin_lifecycle.cpp` (~806/833 lines)
- `tests/unit/test_plugin_concurrency.cpp` (line ~75 onward)
- `src/app/src/plugin_crash_tracker.cpp` / `.hpp`
- `src/app/src/plugin_manager.cpp` (crash-disable logic)

**Issue:** The unit tests exercise the 3-in-30s crash-disable threshold and the per-plugin isolation (one crash disables only itself, siblings survive). However, the tests use **injected fake clocks** and **fake NodeProbe** (no real process spawning), so they do not reproduce actual process termination signals or the interaction between real `QProcess` state machines and the crash counter.

**Status:** Tests pass (713+ test cases in the suite), but **two pre-existing ASan dev-build failures** in these tests indicate potential heap corruption or use-after-free in the crash-tracker or PluginManager state:

- `test_plugin_lifecycle.cpp` line ~806: reported SIGSEGV under ASan (details not catalogued in code)
- `test_plugin_concurrency.cpp`: crash-disable `disabledSpy` count assertion (SIGTERM on a specific test leg)

**Impact:** The failures are reproducible only under ASan instrumentation with specific compiler flags (not reproducible in release/CI). They suggest a heap issue that does not manifest in production (or happens rarely). The risk is a spontaneous crash if a real plugin spawns, crashes 3 times in 30 seconds, and a race condition in the state cleanup fires.

**Prevention:** When the next developer touches the crash-tracker code (e.g., adding a new metric or refactoring the 3-in-30s window), rebuild with ASan (`-fsanitize=address -g`) and re-run the two failing tests to confirm they still pass. If they fail, prioritize fixing the heap issue before landing the change.

______________________________________________________________________

### Plugin Protocol Wire-Shape Divergence from Elgato

**Area:** Stream Deck plugin protocol wire format

**Issue:** Elgato's official SDK (`$SD` namespace) places plugin action metadata at the **top level** of JSON events:

```json
{
  "event": "keyDown",
  "context": "ctx-1",
  "action": "com.example.action",
  "device": "device-id",
  "payload": { ... }
}
```

Our implementation (verified in `src/app/src/sd_plugin_server.cpp` and `src/app/src/plugin_device_bridge.cpp`) **nests** `context`, `action`, and `device` **under** `payload`:

```json
{
  "event": "keyDown",
  "payload": {
    "context": "ctx-1",
    "action": "com.example.action",
    "device": "device-id",
    ...
  }
}
```

**Files:**

- `src/app/src/sd_plugin_server.cpp` (lines ~330–400, event routing + serialization)
- `src/app/src/plugin_device_bridge.cpp` (lines ~1–100, bridge message construction)

**Status:** ✅ **WORKS in practice** — vendor-supplied plugins (System Monitor, Weather) operate correctly with the nested shape because they are (a) tolerant of extra fields and (b) do not hard-require the nested-under-payload structure in their logic. Our implementation passes unit tests + UAT. However, a **strict Elgato SDK-compliant plugin** that asserts the top-level field positions would fail.

**Impact:** Medium. If a vendor publishes a plugin that parses the `context` field position strictly (e.g., `payload.context` exists but the plugin code looks for a top-level `context`), it will silently fail to find the field and behave incorrectly. No runtime error (JSON is tolerant); just silent no-op.

**Fix approach:** The wire protocol is foundational and will require bumping a version or providing a compat layer. Before refactoring, check the vendor SDK spec (`$SD` event schema) and any OpenDeck / opendeck-plugin references to confirm whether the top-level field convention is universal or if our nesting is acceptable. This is a **Phase 27+** item pending deep vendor protocol review.

______________________________________________________________________

## Security Posture

### WebSocket & Plugin Server Loopback Binding

**Area:** Plugin WebSocket server (`SdPluginServer`)

**Files:** `src/app/src/sd_plugin_server.cpp`, `src/app/src/sd_plugin_server.hpp`

**Status:** ✅ **SECURITY-CRITICAL INVARIANT ENFORCED**

The server is **bound to loopback only** (127.0.0.1 / ::1) by design. No TLS is configured because the binding is loopback + per-user isolation. Code comments at `sd_plugin_server.cpp:88` explicitly state `// SECURITY-CRITICAL invariant: loopback-only binding.` The bind address is hardcoded; no configuration option exists to expose it publicly.

**Verification:** `sd_plugin_server.cpp` lines 145–150 bind to `QHostAddress::LocalHost` only; the returned address is logged for inspection/assertion.

**Risk:** If a future PR introduces a configuration option to customize the bind address or removes the hardcoded loopback guard, the server becomes network-exposed and plugins can be remotely controlled. High-impact security issue. Guard with a code-review checklist: "WebSocket server bind address must remain `QHostAddress::LocalHost`".

______________________________________________________________________

### Plugin Zip-Slip Protection

**Area:** `.sdPlugin` archive extraction

**Files:** `src/app/src/sdplugin_extractor.cpp` (lines 52–71, 61–64)

**Status:** ✅ **IMPLEMENTED & HARDENED**

The extractor rejects entries that escape the staging directory by checking:

1. Entries starting with `/` (absolute paths)
1. Entries containing `:/` or `:\\` (drive prefixes)
1. Normalised destination path must start with the staging root (canonicalised, trailing `/` enforced)

Symlinks are explicitly skipped (line 89–91) because vendor `.sdPlugin` payloads are flat trees with no symlinks.

**Verification:** See `sdplugin_extractor.cpp:52–71` — the guard is the first operation in the extraction loop; any violation logs a warning and aborts the extraction (line 65–70).

**Risk:** If a future refactor removes the path-normalization check or simplifies the guard to a string-prefix match (e.g., `outPath.startsWith(tmpPath)`), the guard becomes vulnerable to traversal attacks. The canonicalised path comparison is load-bearing.

______________________________________________________________________

### Plugin Sandbox Isolation (Multi-Platform)

**Area:** Out-of-process plugin host sandboxing

**Files:**

- Linux: `src/plugins/src/linux_bwrap_sandbox.cpp` (Bubblewrap)
- macOS: `src/plugins/src/macos_sandbox_exec_sandbox.cpp` (`exec-sandbox`)
- Windows: `src/plugins/src/windows_app_container_sandbox.cpp` (AppContainer)

**Status:** ✅ **IMPLEMENTED; GAPS IN VERIFICATION**

Sandboxing is applied uniformly across platforms:

- **Linux:** Bubblewrap with read-only plugin root + RW temp/cache directories.
- **macOS:** `exec_sandbox` with `allow.read` / `allow.write` rules.
- **Windows:** AppContainer with low-privilege token + filtered file/network ACLs.

Code comments assert the implementation; commit history (Phase 13/14/15) documents the design.

**Gaps (from TODO.md line 105):**

- No end-to-end integration tests that spawn a real child process under each sandbox and assert that escapes fail (e.g., `../../../.bashrc` is unreadable, network sockets are blocked).
- Unit test isolation is strong (fixtures use temp dirs) but does not verify the actual OS-level constraint.

**Files:** `tests/unit/test_linux_bwrap_sandbox.cpp` (406 lines, strong unit coverage) vs. missing integration test suites.

**Impact:** Low-to-medium. The sandboxes are code-reviewed and compile; they do not have known bypasses. The missing integration tests mean a subtle regression (e.g., a bwrap flag typo) could slip through without being caught. Each platform's CI covers the build but not runtime isolation enforcement.

**Prevention:** Before any changes to sandbox rules or capabilities, add a TODO to the PR: "Add an end-to-end sandbox escape test on this platform."

______________________________________________________________________

### COD-031: nlohmann::json Boundary Enforcement

**Area:** Core library (ajazz_core) public headers

**Status:** ✅ **BOUNDARY ENFORCED**

The project has a hard rule: no `nlohmann::json` in `src/core/include/` or any installed public header (the "COD-031 boundary"). The library uses only Qt's `QJsonDocument` at the app tier, and plugins receive a hand-rolled mini JSON parser (`src/plugins/src/wire_protocol.hpp`) to avoid binary-compat nightmares.

**Files Marking the Boundary:**

- `src/core/include/ajazz/core/*.hpp` — use Qt only, never nlohmann
- `src/app/src/*.hpp` — QJson only in public-facing types
- `src/plugins/src/wire_protocol.hpp` — mini parser, intentional scope limitation

**Verification:** Commit history contains a one-time audit grep (`grep -rn nlohmann src/core/include/` must return 0). Re-run this grep on every major refactor.

**Impact:** Release-blocker if violated. The boundary prevents ABI churn (different nlohmann versions ship incompatible symbols) and keeps the core library light.

______________________________________________________________________

## Performance Bottlenecks

### Large Translation Units & Complex Interdependencies

**Area:** Component complexity

**Largest files (performance-relevant):**

- `src/core/include/ajazz/core/capabilities.hpp` — 1600 lines of enum + trait declarations. **Concern:** header-only; every `.cpp` that includes it recompiles the enum every time the header changes. No immediate bottleneck (enums are not code-heavy), but if traits or static helpers are added, consider extracting to a `.cpp` TU.
- `src/app/src/plugin_catalog_model.cpp` — 1530 lines. Handles online/offline catalog synthesis, HTTPS fetch + parse, and model updates. **Concern:** monolithic; logic could be factored into a fetcher service (already done: `StreamdockCatalogFetcher`) and a separate model class, but not urgent.
- `src/devices/keyboard/src/proprietary_keyboard.cpp` — 1183 lines. Time-sync, macro upload, per-LED RGB. **Concern:** no immediate bottleneck (these are initialization-time operations, not hot paths).

**Status:** No evidence of performance regressions. Tests run in \<5 minutes on CI. The app is responsive on modest hardware (older Fedora / Windows / macOS machines used during testing).

**Impact:** Low. If a future feature adds heavy computation (e.g., real-time image processing for animated display updates), revisit these boundaries.

______________________________________________________________________

### AKP05E Strip-Zone Rendering (128px Zones, Provisional Geometry)

**Area:** Stream Dock Plus touch strip 4-zone rendering

**Files:** `src/app/src/stream_dock_control_service.hpp` (lines 230–291), `stream_dock_control_service.cpp` (lines 367–402)

**Issue:** The 4 touch-strip zones are rendered via **BAT wire 1..4** at a fixed **128×128 pixel size** with `Rot180` orientation. The zone geometry is **PROVISIONAL** (akp05.md §5, confirmed live on the demo unit `0x0300:0x3004` but not verified on a retail SKU):

- Zone mapping: `X*4/640` (width 640 px → 4 zones of ~160px logical).
- Physical placement: 200px per zone (200×100 viewport), with gaps between zones.
- Rotation: Rot180 (panel mounted inverted).

**Status:** ✅ **Hardware-confirmed on the demo unit (2026-05-31)**. The zones render correctly with the 128px size and Rot180 rotation. However, the demo unit (`0x3004`) has **input unreachable** — a retail AKP05E / Mirabox N4 may have different firmware geometry or a different LCD size.

**Files:** `src/app/src/stream_dock_control_service.cpp` (lines 386–402, zone rendering), `akp05.md` (§5, documented as PROVISIONAL)

**Impact:** Medium. If a retail unit ships with 112px zones or a different rotation, the touch-strip rendering will be cosmetically wrong (zones misaligned) or off by a fixed pixel offset. The app continues to function (the zones are addressable), but visual alignment is lost. Unit tests pass because they use a fake device; only hardware verification catches this.

**Hardware-Gated Verification Debt:** The app needs a real retail AKP05E or Mirabox N4 to confirm the provisional geometry matches. Until then, the zones work but remain "cosmetically unverified."

______________________________________________________________________

## Fragile & Provisional Areas

### AKP05E Input Unreachable on Demo Unit

**Area:** Stream Dock Plus input events (key press, encoder rotation, touch)

**Status:** ✗ **NOT REACHABLE on demo unit `0x0300:0x3004`**

**Files:** `docs/protocols/streamdeck/akp05_input_corrections.md` (§7.1, proof chain), `src/app/src/stream_dock_input_service.cpp` (input parsing), `scripts/akp05_input_probe.py` (diagnostic tool)

**Evidence:**

- Tested with 5 independent methods: raw hidraw read, GET_REPORT polling, evdev sysfs, raw usbmon, and the reference `4ndv/mirajazz` library.
- All five captured **zero input on physical key press**.
- Kernel correctly arms the input endpoint EP `0x82` (usbmon confirms the arm request).
- The device declines to fill the endpoint — firmware-level issue, not a driver/app bug.

**Root Cause (High Confidence):** The `0x3004` is a **demo / development firmware** with the input path disabled or stubbed. It is a white-label unit ("HOTSPOTEKUSB HID DEMO") — possibly an engineering sample that was never meant for production.

**Impact:** The app detects the device and renders output correctly (keys + strip render fine), but user key presses / encoder rotations / touch events never fire. Functionality is ~50% (display-only). The vendor actions (setImage, setTitle, setState, showAlert) work correctly when driven by **synthetic input injection** (via the debug-control channel, commit `ac24a33`), so the wiring is sound.

**Hardware-Gated Verification Debt:** A **retail AKP05E** or **Mirabox N4** unit is needed to verify that input works on production firmware. This is **not a code bug** — it is a firmware limitation of the demo unit.

**Workaround for Testing:** The debug-control channel exposes `input.key`, `input.encoder`, `input.touch` RPC methods that inject synthetic events directly into the full pipeline (profile + plugins). Tests use this for end-to-end validation without needing real input hardware.

______________________________________________________________________

### Encoder Index & Polarity (PROVISIONAL on Demo Unit)

**Area:** AKP05E encoder decoding

**Files:** `src/app/src/stream_dock_input_service.cpp` (lines 172–185, zone-to-encoder mapping), `akp05_input_corrections.md` (§3–4, encoder structure)

**Status:** ✅ **DECODED but PROVISIONAL** (commit `7eb5501` + `89c0db6` refactored the wire format; encoder index/polarity are correctly parsed but not validated on real hardware pressing).

**Known Gap:** The encoder **index and polarity mapping** are based on RE of the vendor DLL (`SDLibrary1.dll`) and not cross-checked against a **real retail unit**. The demo unit has input unreachable, so live encoder rotation cannot be verified.

**Provisional Fields:**

- Encoder index derivation: `zone = (frame[9] & 0xF0) >> 4` (assumed zones 0..3 = encoders 0..3).
- Polarity: `dir = (frame[9] & 0x0F) > 0x07 ? -1 : +1` (assumed 0x01–0x07 = CW, 0x08–0x0F = CCW).
- Touch-zone derivation: `zone = X * 4 / 640` (provisional formula, confirmed pixel-wise on the display but not on touch input).

**Impact:** Low. If encoder rotation feels "backwards" on a retail unit, the polarity bit is wrong; a simple negate fixes it. If zones are offset by 1, the index derivation is off by one. Both are trivial fixes. No safety/security issue.

**Prevention:** When a retail unit arrives, run `scripts/akp05_input_probe.py` + the app with synthetic injections to confirm the index/polarity/zone values match real button presses.

______________________________________________________________________

### AK980 RGB Path Divergence (0x0A vs 0x20/0x04)

**Area:** AK980 keyboard per-key RGB

**Files:** `src/devices/keyboard/src/proprietary_protocol.hpp` (lines 160–167, opcode definitions)

**Issue:** Ghidra audit of the vendor SDK (`SDLibrary1.dll`) identified **two RGB buffer paths**:

1. **Legacy 0x0A** (`setRgbBuffer`) — has an off-by-two bug in the vendor code itself; unused in current firmware.
1. **Current 0x20/0x04** (`buildPerKeyRgbWriteHeader`) — correct implementation per Ghidra struct alignment.

Our code implements **only 0x20/0x04** (line 167: `kPerKeyRgbSub = 0x04`), which is correct for current firmware. The legacy 0x0A opcode is **never sent**.

**Status:** ✅ **CORRECT PATH SHIPPED**. The 0x0A legacy path was identified as broken vendor code and intentionally not replicated. The 0x20/0x04 path is verified by unit tests + hardware round-trip (commit `d70503d` notes the unification).

**Concern:** The 0x0A constant exists in the enum (`src/devices/keyboard/src/aj_series_protocol.hpp:59, GetBattery = 0x83`) for **cataloguing purposes** (RE reconciliation). If a future contributor misreads the enum and assumes 0x0A is an alternative RGB path to try, they might implement it and regress the keyboard's RGB output.

**Prevention:** Comments in the enum and in the RGB builder are sufficient. Code review should catch any attempt to send 0x0A RGB commands.

______________________________________________________________________

### Mouse Battery 0x83 vs 0xF7 RE Reconciliation

**Area:** AJ-series mouse battery status polling

**Status:** ✅ **RESOLVED to 0xF7 (commit `b81fd35`, hardware-confirmed 2026-05-22)**

**Historical:** The RE initially catalogued a `0x83` opcode as the battery query poke (`GetBattery = 0x83`). Live hardware testing revealed the **actual poke is 0xF7** (status-poll command), after which a GET_FEATURE read on report `0x00` (frame `[00, 00, charge, 01 01 01 02]`) returns the charge at byte 2.

**Current State:** The `0xF7` poke + GET_FEATURE flow is implemented and confirmed working on both Windows and Linux (commit `9019682` on feat/linux-device-support, hardware-verified 2026-05-22 Fedora). The old `buildGetBattery(0x83)` builder was **removed** in commit `b81fd35` (2026-05-22).

**Enum Artifact:** `FeaCmd::GetBattery = 0x83` remains in `src/devices/mouse/src/aj_series_protocol.hpp:59` for cataloguing (the opcode is still a valid vendor wire constant, just not the one we use for battery). The enum comment should note "0x83 is a catalogued opcode; the live battery path uses 0xF7 status-poll + GET_FEATURE".

**Impact:** None in production. The 0x83 constant is unreachable dead code (no call site). If a future contributor sees the enum and tries to use it, the implementation will not work (the device ignores 0x83 for battery) — but the failure is silent (no battery appears), not a crash. A code-review comment ("use the 0xF7 path, not 0x83") is sufficient.

______________________________________________________________________

## Test Coverage Gaps

### Device Render Features (setImage, setState, setTitle, showAlert)

**Area:** Plugin device bridge

**Files:**

- `src/app/src/plugin_device_bridge.cpp` / `.hpp` (1146 lines)
- `tests/unit/test_plugin_device_bridge.cpp` (1670 lines)
- `src/app/src/stream_dock_control_service.cpp` (output path)

**Status:** ✅ **UNIT TESTS PASS; PIXEL RENDER UNVERIFIED**

The unit tests exercise the **e2e wiring**:

- `setImage` routes to `StreamDockDevice::setKeyImage` (1-based index)
- `setState` updates the stored state index
- `setTitle` / `showAlert` write to transient overlay buffers

Tests use a **fake device fixture** (`FakeStreamDockDevice`) that records method calls but does not render. The tests verify:

1. Correct routing (the right method called on the device backend)
1. Symmetry (setState/getState round-trip)
1. No crashes (visual family no-crash test at line ~21)

**Hardware-Gated Gap:** The tests do **not** verify that a **pixel-accurate image** appears on the physical device. The demo unit `0x3004` has limited input and no automated way to photograph the LCD, so visual verification is manual (take a photo, compare against expected).

**Impact:** Medium. The wiring is sound (unit tests + manual UAT confirm it), but pixel-level bugs (JPEG re-encoding artifacts, color space misalignment, key position off-by-one) require human visual inspection. The risk is that a future refactor changes the image pipeline (size, rotation, format) and breaks the render without unit tests catching it.

**Prevention:** Before any image-transform changes, run a unit test that encodes a known test pattern (e.g., a red square at (0,0)) and visually verify on the device that it renders at the correct location and color. Add a capture to the relevant test (e.g., `tests/unit/test_stream_dock_control_service.cpp` line 145).

______________________________________________________________________

### QML Geometry & Drag-Drop End-to-End

**Area:** DeviceView layout + plugin drag-drop wiring

**Files:**

- `tests/qml/CMakeLists.txt` (lines 18–24, Phase 26 Plan 26-05 adds QML tests)
- `tests/qml/test_device_view_geometry.qml` (Phase 26 Plan 26-05 — not yet merged)
- `tests/qml/test_device_view_drag_drop.qml` (Phase 26 Plan 26-05 — not yet merged)

**Status:** 🔄 **IN PROGRESS** (Phase 26 Plan 26-05 requirement REQ-26-B)

Unit tests for geometry + drag-drop were added to the QML smoke target but the `.qml` test files themselves may not be fully wired. The CMakeLists references them at lines 121–124 as a **future addition**.

**Known Gap:** The QML unit tests compile and link, but the .qml spec files may not exist or may be stub implementations. If they are placeholders, the test is a smoke-gate (no content), not a real verification.

**Impact:** Low-to-medium. The drag-drop wiring itself is code-reviewed (C++ + QML), so if the QML tests are incomplete, the feature still works. However, regressions (e.g., a QML delegate property renamed) could slip through without a QML-level test catching them.

**Prevention:** Confirm that `test_device_view_geometry.qml` and `test_device_view_drag_drop.qml` are **non-stub implementations** (not placeholders). If they are stubs, file a Phase 26+ sub-item to complete them.

______________________________________________________________________

## Scaling Limits

### Plugin Catalog Model Monolithic Fetch & Parse

**Area:** Online plugin catalog (`StreamdockCatalogFetcher` + `PluginCatalogModel`)

**Files:**

- `src/app/src/streamdock_catalog_fetcher.cpp` (659 lines)
- `src/app/src/plugin_catalog_model.cpp` (1530 lines)

**Issue:** The catalog fetcher downloads a **single JSON document** (typically ~150 KB for 200–300 plugins) over HTTPS and parses it in-memory. If the catalog grows to **1000+ plugins** or the JSON swells to **>5 MB**, parsing becomes observable (users see a brief stall during the fetch).

**Current State:** The live AJAZZ catalog is ~200 plugins. The fetch is wrapped in a `QNetworkReply` with **no timeout** (it relies on the system TCP timeout, ~30–60 sec on most OSes). If the server is slow or the network is spotty, the app blocks.

**Potential Improvements:**

1. **Streaming JSON parser** — parse the array incrementally instead of loading the whole file.
1. **Timeout + cancel** — add a user-facing "cancel fetch" button + a 15-sec timeout.
1. **Pagination** — fetch 50 plugins per page so users see results faster.

**Impact:** Low. The current catalog is manageable. If the AJAZZ store expands to 1000+ plugins, revisit. Unlikely to be a bottleneck on modern hardware.

______________________________________________________________________

### Device Hotplug Debouncer (16 ms Coalescer)

**Area:** Hotplug detection & device enumeration

**Files:** `src/app/src/hotplug_debouncer.cpp`, `src/core/src/hotplug_monitor.cpp`

**Issue:** Hotplug events are coalesced over a **16 ms window** (commit history notes this is the USB HUB propagation latency). If a user plugs in 10 devices in rapid succession, the app may re-enumerate only once (bundling all 10) rather than once per device.

**Current State:** The debouncer is intentional (prevents UI flicker + reduces enumeration RPCs). On modern Linux (systemd ≥258), the `uaccess` ACL can be **lost during a rapid replug storm** (Debian #1112660), requiring a manual `setfacl` to recover. The app cannot fix this at runtime (it's a systemd issue), but documenting the workaround is important.

**Impact:** Low. Users rarely plug in 10 devices at once. If they do, a 16 ms delay is imperceptible.

______________________________________________________________________

## Missing Critical Features

### Per-LED RGB Matrix on VIA Keyboards

**Area:** Keyboards with QMK RGB Matrix (e.g., AK820)

**Status:** ⚠️ **STUB** (throws `std::runtime_error` at runtime)

**Files:** `src/devices/keyboard/src/via_keyboard.cpp:185`

**Issue:** The VIA protocol supports two RGB modes:

1. **qmk_rgblight** (channel ID varies by firmware; we hardcode `0x01`) — simpler, fewer LEDs, entire-strip control. ✅ Implemented.
1. **qmk_rgb_matrix** (channel ID varies by firmware, often `0x03`) — per-LED control, full matrix support. ⚠️ **Not implemented** — the code throws an exception.

When a user tries to set per-LED RGB on an RGB-matrix keyboard, the app crashes with "per-LED RGB buffer: TODO (requires QMK_RGB_MATRIX path)".

**Fix Approach:**

1. At device-open time, probe VIA supported channels to detect which RGB mode this firmware supports.
1. Set a `KeyboardCapabilities::hasRgbMatrix` flag.
1. Implement the 0x03 write path (per-LED payload format needs a hardware round-trip to confirm).

**Impact:** Medium. Users with high-end mechanical keyboards (AK820 + RGB) cannot control per-key RGB — only global brightness/effect. The app handles it gracefully (exception + graceful degradation), not a crash.

**Estimated effort:** 1–2 days (probe + wire-format confirmation).

______________________________________________________________________

### macOS & Windows Autostart Service

**Area:** Launch at login

**Status:** ⚠️ **STUB** (not implemented)

**Files:** `src/app/src/autostart_service.cpp:163` (error stub on non-Linux)

**Issue:** Linux ships autostart via XDG `.desktop` files. macOS and Windows have no implementation — users must manually enable "Launch at login" in the system settings.

**Implementation Plan (from TODO.md):**

- **macOS:** Write a LaunchAgent plist + `launchctl load -w`.
- **Windows:** Write registry `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

**Impact:** Low. Convenience feature only. Desktop apps work without it.

**Estimated effort:** 0.5 day per platform.

______________________________________________________________________

### Real MacroRecorder Implementation

**Area:** Keystroke + mouse event capture for macro recording

**Status:** ⚠️ **STUB** (returns `StubRecorder` on all platforms)

**Files:** `src/core/src/macro_recorder.cpp:10–12` (TODO tags), `src/core/include/ajazz/core/macro_recorder.hpp:14–15`

**Issue:** The macro recorder is a stub — calling `start()` / `stop()` just logs "stub" messages. No real keystroke capture happens. The UI workflow exists (users can click "Record", but no events are captured).

**Implementation Plan (from TODO.md):**

- **Linux:** evdev reader thread on `/dev/input/eventN` (requires `input` group membership or CAP_DAC_READ_SEARCH).
- **macOS:** `CGEventTap` with Accessibility + "Input Monitoring" permissions.
- **Windows:** `SetWindowsHookExW(WH_KEYBOARD_LL, ...)` low-level hook.

**Impact:** Medium. The UI shows macro-record affordances, but the feature does not work. Users who try it get stuck (nothing happens when they press keys).

**Estimated effort:** 1 day per platform + 0.25 days for CMake option wiring.

______________________________________________________________________

## Architectural Concerns

### Sidecar Streamdock Device Persistent Handle Stability

**Area:** Mirajazz Rust sidecar + AKP05 / AKP03 / AKP153 / AKP815 backends

**Files:**

- `src/app/src/sidecar_stream_dock_device.cpp` (432 lines, persistent handle holder)
- `streamdock-host/` (Rust sidecar, git submodule at vendored mirajazz)
- `src/app/src/sidecar_stream_dock_device.cpp:294` (TODO: keep_alive / CRT CONNECT)

**Status:** ✅ **HARDWARE-CONFIRMED NO WEDGE** (commit `5722ead` + `decc85c`)

The old C++ wire backend (`akp05.cpp` etc., removed in Slice D) had a **critical bug**: every interaction open/close/reopen the device, which **wedged the AKP05E display via a `DIS,STP,DIS` churn** (confirmed hardware 2026-05-31). The sidecar holds a **persistent handle for the session** (one `CRT DIS` at init, `CRT CONNECT` keep-alive every 5 sec), which **eliminates the wedge**.

**Known Gap (TODO at line 294):** The keep-alive `CRT CONNECT` command was backported from mirajazz. On very long-idle sessions (e.g., user walks away for hours), the device *might* time out. The keep-alive interval (5 sec) should prevent this, but has not been tested on a device that implements a strict timeout. Current assumption: no timeout (device stays alive indefinitely on keep-alive).

**Impact:** Low. The keep-alive is already implemented (not a future TODO); the gap is just test coverage on a device with a strict idle timeout.

**Prevention:** When the next user plugs in a device for a week-long soak test, monitor the app logs for any "device disconnected" messages. If none appear, the assumption holds.

______________________________________________________________________

### Plugin Host Crash-Disable State Machine (WR-02 Guard)

**Area:** Plugin lifecycle + crash tracking

**Files:**

- `src/app/src/plugin_manager.cpp` (804 lines, crash-disable state machine)
- `src/app/src/plugin_crash_tracker.cpp` / `.hpp` (injected clock, 3-in-30s window)
- `tests/unit/test_plugin_concurrency.cpp` (guard against shared-key collision)

**Known Issue (WR-02):** The `onProcessFailed` callback is invoked whenever a plugin process terminates (either user-stop or actual crash). The state machine must:

1. **Record the crash** in `m_crashTracker[pluginId]` and increment the count.
1. **Check if disabled:** if `m_live[pluginId]` is absent (HTML plugins, no spawned process), skip the re-spawn attempt even if the count < 3.
1. **Disable on threshold:** if count ≥ 3, call `disableWithNotice` and mark as disabled in `m_disabled[pluginId]`.

**Guard Status:** ✅ **TESTED** (commit `5725cb0`, test case at `test_plugin_concurrency.cpp:75`). The guard ensures one plugin's crash does not disable a sibling.

**Fragile Area:** The state is spread across two maps (`m_live`, `m_disabled`, `m_crashTracker`). If a future refactor consolidates them incorrectly or removes the `m_live` check in `onProcessFailed`, the WR-02 invariant breaks silently (an HTML plugin crash could trigger a re-spawn attempt on non-existent process → confusing error).

**Prevention:** Keep the WR-02 guard test in the suite. Code review: any change to `onProcessFailed` must preserve the `!m_live.contains(pluginId)` check.

______________________________________________________________________

## Dependencies at Risk

### Qt 6.11+ Deprecation of `QImage::mirrored`

**Area:** Image rotation & transformations

**Status:** ⚠️ **GATED; BUILD BREAKS ON Qt 6.11+**

**Files:** Any file using `QImage::mirrored()` (likely in `stream_dock_control_service.cpp` or image-rendering code)

**Issue:** Qt 6.11 deprecated `QImage::mirrored()` in favor of `QImage::flipped()`. The old function still works but emits a deprecation warning. Under `-Werror`, this becomes a hard error on Qt 6.11+.

**Current State:** The codebase targets Qt 6.7 (CI uses 6.11.1 on macOS per the wiki). If `-Werror` is enabled (it is, per CLAUDE.md), deprecation warnings become errors, and a Qt 6.11+ build breaks.

**Fix:** Version-guard any `mirrored()` calls:

```cpp
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    image.flipped(QImage::Vertical | QImage::Horizontal)
#else
    image.mirrored(true, true)
#endif
```

**Impact:** Medium. If the user upgrades to Qt 6.11.1 and rebuilds, the build fails with a deprecation error. The fix is trivial once identified.

**Prevention:** CI should build on Qt 6.11+ (currently on 6.11.1 for macOS) so the deprecation is caught early.

______________________________________________________________________

### Mirajazz Sidecar Dependency (Rust Crate)

**Area:** Streamdeck backends (AKP03 / AKP05 / AKP153)

**Status:** ✅ **CLEAN DEPENDENCY; READ-ONLY**

**Files:** `streamdock-host/` (git submodule @ pristine `mirajazz`)

**Constraint (from CLAUDE.md):** "Do NOT modify the mirajazz crate itself — it is a pristine git dependency."

**Rationale:** The mirajazz crate is an external OSS library (`4ndv/mirajazz` on GitHub). Our sidecar (`streamdock-host/src/main.rs`) wraps it with JSON-over-stdio glue code. If we patch mirajazz in-tree, we fork the library and diverge from upstream bug fixes.

**Impact:** Low. Mirajazz is stable; no known bugs. If a future issue arises (e.g., a device-specific quirk), the fix must land in upstream mirajazz, not in our vendored copy.

**Prevention:** Any bug report against streamdock functionality should start with "Is this an mirajazz issue?" If yes, file a PR against `4ndv/mirajazz` and wait for the upstream fix, rather than patching locally.

______________________________________________________________________

## Summary Table

| Category             | Item                                    | Severity | Status                             | Files                                               |
| -------------------- | --------------------------------------- | -------- | ---------------------------------- | --------------------------------------------------- |
| **Tech Debt**        | QML test link gaps                      | Low      | ✅ Fixed Phase 26                  | `tests/qml/CMakeLists.txt`                          |
|                      | Streamdeck Linux hidraw report-id       | High     | ✅ Fixed                           | `src/core/src/hid_transport.cpp`                    |
|                      | Mouse battery interface selection Linux | High     | ✅ Fixed pending confirm           | `src/core/src/hid_transport.cpp`                    |
| **Bugs & Test Gaps** | Plugin crash-disable ASan failures      | Medium   | ⚠️ Pre-existing                    | `tests/unit/test_plugin_lifecycle.cpp`              |
|                      | Plugin wire-shape divergence Elgato     | Medium   | ✅ Works; non-compliant            | `src/app/src/sd_plugin_server.cpp`                  |
| **Security**         | WebSocket loopback binding              | Critical | ✅ Enforced                        | `src/app/src/sd_plugin_server.cpp`                  |
|                      | Zip-slip protection                     | High     | ✅ Implemented                     | `src/app/src/sdplugin_extractor.cpp`                |
|                      | Plugin sandbox isolation                | Medium   | ✅ Implemented; untested e2e       | `src/plugins/src/*_sandbox.cpp`                     |
|                      | COD-031 boundary                        | Critical | ✅ Enforced                        | `src/core/include/`                                 |
| **Performance**      | Large TU complexity                     | Low      | ✅ None observed                   | Various                                             |
|                      | AKP05 strip zone geometry PROVISIONAL   | Medium   | ✅ Hardware confirmed demo         | `src/app/src/stream_dock_control_service.cpp`       |
| **Fragile Areas**    | AKP05E input unreachable demo unit      | High     | ✅ Documented                      | `scripts/akp05_input_probe.py`                      |
|                      | Encoder polarity PROVISIONAL            | Low      | ✅ Decoded; unverified retail      | `src/app/src/stream_dock_input_service.cpp`         |
|                      | AK980 RGB path 0x0A vs 0x20             | Low      | ✅ Correct path shipped            | `src/devices/keyboard/src/proprietary_protocol.hpp` |
|                      | Mouse battery 0x83 vs 0xF7              | Low      | ✅ Resolved to 0xF7                | `src/devices/mouse/src/aj_series_protocol.hpp`      |
| **Test Coverage**    | Device render pixel verification        | Medium   | ⚠️ Unit tests pass; hardware-gated | `tests/unit/test_plugin_device_bridge.cpp`          |
|                      | QML geometry & drag-drop e2e            | Low      | 🔄 Phase 26 in-progress            | `tests/qml/`                                        |
| **Missing Features** | VIA per-LED RGB matrix                  | Medium   | ⚠️ Stub throws                     | `src/devices/keyboard/src/via_keyboard.cpp`         |
|                      | macOS/Windows autostart                 | Low      | ⚠️ Stub                            | `src/app/src/autostart_service.cpp`                 |
|                      | MacroRecorder real impl                 | Medium   | ⚠️ Stub                            | `src/core/src/macro_recorder.cpp`                   |
| **Architecture**     | Sidecar keep-alive timeout              | Low      | ✅ Implemented; untested           | `src/app/src/sidecar_stream_dock_device.cpp`        |
|                      | Plugin crash-disable WR-02 guard        | Medium   | ✅ Tested; fragile state machine   | `src/app/src/plugin_manager.cpp`                    |
| **Dependencies**     | Qt 6.11+ mirrored() deprecation         | Medium   | ⚠️ Unguarded                       | TBD                                                 |
|                      | Mirajazz sidecar fork risk              | Low      | ✅ Clean read-only dependency      | `streamdock-host/`                                  |

______________________________________________________________________

*Concerns audit: 2026-06-02*
