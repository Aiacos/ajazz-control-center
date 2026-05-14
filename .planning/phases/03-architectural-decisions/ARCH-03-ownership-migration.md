---
decision: ARCH-03
title: DeviceRegistry slot ownership migration
status: Locked
locked: 2026-05-14
phase: 3
---

# ARCH-03: DeviceRegistry slot ownership migration

**Status:** Locked 2026-05-14

## Decision

`DeviceRegistry` slot ownership migrates from `std::unique_ptr<IDevice>` to `std::shared_ptr<IDevice>` **in Phase 4**, before Phase 5 (time-sync) wires the 300 ms debounced auto-sync.

## Acknowledgement

No code consumer of v1.1 outside Phase 4 holds an `IDevice*` across an event-loop turn until the migration ships. This is the load-bearing ordering constraint flagged in research `SUMMARY.md §1` (Executive Summary) and `PITFALLS.md §1` (Pitfall 1: use-after-free during disconnect-while-in-use). The time-sync 300 ms debounce is the first consumer that would trigger the UAF if ownership stays `unique_ptr`.

## What this commits Phase 4 to

- All `DeviceRegistry::registerDevice(std::unique_ptr<IDevice>)` call sites migrate to `std::shared_ptr<IDevice>` — touches every device backend factory (`register.cpp` in `src/devices/streamdeck`, `src/devices/keyboard`, `src/devices/mouse`).
- All `IDevice*` raw-pointer escapes from the registry (e.g. `DeviceRegistry::deviceFor(codename)`) return `std::shared_ptr<IDevice>` (or `std::weak_ptr<IDevice>` where appropriate).
- Existing callers that store `IDevice*` for any duration beyond a single function call are audited and migrated to `shared_ptr` capture.
- Hot-plug device removal (`reset()`) decrements the registry's `shared_ptr`; consumers holding their own `shared_ptr` keep the device alive until their last reference releases — eliminating the UAF window.

## Risk window during migration

The migration commit itself must be atomic — interleaved `unique_ptr` / `shared_ptr` in different TUs would break compilation. ARCH-03 commits to landing the migration as a **single coordinated commit** across `src/core/`, `src/devices/streamdeck/`, `src/devices/keyboard/`, `src/devices/mouse/`, and any `src/app/` consumer that holds a raw `IDevice*`.

## Alternatives rejected

- **Keep `unique_ptr` + add external lifetime guards** — re-introduces the UAF in any future code path that escapes a raw pointer; relies on reviewer vigilance rather than the type system. Rejected.
- **Defer to v1.2** — Phase 5 time-sync's 300 ms debounce would land on top of the latent UAF. Rejected; ordering before Phase 5 is the locked constraint.

## References

- `.planning/research/PITFALLS.md` Pitfall 1 — UAF risk driving the migration.
- `.planning/research/SUMMARY.md §1` — Executive Summary, ordering constraint flag.
- `.planning/REQUIREMENTS.md` ARCH-03 row + Phase 4 HOTPLUG-01.
