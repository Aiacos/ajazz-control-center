// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device_registry.cpp
 * @brief DeviceRegistry singleton implementation.
 *
 * Provides a process-wide thread-safe map from USB VID/PID pairs to device
 * backend factories. Duplicate registrations on the same (VID, PID) pair
 * are silently dropped after emitting a warning.
 */
#include "ajazz/core/device_registry.hpp"

#include "ajazz/core/logger.hpp"

#include <algorithm>

#include <hidapi.h>

namespace ajazz::core {

DeviceRegistry& DeviceRegistry::instance() {
    static DeviceRegistry sInstance;
    return sInstance;
}

void DeviceRegistry::registerDevice(DeviceDescriptor descriptor, DeviceFactory factory) {
    std::lock_guard const lock(m_mutex);
    auto const it = std::ranges::find_if(m_entries, [&](Entry const& e) {
        return e.descriptor.vendorId == descriptor.vendorId &&
               e.descriptor.productId == descriptor.productId;
    });
    if (it != m_entries.end()) {
        AJAZZ_LOG_WARN("registry",
                       "descriptor for VID={:04x} PID={:04x} already registered, skipping",
                       descriptor.vendorId,
                       descriptor.productId);
        return;
    }
    AJAZZ_LOG_INFO("registry",
                   "registered {} (VID={:04x} PID={:04x})",
                   descriptor.model,
                   descriptor.vendorId,
                   descriptor.productId);
    m_entries.push_back({std::move(descriptor), std::move(factory)});
}

std::vector<DeviceDescriptor> DeviceRegistry::enumerate() const {
    std::lock_guard const lock(m_mutex);
    std::vector<DeviceDescriptor> out;
    out.reserve(m_entries.size());
    for (auto const& entry : m_entries) {
        out.push_back(entry.descriptor);
    }
    return out;
}

std::set<std::pair<std::uint16_t, std::uint16_t>>
DeviceRegistry::enumerateConnectedHidKeys() const {
    // hidapi calls hid_init() transparently on first use; the call is
    // idempotent and reference-counted, so it costs nothing on repeat
    // invocations from refresh()/hot-plug paths.
    std::set<std::pair<std::uint16_t, std::uint16_t>> keys;
    if (hid_device_info* head = ::hid_enumerate(0, 0)) {
        for (auto const* p = head; p != nullptr; p = p->next) {
            keys.emplace(static_cast<std::uint16_t>(p->vendor_id),
                         static_cast<std::uint16_t>(p->product_id));
        }
        ::hid_free_enumeration(head);
    }
    return keys;
}

DevicePtr DeviceRegistry::open(DeviceId const& id) const {
    // Mutex hygiene (COD-004/005): never invoke user code (the factory) while
    // holding the registry mutex. Copy the descriptor + factory out under the
    // lock, then release before constructing the device. Factories can run
    // arbitrary I/O (HID open, hidraw ioctls) and must not block other
    // registry operations.
    DeviceDescriptor descriptor;
    DeviceFactory factory;
    {
        std::lock_guard const lock(m_mutex);
        auto const it = std::ranges::find_if(m_entries, [&](Entry const& e) {
            return e.descriptor.vendorId == id.vendorId && e.descriptor.productId == id.productId;
        });
        if (it == m_entries.end()) {
            AJAZZ_LOG_WARN(
                "registry", "no backend for VID={:04x} PID={:04x}", id.vendorId, id.productId);
            return nullptr;
        }
        descriptor = it->descriptor;
        factory = it->factory;
    }
    return factory(descriptor, id);
}

} // namespace ajazz::core
