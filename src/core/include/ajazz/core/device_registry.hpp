// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ajazz/core/device.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace ajazz::core {

/// Thread-safe registry of known device descriptors and factories.
/// Backend modules register themselves at static initialization or via
/// explicit bootstrap calls from main().
class DeviceRegistry {
public:
    static DeviceRegistry& instance();

    /// Register a supported device model. Idempotent on (vid, pid).
    void registerDevice(DeviceDescriptor descriptor, DeviceFactory factory);

    /// Enumerate currently plugged devices that match any registered descriptor.
    [[nodiscard]] std::vector<DeviceDescriptor> enumerate() const;

    /// Open a device by id. Returns nullptr if no matching backend is found
    /// or the device cannot be opened.
    [[nodiscard]] DevicePtr open(DeviceId const& id) const;

    DeviceRegistry(DeviceRegistry const&)            = delete;
    DeviceRegistry& operator=(DeviceRegistry const&) = delete;
    DeviceRegistry(DeviceRegistry&&)                 = delete;
    DeviceRegistry& operator=(DeviceRegistry&&)      = delete;

private:
    DeviceRegistry() = default;

    struct Entry {
        DeviceDescriptor descriptor;
        DeviceFactory    factory;
    };

    mutable std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

}  // namespace ajazz::core
