// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device_registry.hpp
 * @brief Registry that maps USB VID/PID pairs to device backends.
 *
 * Backend modules call registerDevice() (typically from a registerAll(registry)
 * bootstrap function invoked from Application::bootstrap()) to advertise the
 * models they support. The application layer then calls enumerate() and open()
 * to discover and instantiate connected devices.
 *
 * @note Audit finding A1 — the previous Meyers-singleton `instance()` was
 *       replaced with constructor-injected ownership so each Application
 *       (and each unit test) owns its own registry. A `[[deprecated]]`
 *       shim remains so any not-yet-migrated caller keeps compiling against
 *       a process-wide fallback registry; new code MUST take a
 *       `DeviceRegistry&` parameter explicitly.
 *
 * @see IDevice, DeviceFactory
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace ajazz::core {

/**
 * @brief Thread-safe registry of device descriptors and factories.
 *
 * Backend modules register themselves at startup via registerDevice().
 * All public methods are safe to call from multiple threads concurrently.
 *
 * The registry is now ownership-injected: `Application` holds one as a
 * data member and threads it through every `registerAll(DeviceRegistry&)`
 * call. Tests construct their own local instances.
 *
 * @see IDevice, DeviceFactory
 */
class DeviceRegistry {
public:
    DeviceRegistry() = default;

    /**
     * @brief Process-wide transition shim used by code that has not yet
     *        been migrated to constructor injection.
     *
     * @deprecated Audit finding A1 — replace every call site with an
     *             injected `DeviceRegistry&` parameter. The shim only
     *             exists so device backends keep compiling during the
     *             multi-PR migration; it returns a private function-local
     *             static instance that is shared across translation units.
     *
     * @return Reference to the legacy fallback registry.
     */
    [[deprecated("Use constructor injection — pass DeviceRegistry& explicitly. "
                 "This singleton is a transition shim (audit finding A1).")]] static DeviceRegistry&
    instance();

    /**
     * @brief Register a device model and its factory.
     *
     * Silently skips duplicate registrations (same VID+PID). Logs a
     * warning if a duplicate is detected.
     *
     * @param descriptor Static descriptor for the device model.
     * @param factory    Factory function that creates a backend instance.
     */
    void registerDevice(DeviceDescriptor descriptor, DeviceFactory factory);

    /**
     * @brief Return all registered device descriptors.
     *
     * Does not perform any USB enumeration; returns the full list of
     * descriptors that have been registered, connected or not.
     *
     * @return Copy of the descriptor list at the time of the call.
     */
    [[nodiscard]] std::vector<DeviceDescriptor> enumerate() const;

    /**
     * @brief Return the set of (vendorId, productId) pairs currently visible
     *        to hidapi (i.e. plugged in *and* user-readable).
     *
     * Walks `hid_enumerate(0, 0)` once. On Linux the result reflects the
     * /dev/hidraw* nodes the calling user can open (uaccess ACL); on Windows
     * and macOS it reflects every device the OS has enumerated. This is the
     * source of truth for the UI's online/offline indicator.
     *
     * @note Cheap (≈ms on a typical desktop) but not free; cache the result
     *       and only call it from refresh() / hot-plug paths.
     */
    [[nodiscard]] std::set<std::pair<std::uint16_t, std::uint16_t>>
    enumerateConnectedHidKeys() const;

    /**
     * @brief Instantiate (or return a cached) backend for a given device ID.
     *
     * Looks up the factory by (vendorId, productId). On a cache miss (or
     * after the previous shared_ptr for that key has been released) a fresh
     * backend is constructed via the registered factory; on a cache hit the
     * existing instance is returned directly.
     *
     * Flyweight semantics (D-06): if a previous shared_ptr returned by
     * `open(id)` for the same (vendorId, productId) is still alive
     * somewhere in the process, this call returns the **same** instance —
     * multiple consumers share one backend / one HID handle. This pre-empts
     * the Phase 5 multi-consumer Windows-exclusive-open bug and is the
     * load-bearing precondition for any consumer that captures a
     * `shared_ptr<IDevice>` across an event-loop turn.
     *
     * Cache eviction is **passive**: the cache slot is replaced when the
     * `weak_ptr` expires (i.e. the last shared_ptr drops). There is no
     * proactive invalidation on hot-plug Removed events — instead, the
     * underlying IDevice implementation honours the zombie contract from
     * `IDevice`'s class doc (return Result::DeviceGone or equivalent
     * sentinel after the USB device disappears).
     *
     * @param id Runtime identifier of the device to open.
     * @return Shared backend instance for `id`, or `nullptr` if no matching
     *         backend factory is registered (no toast / UI surface — log
     *         only, per D-02).
     * @see D-06 in 04-CONTEXT.md, ARCH-03 in 03-architectural-decisions/.
     */
    [[nodiscard]] DevicePtr open(DeviceId const& id) const;

    DeviceRegistry(DeviceRegistry const&) = delete;
    DeviceRegistry& operator=(DeviceRegistry const&) = delete;
    DeviceRegistry(DeviceRegistry&&) = delete;
    DeviceRegistry& operator=(DeviceRegistry&&) = delete;

private:
    /// Internal pairing of a descriptor with its backend factory.
    struct Entry {
        DeviceDescriptor descriptor; ///< Static device information.
        DeviceFactory factory;       ///< Factory used to instantiate the backend.
    };

    mutable std::mutex m_mutex;   ///< Guards m_entries for thread-safe access.
    std::vector<Entry> m_entries; ///< Registered backends, ordered by insertion.

    /// Separate mutex protecting `m_open_devices` (D-06).
    ///
    /// Kept distinct from `m_mutex` so a hot-plug-driven flurry of
    /// `open()` calls does not contend with `enumerate()` /
    /// `registerDevice()` traffic. Lock order: never hold both
    /// simultaneously — the cache lookup releases `m_open_mutex` before
    /// taking `m_mutex` for the descriptor/factory copy, then drops
    /// `m_mutex` before invoking the factory, then re-takes
    /// `m_open_mutex` only to store the resulting weak_ptr.
    mutable std::mutex m_open_mutex;

    /// Per-(vid, pid) flyweight cache of backend instances (D-06).
    ///
    /// Keyed by `(vendorId, productId)` — `serial` is intentionally not in
    /// the key because the v1.1 contract is "one backend per device class
    /// per process". The `weak_ptr` lets a backend be reclaimed naturally
    /// when the last consumer drops its `shared_ptr` (passive eviction —
    /// no proactive invalidation on hot-plug Removed).
    mutable std::map<std::pair<std::uint16_t, std::uint16_t>, std::weak_ptr<IDevice>>
        m_open_devices;
};

} // namespace ajazz::core
