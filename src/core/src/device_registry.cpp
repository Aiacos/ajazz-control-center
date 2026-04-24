// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/device_registry.hpp"

#include "ajazz/core/logger.hpp"

#include <algorithm>

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
            descriptor.vendorId, descriptor.productId);
        return;
    }
    AJAZZ_LOG_INFO("registry",
        "registered {} (VID={:04x} PID={:04x})",
        descriptor.model, descriptor.vendorId, descriptor.productId);
    m_entries.push_back({ std::move(descriptor), std::move(factory) });
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

DevicePtr DeviceRegistry::open(DeviceId const& id) const {
    std::lock_guard const lock(m_mutex);
    auto const it = std::ranges::find_if(m_entries, [&](Entry const& e) {
        return e.descriptor.vendorId == id.vendorId &&
               e.descriptor.productId == id.productId;
    });
    if (it == m_entries.end()) {
        AJAZZ_LOG_WARN("registry", "no backend for VID={:04x} PID={:04x}",
                       id.vendorId, id.productId);
        return nullptr;
    }
    return it->factory(it->descriptor, id);
}

}  // namespace ajazz::core
