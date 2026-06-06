# Phase 10: AKP05E (0x3004) Promotion - Context

**Gathered:** 2026-05-20
**Status:** Ready for planning
**Mode:** Smart-discuss (autonomous), grey areas accepted as ARCH-grounded defaults

<domain>
## Phase Boundary

Promote the `0300:3004` AKP05E Stream Dock Plus backend (10 LCD keys / 4
endless encoders / LCD touch strip, routed via `makeAkp05`) from `scaffolded`
to `functional`: a user with the device plugged in can push `QImage`s to any
LCD key, see real encoder rotate/press/release events, set per-key colour,
set global brightness, clear the device, and flush pending writes. The
`devices.yaml` row stops falsely advertising `clock`.

**Out of this phase:** the per-device USB capture that *confirms* the wire
format (deferred to Phase 9.x) and any non-Stream-Dock SKU work. This phase
implements against the documented protocol + the Phase 9 default-verdict
ADRs; real-hardware verification is gated behind `AJAZZ_REAL_HARDWARE`.

</domain>

<decisions>
## Implementation Decisions

### Locked by Phase 9 ADRs (DEFAULT VERDICT — capture-pending)

- **Image pipeline location (ARCH-04):** host-side Qt6 `QImage` resize +
  JPEG encode in `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`,
  PRIVATE-linked to `ajazz_devices_streamdeck`. Files already exist in
  tree; this phase wires `setKeyImage` through them. **Plan MUST cite the
  conditional status** and gate final acceptance on the Pitfall 22 capture
  matrix (1024-byte chunks + last-chunk Transfer-Done `0x01` + 60×60 JPEG
  Rot0).
- **Clock capability (ARCH-05):** no RTC opcode exists in any AJAZZ corpus
  → `hasClock=false` on `akp05e`; `setTime` stays
  `NotImplemented` (v1.1 D-02 honesty contract preserved). Remove `clock`
  from the `devices.yaml` row with a `notes:` line citing ARCH-05.

### Accepted grey-area defaults (2026-05-20)

- JPEG: per-key image size + rotation table-driven for AKP05E, quality ~85,
  `Qt::SmoothTransformation` resize — confirm the exact key dimensions by
  capture (the 64×64 Rot90 lineage is AKP03R rev. 2, a different SKU).
- `setKeyColor`: short-circuit by pushing a 1×1 solid-colour JPEG through
  the same image path (no dedicated solid-colour opcode until a capture
  confirms one exists).
- Real-hardware 100-image power-cycle smoke test lives in
  `tests/integration/`, manual, behind the `AJAZZ_REAL_HARDWARE` ctest
  filter — it is the promotion gate, run by an operator, not in CI.

### Claude's Discretion

Error-message wording, chunk-loop structure, and test fixture shapes are at
plan/execution discretion, following existing `akp03.cpp` and sibling-SKU
conventions.

</decisions>

\<code_context>

## Existing Code Insights

- `src/devices/streamdeck/src/akp03.cpp` — backend already implements
  `IDevice`, `IDisplayCapable`, `IEncoderCapable`, `IClockCapable`;
  `setKeyImage` body is currently `NotImplemented`. Encoder
  press/release plumbing already present (`EncoderReleased` at ~line 260 /
  388); the `value=0` half-step workaround is the documented replacement
  target.
- `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` — already present
  (ARCH-04 Option C target); needs to be the home of the resize+encode+chunk
  logic `setKeyImage` calls.
- `akp03_protocol.hpp` — `PacketSize` is 1024 (the AKP05E 0x3004 device uses
  protocol_version 3, 1024-byte packets); this also covers the Stream Dock
  sibling SKUs (per-codename framing test must cover the relevant sizes).
- `docs/protocols/streamdeck/akp03.md` — documented Stream Dock family wire
  format (60×60 JPEG Rot0 lineage vs 64×64 Rot90 AKP03R r2).
- `docs/_data/devices.yaml` — `akp05e` row currently lists
  `capabilities: [display, encoder, clock]`; `clock` is removed here.

**Build precondition:** the C++ build currently fails to configure (Qt6
`CorePrivate` / `qzipreader_p.h` missing — needs `qt6-qtbase-private-devel`).
Execution of this phase is blocked until that is resolved.

\</code_context>

<specifics>
## Specific Ideas (ROADMAP success criteria)

1. `PacketSize` 512 → 1024 in one commit; per-codename framing test covers both.
1. `setKeyImage(int keyIndex, QImage)` works on any LCD key via the
   `image_pipeline` host path.
1. `CLE` clear, `setKeyColor` solid-colour short-circuit, `LIG` global
   brightness, explicit `flush` — all four backed by real opcodes.
1. **Promotion gate:** real-hardware 100-image power-cycle smoke test
   (manual, `AJAZZ_REAL_HARDWARE`) passes without hanging the device.
1. Encoder spin delivers a proper `EncoderReleased(int encoderIndex)`
   event, replacing the `value=0` half-step workaround.
1. `devices.yaml` removes `clock`, with a `notes:` line citing ARCH-05.

</specifics>

<deferred>
## Deferred Ideas

- **Phase 9.x capture confirmation** (Pitfall 22 matrix) — finalizes
  ARCH-04 and ARCH-05 from DEFAULT VERDICT to RATIFIED. This phase's plan
  must gate on it.
- **Real-hardware smoke test execution** — requires a physical `0300:3004`
  device; cannot run in CI or this session.
- **Build fix** — Qt6 `CorePrivate` install / mitigation precedes any
  compile or test step here.

</deferred>
