# Migration plan — Stream Dock backend → mirajazz sidecar

> Branch: `experiment/mirajazz`. Goal: **remove all custom Stream Dock wire
> code and drive the family through the out-of-process mirajazz Rust sidecar
> by default — no dead code — and update docs/docstrings/CLAUDE.md/README/
> planning substantially, with the full test suite green at every step.**

## Why

The in-tree C++ Stream Dock wire layer (`src/devices/streamdeck/`, ~4 kLOC)
re-implements a protocol that the [`mirajazz`](https://github.com/4ndv/mirajazz)
Rust crate already implements and maintains. A persistent-handle sidecar built
on mirajazz was proven on the live AKP05E (`0x0300:0x3004`): connects, reads
firmware, renders all keys + the 4 touch-strip zones correctly (Rot180), and —
crucially — does **not** wedge the panel (one `CRT DIS` for the handle lifetime
vs the per-interaction open/close churn the C++ control service does today).

## Scope decisions (LOCKED 2026-06-01)

| Item                                 | Decision                                                                                                                                                                     |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| AKP03 / AKP153 / AKP05-N4            | mirajazz covers them → **remove C++ wire code**, route via sidecar.                                                                                                          |
| **AKP815** (800×480 strip)           | **NOT a mirajazz device → carve-out: keep custom.** Keep `akp815.cpp`, `akp815_protocol.hpp`, `akp_common_protocol.hpp`, `image_pipeline.*`.                                 |
| Keyboards (AK980) / mice (AJ-series) | Not stream controllers → out of mirajazz scope, stay custom, untouched.                                                                                                      |
| App test fixtures                    | `makeAkp05WithTransport` (in-process, mock-transport) backs ~10 app test suites; the QProcess sidecar can't be used at unit time → write `FakeStreamDockDevice` and migrate. |

### Known limitation

AKP03 / AKP153 sidecar paths are **unverifiable without hardware** — only the
AKP05E demo (`0x3004`), AK980, and an AJ mouse are attached. They will be
implemented against the mirajazz / opendeck-akp03 / opendeck-akp153 references
and marked PROVISIONAL, the same posture as retail-AKP05E input.

## Slices (each ends with: build green + full ctest green + atomic commit)

- **A. Sidecar multi-device (Rust).** Extend `streamdock-host` queries +
  per-family params (protocol version, key count, image format, usage page) to
  cover AKP03 (pv1/2) and AKP153 (pv2) in addition to AKP05/N4 (pv3). Reference:
  mirajazz protocol versions + opendeck-akp03 / opendeck-akp153 device defs.
- **A′. Sidecar wire tests (Rust).** The ~10 C++ app suites that use
  `makeAkp05WithTransport` are **MockTransport wire-level** tests (they assert
  the BAT/ULEND/LIG byte sequences `StreamDockControlService` emits). With the
  wire moving into the sidecar, that coverage moves to **cargo tests** in
  `streamdock-host` (assert the bytes mirajazz/our framing produce). Add them in
  Slice A so wire coverage is not lost when the C++ wire tests are deleted.
- **B. Test fixture (C++).** Add `FakeStreamDockDevice` (in-process
  `core::IDevice + IDisplayCapable + IEncoderCapable`, records *calls* not bytes,
  injectable input). Per-test triage of the ~10 suites: **migrate** the
  app-behavior assertions (repaint-count, binding dispatch, key-index mapping)
  onto the fake; **delete** the pure wire-byte assertions (now covered by A′).
- **C. Registration flip (C++).** App `bootstrap` registers
  `makeSidecarStreamDock` for every mirajazz-covered Stream Dock PID
  (AKP03/AKP153/AKP05-N4) before `registerAll`; remove those registrations from
  `register.cpp` (keep AKP815). Live-verify AKP05E.
- **D. Delete C++ wire code.** `git rm` akp03/akp05/akp153 `.cpp` + `_protocol.hpp`;
  drop their `makeAkp*` decls from `streamdeck.hpp`; update `register.cpp` +
  CMake; delete the orphaned wire tests (akp03/akp05/akp153 protocol/input/touch).
  Keep AKP815 + common + image_pipeline.
- **E. Docs + planning.** CLAUDE.md (sidecar architecture, retire the C++-wire
  glossary claims), README, docstrings, `docs/_data/devices.yaml`, `docs/protocols`,
  `.planning/STATE.md` + ROADMAP. No stale references to deleted code.
- **F. Cross-platform build + bundle.** CMake builds the Rust sidecar (cargo)
  and installs/bundles it beside the app for deb/rpm/flatpak/msi/dmg; CI matrix
  gains a Rust toolchain step. (Heaviest infra slice; sequenced last.)

## Slice B — test migration pattern (established)

The ~7 app suites on `makeAkp05WithTransport` + `MockTransport` are wire-byte
tests. Migration recipe (proven in `test_stream_dock_controls`):

1. Includes: `fixtures/mock_transport.hpp` → `fixtures/fake_stream_dock_device.hpp`.
1. Fixture: `makeAkp05WithTransport(desc,id,transport)` →
   `std::make_shared<tests::FakeStreamDockDevice>(desc, id)`; the lookup lambda
   returns the fake.
1. Assertions: byte-packet counting (BAT/ULEND/LIG/CLE via `obs->writes()`) →
   capability-call counting on the fake (`keyImages` / `brightnessCalls` /
   `clearedKeys` / `flushCount`). "N keys painted" → `keyImages.size()` delta N.
1. Input tests: `MockTransport::enqueueRead(rawframe)` + decode → drive
   `fake->injectEvent(DeviceEvent{...})` directly (skips the deleted decode).
1. Behaviour tests independent of the wire (QSignalSpy on pageNavRequested,
   profile model, bridge coords) stay as-is.
1. Pure wire-format assertions with no behavioural counterpart are DROPPED —
   that coverage is the sidecar's (cargo) + live verification.

## Status

- [x] Sidecar Slices 1–2 (announce/firmware/input/output, hw-validated render).
- [x] C++ protocol helper + tests (3a), `SidecarStreamDockDevice` (3b).
- [x] AKP05E routed to sidecar by default + live-verified (4c).
- [x] A — sidecar multi-family (AKP03/153/05) + 9 cargo tests, register.cpp parity.
- [x] C — `streamDockSidecarDescriptors()` + bootstrap wiring (all SKUs → sidecar).
- [~] B — FakeStreamDockDevice fixture done; migrated 1/7 (`test_stream_dock_controls`).
  Remaining: control_service, input_service, profile_pages, profile_persistence,
  plugin_device_bridge, akp05_touch_strip.
- [ ] D — `git rm` akp03/05/153.{cpp,\_protocol.hpp} + streamdeck.hpp decls +
  register.cpp makeAkp\* calls + pure-wire unit tests; fix test_register_akp05e_clock
  (assert streamDockSidecarDescriptors, both hasClock=false). Keep AKP815 + common
  \+ image_pipeline.
- [ ] E — docs (CLAUDE.md glossary, README, docstrings, devices.yaml).
- [ ] F — cmake builds + bundles the Rust sidecar cross-platform + CI Rust step.
