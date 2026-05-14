---
phase: 04-hot-plug-hardening
plan: 03
subsystem: app
tags: [hotplug, debounce, qtimer, qhash, application, D-05, HOTPLUG-05, Pitfall-3]

requires:
  - phase: 04-hot-plug-hardening
    provides: 04-01 (shared_ptr ownership migration — backends survive the 300ms debounce window without UAF)
provides:
  - HotplugDebouncer class — per-key 300ms trailing-edge coalescing
  - Application::onHotplug routes raw events through the debouncer
  - GUI-thread thread-safety via internal Qt::QueuedConnection marshalling
  - HotplugKey {vid, pid, serial} compound key with qHash ADL overload
affects:
  - 04-04 (DeviceModel diff-driven dataChanged — relies on coalesced events to avoid thrash)
  - 04-05 (multi-device test harness — will assert SC4 / debouncer coalescing mechanically)
  - Phase 5 (TimeSyncService — receives the same coalesced stream, downstream of debouncer)

tech-stack:
  added: []
  patterns:
    - Per-key QTimer trailing-edge restart on event
    - QObject thread-affinity + Qt::QueuedConnection marshalling for cross-thread observe()
    - qHash ADL overload for std::string-containing compound keys

key-files:
  created:
    - src/app/src/hotplug_debouncer.hpp
    - src/app/src/hotplug_debouncer.cpp
  modified:
    - src/app/CMakeLists.txt
    - src/app/src/application.hpp
    - src/app/src/application.cpp

key-decisions:
  - Compound key is (vid, pid, serial) with empty serial collapsing — D-04 acceptable per v1.1 'one backend per device class per process'
  - qHash ADL overload uses qHashBits over std::string bytes (Qt does not specialise for std::string)
  - 300ms pinned in source as static constexpr kDebounceMs (D-05 value)
  - Debouncer parented to Application (QObject on GUI thread) — QTimers fire on GUI thread, observe() marshals via Qt::QueuedConnection from any caller thread
  - Debouncer declared in Application AFTER m_hotplug — reverse-order implicit destruction tears down debouncer FIRST, cancelling pending timers before m_hotplug and well before m_deviceModel
  - On fire, emit the latest event for that key via m_pending.value() (the burst's final state); pass-by-value into the signal because Qt::QueuedConnection copies

patterns-established:
  - Trailing-edge debounce + per-key timer in C++/Qt6 (QHash<HotplugKey, QTimer*>)
  - Cross-thread Qt class with thread-confined internals via QMetaObject::invokeMethod(this, lambda, Qt::QueuedConnection)

requirements-completed: [HOTPLUG-05]

duration: 25 min
completed: 2026-05-14
---

# Phase 4 Plan 03: HotplugDebouncer (300ms Trailing-Edge Coalescing) Summary

**Added a per-key 300ms trailing-edge `HotplugDebouncer` (~70 LoC) that coalesces raw `HotplugEvent`s by `(vid, pid, serial)`. `Application::onHotplug` now routes through the debouncer instead of directly invoking `DeviceModel::refresh`, so a USB-hub yank, composite-USB Stream Dock arrival, or flaky-cable bounce collapses to one downstream event per stable transition.**

## Performance

- **Duration:** ~25 min
- **Started:** 2026-05-14T11:30Z
- **Completed:** 2026-05-14T11:33Z
- **Tasks:** 2 (combined into one production commit)
- **Files modified:** 5 (3 modified + 2 created)

## Accomplishments

- `HotplugDebouncer` class compiles into the app target. Public surface: `observe(HotplugEvent const&)` thread-safe entry, `coalesced(HotplugEvent)` Qt signal.
- Per-key `QTimer` model: each new event for an existing `(vid, pid, serial)` key restarts that key's timer; first event for a new key creates a fresh `QTimer(this)`. Trailing-edge fire emits the *latest* observed event (Arrived vs Removed final state typically reflects device's stable state when the burst ends).
- Per-key isolation verified by construction: distinct keys never share a timer (`QHash<HotplugKey, QTimer*>` direct lookup), so events for different devices never reset each other's windows.
- `kDebounceMs = 300` pinned as `static constexpr` per D-05 — no magic numbers.
- Thread-safety: `observe()` callable from the `HotplugMonitor` worker thread; internal `QMetaObject::invokeMethod(this, lambda, Qt::QueuedConnection)` marshals onto the debouncer's owning thread (GUI thread per Application wiring) before any `QTimer` manipulation. Avoids the `QObject::startTimer: Timers can only be used with threads started with QThread` assertion.
- `Application::onHotplug` body reduced to: log the raw event + `m_debouncer->observe(ev)`. The previous direct `QMetaObject::invokeMethod(m_deviceModel.get(), refresh, QueuedConnection)` is gone; the debouncer owns thread safety and emits `coalesced` to a connected lambda that calls `m_deviceModel->refresh()` on the GUI thread (auto-connection works because both QObjects live on the same thread).

## Task Commits

1. **Tasks 1 & 2 (combined):** `333b767` — feat(app): add HotplugDebouncer with 300ms trailing-edge coalescing

_Plan metadata commit follows below._

## Files Created/Modified

- `src/app/src/hotplug_debouncer.hpp` (new) — class declaration: `HotplugKey` compound key, `qHash` ADL overload, `observe()` + `coalesced` signal, private `QHash<HotplugKey, QTimer*>` + `QHash<HotplugKey, HotplugEvent>` members.
- `src/app/src/hotplug_debouncer.cpp` (new) — `observe()` body marshals onto owning thread; lambda creates/restarts the per-key timer; on fire, takes the pending event, deletes the timer (via `deleteLater`), removes both map entries, logs the coalesced event, emits `coalesced(pending)`.
- `src/app/CMakeLists.txt` — `src/hotplug_debouncer.cpp` added to the `qt_add_executable(ajazz-control-center …)` source list. AUTOMOC picks up the `Q_OBJECT` macro from the header automatically.
- `src/app/src/application.hpp` — forward-declares `class HotplugDebouncer;` in `ajazz::app`; new private member `std::unique_ptr<HotplugDebouncer> m_debouncer;` declared after `m_hotplug` (so reverse-order implicit destruction tears down debouncer before hot-plug source and well before `m_deviceModel`).
- `src/app/src/application.cpp` — `#include "hotplug_debouncer.hpp"`; ctor initializer list constructs `m_debouncer = std::make_unique<HotplugDebouncer>(this)` and connects `coalesced -> m_deviceModel.refresh()`. `onHotplug()` body simplified to log + `m_debouncer->observe(ev)`.

## Design Choices

### `HotplugKey` hash strategy

Chosen: combine `qHash(vid, seed) -> qHash(pid, seed) -> qHashBits(serial.data(), serial.size(), seed)`. Reasoning:

- `vid` and `pid` are `uint16_t` — `::qHash(uint16_t, seed)` is a Qt built-in.
- `std::string` has no Qt qHash specialisation (Qt covers `QString` / `QByteArray` only). `qHashBits` operates on raw bytes + length and accepts `std::string::data()` directly. This avoids constructing a `QByteArray` per hash invocation.
- Seed propagation through each combine step preserves Qt's per-process hash randomization.

### `HotplugEvent` copy-vs-move tradeoff in the signal/slot connection

Chosen: pass `coalesced` argument **by value** (not by `const&`). Reasoning:

- `Qt::QueuedConnection` and `Qt::AutoConnection` (when crossing threads) deep-copy signal arguments into the queued event. Pass-by-value matches the queue's semantic faithfully and avoids dangling-reference confusion if a future receiver lives on a different thread.
- The `HotplugEvent` payload is small (8-byte enum + 4 bytes packed VID/PID + a `std::string` for serial). Copy cost is negligible for an event that fires at most once per 300ms per key.
- Inside `observe()`'s lambda the captured event is also `[this, ev]() { ... }` (capture-by-value) for the same reason: the `HotplugEvent const&` parameter would be dangling by the time `Qt::QueuedConnection` delivery runs.

### Empty-serial collapsing edge case

Two distinct same-VID/PID units without serial strings hash to the same key (`HotplugKey{vid, pid, ""}`) and therefore share one debouncer timer. **This is acceptable per D-04** — the v1.1 contract is "one backend per device class per process". Surfacing two physical units as one logical row is consistent with the registry's flyweight cache (D-06) keyed by `(vid, pid)` only. If/when the v1.2 multi-instance feature lands (out of scope for v1.1 per the same D-04), the debouncer key will need to gain serial-disambiguation logic alongside the registry change.

### Destruction order rationale

Order in declaration: `... m_deviceModel ... m_hotplug, m_debouncer`. Reverse-order implicit destruction therefore destroys: `m_debouncer` -> `m_hotplug` -> `... m_deviceModel`. Combined with the existing explicit `Application::~Application()` body (`m_hotplug->setCallback({})`, `m_hotplug->stop()`, `removePostedEvents(m_deviceModel.get())`), this means:

1. Explicit body stops the hot-plug source so no new events arrive into the debouncer during teardown.
1. Implicit destruction starts; `m_debouncer` destructs first — its `QHash<HotplugKey, QTimer*>` members are torn down, the QTimers are deleted as children of the QObject (no pending fires).
1. `m_hotplug` destructs (already stopped, idempotent).
1. `m_deviceModel` destructs much later — the debouncer's connected lambda is already gone, so no risk of firing into a destroyed model.

## Verification Run

```
$ cmake --build --preset linux-release
[65/65] Linking CXX executable src/app/ajazz-control-center
(no errors, no undefined references)

$ test -f src/app/src/hotplug_debouncer.hpp && test -f src/app/src/hotplug_debouncer.cpp && echo PASS
PASS

$ grep -q "kDebounceMs = 300" src/app/src/hotplug_debouncer.hpp && echo PASS
PASS

$ grep -q "QHash<HotplugKey" src/app/src/hotplug_debouncer.hpp && echo PASS
PASS

$ grep -q "void coalesced" src/app/src/hotplug_debouncer.hpp && echo PASS
PASS

$ grep -q "Qt::QueuedConnection" src/app/src/hotplug_debouncer.cpp && echo PASS
PASS

$ grep -q "hotplug_debouncer" src/app/CMakeLists.txt && echo PASS
PASS

$ grep -c "QMetaObject::invokeMethod" src/app/src/application.cpp
0
```

## Issues Encountered

None. Build clean on first attempt. clang-format and cmake-format pre-commit hooks both passed without reformatting (the new files honoured the project style from the start).

## Deviations from Plan

None — plan executed exactly as written.

**Total deviations:** 0. **Impact:** none.

## Phase 4 / Phase 5 Readiness Note

The unit test deferred to Plan 04-05 will mechanically prove the SC4 / D-05 coalescing contract by firing 4 rapid synthetic events for the same key (via `HotplugMonitor::injectEvent` from Plan 04-02) and asserting exactly one `coalesced` emission. That harness is the load-bearing follow-up for this plan's behavioural claims; until it lands, the coalescing is verified only by code inspection + the build gate.

## Self-Check: PASSED
