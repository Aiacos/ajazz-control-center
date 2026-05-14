// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hotplug_debouncer.hpp
 * @brief 300ms trailing-edge debounce of `HotplugEvent`s by (vid, pid, serial).
 *
 * Coalesces redundant arrivals/removals on the same physical device so
 * downstream consumers (`DeviceModel::refresh`, future `TimeSyncService`)
 * see at most one event per stable transition. Closes Pitfall 3
 * (toast-flood / event-storm UX) at the application boundary.
 *
 * Per D-05 / HOTPLUG-05:
 *   - Per-key `QTimer` with `kDebounceMs = 300` interval.
 *   - Each new event for an existing key restarts that key's timer
 *     (trailing-edge: fire 300ms after the *last* event in the burst).
 *   - On fire, emit the most-recent event for that key — Arrived vs
 *     Removed final state typically reflects the device's stable state
 *     when the burst ends.
 *   - Per-key isolation: events for distinct (vid, pid, serial) keys
 *     never restart each other's timers.
 *
 * Threading: `observe()` is safe to call from any thread; it marshals
 * onto the debouncer's owning thread (always the GUI thread per the
 * Application wiring) via `Qt::QueuedConnection` before touching any
 * `QTimer`. Otherwise `QTimer::start()` would assert
 * "QObject::startTimer: Timers can only be used with threads started
 * with QThread".
 *
 * @see Application::onHotplug, .planning/phases/04-hot-plug-hardening/04-CONTEXT.md (D-05)
 */
#pragma once

#include "ajazz/core/hotplug_monitor.hpp"

#include <QHash>
#include <QObject>
#include <QTimer>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ajazz::app {

/**
 * @class HotplugDebouncer
 * @brief Per-key 300ms trailing-edge `HotplugEvent` coalescer.
 *
 * Lifetime: one instance per `Application`, parented to the Application
 * (a `QObject` on the GUI thread). The debouncer's `QTimer`s therefore
 * fire on the GUI thread regardless of where `observe()` was called from.
 */
class HotplugDebouncer : public QObject {
    Q_OBJECT

public:
    explicit HotplugDebouncer(QObject* parent = nullptr);
    ~HotplugDebouncer() override = default;

    HotplugDebouncer(HotplugDebouncer const&) = delete;
    HotplugDebouncer& operator=(HotplugDebouncer const&) = delete;
    HotplugDebouncer(HotplugDebouncer&&) = delete;
    HotplugDebouncer& operator=(HotplugDebouncer&&) = delete;

    /**
     * @brief Compound key identifying a unique physical device.
     *
     * Empty `serial` is acceptable per D-04: two distinct same-VID/PID
     * units without serial strings collapse onto the same key, which is
     * the v1.1 contract (one backend per device class per process).
     */
    struct HotplugKey {
        std::uint16_t vid{0};
        std::uint16_t pid{0};
        std::string serial;

        [[nodiscard]] bool operator==(HotplugKey const& other) const noexcept {
            return vid == other.vid && pid == other.pid && serial == other.serial;
        }
    };

    /**
     * @brief Observe a raw `HotplugEvent` from the monitor thread.
     *
     * Safe to call from any thread. Internally marshals onto the
     * debouncer's owning thread (GUI thread) via `Qt::QueuedConnection`
     * before manipulating QTimers. The `coalesced` signal is emitted
     * `kDebounceMs` after the most-recent event for the matching key.
     *
     * @param ev Raw hot-plug event to coalesce.
     */
    void observe(core::HotplugEvent const& ev);

    /// Debounce window in milliseconds — pinned at 300ms per D-05.
    static constexpr int kDebounceMs = 300;

signals:
    /**
     * @brief Emitted once per stable (vid, pid, serial) transition.
     *
     * Pass-by-value because Qt's signal queue copies arguments across
     * connection types (the only safe assumption for `Qt::QueuedConnection`
     * delivery).
     *
     * @param ev The most-recent raw event observed for this key.
     */
    void coalesced(ajazz::core::HotplugEvent ev);

private:
    QHash<HotplugKey, QTimer*> m_timers;             ///< Per-key trailing-edge timer.
    QHash<HotplugKey, core::HotplugEvent> m_pending; ///< Latest event per key.
};

/**
 * @brief Qt `QHash` ADL hash overload for `HotplugDebouncer::HotplugKey`.
 *
 * Combines `vid`, `pid` and the serial string bytes into a single
 * `size_t` hash. `qHashBits` covers the `std::string` payload (Qt does
 * not specialise `qHash` for `std::string` — it does for `QString` /
 * `QByteArray`).
 */
inline std::size_t qHash(HotplugDebouncer::HotplugKey const& key, std::size_t seed = 0) noexcept {
    seed = ::qHash(key.vid, seed);
    seed = ::qHash(key.pid, seed);
    seed = ::qHashBits(key.serial.data(), key.serial.size(), seed);
    return seed;
}

} // namespace ajazz::app
