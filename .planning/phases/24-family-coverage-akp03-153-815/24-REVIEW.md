---
phase: 24-family-coverage-akp03-153-815
reviewed: 2026-05-24T00:00:00Z
depth: standard
files_reviewed: 5
files_reviewed_list:
  - src/devices/streamdeck/src/akp03.cpp
  - src/devices/streamdeck/src/akp153.cpp
  - src/devices/streamdeck/src/akp815.cpp
  - src/app/src/stream_dock_control_service.cpp
  - src/app/src/stream_dock_input_service.cpp
findings:
  critical: 2
  warning: 5
  info: 3
  total: 10
status: issues_found
---

# Phase 24: Code Review Report

**Reviewed:** 2026-05-24
**Depth:** standard
**Files Reviewed:** 5
**Status:** issues_found

## Summary

Phase 24 added three new device backends (AKP03, AKP153, AKP815) and generalized
the control/input services to be descriptor-driven rather than AKP05-specific.
The wire-level framing (CRT prefix, 512-byte packets, BAT/LIG/CLE/ULEND opcodes)
is correctly implemented. The DI seams (WithTransport factories) are thin wrappers
with no wire-format regression. The capability-honesty enforcement (encoder/touch
gating) is solid.

Two BLOCKER-class issues were found:

1. **CR-01**: `akp03.cpp` validates `setKeyImage` / `clearKey` against `akp03::KeyCount`
   (= 9, including 3 non-LCD side buttons) instead of `akp03::DisplayKeyCount` (= 6).
   This allows the image-transfer machinery to be invoked for key indices 7–9 which have
   no LCD surface, silently sending a BAT burst to the firmware for a non-existent slot.

1. **CR-02**: The stale file-level comment and class docstring in `akp03.cpp` claim the
   device uses "72×72 PNG" images and "one rotary encoder". The RE docs (`akp03.md`,
   `akp_device_matrix.md`, `akp03_protocol.hpp`) confirm **60×60 JPEG** and **3 encoders**.
   The `DisplayInfo` returned at runtime is correct (`jpegEncoded=true`, `KeyWidthPx=60`),
   but the header-packet builder is still named `buildImageHeader` with the `@param pngSize`
   doc-tag and the `sendImage` function calls its argument `png`. These inconsistencies
   mean a reviewer or future maintainer cannot trust the documentation, and more critically
   the misnamed parameter silently passes the wrong format contract to callers still relying
   on stale docs.

Five WARNING-class issues concern: the AKP815 strip-size disagreement between docs, the
stale `akp153_protocol.hpp` USB IDs, the `akp153.cpp` `parseInputReport` always returning
`pressed=true`, the `stream_dock_input_service.hpp` claiming to be "AKP05E-specific" in
its file comment, and a missing `encoderReleaseSynthesised` emit path for AKP03 (whose
hardware does send release events and whose backend dispatches `EncoderReleased` events).

______________________________________________________________________

## Critical Issues

### CR-01: `setKeyImage` and `clearKey` validate against total button count (9) not LCD count (6)

**File:** `src/devices/streamdeck/src/akp03.cpp:443`, `src/devices/streamdeck/src/akp03.cpp:468`

**Issue:** `akp03::KeyCount` is defined as `DisplayKeyCount + SideButtonCount` = 6 + 3 = 9.
The three side buttons (indices 7, 8, 9) are non-LCD buttons with no visual surface; sending
a BAT image-transfer burst to key slot 7, 8, or 9 is either a firmware no-op or a desync risk.
`setKeyImage` guards with `keyIndex > akp03::KeyCount` (9) so it accepts indices 7–9 and
passes them to `sendImage()`, which then executes the full BAT header + JPEG chunk + ULEND
sequence for a physically non-existent LCD. `clearKey` has the same boundary.

Cross-ref: `akp03.md` §Features: "LCD keys: **6**"; `akp03_protocol.hpp:67–69` explicitly
distinguishes `DisplayKeyCount = 6` from `KeyCount = 9`.

**Fix:**

```cpp
// setKeyImage: line 443
if (keyIndex == 0 || keyIndex > akp03::DisplayKeyCount) {

// clearKey: line 468
if (keyIndex != 0xffU && (keyIndex == 0 || keyIndex > akp03::DisplayKeyCount)) {
```

The `clearKey` broadcast (`0xff`) path is correct and unchanged. Only the upper bound
for individual key operations needs to be `DisplayKeyCount`, not `KeyCount`.

______________________________________________________________________

### CR-02: `akp03.cpp` file comment, class docstring, and `sendImage` function name claim PNG / 72×72 / 1 encoder — all contradicted by the RE

**File:** `src/devices/streamdeck/src/akp03.cpp:7-9`, `src/devices/streamdeck/src/akp03.cpp:285-286`, `src/devices/streamdeck/src/akp03.cpp:525-531`

**Issue:** Three places embed stale/wrong facts from an earlier, uncorrected stub:

- Line 7–9 (file docstring): "72×72 PNG images per key and **one** rotary encoder" and
  "PNG-typed image command ('PNG' instead of 'BAT')".
- Line 285–286 (class docstring): "IDisplayCapable (6-key **72×72 PNG** grid)" and
  "**one** pressable rotary encoder".
- Line 525 (`sendImage` docstring): "@param png Raw PNG bytes".

The RE docs are unambiguous:

- `akp03.md` §Image upload: "60×60 JPEG, Rot0"; `akp03_protocol.hpp:71–72` `KeyWidthPx=60`, `KeyHeightPx=60`.
- `akp03.md` §Hardware: "Rotary encoders: **3**"; `akp03_protocol.hpp:70` `EncoderCount=3`.
- `akp03_protocol.hpp:110–115`: the image command is `CmdImage = BAT`, with the old `CmdImagePng`
  kept only as a deprecated alias.

The `displayInfo()` implementation is correct at runtime (uses `akp03::KeyWidthPx=60`,
`jpegEncoded=true`) and `encoderInfo().count` returns `akp03::EncoderCount=3`. The bug is that
the documentation layer is wrong, which creates a false contract for callers and reviewers.

This is classified BLOCKER because the PNG claim propagates into the `sendImage` argument
name (`png`) and appears in the builder doc (`@param pngSize`), making it non-obvious
that the `buildImageHeader` call at line 544 uses the BAT opcode (JPEG protocol)
while the wrapper function is named and documented as PNG.

**Fix:** Update the three doc-blocks to read:

```
// File docstring line 7-9:
//  a 6-key (2×3) stream deck with 60×60 JPEG images per key and three rotary
//  encoders (1 large + 2 small). The wire protocol is a close cousin of the AKP153
//  (same "CRT" prefix and 512-byte packet size) using the BAT image command.

// Class docstring line 285-286:
//  Aggregates IDevice, IDisplayCapable (6-key 60×60 JPEG grid), and
//  IEncoderCapable (three pressable rotary encoders with no screen).

// sendImage docstring: rename parameter from `png` to `jpeg`, update @param tag.
```

______________________________________________________________________

## Warnings

### WR-01: AKP815 LCD strip size is internally contradicted (800×480 vs 854×480)

**File:** `src/devices/streamdeck/src/akp815.cpp:13`, `src/devices/streamdeck/src/akp815_protocol.hpp:41-42`

**Issue:** `akp815.cpp:13` and `akp815_protocol.hpp:41-42` document the strip as `800×480`.
`docs/protocols/streamdeck/akp_device_matrix.md` line 257 (§10 cross-reference table) lists
AKP815's strip as `854×480`. `docs/protocols/streamdeck/akp815.md` consistently says `800×480`
(§Hardware, §Wire protocol, §Features table). The device matrix is likely wrong (it has `854×480`
for AKP815 but `800×480` for AKP05's touch strip, while `akp815.md` says `800×480` is derived
from `[ajazz-sdk]`'s `lcd_strip_size()`). However, neither value has been confirmed against
hardware — both are sourced from `[ajazz-sdk]` alone. `akp815.md` itself warns "vendor capture
required to confirm physical dimensions".

This discrepancy is a documentation bug today but will become a wire-format bug when the strip
upload path is implemented.

**Fix:** Reconcile the matrix entry. The `akp815.md` value (`800×480`, attributed directly to
`[ajazz-sdk]/info.rs::Kind::Akp815::lcd_strip_size()`) is more precisely sourced. Update the
matrix cell at line 257 to read `800×480` and add a note that the `854×480` logo-channel is
an AKP153 attribute.

______________________________________________________________________

### WR-02: `akp153_protocol.hpp` USB IDs are stale and contradicted by `akp153.md` research

**File:** `src/devices/streamdeck/src/akp153_protocol.hpp:41-42`

**Issue:** `akp153_protocol.hpp` declares:

```cpp
inline constexpr std::uint16_t ProductIdInternational = 0x1001;
inline constexpr std::uint16_t ProductIdChinese = 0x1002;
```

But `akp153.md` §Variants documents:

- AKP153 international canonical pair: `0x5548:0x6674` (NOT `0x0300:0x1001`)
- AKP153E (China market) canonical PID: `0x1010` (NOT `0x1002`)

`register.cpp` correctly uses these stale IDs (for backwards-compat "legacy" descriptors) but
also registers the updated canonical PIDs. The header constants are misleadingly named
`ProductIdInternational` / `ProductIdChinese` while both are now known to be wrong for their
respective target SKUs. Any future code that trusts these names (e.g. a new backend or a test)
will use incorrect IDs.

**Fix:** Rename or deprecate the stale constants:

```cpp
/// @deprecated Pre-2026-05-14 PID. Kept for legacy descriptor compat; use VendorId=0x5548
///             + ProductId=0x6674 for the canonical international AKP153.
inline constexpr std::uint16_t ProductIdLegacyInternational = 0x1001;
/// @deprecated Pre-2026-05-14 PID. Canonical AKP153E PID is 0x1010 per [ajazz-sdk].
inline constexpr std::uint16_t ProductIdLegacyChinese = 0x1002;
```

______________________________________________________________________

### WR-03: `akp153::parseInputReport` always returns `pressed = true` — no release events

**File:** `src/devices/streamdeck/src/akp153.cpp:176-196`

**Issue:** `parseInputReport` unconditionally sets `pressed = true` for any valid key index
(lines 193–195):

```cpp
return KeyEvent{.keyIndex = keyIndex, .pressed = true};
```

The comment at line 169 documents this: "always reports `pressed = true`, which is the open bug".
The `akp153.md` §Wire protocol §Input reports confirms: "today it always reports `pressed = true`".

This is a known accepted limitation. It is classified WARNING (not BLOCKER) because the bug is
self-documented and bounded by the current phase scope. However, it means the AKP153 backend
never fires `DeviceEvent::Kind::KeyReleased` — a consumer that requires paired press/release
(e.g. a hold-to-activate action) will silently malfunction. This is a latent correctness
defect that should be tracked and fixed before `stable` classification.

**Fix:** Implement a per-key state diff. Track the previously-reported key set and emit
`KeyReleased` when a key index disappears from successive reports. This requires maintaining
a bitmask across calls, but the parser becomes stateful. Alternative: emit both edges at
the protocol level (press on index != 0, synthetic release on `keyIndex == 0` or the next
frame showing a different key) — consistent with how `[companion]` handles the 293V3.

______________________________________________________________________

### WR-04: `stream_dock_input_service.hpp` file header and class docstring still claim "AKP05E-specific" — misleading after Phase 24 generalization

**File:** `src/app/src/stream_dock_input_service.hpp:7-11`, `src/app/src/stream_dock_input_service.hpp:78-80`

**Issue:** The file docstring (lines 7–11) says "App-layer input dispatch service for **AKP05E** Stream Dock devices" and the class docstring (line 78) repeats the same claim. After Phase 24 the service is descriptor-driven and handles AKP03 (3 encoders), AKP153, AKP815 (no encoders), and AKP05 (4 encoders + touch). The AKP05E specificity is now false:

- `setActiveDevice()` reads `desc.encoderCount` to size `m_encAccum` for all families.
- The touch-strip gate (`m_hasTouchStrip`) is false for AKP03/153/815.
- All families use the same `dispatch()` / `drainCoalescedRotation()` code.

A developer reading the header for AKP03 integration will be misled into thinking they need a separate service.

**Fix:** Update the file docstring and class docstring to reflect the generalized scope:

```
// @brief App-layer input dispatch service for AKP-family Stream Dock devices
//        (AKP03, AKP05/05E, AKP153, AKP815).
```

______________________________________________________________________

### WR-05: AKP03 backend dispatches `EncoderReleased` device events but `stream_dock_input_service.cpp` silently drops them (the AKP05 press-only comment is wrong for AKP03)

**File:** `src/app/src/stream_dock_input_service.cpp:235-237`

**Issue:** `dispatch()` at line 235–237 has:

```cpp
case core::DeviceEvent::Kind::EncoderReleased:
    // Dormant on hardware (akp05.md:76 press-only); ignore safely.
    break;
```

This is correct for AKP05 (press-only encoder). But AKP03 v3-firmware encodes press/release
pairs: the `akp03.cpp:poll()` switch at lines 396–397 dispatches `DeviceEvent::Kind::EncoderReleased`
when `ev->kind == InputEvent::Kind::EncoderReleased`. The comment "Dormant on hardware" makes
the silent drop look intentional for all families.

For AKP03, dropping the hardware release event means `EncoderPressed` → `EncoderReleased`
pairs from AKP03 v3 firmware will never fire a bound `onRelease` chain. The service also
calls `synthesiseEncoderRelease()` unconditionally on `EncoderPressed`, so AKP03 v3 users
will get a synthetic release immediately (which is correct for AKP05) PLUS a dropped hardware
release. This is benign today (profile.hpp has no `onRelease` field for encoders), but as soon
as an `EncoderBinding::onRelease` is added (noted in `synthesiseEncoderRelease()` comment at
line 333), the AKP03 path will silently fail to fire it from the hardware edge.

**Fix:** Add a comment clarifying the intentional drop is AKP05-specific:

```cpp
case core::DeviceEvent::Kind::EncoderReleased:
    // AKP05 is press-only (akp05.md:76); we synthesise a release after EncoderPressed.
    // AKP03 v3 firmware does emit release events, but EncoderBinding has no onRelease
    // field yet (profile.hpp:113-118). When onRelease is added, run it here for both
    // real and synthesised releases.
    break;
```

______________________________________________________________________

## Info

### IN-01: Stale comment in `akp03.cpp` `displayInfo()` claims "PNG" format in inline remark

**File:** `src/devices/streamdeck/src/akp03.cpp:434`

**Issue:** The `displayInfo()` inline comment reads:

```cpp
.jpegEncoded = true, // `[ajazz-sdk]` key_image_format: JPEG, Rot0, no mirror
```

This comment is correct. However the comment immediately above the struct literal at line 427
says nothing, and the struct itself is prefixed at line 429 by a comment referencing "side buttons
7..9 are surfaced as key events but have no visual surface" which is accurate. The contradiction
is between the correct `jpegEncoded=true` and the stale file-level "PNG" claim (CR-02). Fixing
CR-02 resolves this.

______________________________________________________________________

### IN-02: `buildImageHeader` in `akp03_protocol.hpp` and `akp03.cpp` still use `pngSize` / `CmdImagePng` parameter names

**File:** `src/devices/streamdeck/src/akp03_protocol.hpp:191-192`, `src/devices/streamdeck/src/akp03.cpp:128-129`

**Issue:** Function signature uses `pngSize` as the parameter name:

```cpp
[[nodiscard]] std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                                    std::uint16_t pngSize);
```

The deprecated alias `CmdImagePng = CmdImage` (line 115 of the header) is also still present
with a deprecation notice. Both are named legacy artifacts from before the JPEG-not-PNG
correction. Callers currently pass JPEG bytes through `pngSize`. No runtime bug (the opcode
is `BAT = JPEG`), but misleading names.

**Fix:** Rename `pngSize` → `imageSize` in the declaration and definition. Remove `CmdImagePng`
in a follow-up once all call sites have migrated to `CmdImage`.

______________________________________________________________________

### IN-03: `stream_dock_control_service.cpp` comment at line 27 says "AKP05E backend" in key-index-mapping note

**File:** `src/app/src/stream_dock_control_service.cpp:26-27`

**Issue:** The key-index mapping note reads:

```
//   the AKP05E backend requires 1-based indices (1..10).
```

After Phase 24 generalization the service operates on AKP03 (1..6 LCD keys), AKP153 (1..15),
AKP815 (1..15), and AKP05 (1..10). The "1..10" bound is AKP05-specific and misleading —
the actual upper bound is `descriptor.keyCount`, derived per device. The code itself is correct
(it validates via the backend's `setKeyImage` guard, not a hardcoded 10), so this is documentation-
only.

**Fix:** Update the comment:

```
//   device backends expect 1-based indices (1..descriptor.keyCount). Add 1 when
//   iterating profile keys for repaintFromProfile().
```

______________________________________________________________________

_Reviewed: 2026-05-24_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
