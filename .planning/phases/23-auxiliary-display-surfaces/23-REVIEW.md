---
phase: 23-auxiliary-display-surfaces
reviewed: 2026-05-27T00:00:00Z
depth: standard
files_reviewed: 2
files_reviewed_list:
  - src/app/src/stream_dock_control_service.cpp
  - src/app/src/application.cpp
findings:
  critical: 0
  warning: 2
  info: 2
  total: 4
status: issues_found
---

# Phase 23: Code Review Report

**Reviewed:** 2026-05-27
**Depth:** standard
**Files Reviewed:** 2 (`stream_dock_control_service.cpp`, `application.cpp`)
**Status:** issues_found

## Summary

Phase 23 adds three auxiliary-surface write methods (`assignMainImage`, `assignEncoderImage`, `assignTouchStripZone`), extends the `drainPendingWrites` coalescing loop via a `PendingKey` (SurfaceTag + index) discriminant, and wires `repaintEncodersFromProfile` into `Application`'s `profileChanged` signal path.

**Wire layer confirmed untouched.** `git diff 19e7c87..HEAD` touches only `src/app/src/stream_dock_control_service.cpp` and `src/app/src/application.cpp`  -  no core, streamdeck, or protocol files were modified.

**Key controls verified:**
- All three `dynamic_cast` paths (IDisplayCapable, IEncoderCapable, ITouchStripDisplayCapable) null-checked within 2 lines  -  Pitfall 1 satisfied throughout.
- Single `QTimer` drain preserved  -  `m_drainTimer` is the same instance used by all four surface tags; no parallel timer introduced.
- Exception wrapping covers every `drainPendingWrites` write call; yank path resets `m_activeDevice` and breaks the loop  -  Pitfall 4 satisfied.
- Range check pass-through confirmed: `assignEncoderImage` and `assignTouchStripZone` enqueue without pre-validating index/zone; backend receives raw value.
- PROVISIONAL geometry annotation is present in comments, header doc, and inline code; ENC path is kept reachable and not deleted.
- COD-031: no `nlohmann::json` or `<nlohmann/json.hpp>` included anywhere in the diff.

Two logic-level warnings and two info items are noted below.

---

## Warnings

### WR-01: `assignMainImage` casts to `IDisplayCapable` but enqueues under `SurfaceTag::Main`  -  drain sends via `IDisplayCapable::setMainImage`, creating a mismatch when only `IEncoderCapable`/`ITouchStripDisplayCapable` is present

**File:** `src/app/src/stream_dock_control_service.cpp:186-196`

**Issue:** `assignMainImage` does an upfront capability check (`dynamic_cast<IDisplayCapable*>`) and silently returns `nullptr` if the device lacks that interface. However, the subsequent drain (`drainPendingWrites`) re-derives `disp` independently. The pre-check and the drain check are consistent for the normal path, so this is not a crash. However, the pre-check in `assignMainImage` calls `dynamic_cast` on `m_activeDevice.get()` without first checking `m_activeDevice` for `nullptr`. If `setActiveDevice` was never called (or reset the handle on a failed `open()`), `m_activeDevice` is null and `m_activeDevice.get()` returns `nullptr`. Passing `nullptr` to `dynamic_cast<T*>` is defined behavior (returns `nullptr`), so there is no UB. But the issue is subtler: the return path on `disp == nullptr` silently swallows the case where `m_activeDevice` itself is null  -  the caller cannot distinguish "no device" from "device lacks IDisplayCapable". The `assignEncoderImage` and `assignTouchStripZone` methods have the identical pattern. This is not a crash, but the pre-check bypasses the explicit `if (!m_activeDevice)` guard present in `repaintPage` and `repaintEncodersFromProfile`, creating inconsistent defensive depth across sibling methods.

**Fix:** Add the `m_activeDevice` null guard at the top of all three assign methods, matching the style used in `repaintPage`:
```cpp
void StreamDockControlService::assignMainImage(QImage const& img) {
    if (!m_activeDevice) {
        return;
    }
    auto* disp = dynamic_cast<core::IDisplayCapable*>(m_activeDevice.get());
    if (disp == nullptr) {
        return;
    }
    m_pendingWrites[PendingKey{SurfaceTag::Main, 0}] = img;
    if (!m_drainTimer->isActive()) {
        m_drainTimer->start(0);
    }
}
```
Apply identically to `assignEncoderImage` and `assignTouchStripZone`.

---

### WR-02: `repaintEncodersFromProfile` guard checks `ITouchStripDisplayCapable` but `assignTouchStripZone` re-checks the same cast  -  if the device gained the capability between calls (hypothetical) the drain could reach `setTouchStripImage` with data enqueued without an active guard; more concretely, the double-cast wastes work and the guard capability differs from `repaintPage`'s `IDisplayCapable` guard style

**File:** `src/app/src/stream_dock_control_service.cpp:235-244`

**Issue:** `repaintEncodersFromProfile` performs a capability guard cast to `ITouchStripDisplayCapable` at line 242  -  then immediately calls `assignTouchStripZone`, which independently re-casts to `ITouchStripDisplayCapable` at line 214. The result of the first cast (`strip`) is obtained but never used; the variable is assigned, null-checked, and discarded. The sole purpose of that upfront guard is the early-exit when `strip == nullptr`, but since `assignTouchStripZone` does the same check, the function is correct. However, the unused `strip` variable may produce a compiler `-Wunused-variable` warning on some build configurations (MSVC `/W4`). More importantly, the pre-check at line 242 uses the method-local variable name `strip` whose value is never forwarded to the underlying call  -  the comment correctly documents the intent but the variable should be either removed (rely solely on `assignTouchStripZone`'s guard) or used (call `strip->setTouchStripImage(...)` directly to avoid the re-cast).

**Fix (preferred  -  remove redundant capability pre-check and rely on `assignTouchStripZone`'s own guard):**
```cpp
void StreamDockControlService::repaintEncodersFromProfile() {
    if (!m_activeDevice) {
        return;
    }
    if (!m_profileAccessor) {
        AJAZZ_LOG_INFO("stream-dock-control",
                       "repaintEncodersFromProfile: no profile accessor set");
        return;
    }
    // ... rest of loop unchanged; assignTouchStripZone handles the cast guard
```
This matches the pattern used by `repaintFromProfile`, which only guards on `m_activeDevice` and the profile accessor before delegating to `repaintPage` (which then performs its own `IDisplayCapable` check). The redundant `ITouchStripDisplayCapable` check is extra work that could hide a future refactor where the guard capability type drifts from the actual dispatch path.

---

## Info

### IN-01: `drainPendingWrites` calls `m_activeDevice.reset()` on exception mid-loop but the outer `m_pendingWrites.clear()` at line 621 still runs  -  this is correct behavior but the comment says "remaining writes in this burst cannot be sent" while the clear discards them silently with no per-entry diagnostic

**File:** `src/app/src/stream_dock_control_service.cpp:615-621`

**Issue:** When a device-yank exception fires mid-drain, the code resets `m_activeDevice`, breaks the loop, and then falls through to `m_pendingWrites.clear()`. The pending writes for the remaining entries (all surfaces except the one that threw) are silently dropped. This is by design (yank = device gone = writes are futile), but there is no log message recording how many entries were discarded. Given that a yank during a multi-surface flush (e.g. keys + encoder overlays together after `profileChanged`) could silently drop the encoder DRA writes without any observable signal, a single warning log at break-point would aid debugging on hardware.

**Fix:** Add a diagnostic before the `break`:
```cpp
AJAZZ_LOG_WARN("stream-dock-control",
               "drainPendingWrites: device yanked, {} pending write(s) discarded",
               static_cast<int>(m_pendingWrites.size()));
m_activeDevice.reset();
m_activeCodename.clear();
break;
```

---

### IN-02: `application.cpp` `profileChanged` connects to both `repaintFromProfile` and `repaintEncodersFromProfile` on the same signal  -  both use the same coalescing timer, so a single `profileChanged` emission causes two timer arms in the same event-loop turn

**File:** `src/app/src/application.cpp:436-449`

**Issue:** The comment at line 444 correctly notes "RESEARCH A5  -  no second accessor or timer". Both `repaintFromProfile` and `repaintEncodersFromProfile` are connected to `ProfileController::profileChanged`. `repaintFromProfile` calls `repaintPage("root")` which calls `assignKeyImage` for each bound key, each of which arms `m_drainTimer` if not already active. `repaintEncodersFromProfile` then calls `assignTouchStripZone` for each bound encoder, which also tries `m_drainTimer->start(0)`  -  but since it is already active from the key arm, the timer is not re-armed (the `!m_drainTimer->isActive()` guard is correctly present). This means all key + encoder writes correctly coalesce into a single drain. The behavior is correct. However, the two signal connections fire in undefined slot-connection order relative to each other (Qt delivers queued connections in registration order for direct connections on the same thread; here both are direct). Since `repaintFromProfile` resets `m_carouselIndex = 0` before calling `repaintPage`, and `repaintEncodersFromProfile` does not touch `m_carouselIndex`, there is no ordering hazard today. This is a latent coupling fragility worth noting: if either method is later changed to read `m_carouselIndex`, the order of the two connections matters and is implicit.

**Fix (no immediate code change required):** Add a comment to the second `QObject::connect` call in `application.cpp` noting the slot order dependency:
```cpp
// NOTE: This connection is registered AFTER repaintFromProfile's connection.
// Qt delivers direct-connection slots in registration order on the same thread.
// repaintEncodersFromProfile must NOT reset m_carouselIndex (repaintFromProfile
// owns that reset); if that invariant changes, reorder these connects explicitly.
QObject::connect(m_profileController.get(),
                 &ProfileController::profileChanged,
                 m_streamDockControl.get(),
                 &StreamDockControlService::repaintEncodersFromProfile);
```

---

_Reviewed: 2026-05-27_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
