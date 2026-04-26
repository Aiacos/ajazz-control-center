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
     * @brief Instantiate the backend for a given device ID.
     *
     * Looks up the factory by (vendorId, productId) and delegates
     * construction to it. The returned device is not yet open.
     *
     * @param id Runtime identifier of the device to open.
     * @return Newly constructed DevicePtr, or nullptr if no matching
     *         backend is registered.
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
};

} // namespace ajazz::core
