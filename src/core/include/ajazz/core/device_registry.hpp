// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device_registry.hpp
 * @brief Global registry that maps USB VID/PID pairs to device backends.
 *
 * Backend modules call registerDevice() (typically from a registerAll()
 * bootstrap function invoked in main()) to advertise the models they
 * support. The application layer then calls enumerate() and open() to
 * discover and instantiate connected devices.
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
 * @brief Thread-safe singleton registry of device descriptors and factories.
 *
 * Backend modules register themselves at startup via registerDevice().
 * All public methods are safe to call from multiple threads concurrently.
 *
 * @note The singleton is initialised lazily on first call to instance().
 * @see IDevice, DeviceFactory
 */
class DeviceRegistry {
public:
    /**
     * @brief Return the process-wide singleton instance.
     * @return Reference to the global DeviceRegistry.
     */
    static DeviceRegistry& instance();

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
    DeviceRegistry() = default;

    /// Internal pairing of a descriptor with its backend factory.
    struct Entry {
        DeviceDescriptor descriptor; ///< Static device information.
        DeviceFactory factory;       ///< Factory used to instantiate the backend.
    };

    mutable std::mutex m_mutex;   ///< Guards m_entries for thread-safe access.
    std::vector<Entry> m_entries; ///< Registered backends, ordered by insertion.
};

} // namespace ajazz::core
