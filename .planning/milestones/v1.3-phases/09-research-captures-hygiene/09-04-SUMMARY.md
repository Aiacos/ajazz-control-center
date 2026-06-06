---
phase: 09-research-captures-hygiene
plan: 04
subsystem: testing
tags: [cpp20, catch2, mock-transport, hid, dependency-injection, cod-026, cod-031, capture-04]

# Dependency graph
requires:
  - phase: 09-research-captures-hygiene/01
    provides: capture-data-hygiene policy + pre-commit reject hook (CAPTURE-01)
provides:
  - tests/unit/fixtures/mock_transport.hpp (header-only ITransport mock; ajazz::tests::MockTransport)
  - ajazz::mouse::makeAjSeriesWithTransport(d, id, transport) public factory overload
  - tests/unit/test_aj_series_mock_transport.cpp (3 Catch2 TEST_CASEs, +3 net to suite)
  - Reusable DI seam for Phase 10/11/12 byte-level wire-format tests (no further infra needed)
affects:
  - Phase 10 (Akp03Device chunked-image upload) — will add makeAkp03WithTransport on the same shape
  - Phase 11 (8K mouse cmd 0x21..0x50) — reuses MockTransport unchanged against AjSeriesMouse
  - Phase 12 (AK980 PRO three-stage RGB) — will add makeProprietaryKeyboardWithTransport on the same shape

# Tech tracking
tech-stack:
  added: []
  patterns:
    - 'Header-only test fixtures live under tests/unit/fixtures/ in the ajazz::tests:: namespace (parallels mock_hid_enumerator.hpp)'
    - Public *WithTransport factory overloads expose anonymous-namespace COD-026 DI constructors across translation units (the canonical extension of the existing makeAjSeries shape)
    - Catch2 wire-format smoke tests assert byte-level equality on MockTransport::writes() — no real HID device required
    - static_assert(!std::is_move_constructible_v<T>) co-located with the class body locks the rule-of-five contract inherited from ITransport

key-files:
  created:
    - tests/unit/fixtures/mock_transport.hpp
    - tests/unit/test_aj_series_mock_transport.cpp
  modified:
    - src/devices/mouse/include/ajazz/mouse/mouse.hpp
    - src/devices/mouse/src/aj_series.cpp
    - tests/unit/CMakeLists.txt

key-decisions:
  - MockTransport is header-only (D-04) — no .cpp; every member defined inline; static_asserts lock rule-of-five
  - Plan-shape decision (D-04) explicitly scopes the smoke test to ONE backend (AjSeriesMouse — the easiest 64-byte feature-report envelope); Phase 10/11/12 add their own factories
  - Inspection API surfaces write() AND writeFeature() in a single writes() vector in call order, with a separate writeFeatureCount() to disambiguate
  - core::TransportPtr is `std::unique_ptr<ITransport>` — ownership transfers from test into device factory; observer pointer captured before std::move
  - mouse.hpp gains a `#include "ajazz/core/transport.hpp"` (TransportPtr is now part of the public mouse API surface; was a forward-decl pre-CAPTURE-04)

patterns-established:
  - 'Header-only mock fixture under tests/unit/fixtures/<name>.hpp implementing the production interface; no .cpp; ajazz::tests:: namespace'
  - make<Backend>WithTransport(d, id, TransportPtr) public factory overload defined adjacent to the anonymous-namespace backend class; production make<Backend>(d, id) unchanged
  - Wire-format Catch2 tests live alongside per-backend protocol tests in tests/unit/ (test_<backend>_mock_transport.cpp) and tag [CAPTURE-04][mock_transport]

requirements-completed: [CAPTURE-04]

# Metrics
duration: 4min
completed: 2026-05-15
---

# Phase 9 Plan 04: MockTransport fixture + AjSeriesMouse smoke test Summary

**Header-only `ajazz::tests::MockTransport` implementing the full `ITransport` surface, a public `makeAjSeriesWithTransport` factory overload that exposes the existing COD-026 DI constructor across TU boundaries, and a Catch2 smoke test that asserts the exact 64-byte feature-report envelope produced by `AjSeriesMouse::setActiveDpiStage(0)` — locks the wire-format mock seam in place before Phase 10/11/12 byte-level tests depend on it.**

## Performance

- **Duration:** ~4 min
- **Started:** 2026-05-15T07:54:19Z
- **Completed:** 2026-05-15T07:58:03Z

## MockTransport API

| Method (public surface)                                      | Kind       | Purpose                                                                |
| ------------------------------------------------------------ | ---------- | ---------------------------------------------------------------------- |
| `writes() const -> std::vector<std::vector<uint8_t>> const&` | Inspection | Every `write` + `writeFeature` invocation, in call order, bytes copied |
| `writeCount() const -> std::size_t`                          | Inspection | Total `write` + `writeFeature` invocation count                        |
| `writeFeatureCount() const -> std::size_t`                   | Inspection | `writeFeature` invocation count only (subset of `writes()`)            |
| `enqueueRead(std::vector<uint8_t>)`                          | Injection  | FIFO queue feeding the next `read()` call                              |
| `enqueueReadFeature(std::vector<uint8_t>)`                   | Injection  | FIFO queue feeding the next `readFeature()` call                       |
| `reset() noexcept`                                           | Reset      | Clears writes, queued reads, open flag, stats                          |
| `open()` / `close()` / `isOpen() const`                      | ITransport | Toggles internal `m_open` flag; no real device                         |
| `write(span<const uint8_t>) -> size_t`                       | ITransport | Copies bytes into `m_writes`, increments `m_stats.bytesSent`           |
| `writeFeature(span<const uint8_t>) -> size_t`                | ITransport | Same as `write`, also increments `m_writeFeatureCount`                 |
| `read(span<uint8_t>, chrono::milliseconds) -> size_t`        | ITransport | Pops front of `m_reads` FIFO; returns 0 if empty (simulated timeout)   |
| `readFeature(span<uint8_t>) -> size_t`                       | ITransport | Pops front of `m_readFeatures` FIFO; returns 0 if empty                |
| `stats() const noexcept -> TransportStats`                   | ITransport | Snapshot of cumulative byte counters                                   |

## AjSeriesMouse envelope assertion (the 64 bytes)

`setActiveDpiStage(0)` produces a single 64-byte HID feature report via `writeFeature`:

| Byte  | Value | Meaning                                                |
| ----- | ----- | ------------------------------------------------------ |
| 0     | 0x05  | HID report ID                                          |
| 1     | 0x21  | `kCmdDpi` (CommandId::kCmdDpi)                         |
| 2     | 0x01  | sub-command: setActiveDpiStage                         |
| 3     | 0x01  | payload length (one byte: the stage index)             |
| 4     | 0x00  | payload byte: stage index 0                            |
| 5..62 | 0x00  | zero-padding (58 bytes)                                |
| 63    | 0x23  | checksum = `(0x21 + 0x01 + 0x01 + 0x00) & 0xff = 0x23` |

The smoke test `REQUIRE`s exactly one `writeFeature` invocation and `CHECK`s every byte against the table above (the zero-padding loop uses `CAPTURE(i)` to localise any future failure to the exact byte position).

## New public factory signature

```cpp
// src/devices/mouse/include/ajazz/mouse/mouse.hpp
namespace ajazz::mouse {

// Production (unchanged): builds a real HidTransport from id.vendorId/productId/serial
[[nodiscard]] core::DevicePtr
makeAjSeries(core::DeviceDescriptor const& d, core::DeviceId id);

// CAPTURE-04 (new): test-only factory that forwards to the
// anonymous-namespace AjSeriesMouse COD-026 DI constructor.
[[nodiscard]] core::DevicePtr
makeAjSeriesWithTransport(core::DeviceDescriptor const& d,
                          core::DeviceId id,
                          core::TransportPtr transport);

} // namespace ajazz::mouse
```

## Test-count delta

| Phase                                | ctest --preset linux-release | Delta |
| ------------------------------------ | ---------------------------- | ----- |
| Pre-CAPTURE-04 baseline (v1.1 close) | 178 / 178                    | —     |
| Post-CAPTURE-04                      | 181 / 181                    | +3    |

The +3 TEST_CASEs are:

1. `MockTransport captures setActiveDpiStage envelope on AjSeriesMouse`
1. `MockTransport reset() clears captured writes`
1. `MockTransport differentiates write and writeFeature in counts`

## COD-031 boundary verification

```
$ grep -rn nlohmann tests/unit/fixtures/mock_transport.hpp \
    src/devices/mouse/include/ src/devices/mouse/src/
(no output — 0 matches)
```

mock_transport.hpp's transitive includes (`<chrono>`, `<cstddef>`, `<cstdint>`, `<queue>`, `<span>`, `<vector>`, `<algorithm>`, `<type_traits>`, `"ajazz/core/transport.hpp"`) do not pull nlohmann into any device test TU that later includes the fixture (T-09-18 mitigation).

## Threat-register mitigations applied

| Threat ID | Mitigation                                                                                                                                                 |
| --------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| T-09-15   | `writes()` is `std::vector<std::vector<uint8_t>>` with no fixed capacity; each write copies bytes; smoke test asserts exact count + exact bytes            |
| T-09-16   | Fixture lives under `tests/unit/fixtures/`; production CMake targets (`ajazz_core`, `ajazz_devices_mouse`) do NOT compile or link any `tests/` path        |
| T-09-17   | All ITransport pure virtuals overridden with `override`; static_asserts on copy/move semantics; future ITransport extensions surface as compile error here |
| T-09-18   | mock_transport.hpp includes only `<chrono>`, `<cstddef>`, `<cstdint>`, `<queue>`, `<span>`, `<vector>`, `<algorithm>`, `<type_traits>`, transport.hpp      |

## Deviations from Plan

None — the plan executed exactly as written. Three minor adjustments worth noting (all match the plan's "if X then Y" branches, not actual deviations):

- The smoke test uses `d.model = "AJAZZ 2.4G 8K (test)"`, not `d.displayName` as the plan's sample code suggested. The actual `DeviceDescriptor` struct has a `model` field, not `displayName` — the plan's sample was a placeholder. Confirmed by grepping `src/core/include/ajazz/core/device.hpp:56`.
- Added a third TEST_CASE ("differentiates write and writeFeature in counts") beyond the plan's two — locks the writeFeatureCount() vs writeCount() contract that Phase 11 will rely on to distinguish wire-format paths.
- mouse.hpp gained a new `#include "ajazz/core/transport.hpp"` because `core::TransportPtr` is now part of the public mouse API surface (was previously a forward-decl-free transitive include). The plan called this out as a conditional ("only add if absent") — it was absent.

## Self-Check: PASSED

- tests/unit/fixtures/mock_transport.hpp: **FOUND** (178 LoC, header-only, no nlohmann)
- src/devices/mouse/include/ajazz/mouse/mouse.hpp: **MODIFIED** (factory declared)
- src/devices/mouse/src/aj_series.cpp: **MODIFIED** (factory defined)
- tests/unit/test_aj_series_mock_transport.cpp: **FOUND** (127 LoC, 3 TEST_CASEs)
- tests/unit/CMakeLists.txt: **MODIFIED** (test wired into ajazz_unit_tests)
- Commit 3f82ebf: **FOUND** (feat(test): add MockTransport fixture + AjSeriesMouse smoke (CAPTURE-04), 5 files, 355 insertions)
- ctest --preset linux-release: **181 / 181** passed (delta +3 from baseline 178)
- COD-031 gate: **PASSED** (grep returns 0 matches)
