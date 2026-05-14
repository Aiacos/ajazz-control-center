---
decision: ARCH-02
title: HotplugMonitor mock-seam approach
status: Locked
locked: 2026-05-14
phase: 3
---

# ARCH-02: HotplugMonitor mock-seam approach

**Status:** Locked 2026-05-14

## Decision

Test-only `HotplugMonitor::injectEvent(HotplugEvent const&)` shim guarded by `#ifdef AJAZZ_TESTING`.

## Rationale

This is the cheapest seam that meets Phase 4's needs (multi-device integration tests + Windows CI smoke for `WM_DEVICECHANGE`). Promoting `HotplugMonitor::runImpl()` to `protected virtual` would add a virtual call per real hot-plug for one test-only override; extracting a full `IHotplugSource` interface would over-design for a single use case. The `injectEvent` shim costs ~10 LoC, is invisible to production builds, and mirrors the existing `FakeAsyncExecutor` precedent at `tests/unit/test_action_engine.cpp:119`.

## What this commits Phase 4 to

- `HotplugMonitor::injectEvent(HotplugEvent const&)` declared + implemented behind `#ifdef AJAZZ_TESTING` in `src/core/include/ajazz/core/hotplug_monitor.hpp` + `src/core/src/hotplug_monitor.cpp`.
- The shim synthesises a `HotplugEvent` into the same delivery pipeline as a real udev / `WM_DEVICECHANGE` / IOKit event — emits via the existing internal signal so all subscribers see it indistinguishably.
- The constructor-injectable `HidEnumerator = std::function<std::set<HidKey>()>` on `DeviceRegistry` (Phase 4 separate work) is the other half: it lets `MockHidEnumerator` drive the synthetic devices the injected events refer to.

## Long-term shape (not in this phase)

If a future requirement surfaces multiple `HotplugMonitor` implementations (e.g. a Bluetooth-side enumerator), promote `injectEvent` from test-only to a public `IHotplugSource` boundary. ARCH-02 records this as a possible v1.2+ evolution, not a current commitment.

## Alternatives rejected

- **`protected virtual runImpl()`** — adds a virtual call per real event for a test-only override. Over-engineering.
- **Full `IHotplugSource` interface extraction** — substantially more code change for a single use case in v1.1.

## References

- `.planning/research/ARCHITECTURE.md Q2` — HotplugMonitor mock-seam options (A/B/C).
- `tests/unit/test_action_engine.cpp:119` — `FakeAsyncExecutor` precedent for the same pattern.
- `.planning/REQUIREMENTS.md` ARCH-02 row + Phase 4 HOTPLUG-06.
