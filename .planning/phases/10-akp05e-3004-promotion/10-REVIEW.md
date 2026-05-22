---
phase: 10-akp05e-3004-promotion
reviewed: 2026-05-22T14:31:07Z
depth: standard
files_reviewed: 19
files_reviewed_list:
  - src/devices/streamdeck/CMakeLists.txt
  - src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp
  - src/devices/streamdeck/src/akp03.cpp
  - src/devices/streamdeck/src/akp03_protocol.hpp
  - src/devices/streamdeck/src/akp05.cpp
  - src/devices/streamdeck/src/akp05_protocol.hpp
  - src/devices/streamdeck/src/akp153.cpp
  - src/devices/streamdeck/src/akp153_protocol.hpp
  - src/devices/streamdeck/src/akp815.cpp
  - src/devices/streamdeck/src/akp_common_protocol.hpp
  - src/devices/streamdeck/src/image_pipeline.cpp
  - src/devices/streamdeck/src/image_pipeline.hpp
  - src/devices/streamdeck/src/register.cpp
  - tests/unit/test_akp03_protocol.cpp
  - tests/unit/test_akp05_protocol.cpp
  - tests/unit/test_akp05_touch_strip.cpp
  - tests/unit/test_akp153_protocol.cpp
  - tests/unit/test_factory_reset_and_logo.cpp
  - tests/unit/test_image_pipeline.cpp
findings:
  critical: 1
  warning: 7
  info: 6
  total: 14
status: issues_found
---

# Phase 10: Code Review Report

**Reviewed:** 2026-05-22T14:31:07Z
**Depth:** standard
**Files Reviewed:** 19
**Status:** issues_found

## Summary

This phase promotes USB `0x0300:0x3004` from a mis-filed 6-key AKP03 to its
confirmed identity as an AKP05E (10 keys / 4 encoders / touch strip), adds the
host-side RGBA->JPEG/PNG image pipeline, the touch-strip DRA path, boot-logo
upload, and propagates the ULEND commit sentinel across the family.

The wire-format work is largely sound: the COD-031 boundary holds (no
`nlohmann::json` in any installed header — grep returns 0), the `0x0300:0x3004`
descriptor is correctly routed through `makeAkp05` with full 10-key/4-encoder/
touch-strip geometry, the `BAT`/`ENC`/`MAI`/`DRA`/`LOG` opcodes match their
tests byte-for-byte, the ULEND 5-byte command at offsets 5..9 is consistent
across all four backends, and the 0xFFFF oversize guards are present on every
`sendImage`. Image-pipeline chunk math (`std::min` clamp + zero-padded trailing
chunk) has no off-by-one.

The defects below cluster in three areas: (1) the new bool-returning
touch-strip capability methods do not honour their documented "return false on
transport error" contract and can leak an exception out of an `override`;
(2) the touch-strip X-coordinate clamp silently discards legitimate input on
the right ~20% of the real 800px panel; (3) several index-validation and
quality gaps inherited or introduced alongside the new surfaces.

## Critical Issues

### CR-01: `setTouchStripImage` / `clearTouchStrip` violate their bool contract and can leak an exception out of the override

**File:** `src/devices/streamdeck/src/akp05.cpp:673-722` (and the shared `sendImage` at `798-834`)
**Issue:** The `ITouchStripDisplayCapable` contract (capabilities.hpp:1561-1562,
1582\) states these return `@c false on transport error`. The implementations
delegate to `setSecondaryScreenImage(...)`, which is `void` and calls the
private `sendImage(...)`. `sendImage` issues `(void)m_transport->write(...)`
discarding every return value, then both public methods `return true`
unconditionally. A failed transport write therefore reports **success**.

Worse: `ITransport::write()` can throw (the `close()` paths in this very file
wrap it in `try/catch` precisely because it can). `setTouchStripImage` /
`clearTouchStrip` have **no** try/catch, so a write that throws mid-burst
escapes the `override` to the caller — the opposite of the documented
`false`-return behaviour, and inconsistent with `AjSeriesMouse::factoryReset`
(aj_series.cpp:553-561) which correctly catches and returns false. A caller
that trusts the `bool` (e.g. a UI "retry on failure" loop) will misbehave on a
device-yank exactly when error handling matters most.

**Fix:** Make `sendImage` report success and have the bool methods honour it:

```cpp
// sendImage returns bool; wrap writes so a yank-induced throw becomes false.
bool sendImage(std::array<std::uint8_t, akp05::PacketSize> const& header,
               std::span<std::uint8_t const> payload) {
    if (payload.size() > 0xFFFFu) { /* WARN */ return false; }
    try {
        (void)m_transport->write(header);
        std::size_t offset = 0;
        while (offset < payload.size()) { /* ...chunk... */ }
        (void)m_transport->write(akp05::buildUploadFinished());
    } catch (...) {
        AJAZZ_LOG_WARN("akp05", "sendImage: transport write failed");
        return false;
    }
    return true;
}
// setSecondaryScreenImage should likewise return bool and propagate it;
// setTouchStripImage / clearTouchStrip then `return setSecondaryScreenImage(...)`.
```

## Warnings

### WR-01: Touch-strip X clamp drops valid input on the right ~20% of the 800px panel

**File:** `src/devices/streamdeck/src/akp05.cpp:285-294`; constant at `akp05_protocol.hpp:82-84`
**Issue:** `parseInputReport` discards any touch whose decoded X `>= TouchStripRangeX`,
and `TouchStripRangeX == 640`. But `TouchStripWidthPx == 800` (akp05_protocol.hpp:76).
Every legitimate touch in X 640..799 (the right ~20% of the strip) is silently
dropped — the gesture never reaches the callback. Per CLAUDE.md "when the RE and
the hardware disagree, the hardware wins"; the strip is 800px, so the 640 bound
is wrong for the real panel. The header even flags the value as provisional
("capture pending"), yet it is used as a hard discard threshold on live input.
The same code path also reads bytes 10..11 as an X coordinate for swipe/long-press
gestures whose payload layout is unconfirmed — a swipe carrying >=640 in those
bytes drops the entire gesture, not just the coordinate.

**Fix:** Gate on the actual panel width and clamp rather than discard, or drop
the bound entirely until a capture confirms the field:

```cpp
// Clamp into panel range instead of discarding; never silently eat a gesture.
auto const clampedX = std::min<std::uint16_t>(x, akp05::TouchStripWidthPx - 1);
ev.value = static_cast<std::int16_t>(clampedX);
```

If the gesture-specific byte layout for swipes is genuinely unknown, parse X
only for `TouchTap` and leave `value == 0` for the others until captured.

### WR-02: No key / encoder / location index validation before writing to the wire

**File:** `src/devices/streamdeck/src/akp05.cpp:544-562, 601-609`; `akp153.cpp:334-359`; `akp815.cpp:176-198`
**Issue:** `setKeyImage`, `setKeyColor`, `setEncoderImage`, and `clearKey` pass
`keyIndex` / `encoderIndex` straight into `buildKeyImageHeader` /
`buildEncoderImageHeader` / `buildClearKey` (which write the raw byte to
`pkt[12]` or `pkt[11]`) with no bounds check against `KeyCount` (10) /
`EncoderCount` (4). A caller passing keyIndex 11+ or encoderIndex 4+ ships a
malformed targeting command to firmware. The AKP05 is the first family member
with a *non-contiguous* index space (10 keys vs 4 encoders vs 4 touch zones),
so an index meant for one surface silently addresses another. `clearKey`
special-cases only `0xff` (clear-all); 11..254 fall through to `buildClearKey`
verbatim. This matches the pre-existing AKP153 pattern, but the new encoder/
touch surfaces widen the blast radius.

**Fix:** Range-check at the public boundary and WARN+no-op out of range:

```cpp
void setEncoderImage(std::uint8_t encoderIndex, ...) override {
    if (encoderIndex >= akp05::EncoderCount) {
        AJAZZ_LOG_WARN("akp05", "setEncoderImage: index {} >= {}; ignoring",
                       encoderIndex, akp05::EncoderCount);
        return;
    }
    ...
}
```

### WR-03: `setKeyImage` / `setBootLogo` etc. swallow transport throws asymmetrically with `close()`

**File:** `src/devices/streamdeck/src/akp05.cpp:544-579, 733-747`; `akp153.cpp:334-368`; `akp03.cpp:438-475`
**Issue:** `close()` wraps its single `write()` in `try/catch` because a yanked
device makes `write` throw. The image-upload paths (`setKeyImage`,
`setMainImage`, `setEncoderImage`, `setBootLogo`, `clearKey`, `setBrightness`,
`flush`) do not. On a mid-burst device-yank, the exception propagates up through
the `IDisplayCapable` override into the Qt event-loop callback. Whether that is
"correct" depends on the upstream contract, but the inconsistency (close()
guards, setKeyImage() doesn't) means a yank during a redraw behaves differently
from a yank during shutdown — and the docstrings on these methods make no
mention of throwing.

**Fix:** Decide one policy and apply it uniformly. If display writes are
allowed to throw, document it on the interface; if not, wrap them like `close()`
does (or centralise in `sendImage` per CR-01).

### WR-04: `static_cast<std::uint32_t>(jpeg.size())` can silently truncate the DRA BE32 size field

**File:** `src/devices/streamdeck/src/akp05.cpp:651-652, 718-719, 745`
**Issue:** `setSecondaryScreenImage` / `clearTouchStrip` build the DRA header
with `static_cast<std::uint32_t>(jpeg.size())`. `jpeg.size()` is a `std::size_t`
(64-bit). On a pathological large encode the cast truncates to 32 bits before
the size is written BE32 into the header — but `sendImage` then independently
refuses anything `> 0xFFFFu`. So the header could advertise a truncated BE32
size while `sendImage` refuses the body, or vice-versa: the BE32 header field
(up to 4GB) and the 16-bit `sendImage` guard disagree about the maximum. For
the full-panel `clearTouchStrip` black JPEG and 200x100 zone uploads this never
triggers in practice, but the two size ceilings are inconsistent and the
narrowing cast is unguarded.

**Fix:** Compute the size once, guard it once, and pass the guarded value to
both the header builder and the chunk loop. Reconcile the BE32 header field
with the actual `sendImage` limit (either raise the guard for DRA or document
the 16-bit ceiling on the BE32 field).

### WR-05: `firmwareVersion()` reads `m_firmwareVersion` without the mutex that other shared state uses

**File:** `src/devices/streamdeck/src/akp05.cpp:434, 782-796, 839`
**Issue:** `m_firmwareVersion` is written in `probeFirmwareVersion()` (from
`open()`) and read in `firmwareVersion() const` (which returns a `std::string`
by value). `m_callback` gets a `std::mutex` for cross-thread access, but
`m_firmwareVersion` does not. If `firmwareVersion()` is called from the UI
thread while `open()` runs the probe on the I/O thread, that is a data race on a
`std::string` (UB). The other backends return a compile-time-cached
`m_firmware.version` so they are not affected; only AKP05 mutates it at runtime.

**Fix:** Either document `firmwareVersion()` as I/O-thread-affine, or guard the
read/write of `m_firmwareVersion` with `m_mutex`.

### WR-06: `clearKey` clears-all sentinel `0xff` collides with a hypothetical 255-key index and is undocumented at the call boundary

**File:** `src/devices/streamdeck/src/akp05.cpp:564-568`; `akp153.cpp:355-359`; `akp815.cpp:194-198`
**Issue:** `clearKey(0xff)` is overloaded to mean "clear all". This is fine for
a 10/15-key device, but it is an implicit magic-number protocol that the
`IDisplayCapable::clearKey` interface does not document. A caller iterating
`for (i = 1; i <= keyCount(); ++i) clearKey(i)` is safe, but a caller that
computes `0xff` as a sentinel for "no key" would inadvertently clear the whole
panel. Combined with WR-02 (no upper-bound check), the 0xff branch is the only
guarded value in an otherwise unvalidated index space.

**Fix:** Document the `0xff == all` convention on the `clearKey` interface, or
expose an explicit `clearAllKeys()` and treat `clearKey(>=KeyCount)` as a no-op.

### WR-07: AKP05 `setSecondaryScreenImage` is a public-by-accident method on a `final` class with no interface

**File:** `src/devices/streamdeck/src/akp05.cpp:628-654`
**Issue:** `setSecondaryScreenImage` is declared `public` (it sits above the
`private:` at line 761) on the anonymous-namespace `Akp05Device`. The comment
at 656-664 calls it "formerly only reachable via the package-private
setSecondaryScreenImage method" and "Shared helper", implying it is meant to be
an implementation detail. Because `Akp05Device` is in an anonymous namespace and
not exposed via any interface, it is not actually reachable externally — but
leaving it `public` (rather than `private`) is misleading and invites a future
contributor to call it directly, bypassing the `location`-range validation that
`setTouchStripImage` performs (it only blocks `0x12`, not `>= TouchZoneCount`).

**Fix:** Move `setSecondaryScreenImage` below `private:` so the only public
entry points are the validated `ITouchStripDisplayCapable` overrides.

## Info

### IN-01: Boot-logo test prose says "512-byte chunks" but AKP05 packets are 1024 bytes

**File:** `tests/unit/test_factory_reset_and_logo.cpp:11-14, 218-219, 254`
**Issue:** The file/test titles and comments say "512-byte JPEG chunking" /
"512-byte chunks", but `akp05::PacketSize == 1024` and the assertions correctly
check `writes[i].size() == streamdeck::akp05::PacketSize` (1024). The test
passes; only the prose is stale and will mislead a future maintainer.
**Fix:** Update the comments/titles to "1024-byte chunks" for the AKP05 path.

### IN-02: `MiraboxN3VendorNew` and `MiraboxN4Vendor` are duplicate constants (both 0x6603)

**File:** `src/devices/streamdeck/src/register.cpp:45, 48`
**Issue:** Two named constants hold the same value `0x6603`. Harmless today, but
two sources of truth for one VID invites drift if one is "corrected" later.
**Fix:** Define one and alias, or add a comment noting the intentional aliasing.

### IN-03: Stale "PNG" naming and `CmdImagePng` deprecated alias retained in AKP03

**File:** `src/devices/streamdeck/src/akp03_protocol.hpp:110-115`; `akp03.cpp:118-136, 442-447`
**Issue:** The header now documents AKP03 images as JPEG ("BAT"), but
`CmdImagePng` remains as a deprecated alias and `buildImageHeader`'s docs/param
still say "PNG payload size". `Akp03Device::setKeyImage` comment says "already-
PNG-encoded bytes" while `displayInfo().jpegEncoded == true`. The naming
contradicts the encoding and the deprecated alias has no migration deadline.
**Fix:** Rename to `pngSize`->`imageSize`, drop the `CmdImagePng` alias once the
single in-file caller is migrated, and reconcile the "PNG" docstrings with JPEG.

### IN-04: `QImage final` shadows the contextual `final` keyword and `oriented` is moved-from conditionally

**File:** `src/devices/streamdeck/src/image_pipeline.cpp:131-134`
**Issue:** Naming a local `final` is legal (it is only a contextual keyword) but
hurts readability and trips some linters. The ternary `transform.format == Jpeg ? oriented.convertToFormat(...) : std::move(oriented)` is correct but reads as
if `oriented` might be used after move on the JPEG branch (it is not).
**Fix:** Rename to `encoded` / `output`.

### IN-05: `applyOrientation` rounds rotation to nearest 90 but the doc says "0/90/180/270; other values rounded"

**File:** `src/devices/streamdeck/src/image_pipeline.cpp:29-53`; `image_pipeline.hpp:45`
**Issue:** Behaviour is correct (negative angles normalised, rounded to nearest
90), but no transform in the codebase ever passes a non-multiple-of-90 angle, so
the rounding branch is effectively dead. Not a bug; flagged so a future reader
knows it is untested by the suite (the tests only use 0/90/180).
**Fix:** Add a test for a 45-degree input, or note in the doc that non-90 inputs
are untested.

### IN-06: Provisional AKP05 placeholder pair `0x0300:0x5001` still registered as a phantom device

**File:** `src/devices/streamdeck/src/register.cpp:253-269`; `akp05_protocol.hpp:64-65`
**Issue:** `akp05::VendorId`/`ProductId` are flagged "Provisional" and the
registry still registers a "AJAZZ AKP05 / AKP05E (provisional)" entry for a
PID (0x5001) no real hardware reports. It is intentional (TODO retirement
tracked), but a provisional VID:PID in the live match table can shadow or
confuse enumeration if a future real device happens to land on 0x5001.
**Fix:** Proceed with the tracked retirement; until then, keep the comment that
it is a phantom entry.

______________________________________________________________________

_Reviewed: 2026-05-22T14:31:07Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
