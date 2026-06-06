---
phase: 10-akp05e-3004-promotion
plan: '01'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: streamdeck-display
tags: [devices, streamdeck, display, akp05e, DISPLAY-01, DISPLAY-02, DISPLAY-03]
dependency_graph:
  requires: []
  provides: [akp05-1024-framing, akp05-image-pipeline-wiring, akp05-mocktransport-wire-tests]
  affects: [src/devices/streamdeck/src/akp05.cpp, src/devices/streamdeck/src/akp05_protocol.hpp]
tech_stack:
  added: []
  patterns: [image_pipeline-encodeForDevice, image_pipeline-encodeSolid, MockTransport-DI-seam-COD-026]
key_files:
  created: []
  modified:
    - src/devices/streamdeck/src/akp05_protocol.hpp
    - src/devices/streamdeck/src/akp05.cpp
    - src/devices/streamdeck/src/register.cpp
    - tests/unit/test_akp05_protocol.cpp
    - tests/unit/test_akp05_touch_strip.cpp
    - docs/_data/devices.yaml
decisions:
  - 0x0300:0x3004 is firmware-confirmed AKP05E (V3, 1024-byte packets), routed via makeAkp05 — NOT a 6-key AKP03 via makeAkp03 (a43a1cd). The plan files target the akp03_protocol.hpp/akp03.cpp backend; the actual work landed in the akp05*.{hpp,cpp} backend.
  - PacketSize is 1024 in akp05_protocol.hpp (protocol_version 3). Vendor OUT endpoint is 1024 B, IN endpoint is 512 B; PacketSize sizes the OUT report only.
  - setKeyImage/setKeyColor route through the host-side image_pipeline (encodeForDevice / encodeSolid); setKeyColor uses encodeSolid (1x1 solid JPEG), NOT a clearKey fallback.
metrics:
  mode: retrospective-reconciliation
  reconciled: '2026-05-27'
  tasks_completed: 3
  shipping_commits: [a43a1cd, 3ee11f1]
---

# Phase 10 Plan 01: AKP05E Display Surface Promotion (DISPLAY-01/02/03) — Reconciliation

**Mode: retrospective-reconciliation.** This work shipped ad-hoc ahead of GSD
bookkeeping. This SUMMARY documents what actually landed and how it deviated
from the plan — it is NOT an execution record.

**One-liner:** Promote the AKP05E (`0x0300:0x3004`) display surface to
functional: 1024-byte framing, `setKeyImage`/`setKeyColor` wired through the
host-side `image_pipeline`, and MockTransport byte-level wire tests — landed in
the `akp05*` backend, not the `akp03*` backend the plan named.

## Primary Deviation: backend file rename (akp03 -> akp05)

The plan's `files_modified` targets `src/devices/streamdeck/src/akp03_protocol.hpp`,
`akp03.cpp`, and `tests/unit/test_akp03_protocol.cpp` because at plan-write time
`0x0300:0x3004` was believed to route through the AKP03/AKP05 family code in the
`akp03` files. Commit **a43a1cd** ("0x0300:0x3004 is an AKP05E (V3), not an AKP03
sibling") re-classified the device after a live `CRT VER` handshake returned
`V3.AKP05E.01.007`. As a result **all DISPLAY-01/02/03 work landed in
`akp05_protocol.hpp` / `akp05.cpp` / `test_akp05_protocol.cpp` /
`test_akp05_touch_strip.cpp`**, not the `akp03*` files. The plan's NOTE header
(dated 2026-05-20) already anticipated this; the file names in the frontmatter
were never updated.

## Tasks Completed

| Task | Plan name                                                                 | Shipped as                                                                    | Commit                                                               | Files                                                          |
| ---- | ------------------------------------------------------------------------- | ----------------------------------------------------------------------------- | -------------------------------------------------------------------- | -------------------------------------------------------------- |
| 1    | Migrate PacketSize 512 -> 1024 + dual-size framing test                   | AKP05 1024-byte framing (protocol_version 3)                                  | 3ee11f1 (+ a43a1cd reclassification)                                 | akp05_protocol.hpp, akp05.cpp                                  |
| 2    | Wire setKeyImage + setKeyColor through image_pipeline; chunked BAT upload | setKeyImage->encodeForDevice, setKeyColor->encodeSolid, sendImage chunk+ULEND | (pre-existing akp05 wiring, confirmed under 1024 framing in 3ee11f1) | akp05.cpp (`#include "image_pipeline.hpp"`, akp05KeyTransform) |
| 3    | MockTransport byte-level tests (clearKey/setBrightness/flush/image)       | Builder-level wire tests + device-level MockTransport tests                   | e652913, ef9597b                                                     | test_akp05_protocol.cpp, test_akp05_touch_strip.cpp            |

## What actually shipped (verified against the tree)

### DISPLAY-01 — 1024-byte framing

`akp05_protocol.hpp:97` — `inline constexpr std::size_t PacketSize = 1024;`. Every
builder (`buildCmdHeader`, `buildSetBrightness`, `buildClearAll`, `buildClearKey`,
`buildKeyImageHeader`, `buildEncoderImageHeader`, `buildMainImageHeader`,
`buildLogoSizeHeader`, `buildVersionRequest`, `buildUploadFinished`) returns
`std::array<std::uint8_t, PacketSize>`. The header documents the endpoint
asymmetry: 1024-byte OUT, 512-byte IN. Landed in **3ee11f1**.

### DISPLAY-02 — image_pipeline wiring

`akp05.cpp` includes `image_pipeline.hpp` and routes:

- `setKeyImage(keyIndex, rgba, w, h)` -> `encodeForDevice(rgba, w, h, akp05KeyTransform())` -> `sendImage(...)` (BAT header + 1024-byte chunks + ULEND).
- `setKeyColor(keyIndex, color)` -> `encodeSolid(color, akp05KeyTransform())` -> `sendImage(...)` — solid-colour short-circuit through the image path, NOT a `clearKey` fallback (the plan's key requirement).
- `setEncoderImage` / `setMainImage` / touch-strip uploads also go through `encodeForDevice`.

### DISPLAY-03 — wire tests

`test_akp05_protocol.cpp` pins builder-level wire bytes (BAT/ENC/MAI/DRA/ULEND/CLE
opcodes, BE16 size fields, `PacketSize`==1024 invariant on every packet).
Device-level MockTransport tests (via the COD-026 `makeAkp05WithTransport` DI
seam) live in `test_akp05_touch_strip.cpp` and assert the captured `writes()` for
`setTouchStripImage` (DRA header + chunks + ULEND), `clearTouchStrip`, `open()`
firmware probe, and bounds rejection for out-of-range `setKeyImage`/`setEncoderImage`.

## Deviations from Plan

1. **Backend file rename (akp03 -> akp05).** See "Primary Deviation" above. The
   plan's automated verification greps (`grep ... akp05*` / `akp03*`) and the
   frontmatter `files_modified` reference the wrong file family; the work is in
   `akp05*`. Functionally equivalent — the shared Stream Dock backend semantics
   the plan described all hold.

1. **No `512 -> 1024` migration commit; AKP05 was authored at 1024 from the
   start.** Because the device was reclassified to the V3 AKP05 backend, there
   was no AKP03 `PacketSize = 512` to migrate. 3ee11f1 set/confirmed 1024 for the
   AKP05 family directly. The plan's "one load-bearing commit unblocking 13 AKP03
   sibling SKUs" framing does not apply to the AKP05E path.

1. **DISPLAY-03 device-level wire tests are split.** The plan expected
   `clearKey`/`setBrightness`/`flush` device-level MockTransport tests in
   `test_akp03_protocol.cpp`. What shipped: builder-level byte assertions in
   `test_akp05_protocol.cpp` plus device-level MockTransport tests in
   `test_akp05_touch_strip.cpp` (covering the DRA upload path, `open()` probe, and
   bounds guards). There is no dedicated device-level `clearKey`/`setBrightness`/
   `flush` MockTransport case named per the plan; the opcode bytes are pinned at
   builder level instead.

1. **ARCH-04 Transfer-Done conditional.** The shipped `sendImage` uses the
   documented ULEND-sentinel commit model (header -> 1024-byte chunks -> ULEND),
   exactly as the plan's capture-pending guidance instructed. No speculative
   per-chunk Transfer-Done `0x01` flag was added.

## Maturity & witness

`docs/_data/devices.yaml` keeps `akp05e` at **`maturity: partial`** — and per the
RE dossier (`dossier/akp-streamdeck.md`) this is correct and must STAY partial:

- **Wire bytes are hypothesised.** §6 confidence matrix marks AKP05E
  "hardware-witnessed (`V3.AKP05E.01.007`); wire bytes still hypothesised". §7.4:
  ALL AKP05 wire bytes (DRA/ENC/MAI/LOG/M_V) are corpus/Ghidra-derived and have
  **never been confirmed against a live USB capture**.
- **Known unresolved Linux render bug.** §2.2: on Linux/hidraw the kernel
  consumes `buffer[0]` (`'C'`=0x43) as the report number, misaligning the panel —
  the AKP05 first-key icon renders on Windows but not on Fedora. The fix (a `0x00`
  report-id prepend under `#ifndef _WIN32`, commit ff0b3b5 on the Linux device
  support line) is **PENDING Fedora hardware confirmation**.
- **No live image-render witness.** The 100-image power-cycle smoke test that
  would close the functional gate is plan 10-03 (LIVE-HW), which did not ship in
  this phase (see 10-03 SUMMARY) and is deferred to Phase 25.

The display surface is byte-correct in tests and wired end-to-end, but the
"functional on real hardware" promotion is honestly OPEN.

## Self-Check

- `akp05_protocol.hpp` PacketSize == 1024 (line 97). Verified.
- `akp05.cpp` references `encodeForDevice` + `encodeSolid`; `setKeyColor` uses
  `encodeSolid` (no clearKey fallback). Verified.
- MockTransport device-level tests exist in `test_akp05_touch_strip.cpp`. Verified.
- Commits a43a1cd, 3ee11f1 exist in git log. Verified.
- devices.yaml akp05e maturity is `partial` (intentional). Verified.
