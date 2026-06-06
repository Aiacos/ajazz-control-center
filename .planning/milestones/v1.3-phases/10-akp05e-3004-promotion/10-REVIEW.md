---
phase: 10-akp05e-3004-promotion
reviewed: 2026-05-22T14:31:07Z
depth: standard
files_reviewed: 14
files_reviewed_list:
  - src/devices/streamdeck/src/akp05.cpp
  - src/devices/streamdeck/src/akp05_protocol.hpp
  - src/devices/streamdeck/src/akp03.cpp
  - src/devices/streamdeck/src/akp153.cpp
  - src/devices/streamdeck/src/akp815.cpp
  - src/devices/streamdeck/src/akp_common_protocol.hpp
  - src/devices/streamdeck/src/image_pipeline.cpp
  - src/devices/streamdeck/src/image_pipeline.hpp
  - src/devices/streamdeck/src/register.cpp
  - src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp
  - tests/unit/test_akp05_protocol.cpp
  - tests/unit/test_akp05_touch_strip.cpp
  - tests/unit/test_image_pipeline.cpp
  - tests/unit/test_factory_reset_and_logo.cpp
findings:
  critical: 0
  warning: 4
  info: 5
  total: 9
status: issues_found
---

# Phase 10: Code Review Report (RE-REVIEW / verification pass)

**Reviewed:** 2026-05-22T14:31:07Z
**Depth:** standard
**Files Reviewed:** 14
**Status:** issues_found

## Summary

Verification re-review of the Phase 10 AKP05E / Stream Dock streamdeck code, focused on
confirming the CR-01 fix (commit 7e04fdd) and catching any regression from the bool-widening
of `sendImage` / `setSecondaryScreenImage`.

**CR-01 is RESOLVED.** `sendImage()` (akp05.cpp:813) now returns `bool`, wraps the
header/chunk/ULEND writes in a `try { … } catch (std::exception const&)` block
(akp05.cpp:828-847), returns `false` on a transport throw and `true` only after the full
burst + ULEND sentinel are written. The bool capability surface propagates it correctly:
`setSecondaryScreenImage` returns `sendImage(...)` (akp05.cpp:653), `setTouchStripImage`
returns `setSecondaryScreenImage(...)` (akp05.cpp:700), and `clearTouchStrip` returns
`sendImage(...)` (akp05.cpp:722). No path returns `true` blindly, and no throw escapes the
overrides. The `ThrowingTransport` regression test (test_akp05_touch_strip.cpp:297-323)
asserts both `setTouchStripImage` and `clearTouchStrip` return `false` (not throw) on a
device-yank.

**No regression from the bool-widening was found.** The five void overrides (`setKeyImage`,
`setKeyColor`, `setMainImage`, `setEncoderImage`, `setBootLogo`) discard the bool deliberately,
and `setBootLogo`'s interface contract is `void` (capabilities.hpp:536), so that is consistent.
There is no missed call site: every `sendImage` invocation either returns its result (the four
bool paths) or is on a void override where the discard is intentional.

**Hard checks all pass:**

- **COD-031:** `grep -rn nlohmann src/core/include/` returns 0. Public-header boundary intact.
- **0x0300:0x3004 = AKP05E:** register.cpp:294-307 routes the retail PID to `makeAkp05`
  (10 keys / 4 encoders / touch strip / hasClock) with codename `akp05e`. No longer
  mis-routed to `makeAkp03`. Correct per the firmware-confirmed "V3.AKP05E.01.007" handshake.
- **Image-pipeline chunk/rotation math:** chunk loop (akp05.cpp:831-837) is correct —
  `take = min(PacketSize, remaining)`, zero-padded `chunk{}` per iteration, advances by
  `take`. Rotation rounds to nearest 90° (image_pipeline.cpp:34) with negative-angle
  normalisation. Correct.
- **Cross-platform -Werror:** `QImage::flipped` vs `mirrored()` is version-guarded at
  Qt 6.9 (image_pipeline.cpp:46-50). No unused file-scope `inline constexpr`. The MSVC `_s`
  CRT rule does not apply (no CRT string calls in scope).
- **ASCII-only test names:** all `TEST_CASE` strings across the four test files are ASCII;
  no em-dash / right-arrow in any test name.

Remaining findings are pre-existing warnings/info carried forward; none are regressions and
none rise to BLOCKER.

## Warnings

### WR-01: Touch-strip X coordinate clamped at 640 on an 800px panel (drops right ~20% of input)

**File:** `src/devices/streamdeck/src/akp05.cpp:285-294`, `src/devices/streamdeck/src/akp05_protocol.hpp:76-84`
**Issue:** `parseInputReport` discards any touch X coordinate `>= akp05::TouchStripRangeX`
(640): `if (x >= akp05::TouchStripRangeX) { return std::nullopt; }`. But the same protocol
header declares the panel `TouchStripWidthPx = 800` and `touchStripInfo()` reports 800px to
callers (akp05.cpp:665-667, asserted by test_akp05_touch_strip.cpp:125). Any tap/swipe landing
in the rightmost 160px (X ∈ [640, 799]) is silently dropped as "malformed," including
legitimate input in the rightmost 200px-wide encoder zone (X ∈ [600, 799]). This is a
functional input-loss bug: roughly the right 20% of the strip is dead to the parser. The
header comment itself flags `TouchStripRangeX` as "preserved for backwards-compat tests;
capture pending" — i.e. acknowledged-provisional and unverified against hardware. Per CLAUDE.md,
provisional RE values are hypotheses, not facts; here the hypothesis directly contradicts the
device's own reported geometry.
**Fix:** Reconcile the X range with the panel width. Either confirm the real on-wire X scale
against a physical AKP05E and set `TouchStripRangeX` accordingly, or gate on `TouchStripWidthPx`
until captured:

```cpp
if (x >= akp05::TouchStripWidthPx) {  // discard only outside the actual panel width
    return std::nullopt;
}
```

If the digitiser genuinely reports a 0..639 space distinct from the 800px display, document the
asymmetry and rescale X to display space before surfacing it, so downstream zone-mapping
(`x / 200`) is correct.

### WR-02: No bounds-check on keyIndex / encoderIndex before writing to the device

**File:** `src/devices/streamdeck/src/akp05.cpp:544-568, 601-609` (also akp03.cpp:438-461, akp153.cpp:334-359, akp815.cpp:176-198)
**Issue:** `setKeyImage`, `setKeyColor`, and `clearKey` accept a `keyIndex` and place it
verbatim into byte 12 / byte 11 of the header (via `buildKeyImageHeader(keyIndex, …)` /
`buildClearKey(keyIndex)`) with no `keyIndex <= KeyCount` validation. `setEncoderImage`
likewise writes `encoderIndex` to byte 12 with no `encoderIndex < EncoderCount` check. The
device exposes 10 keys (1..10) and 4 encoders (0..3), but a caller passing `keyIndex = 200` or
`encoderIndex = 50` ships an out-of-range index to firmware. The *parser* side correctly
range-checks input (encoder `>= EncoderCount` → nullopt at akp05.cpp:264-266; key
`tag <= KeyCount` at line 251), so the write side is asymmetric. `clearKey(0xff)` is a
deliberate broadcast sentinel and must stay, but other values should be validated.
**Fix:** Validate and reject (WARN + early return) before building the header, mirroring the
existing `setTouchStripImage` location-range guard (akp05.cpp:684-690):

```cpp
void setKeyImage(std::uint8_t keyIndex, std::span<std::uint8_t const> rgba,
                 std::uint16_t width, std::uint16_t height) override {
    if (keyIndex == 0 || keyIndex > akp05::KeyCount) {
        AJAZZ_LOG_WARN("akp05", "setKeyImage: keyIndex {} out of range 1..{}",
                       static_cast<int>(keyIndex), static_cast<int>(akp05::KeyCount));
        return;
    }
    ...
}
// And in setEncoderImage: reject encoderIndex >= akp05::EncoderCount.
```

### WR-03: `m_firmwareVersion` read/write data race (mutex guards only m_callback)

**File:** `src/devices/streamdeck/src/akp05.cpp:434, 783-797, 854, 856`
**Issue:** `firmwareVersion()` (line 434) reads `m_firmwareVersion` without any lock, while
`probeFirmwareVersion()` (line 791, called from `open()`) writes it via `std::move(*v)`. The
class's `m_mutex` (line 856) is documented and used to guard *only* `m_callback`
(akp05.cpp:460-462, 516-519). The class doc (akp05.cpp:398-401) claims thread-safety only "with
respect to the event callback registration" and says nothing about `m_firmwareVersion`. If a UI
thread calls `firmwareVersion()` concurrently with the I/O thread running `open()` →
`probeFirmwareVersion()`, that is a data race on a `std::string` (concurrent read during a
reallocating assignment = UB / torn read). The unit test (test_akp05_touch_strip.cpp:287-289)
only exercises the single-threaded path so it does not surface this.
**Fix:** Guard `m_firmwareVersion` with `m_mutex` (made `mutable`) on both read and write, or
publish it once before `open()` returns under the existing lock:

```cpp
[[nodiscard]] std::string firmwareVersion() const override {
    std::lock_guard const lock(m_mutex);   // m_mutex must become `mutable`
    return m_firmwareVersion;
}
// and take the same lock around the assignment in probeFirmwareVersion().
```

### WR-04: DRA advertises a BE32 size field but `sendImage` rejects payloads > 0xFFFF — silent capability mismatch

**File:** `src/devices/streamdeck/src/akp05.cpp:651-653, 813-824`
**Issue:** `setSecondaryScreenImage` builds the DRA header with a **BE32** JPEG-size field
(`buildSecondaryScreenHeader(..., static_cast<std::uint32_t>(jpeg.size()))`, akp05.cpp:651-652;
the wire field is 4 bytes per akp05_protocol.hpp:240 "BE32; max 0xFFFFFFFF"). But the shared
`sendImage()` it delegates to caps every payload at `0xFFFF` and refuses anything larger
(akp05.cpp:818-824). So the DRA path can never transmit a JPEG larger than 64 KB even though
its header reserves 32 bits for the size. A full-panel 800×480 JPEG at quality 85 can plausibly
exceed 64 KB for a photographic source, in which case `clearTouchStrip` / `setTouchStripImage`
WARN-and-return-false rather than transmit — a confusing failure where the wire format would
have supported it. The 16-bit cap is correct for the genuinely-16-bit key/main/encoder paths;
applying it to the 32-bit DRA path is the inverse mistake.
**Fix:** Either (a) parameterise `sendImage` with the protocol size-field width so the DRA path
allows up to its true BE32 limit (bounded by a sane HID-rate cap), or (b) if hardware genuinely
only accepts ≤ 64 KB on DRA, document that at `buildSecondaryScreenHeader` and stop advertising
BE32. Confirm the real limit against the RE corpus / hardware before picking. The current
behaviour silently truncates the *capability* (not the bytes), which is the safer of two bugs
but still a contract mismatch.

## Info

### IN-01: `QImage final` uses a context-sensitive keyword as an identifier

**File:** `src/devices/streamdeck/src/image_pipeline.cpp:131`
**Issue:** `QImage final = ...` names a variable `final`. `final` is a context-sensitive
identifier (legal here, not reserved), but it reads as a keyword and can confuse linters/readers.
**Fix:** Rename to `encoded` or `result`.

### IN-02: `encodeForDevice` detach comment is garbled and the rationale is stale

**File:** `src/devices/streamdeck/src/image_pipeline.cpp:111-115`
**Issue:** The comment reads "the caller's span may outlive shorter than our QImage processing
chain" — grammatically broken ("outlive shorter than") and self-contradictory. The actual reason
for `src.copy()` is that the non-owning `QImage` ctor aliases the caller's buffer and `scaled()`
may lazily re-read the source, so a detach is needed. The code is correct; the comment is not.
**Fix:** Rewrite: "The non-owning QImage ctor aliases the caller's RGBA buffer; copy() forces a
deep copy so subsequent scaled()/transformed() passes never re-read a span the caller may have
freed."

### IN-03: Stale / inaccurate doc comments on AKP05 factory and version-probe paths

**File:** `src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp:71-73`; `src/devices/streamdeck/src/akp05_protocol.hpp:182-190`
**Issue:** (1) `makeAkp05`'s Doxygen says "15-key grid (85×85 JPEG), 4 encoder LCDs" — the AKP05
is a **10-key** device (`KeyCount = 10`); 15 is the AKP153 count. Misleading for API consumers.
(2) `buildVersionRequest`'s comment (akp05_protocol.hpp:182-190) still says the response "is not
yet decoded in our Ghidra dump; this builder only covers the request side" and that the device
"responds with a 512-byte input report" — but `parseVersionResponse` is fully implemented and the
real path is a GET_FEATURE_REPORT on report id 0x01 (akp05.cpp:783-797), documented correctly
elsewhere in the same header (akp05_protocol.hpp:315-329). The `buildVersionRequest` blurb is stale.
**Fix:** Correct the key count to 10 in the factory doc; update the `buildVersionRequest` comment
to point at the GET_FEATURE_REPORT path actually used.

### IN-04: `setKeyColor` voids `keyIndex` then immediately uses it; falls back to clear instead of rendering color

**File:** `src/devices/streamdeck/src/akp153.cpp:347-353`, `src/devices/streamdeck/src/akp815.cpp:188-192`
**Issue:** `Akp153Device::setKeyColor` does `(void)keyIndex; (void)color; clearKey(keyIndex);` —
the `(void)keyIndex` cast is dead/misleading because `keyIndex` IS used on the next line. Neither
AKP153 nor AKP815 actually renders the requested color (both fall back to clear). This is a known
stub, but the AKP05 backend already renders color correctly via `encodeSolid` (akp05.cpp:555-562),
so the AKP153/AKP815 fallbacks are now strictly worse than a sibling sharing the same pipeline.
**Fix:** Drop the spurious `(void)keyIndex` cast, and consider routing AKP153/AKP815 `setKeyColor`
through `encodeSolid` + `sendImage` as AKP05 already does — `image_pipeline` is linked into the
same module.

### IN-05: AKP05 "main display" 800×100 alias over an 800×480 panel — geometry overload risks future confusion

**File:** `src/devices/streamdeck/src/akp05_protocol.hpp:76-92`; `src/devices/streamdeck/src/akp05.cpp:381-390, 734-748`
**Issue:** `MainDisplayHeightPx = 100` is a documented "legacy alias" over the real 480px strip;
`setBootLogo` (akp05.cpp:737-744) and `setMainImage` (akp05.cpp:576) encode at 800×100 while
`clearTouchStrip` / `setTouchStripImage` use the full 800×480, and `EncoderScreen*Px = 100×100`
is a backwards-compat guess against a real ~200×480 zone. Three height conventions for the same
physical panel are in flight, all flagged "legacy / capture-pending." Not a bug today (each path
is internally consistent) but a latent foot-gun: wiring a main-strip image at 800×100 while the
touch overlay paints 800×480 yields a partial-panel update.
**Fix:** Once a v3 capture lands, collapse the legacy aliases to the real per-surface geometry and
drop the "provisional / capture-pending" qualifiers; track explicitly so the conventions converge
rather than accreting more callers.

______________________________________________________________________

_Reviewed: 2026-05-22T14:31:07Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
