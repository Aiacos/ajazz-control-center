// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_hotplug_harness.cpp
 * @brief Phase 4 multi-device integration test harness (HOTPLUG-06).
 *
 * Exercises the SC1/SC2/SC4 contracts of the Phase 4 hot-plug subsystem
 * mechanically, without touching real hidapi or a real OS hot-plug
 * source. Driven by the two ARCH-02 mock seams from Plan 04-02:
 *
 *   1. `DeviceRegistry`'s constructor-injectable `HidEnumerator`
 *      (`MockHidEnumerator` from `mock_hid_enumerator.hpp`) — controls
 *      what `enumerateConnectedHidKeys()` returns.
 *
 *   2. `HotplugMonitor::injectEvent(HotplugEvent const&)` (under
 *      `#ifdef AJAZZ_TESTING`) — synthesises events into the same
 *      `impl->snapshotCallback()` pipeline real OS events take.
 *
 * Coverage map (each TEST_CASE → ROADMAP success criterion):
 *
 *   - SECTION 1 "shared_ptr survives Removed (Pitfall 1 / SC1)"
 *   - SECTION 2 "weak_ptr cache returns same instance (D-06)"
 *   - SECTION 3 "300ms debounce coalesces same-key burst (HOTPLUG-05 / D-05 / SC4)"
 *   - SECTION 4 "Per-key isolation — 3 keys → 3 emissions, not 9"
 *   - SECTION 5 "DeviceModel::dataChanged({ConnectedRole}) one-row-only (D-03 / SC2)"
 *   - SECTION 6 "Disconnect-during-use scenario (HOTPLUG-07 narrative)"
 *
 * Lifecycle assumption: every TEST_CASE is independent — DeviceRegistry,
 * HotplugMonitor, HotplugDebouncer, DeviceModel are stack-allocated
 * inside the case so there is no inter-case state leak.
 */
#include "ajazz/core/device.hpp"
#include "ajazz/core/device_registry.hpp"
#include "ajazz/core/hotplug_monitor.hpp"

#include "device_model.hpp"
#include "hotplug_debouncer.hpp"
#include "mock_hid_enumerator.hpp"
#include "qt_app_fixture.hpp"

#include <QSignalSpy>
#include <QTest>

#include <atomic>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using ajazz::core::DeviceDescriptor;
using ajazz::core::DeviceFactory;
using ajazz::core::DeviceFamily;
using ajazz::core::DeviceId;
using ajazz::core::DevicePtr;
using ajazz::core::DeviceRegistry;
using ajazz::core::EventCallback;
using ajazz::core::HotplugAction;
using ajazz::core::HotplugEvent;
using ajazz::core::HotplugMonitor;
using ajazz::core::IDevice;

using ajazz::app::DeviceModel;
using ajazz::app::HotplugDebouncer;

using ajazz::tests::MockHidEnumerator;
using ajazz::tests::qtApp;

namespace {

/// Minimal IDevice stub for the harness — every method is a no-op or
/// returns trivial values. The `m_alive` flag implements the zombie
/// contract (D-06): once an external Removed event flips it, `poll()`
/// returns 0 instead of touching closed transport state.
class StubDevice : public IDevice {
public:
    StubDevice(DeviceDescriptor desc, DeviceId id) : m_descriptor(std::move(desc)), m_id(std::move(id)) {}

    [[nodiscard]] DeviceDescriptor const& descriptor() const noexcept override { return m_descriptor; }
    [[nodiscard]] DeviceId id() const noexcept override { return m_id; }
    [[nodiscard]] std::string firmwareVersion() const override { return "stub-1.0"; }

    void open() override { m_open = true; }
    void close() override { m_open = false; }
    [[nodiscard]] bool isOpen() const noexcept override { return m_open; }
    void onEvent(EventCallback cb) override { m_cb = std::move(cb); }
    std::size_t poll() override {
        // Zombie contract: skip transport touches once the device is gone.
        if (!m_alive.load(std::memory_order_acquire)) {
            return 0;
        }
        return 0;
    }

    /// Test-only: simulate the underlying USB device disappearing.
    void markGone() noexcept { m_alive.store(false, std::memory_order_release); }

private:
    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    bool m_open{false};
    EventCallback m_cb;
    std::atomic<bool> m_alive{true};
};

/// Build a tiny test backend descriptor + factory pair that yields
/// `StubDevice` instances. Codename + family are settable so SECTION 5
/// can register multiple distinct rows.
struct TestBackendSpec {
    std::uint16_t vid;
    std::uint16_t pid;
    std::string codename;
    DeviceFamily family;
    std::string model;
};

void registerTestBackend(DeviceRegistry& registry, TestBackendSpec const& spec) {
    DeviceDescriptor desc;
    desc.vendorId = spec.vid;
    desc.productId = spec.pid;
    desc.family = spec.family;
    desc.codename = spec.codename;
    desc.model = spec.model;
    registry.registerDevice(desc, [](DeviceDescriptor const& d, DeviceId id) -> DevicePtr {
        return std::make_shared<StubDevice>(d, std::move(id));
    });
}

} // namespace

// ----------------------------------------------------------------------------
// SECTION 1: shared_ptr survives Removed (Pitfall 1 / SC1).
//
// The 2026-05-12 hot-plug UAF (Pitfall 1) was: consumer holds a raw
// IDevice* into the registry slot; hot-plug Removed swapped the slot
// (under unique_ptr ownership); the consumer's pointer dangled. Plan
// 04-01's shared_ptr migration closes this — a consumer holding a
// shared_ptr keeps the backend alive past the Removed event. The
// IDevice's zombie contract (D-06) means subsequent method calls
// return safely without touching closed transport state.
// ----------------------------------------------------------------------------
TEST_CASE("SC1: shared_ptr to IDevice survives a Removed hot-plug event", "[hotplug][harness][SC1]") {
    qtApp();

    DeviceRegistry registry;
    registerTestBackend(registry, {0x5548, 0x6672, "akp03-stub", DeviceFamily::StreamDeck, "Stub AKP03"});

    DeviceId const id{0x5548, 0x6672, ""};
    DevicePtr dev = registry.open(id);
    REQUIRE(dev != nullptr);
    REQUIRE(dev.use_count() >= 1);

    // Synthesise a Removed event into the production dispatch pipeline.
    // No subscriber is registered on the HotplugMonitor here — we are
    // testing the ownership invariant, not the dispatch.
    HotplugMonitor mon;
    mon.injectEvent(HotplugEvent{HotplugAction::Removed, 0x5548, 0x6672, ""});

    // Plan 04-01 contract: the shared_ptr keeps the backend alive even
    // after the Removed event. Before the migration this would have
    // been a UAF (registry overwrote the slot, consumer's raw pointer
    // dangled).
    REQUIRE(dev != nullptr);
    REQUIRE_NOTHROW(dev->poll());
    REQUIRE_NOTHROW(dev->id());
}

// ----------------------------------------------------------------------------
// SECTION 2: weak_ptr cache returns the same instance (D-06).
//
// The flyweight cache means two consumers calling open() for the same
// (vid, pid) get the exact same backend instance / one HID handle.
// After both shared_ptrs drop, a fresh open() constructs a new
// instance (passive eviction).
// ----------------------------------------------------------------------------
TEST_CASE("SC1.5: weak_ptr cache returns the same instance for shared keys (D-06)", "[hotplug][harness][D-06]") {
    qtApp();

    DeviceRegistry registry;
    registerTestBackend(registry, {0x5548, 0x6672, "akp03-stub", DeviceFamily::StreamDeck, "Stub AKP03"});

    DeviceId const id{0x5548, 0x6672, ""};

    {
        DevicePtr a = registry.open(id);
        DevicePtr b = registry.open(id);
        REQUIRE(a != nullptr);
        REQUIRE(b != nullptr);
        REQUIRE(a.get() == b.get()); // Flyweight: same instance.
        REQUIRE(a.use_count() >= 2); // Both shared_ptrs share the slot.
    }
    // Both shared_ptrs dropped — passive eviction triggers on the next open().

    DevicePtr c = registry.open(id);
    REQUIRE(c != nullptr);
    REQUIRE(c.use_count() == 1); // Fresh instance after eviction.
}

// ----------------------------------------------------------------------------
// SECTION 3: 300ms debounce coalesces a same-key burst into one emission
// (HOTPLUG-05 / D-05 / SC4).
//
// A USB-hub yank, composite Stream Dock arrival, or flaky cable bounce
// can produce 2-4 raw HotplugEvents within tens of milliseconds. The
// debouncer collapses these into ONE emission per stable transition.
// ----------------------------------------------------------------------------
TEST_CASE("SC4: 300ms debounce coalesces 4 rapid same-key events into 1 emission", "[hotplug][harness][SC4][debouncer]") {
    qtApp();

    HotplugDebouncer debouncer;
    QSignalSpy spy(&debouncer, &HotplugDebouncer::coalesced);
    REQUIRE(spy.isValid());

    HotplugEvent const ev{HotplugAction::Arrived, 0x5548, 0x6672, "serial-A"};
    for (int i = 0; i < 4; ++i) {
        debouncer.observe(ev);
    }

    // Wait > kDebounceMs (300ms) for the trailing-edge timer to fire.
    spy.wait(HotplugDebouncer::kDebounceMs + 100);

    REQUIRE(spy.count() == 1); // Exactly one coalesced emission.
}

// ----------------------------------------------------------------------------
// SECTION 4: Per-key isolation — 3 distinct keys produce 3 emissions, not 9.
//
// Per-key timer isolation is what makes the debouncer correct under a
// device-shuffle scenario: distinct (vid, pid, serial) keys never reset
// each other's windows.
// ----------------------------------------------------------------------------
TEST_CASE("SC4.2: per-key isolation — 3 distinct keys produce 3 coalesced emissions", "[hotplug][harness][debouncer][isolation]") {
    qtApp();

    HotplugDebouncer debouncer;
    QSignalSpy spy(&debouncer, &HotplugDebouncer::coalesced);
    REQUIRE(spy.isValid());

    // 3 distinct keys, each fired 3 times in close succession (9 raw events,
    // alternating round-robin so timers overlap but cannot collide).
    HotplugEvent const evA{HotplugAction::Arrived, 0x5548, 0x6672, "serial-A"};
    HotplugEvent const evB{HotplugAction::Arrived, 0x5548, 0x6673, "serial-B"};
    HotplugEvent const evC{HotplugAction::Arrived, 0x5548, 0x6674, "serial-C"};
    for (int i = 0; i < 3; ++i) {
        debouncer.observe(evA);
        debouncer.observe(evB);
        debouncer.observe(evC);
    }

    spy.wait(HotplugDebouncer::kDebounceMs + 100);

    // Per-key isolation: 3 stable transitions → 3 emissions (NOT 9).
    REQUIRE(spy.count() == 3);
}

// ----------------------------------------------------------------------------
// SECTION 5: DeviceModel emits dataChanged({ConnectedRole}) for the affected
// row only (D-03 / SC2).
//
// The diff-driven refresh from Plan 04-04: when the connected-state of
// a single row flips, exactly ONE dataChanged(idx, idx, {ConnectedRole})
// is emitted on the affected row. The row count is unchanged. No
// modelReset is emitted.
// ----------------------------------------------------------------------------
TEST_CASE("SC2: DeviceModel emits exactly one dataChanged({ConnectedRole}) per row flip", "[hotplug][harness][SC2][device_model]") {
    qtApp();

    MockHidEnumerator mock;
    mock.setKeys({{0x5548, 0x6672}, {0x5548, 0x6673}});

    DeviceRegistry registry{mock.asEnumerator()};
    registerTestBackend(registry, {0x5548, 0x6672, "akp03-stub", DeviceFamily::StreamDeck, "Stub AKP03"});
    registerTestBackend(registry, {0x5548, 0x6673, "akp05-stub", DeviceFamily::StreamDeck, "Stub AKP05"});

    DeviceModel model{registry};
    model.refresh();

    // Two distinct codenames, both connected initially.
    REQUIRE(model.rowCount() == 2);

    QSignalSpy dataChangedSpy(&model, &DeviceModel::dataChanged);
    QSignalSpy resetSpy(&model, &DeviceModel::modelReset);
    REQUIRE(dataChangedSpy.isValid());
    REQUIRE(resetSpy.isValid());

    // Disconnect the second device — keep the first.
    mock.setKeys({{0x5548, 0x6672}});
    model.refresh();

    // SC2: exactly one dataChanged emission for the flipped row;
    // zero modelReset (D-03 — common path is not a reset).
    REQUIRE(dataChangedSpy.count() == 1);
    REQUIRE(resetSpy.count() == 0);

    // Row count unchanged — disconnected row stays in place (HOTPLUG-02).
    REQUIRE(model.rowCount() == 2);

    // The emitted dataChanged carries ConnectedRole in its role-mask.
    auto const args = dataChangedSpy.takeFirst();
    REQUIRE(args.size() == 3); // (topLeft, bottomRight, roles)
    auto const roles = args.at(2).value<QList<int>>();
    REQUIRE(roles.contains(static_cast<int>(DeviceModel::ConnectedRole)));
}

// ----------------------------------------------------------------------------
// SECTION 6: Disconnect-during-use scenario (the 2026-05-12/13 fix narrative —
// HOTPLUG-07 cross-link).
//
// Composite scenario: register a backend, open it, hold the shared_ptr,
// inject Removed, assert the shared_ptr still works for one more poll()
// without crash, drop the shared_ptr, assert the next open() constructs
// a fresh instance (cache miss after passive eviction).
// ----------------------------------------------------------------------------
TEST_CASE("SC1+D-06: disconnect-during-use composite (HOTPLUG-07 narrative)", "[hotplug][harness][HOTPLUG-07]") {
    qtApp();

    DeviceRegistry registry;
    registerTestBackend(registry, {0x5548, 0x6672, "akp03-stub", DeviceFamily::StreamDeck, "Stub AKP03"});

    DeviceId const id{0x5548, 0x6672, ""};
    HotplugMonitor mon;

    // Acquire the backend, hold the shared_ptr, simulate disconnect.
    {
        DevicePtr held = registry.open(id);
        REQUIRE(held != nullptr);
        IDevice* raw = held.get();

        // Mark gone (zombie contract) before injecting Removed — mirrors the
        // production sequence where the IDevice notices the underlying USB
        // device disappeared before the OS event reaches us.
        static_cast<StubDevice*>(raw)->markGone();
        mon.injectEvent(HotplugEvent{HotplugAction::Removed, 0x5548, 0x6672, ""});

        // SC1: shared_ptr keeps the backend alive; poll() returns safely.
        REQUIRE(held != nullptr);
        REQUIRE_NOTHROW(held->poll());
        REQUIRE(held.get() == raw);
    }
    // `held` dropped — weak_ptr in cache expires.

    // Next open() constructs a fresh instance (passive eviction).
    DevicePtr fresh = registry.open(id);
    REQUIRE(fresh != nullptr);
    REQUIRE(fresh.use_count() == 1); // Fresh, no other strong refs.
}
