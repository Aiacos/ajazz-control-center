// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device_registry.cpp
 * @brief DeviceRegistry implementation.
 *
 * Provides a thread-safe map from USB VID/PID pairs to device backend
 * factories. Duplicate registrations on the same (VID, PID) pair are
 * silently dropped after emitting a warning.
 *
 * Audit finding A1: ownership now lives in `Application`. The legacy
 * `instance()` Meyers singleton is kept as a `[[deprecated]]` shim that
 * returns a process-wide fallback registry — call sites must be migrated
 * to constructor injection.
 */
#include "ajazz/core/device_registry.hpp"

#include "ajazz/core/logger.hpp"

#include <algorithm>
#include <exception>

#include <hidapi.h>

namespace ajazz::core {

// The shim is itself marked deprecated, so its own definition would trip
// -Wdeprecated-declarations on compilers that warn on definitions. Suppress
// locally; this is the only translation unit that should still touch it.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
DeviceRegistry& DeviceRegistry::instance() {
    static DeviceRegistry sInstance;
    return sInstance;
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

DeviceRegistry::DeviceRegistry(HidEnumerator enumerator) : m_enumerator(std::move(enumerator)) {}

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
    // ARCH-02 / HOTPLUG-06 test seam: if a custom HID enumerator was
    // injected at construction, defer to it and skip real hidapi entirely.
    // Production callers construct without an argument (m_enumerator is
    // default-constructed empty std::function) and hit the real walker
    // below.
    if (m_enumerator) {
        return m_enumerator();
    }

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

namespace {
// A device returned by open() must have its transport opened, otherwise every
// capability call (batteryPercent, setTime, setKeyImage, lighting, …) fails
// with "device is not open". Backends' open() is idempotent (no-op if already
// open) and best-effort here: an I/O failure is logged and the (unopened)
// device is still returned so the caller surfaces an honest failure (D-02)
// rather than the registry throwing across the lookup boundary.
void ensureTransportOpen(DevicePtr const& device) {
    if (!device) {
        return;
    }
    try {
        if (!device->isOpen()) {
            device->open();
        }
    } catch (std::exception const& e) {
        AJAZZ_LOG_WARN("registry", "open transport failed: {}", e.what());
    }
}
} // namespace

DevicePtr DeviceRegistry::open(DeviceId const& id) const {
    // Cache-aware flyweight per D-06: if a previous shared_ptr returned for
    // the same (vid, pid) is still alive, return that same instance —
    // multiple consumers share one backend / one HID handle. On miss /
    // expiry, fall through to the registered factory and store the result
    // back as a weak_ptr (passive eviction; no proactive invalidation).
    //
    // Lock-order rule (D-06):
    //   1. Take `m_open_mutex` for cache lookup; release before anything else.
    //   2. If miss, take `m_mutex` for descriptor/factory copy; release
    //      before invoking the factory (COD-004/005 mutex hygiene — factories
    //      run hidapi I/O and must not block enumerate()/registerDevice()).
    //   3. Re-take `m_open_mutex` only for the post-construction weak_ptr
    //      store. Never hold both mutexes simultaneously, never hold any
    //      mutex across the factory call.
    std::pair<std::uint16_t, std::uint16_t> const key{id.vendorId, id.productId};

    // Step 1: cache lookup.
    {
        std::lock_guard const cacheLock(m_open_mutex);
        auto const it = m_open_devices.find(key);
        if (it != m_open_devices.end()) {
            if (auto cached = it->second.lock()) {
                AJAZZ_LOG_INFO(
                    "registry", "cache hit for VID={:04x} PID={:04x}", id.vendorId, id.productId);
                ensureTransportOpen(cached);
                return cached;
            }
            // Expired — fall through; the slot will be overwritten below.
        }
    }

    // Step 2: descriptor/factory copy under m_mutex (lock released before
    // factory invocation per existing COD-004/005 hygiene).
    DeviceDescriptor descriptor;
    DeviceFactory factory;
    {
        std::lock_guard const lock(m_mutex);
        auto const it = std::ranges::find_if(m_entries, [&](Entry const& e) {
            return e.descriptor.vendorId == id.vendorId && e.descriptor.productId == id.productId;
        });
        if (it == m_entries.end()) {
            // Per D-02 (log-only error surface): the catalog may reference
            // VID/PIDs that have no registered backend yet (e.g. AKP815
            // before Phase 8 lands its registration). The device simply
            // doesn't appear in the sidebar because enumerate() never
            // returned a row for it. No toast, no UI surface.
            AJAZZ_LOG_WARN(
                "registry", "no backend for VID={:04x} PID={:04x}", id.vendorId, id.productId);
            return nullptr;
        }
        descriptor = it->descriptor;
        factory = it->factory;
    }

    // Step 3: invoke factory with no mutex held; then store the weak_ptr.
    AJAZZ_LOG_INFO("registry", "cache miss for VID={:04x} PID={:04x}", id.vendorId, id.productId);
    DevicePtr fresh = factory(descriptor, id);
    if (fresh) {
        {
            std::lock_guard const cacheLock(m_open_mutex);
            m_open_devices[key] = fresh; // implicit shared_ptr->weak_ptr.
        }
        ensureTransportOpen(fresh);
    }
    return fresh;
}

} // namespace ajazz::core
