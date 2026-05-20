---
adr: ARCH-04
phase: 9
title: AKP03 image-encoding pipeline location
status: DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)
default_verdict: Option C - Qt6 QImage host-side in src/devices/streamdeck/src/image_pipeline.{hpp,cpp}, PRIVATE-linked to ajazz_devices_streamdeck
finalization_gate: AKP03 variant_3004 (0x0300:0x3004) image-upload first-chunk capture confirming 1024-byte chunks + last-chunk Transfer-Done 0x01 flag + 60x60 JPEG Rot0 (per Pitfall 22 confirmation matrix)
binds: Phase 10 - AKP03 variant_3004 Promotion (DISPLAY-01, DISPLAY-02, DISPLAY-03)
confidence: 'HIGH (three-way OSS-corpus agreement: mirajazz + opendeck-akp03 + ajazz-sdk all encode host-side)'
ratified: 2026-05-15
---

# ARCH-04: AKP03 image-encoding pipeline location (DEFAULT VERDICT - PENDING CAPTURE CONFIRMATION)

**Status:** DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION) - ratified 2026-05-15

> **Honesty contract (D-05):** This verdict is **DEFAULT**, not final. Phase 10
> plans referencing this ADR MUST cite the conditional status and gate on the
> Phase 9.x finalization run (a real AKP03 variant_3004 image-upload capture)
> before treating the byte-level wire-format parameters as locked. Architectural
> *location* of the pipeline is decided here; *parameters* (chunk size, image
> dimensions, rotation) are subject to Pitfall 22 confirmation.

## Context

> (NOTE 2026-05-20: the `akp03_variant_3004` device — USB 0x3004 — was later firmware-confirmed to be an AKP05E, codename akp05e; see STATE.md.)

The AKP03 family Stream Dock backend at `src/devices/streamdeck/src/akp03.cpp`
advertises the `display` capability, but the body of `setKeyImage` is currently
`NotImplemented`. The wire format is documented in `docs/protocols/streamdeck/akp03.md`:
60x60 JPEG `Rot0` for `akp03_variant_3004` / AKP03E / AKP03 lineage; 64x64
`Rot90` for AKP03R rev. 2 lineage. The host needs to:

1. Resize a `QImage` (arbitrary input) to 60x60 (or 64x64 + Rot90 for AKP03R r2).
1. JPEG-encode the result at quality ~85.
1. Chunk the encoded bytes into 1024-byte HID output reports with the
   Transfer-Done flag (`0x01`) set on the last chunk (per Pitfall 18 - wrong
   chunk size hangs the device until power-cycle).

The architectural question this ADR resolves: **where does the resize +
JPEG-encode + chunking pipeline live in the source tree?** Three candidate
locations were considered:

- **Option A:** inline inside `Akp03Device::setKeyImage` in `akp03.cpp`.
- **Option B:** a new `ajazz_imaging` static library under `src/imaging/`,
  shared across all device families.
- **Option C:** a new translation unit `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`,
  PRIVATE-linked to `ajazz_devices_streamdeck` only.

This decision must be made architecturally before Phase 10 (AKP03 variant_3004
Promotion - DISPLAY-01/02/03) starts, because Phase 10 plans cannot specify
file paths, CMake target wiring, or unit-test fixture locations without it.
The *content* of the JPEG bytes depends on capture evidence; the *location*
of the encoder does not.

## Default Verdict

**Option C wins for v1.2.** The image pipeline lives at
`src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`:

- PRIVATE-linked to `ajazz_devices_streamdeck`. The translation unit does NOT
  leak to `ajazz_devices_keyboard`, `ajazz_devices_mouse`, or any other device
  module.
- Reuses the existing `Qt6::Gui` dependency (`QImage` + `QImageWriter`) that
  `ajazz_devices_streamdeck` already pulls in for its v1.1 sidebar icon
  rendering. No new link-line entry; no new CMake `find_package` call.
- Exposes a free-function-or-static-method API of the shape
  `encodeForKey(QImage const& src, Akp03Descriptor const& desc) -> std::vector<std::uint8_t>`,
  where the descriptor carries the resize target dimensions + rotation + JPEG
  quality. The same call site handles 60x60 Rot0 and 64x64 Rot90 by descriptor
  table lookup - variant-specific behaviour goes in the descriptor, never in
  the shared backend (Pitfall 22 mitigation).
- The chunking helper lives in the same translation unit:
  `chunkForUpload(std::span<const std::uint8_t> jpeg) -> /* sequence of <= 1024-byte spans */`
  with a `bool isLast` flag enforced at the type level (per Pitfall 18
  signature recommendation - the type system prevents callers from omitting
  the Transfer-Done flag on the final chunk).

## Rationale

- **OSS-corpus convergence (HIGH confidence):** All three independent
  reverse-engineering corpora referenced in `.planning/research/SUMMARY.md`
  (mirajazz crate, opendeck-akp03, ajazz-sdk family) encode JPEG host-side.
  The device firmware does NOT do re-encoding; it accepts raw JPEG bytes
  chunked at the HID layer. This is a three-witness rule pass on the encoding
  side of the pipeline.
- **Smallest blast radius:** PRIVATE-link discipline keeps `Qt6::Gui` out of
  `ajazz_core` and the other device modules. The COD-031 boundary
  (`grep -rn nlohmann src/core/include/` must remain 0) is preserved; this
  ADR adds an analogous grep gate that `<QImage>` includes must not leak into
  any installed public header (`grep -rn QImage src/devices/streamdeck/include/`
  should remain 0 if the descriptor stays in `src/`).
- **No new dep:** `Qt6::Gui` is already on the PRIVATE link line of
  `ajazz_devices_streamdeck` for v1.1 sidebar icons. Option C costs zero new
  dependencies - it is the only option that doesn't widen the build graph.
- **Test seam alignment:** The `image_pipeline` free functions are pure (no
  I/O); they unit-test as Catch2 cases that feed a synthetic `QImage` and
  assert the byte output of `encodeForKey()` against a captured ground-truth
  fixture from `tests/integration/fixtures/akp03_variant_3004/`. This matches
  the MockTransport seam architecture from Phase 9 plan 09-04 (`mock_transport.hpp`):
  pure encoders + transport-mocked devices is the v1.2 wire-format test
  pattern across all three device families.
- **Maintainability:** `akp03.cpp` is already 536 lines as of repo state
  2026-05-15. Inlining the resize + JPEG + chunking pipeline (estimated +250
  LoC) would push the class past the maintainable LOC budget for a device
  backend and obscure the `setKeyImage` flow from readers diffing future
  variants.

## Considered alternatives

### Option A - inline in `Akp03Device::setKeyImage` (REJECTED)

Place the resize + JPEG-encode + chunking directly in the body of
`Akp03Device::setKeyImage` in `akp03.cpp`.

**Why rejected:**

- Couples the device class to image-encoding mechanics, which are not part
  of the `IDisplayCapable` interface contract.
- `akp03.cpp` is already 536 lines; inlining the pipeline pushes the file
  past maintainability and obscures the `setKeyImage` control flow.
- Unit-testing the resize/encode path in isolation requires either friend-class
  access or a public method whose only caller is the test - both anti-patterns.
- Fails the three-witness rule for image-pipeline correctness: a pure
  encoder behind a free-function API can be witness-tested against multiple
  capture fixtures (60x60 Rot0, 64x64 Rot90, edge-case input sizes) without
  spinning up a `MockTransport`-backed device.

### Option B - new `ajazz_imaging` static library (DEFERRED to v1.3+)

Centralise resize + encode in a new CMake subdir `src/imaging/`, building a
static library that could PRIVATE-link to `Qt6::Gui` and be consumed by
multiple device families (streamdeck + keyboard + mouse, if applicable).

**Why deferred:**

- The driving cost for Option B is the future AKP815 (800x480 strip-image
  upload, Mirabox N3 family) and the AK980 PRO TFT image upload (DISPLAY-05 -
  explicitly deferred to v1.2.x per `.planning/REQUIREMENTS.md`). Neither
  consumer materialises in v1.2.
- For v1.2 with one image-pipeline consumer (`ajazz_devices_streamdeck`),
  Option B is YAGNI. A static library with a single client is just a
  translation unit with extra CMake ceremony.
- Promotion path is unambiguous: when a SECOND consumer (AKP815 or AK980 PRO
  TFT) materialises, lift `image_pipeline.{hpp,cpp}` out of
  `src/devices/streamdeck/src/` into `src/imaging/`, expose a public header
  under `src/imaging/include/ajazz/imaging/`, and PRIVATE-link the new library
  from both consumers. The function signatures are designed today to make
  that lift mechanical (free functions + descriptor-table parameterisation).

This is the long-term shape per `.planning/research/SUMMARY.md` §"ARCH-04 -
Image-encoding pipeline location" - it just isn't the v1.2 shape.

## Captures-confirmation trigger (what would flip this)

Per D-05 honesty contract, the Phase 9.x finalization run (deferred from
this partial-scope Phase 9 execution) requires a captured AKP03 variant_3004
image-upload sequence - one full chunked send through the BAT opcode, taken
while the official vendor app pushes an image to a key. Three outcomes matter:

1. **Confirms 1024-byte chunks + 60x60 JPEG Rot0:** verdict stands exactly
   as written. Promote ARCH-04 from "DEFAULT VERDICT" to "Locked" status;
   Phase 10 plans become non-conditional.

1. **Reveals chunk size != 1024 *or* image format != 60x60 JPEG Rot0** (the
   realistic Pitfall 22 outcome - HOTSPOTEKUSB pre-production firmware
   divergence): Option C still wins (pipeline *location* unchanged), but the
   `Akp03Descriptor` parameters for `akp03_variant_3004` change. Update
   `docs/_data/devices.yaml` notes and `docs/protocols/streamdeck/akp03.md`
   accordingly; the descriptor table absorbs the delta. No architectural
   shift. Update this ADR's `default_verdict` frontmatter field to note the
   confirmed parameters; promote to "Locked".

1. **Reveals firmware-side encode/decode delta requiring host-side image
   manipulation beyond Qt6's `QImageWriter` reach** - e.g. a proprietary
   RLE on top of JPEG, or a non-standard JFIF dialect (vanishingly low
   probability per OSS-corpus convergence): Option C no longer suffices.
   File a follow-up ADR (ARCH-04.1 or similar) reconsidering Option B
   early, since a proprietary codec is structurally novel work that warrants
   a shared imaging library even with one consumer.

The first two outcomes are the realistic scenarios. The third is theoretical
and contradicts three independent OSS implementations - if it materialises,
something else is wrong (wrong device, wrong PID, wrong firmware revision).

## Binding to Phase 10

Phase 10 PLAN file(s) implementing DISPLAY-02 (`User can push a QImage to any LCD key on akp03_variant_3004`) MUST:

- Create `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` at the path
  ratified above (NOT a different path; NOT under `src/imaging/`).
- Update `src/devices/streamdeck/CMakeLists.txt` to compile + PRIVATE-link
  `image_pipeline.cpp` into the `ajazz_devices_streamdeck` target.
- Enforce the include-leak grep gate (additional to the existing COD-031
  grep): `grep -rn QImage src/devices/streamdeck/include/` must return 0 -
  `<QImage>` stays in the `.cpp` and is forward-declared (or unused) in
  the `.hpp`.
- Reference this ADR by file path in the plan's `<context>` block so the
  default-verdict caveat propagates into the Phase 10 planning artifact.
- Gate Phase 10 task execution on the Phase 9.x finalization run completing
  (STATE.md `pending_todos` flag must be cleared before any Phase 10
  byte-level wire-format work begins).

## Honesty contract (D-05)

This verdict is **DEFAULT** (pending capture confirmation). Phase 10 plans
referencing it MUST cite this status, NOT treat it as final. The honesty
contract from v1.1 D-02 ("no lying success UX" - render-time-on-keyface
behaves the same under success and failure) applies architecturally here:
shipping Phase 10 against a default verdict that subsequently flips to a
parameter delta is a *documented risk*, not a regression. Shipping Phase 10
against a default verdict without surfacing the conditional in the plan is
the regression.

This is the same shape as v1.1 ARCH-01/02/03 (locked, not default), with the
status field carrying the difference. Treat the `status: DEFAULT VERDICT`
frontmatter as load-bearing; any tool or human reader that drops it is
breaking the contract.

## References

- `.planning/research/SUMMARY.md` §"ARCH-04 - Image-encoding pipeline location"
  (HIGH confidence finding; three-way OSS-corpus agreement).
- `.planning/research/ARCHITECTURE.md` §"AKP03 image pipeline" + §"ARCH-NN
  candidate decisions" (Options A/B/C trade-off table).
- `.planning/research/PITFALLS.md` Pitfall 18 (chunk-size hang - wrong chunk
  size hangs the device until power-cycle) + Pitfall 22 (pre-production
  firmware divergence - HOTSPOTEKUSB dev firmware confirmation matrix).
- `.planning/REQUIREMENTS.md` ARCH-04 + DISPLAY-01 + DISPLAY-02.
- `.planning/phases/09-research-captures-hygiene/09-CONTEXT.md` D-05
  (ARCH default verdicts are PRO-FORMA - finalization gates promotion).
- `docs/protocols/streamdeck/akp03.md` (existing wire-format doc; needs
  Phase 9.x captures appendix).
- `docs/_data/devices.yaml` (devices catalogue with maturity tiers).
- `.planning/milestones/v1.1-phases/03-architectural-decisions/ARCH-01-parser-choice.md`
  (template - this ADR mirrors its shape, with `status: Locked` replaced by
  `status: DEFAULT VERDICT`).
