---
phase: 14-stream-dock-control-service
reviewed: 2026-05-24T00:00:00Z
depth: standard
files_reviewed: 9
files_reviewed_list:
  - src/app/src/stream_dock_control_service.hpp
  - src/app/src/stream_dock_control_service.cpp
  - src/app/src/profile_controller.hpp
  - src/app/src/profile_controller.cpp
  - src/app/src/application.hpp
  - src/app/src/application.cpp
  - src/devices/streamdeck/src/register.cpp
  - tests/unit/test_register_akp05e_clock.cpp
  - tests/unit/test_stream_dock_control_service.cpp
findings:
  critical: 2
  warning: 5
  info: 3
  total: 10
status: issues_found
---

# Phase 14: Code Review Report

**Reviewed:** 2026-05-24
**Depth:** standard
**Files Reviewed:** 9
**Status:** issues_found

## Summary

The Phase 14 implementation correctly closes the UAT gap identified for the AKP05E Stream Dock
panel (DISPLAY-06/07/08, DOCK-01/02). The coalescing QTimer drain, the held-open shared_ptr
flyweight, the LIG brightness-at-open, and the profileChanged->repaintFromProfile wiring are
structurally sound. The AKP05E descriptor re-filing (0x0300:0x3004, hasClock=false) is correct.

Two blockers require attention before this ships: unguarded exceptions from `IDevice::open()` and
`IDisplayCapable::setKeyImage()` can reach the Qt event loop and trigger `std::terminate()`, and
the `IDisplayCapable::setKeyImage` interface contract documents a zero-based key index while every
concrete backend enforces a 1-based index — a permanent trap for future callers.

______________________________________________________________________

## Critical Issues

### CR-01: Unhandled exception from `open()` / `setBrightness()` inside `setActiveDevice()` crashes the process

**File:** `src/app/src/stream_dock_control_service.cpp:79-92`

**Issue:** `IDevice::open()` is annotated `@throws std::runtime_error` in `device.hpp:177`.
`IDisplayCapable::setBrightness()` writes to the HID transport and can throw `std::system_error`
(same transport path as `setKeyImage`). Neither call is wrapped in a try/catch.

`setActiveDevice()` is invoked from a `QTimer::singleShot` lambda fired on the GUI thread
(`application.cpp:495-499`). An uncaught exception escaping a Qt signal/timer callback causes
`std::terminate()` in Qt 6. This means a device yank between `DeviceRegistry::open` returning
the shared_ptr and the subsequent `open()` call (or a marginal LIG write failure) kills the whole
process.

**Fix:**

```cpp
void StreamDockControlService::setActiveDevice(QString const& codename) {
    if (!m_lookup) {
        AJAZZ_LOG_WARN("stream-dock-control", "setActiveDevice: DeviceLookup not set");
        return;
    }
    m_activeDevice = m_lookup(codename);
    if (!m_activeDevice) {
        AJAZZ_LOG_INFO("stream-dock-control",
                       "setActiveDevice: device '{}' not currently connected",
                       codename.toStdString());
        return;
    }
    m_activeCodename = codename;
    try {
        m_activeDevice->open();
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("stream-dock-control",
                       "setActiveDevice: open() failed for '{}': {}",
                       codename.toStdString(), e.what());
        m_activeDevice.reset();
        m_activeCodename.clear();
        return;
    }
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) {
        AJAZZ_LOG_INFO("stream-dock-control",
                       "setActiveDevice: device '{}' has no IDisplayCapable surface",
                       codename.toStdString());
        return;
    }
    try {
        disp->setBrightness(kDefaultBrightnessPercent);
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("stream-dock-control",
                       "setActiveDevice: setBrightness failed for '{}': {}",
                       codename.toStdString(), e.what());
        // Non-fatal: device is open; panel may just be dark.
    }
}
```

______________________________________________________________________

### CR-02: Unhandled exception from `setKeyImage()` in `drainPendingWrites()` crashes the process and leaves the pending-write map in an inconsistent state

**File:** `src/app/src/stream_dock_control_service.cpp:182-198`

**Issue:** `IDisplayCapable::setKeyImage()` is documented `@throws std::system_error if the underlying transport fails` (`capabilities.hpp:146`). The for-loop at line 182 makes no attempt
to catch this exception. Two consequences:

1. The exception propagates through `QTimer::timeout` into the Qt 6 event loop, which calls
   `std::terminate()` — process crash on any device yank mid-burst.

1. Even in a hypothetical "swallow at event loop" scenario: `m_pendingWrites.clear()` at line 198
   is skipped, so the map retains entries that were never sent. The single-shot drain timer will
   not re-arm (no new `assignKeyImage` call), leaving the queue permanently stuck.

**Fix:**

```cpp
void StreamDockControlService::drainPendingWrites() {
    if (!m_activeDevice) { m_pendingWrites.clear(); return; }
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) { m_pendingWrites.clear(); return; }

    for (auto const& [keyIndex, img] : m_pendingWrites) {
        QImage const rgba = img.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.isNull()) { continue; }
        auto const* bits = reinterpret_cast<std::uint8_t const*>(rgba.constBits());
        std::size_t const byteCount =
            static_cast<std::size_t>(rgba.width()) * static_cast<std::size_t>(rgba.height()) * 4u;
        try {
            disp->setKeyImage(keyIndex, {bits, byteCount},
                              static_cast<std::uint16_t>(rgba.width()),
                              static_cast<std::uint16_t>(rgba.height()));
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("stream-dock-control",
                           "drainPendingWrites: setKeyImage key {} failed: {}",
                           static_cast<int>(keyIndex), e.what());
            // Device likely yanked. Release the held handle so the next
            // hot-plug arrival triggers a clean setActiveDevice() cycle.
            m_activeDevice.reset();
            m_activeCodename.clear();
            break; // remaining keys in this burst cannot be sent
        }
    }
    m_pendingWrites.clear(); // always clear, even after partial failure
}
```

______________________________________________________________________

## Warnings

### WR-01: `IDisplayCapable::setKeyImage` interface documents "Zero-based key index" but every concrete backend enforces 1-based indices

**File:** `src/core/include/ajazz/core/capabilities.hpp:141`

**Issue:** The public `IDisplayCapable` interface doxygen for `setKeyImage` reads:
`@param keyIndex Zero-based key index within the display grid.`
The identical mismatch exists for `setKeyColor` (line 160) and `clearKey` (line 168).

In reality, every backend (`Akp05Device`, `Akp153Device`, `Akp03Device`) enforces 1-based indices:
`keyIndexInRange` checks `keyIndex >= 1U`. The service correctly passes 1-based values because the
implementation detail is documented in the `.cpp` file comment. However, the interface contract is
a permanent trap: any future caller who follows the doxygen and passes a 0-based index will
silently fail (index 0 is rejected by `keyIndexInRange`, image not painted, no error surfaced to
the caller).

**Fix:** Update capabilities.hpp to reflect the actual contract:

```cpp
/**
 * @param keyIndex 1-based key index within the display grid (1 .. keyCount).
 *                 Pass 0xFF to address all keys simultaneously (only valid for
 *                 clearKey(); rejected for setKeyImage / setKeyColor).
 */
```

Apply the same correction to `setKeyColor` and `clearKey`. Then audit `assignKeyImage()` and its
callers to ensure the 1-based contract is also documented at the public entry point.

______________________________________________________________________

### WR-02: `setProfileAccessor()` documentation claims "no-op if the accessor was already set" but the implementation unconditionally replaces it

**File:** `src/app/src/stream_dock_control_service.hpp:151` / `stream_dock_control_service.cpp:56-58`

**Issue:** The header doxygen states:

> "Set the profile accessor (called by Application after constructing the service with the
> two-arg ctor; no-op if the accessor was already set)."

The implementation is:

```cpp
void StreamDockControlService::setProfileAccessor(ProfileAccessor accessor) {
    m_profileAccessor = std::move(accessor);
}
```

There is no guard. A second call unconditionally overwrites the first accessor. This divergence
between documented behaviour and actual behaviour will mislead Phase 16 contributors who wire
a second accessor for the persistence-backed slider value and expect the first to be retained.

**Fix:** Either (a) implement the guard:

```cpp
void StreamDockControlService::setProfileAccessor(ProfileAccessor accessor) {
    if (!m_profileAccessor) {
        m_profileAccessor = std::move(accessor);
    }
}
```

or (b) remove the "no-op" claim from the doxygen and document that the accessor can be replaced.
Option (b) is likely better given Phase 16 may intentionally replace the accessor with a richer one.

______________________________________________________________________

### WR-03: `repaintFromProfile()` silently drops keys when `profileKeyIndex + 1` overflows `uint8_t`

**File:** `src/app/src/stream_dock_control_service.cpp:127`

**Issue:**

```cpp
auto const deviceKeyIndex = static_cast<std::uint8_t>(profileKeyIndex + 1);
```

`profileKeyIndex` is `std::uint16_t`. The addition `profileKeyIndex + 1` is computed as `int`
(integral promotion), so `0xFFFF + 1 = 65536`, and `static_cast<uint8_t>(65536) = 0`. Index 0
is rejected silently by `keyIndexInRange()` — the key image is never sent, with no log message.

For any profile key index >= 255 (e.g. a 256-key macropad profile misrouted to an AKP05E),
the conversion wraps silently. In practice AKP05E has 10 keys (indices 0–9), but the Profile
schema allows any `uint16_t` key index and the check is absent.

**Fix:** Add a range guard before the cast:

```cpp
for (auto const& [profileKeyIndex, binding] : prof.keys) {
    if (profileKeyIndex >= core::kMaxKeyIndex) { // or check against descriptor.keyCount
        AJAZZ_LOG_WARN("stream-dock-control",
                       "repaintFromProfile: profile key index {} exceeds uint8_t range, skipping",
                       static_cast<int>(profileKeyIndex));
        continue;
    }
    auto const deviceKeyIndex = static_cast<std::uint8_t>(profileKeyIndex + 1);
    // ...
}
```

______________________________________________________________________

### WR-04: Two constant names alias the same VID value (0x6603) in `register.cpp`, masking a research gap

**File:** `src/devices/streamdeck/src/register.cpp:45,48`

**Issue:**

```cpp
inline constexpr std::uint16_t MiraboxN3VendorNew = 0x6603;   // line 45
// ...
inline constexpr std::uint16_t MiraboxN4Vendor    = 0x6603;   // line 48
```

These two constants have identical values. The N3 family (AKP03-protocol) and the N4 family
(AKP05-protocol) share vendor ID 0x6603. Current PIDs don't collide (N3: 0x1002/0x1003, N4: 0x1007),
but the duplicate constant definition is confusing and could lead a future contributor to add an
N4 PID that actually belongs to the N3 family (or vice versa), causing a wrong-factory mis-route.
The `[opendeck-akp05]` source used to derive `MiraboxN4Vendor` and the `[opendeck-akp03]` source
that surfaced `MiraboxN3VendorNew` should be cross-referenced to confirm they intentionally share
the same silicon vendor.

**Fix:** Collapse to a single named constant with a comment explaining the shared VID:

```cpp
// VID 0x6603 is used by both Mirabox N3 (AKP03 protocol) and N4 (AKP05 protocol)
// families per [opendeck-akp03] and [opendeck-akp05]. PID distinguishes the family.
inline constexpr std::uint16_t MiraboxVendorV2 = 0x6603;
```

Then use `MiraboxVendorV2` for all N3 and N4 registrations.

______________________________________________________________________

### WR-05: `m_activeCodename` is not cleared when `setActiveDevice()` replaces a previously active device with a failed lookup

**File:** `src/app/src/stream_dock_control_service.cpp:60-93`

**Issue:** Consider this sequence:

1. `setActiveDevice("akp05e")` — succeeds; `m_activeDevice` holds a valid ptr, `m_activeCodename = "akp05e"`.
1. Device yanked.
1. `setActiveDevice("akp05e")` called again (hot-plug refresh).
1. `m_lookup("akp05e")` returns `nullptr` (device gone).
1. Line 68: `m_activeDevice = nullptr` (reset).
1. Line 69–73: early return.
1. `m_activeCodename` still holds `"akp05e"` (never cleared).

Now `firmwareVersionFor("akp05e")`:

- Line 159: `m_activeCodename == "akp05e"` — true.
- Line 160: `dev = m_activeDevice` — null.
- Line 164: returns `"unknown"` — correct but only by coincidence.

The real hazard: `m_activeCodename` is non-empty while `m_activeDevice` is null. Any future code
that guards on `m_activeCodename.isEmpty()` to detect "no active device" will incorrectly believe
a device is active.

**Fix:** Clear both fields atomically when the lookup fails:

```cpp
m_activeDevice = m_lookup(codename);
if (!m_activeDevice) {
    AJAZZ_LOG_INFO("stream-dock-control",
                   "setActiveDevice: device '{}' not currently connected", codename.toStdString());
    m_activeCodename.clear(); // keep codename in sync with device handle
    return;
}
m_activeCodename = codename;
```

______________________________________________________________________

## Info

### IN-01: Comment in `register.cpp` (line 181) says "every Stream Dock advertises Capability::Clock" but the AKP05E entry immediately below sets `hasClock = false`

**File:** `src/devices/streamdeck/src/register.cpp:181`

**Issue:** The inline comment attached to the AKP815 descriptor reads:

```
.hasClock = true, // A-03 / D-03: every Stream Dock advertises Capability::Clock.
```

This was accurate before DEVICES-11 introduced `hasClock = false` for the AKP05E. The blanket
assertion is now false and will mislead a contributor adding a new Stream Dock SKU who reads only
the nearest comment.

**Fix:** Update the comment to qualify the claim:

```cpp
.hasClock = true, // A-03 / D-03: AKP815 has a firmware RTC. Note: AKP05E is hasClock=false
                  // per DEVICES-11 / ARCH-05 — check per-SKU before assuming all Stream Docks
                  // support Clock.
```

______________________________________________________________________

### IN-02: Stale comment in `register.cpp` says AKP815 "reuses the AKP153 implementation today" but the registration already uses `makeAkp815`

**File:** `src/devices/streamdeck/src/register.cpp:166-170`

**Issue:**

```cpp
// The backend reuses the AKP153
// implementation today; the per-revision differences ... will graduate into
// a dedicated `makeAkp815` factory in a future change.
```

The registration on line 183 already uses `&makeAkp815`. The "future change" has happened; the
comment is stale.

**Fix:** Remove or update the comment:

```cpp
// AKP815 uses its own makeAkp815 factory (Akp815Device).
// Per-revision geometry differences (100×100 key image, 800×480 strip) are
// handled there; see akp815.cpp.
```

______________________________________________________________________

### IN-03: `drainPendingWrites` test coverage gap — DISPLAY-07+DOCK-02 test verifies ULEND is the last packet but does not guard against a second batch overwriting the first (last-write-wins coalescing)

**File:** `tests/unit/test_stream_dock_control_service.cpp:178-221`

**Issue:** The coalescing drain's "last-write-wins per key" guarantee (Pattern 3, DOCK-02) has no
unit test. The test sends a single key assignment and drains; there is no test case that assigns
the same key index twice before draining and verifies that only one BAT+ULEND burst is emitted
(rather than two). A regression that accidentally fires the timer between the two assignments would
send two bursts and pass the current test undetected.

**Fix:** Add a test case:

```cpp
TEST_CASE("StreamDockControlService: last-write-wins coalescing for same key (DOCK-02 Pattern 3)",
          "[stream-dock-control][DOCK-02]") {
    // ...
    svc.assignKeyImage(1, img_red);
    svc.assignKeyImage(1, img_blue); // overwrites without draining
    drainQueue();
    // Expect exactly 1 BAT header (not 2) for key 1.
    std::size_t batCount = 0;
    for (auto const& pkt : obs->writes()) {
        if (pkt.size() >= 8 && pkt[5] == 0x42 && pkt[6] == 0x41 && pkt[7] == 0x54) ++batCount;
    }
    CHECK(batCount == 1);
}
```

______________________________________________________________________

_Reviewed: 2026-05-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
