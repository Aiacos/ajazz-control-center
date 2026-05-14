// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mock_hid_enumerator.hpp
 * @brief Test helper: settable synthetic (vid, pid) set for DeviceRegistry.
 *
 * Pairs with `DeviceRegistry`'s constructor-injectable `HidEnumerator`
 * seam (ARCH-02 / Plan 04-02) so the multi-device integration harness
 * (HOTPLUG-06 / Plan 04-05) can drive "currently connected" without
 * touching real `::hid_enumerate(0, 0)`.
 *
 * Usage:
 *
 * @code
 *   MockHidEnumerator mock;
 *   mock.setKeys({{0x5548, 0x6672}, {0x5548, 0x6673}});
 *   ajazz::core::DeviceRegistry registry{mock.asEnumerator()};
 *   // registry.enumerateConnectedHidKeys() now returns the synthetic set.
 *
 *   // Later: simulate a disconnect of the second key
 *   mock.setKeys({{0x5548, 0x6672}});
 * @endcode
 */
#pragma once

#include "ajazz/core/device_registry.hpp"

#include <cstdint>
#include <set>
#include <utility>

namespace ajazz::tests {

/// Settable wrapper that adapts a `(vid, pid)` set into a
/// `DeviceRegistry::HidEnumerator` callable. The instance must outlive
/// every registry that captured its `asEnumerator()` callable (the
/// returned `std::function` captures `this` by reference).
class MockHidEnumerator {
public:
    /// Replace the synthetic "currently connected" set. Cheap; safe to
    /// call between `refresh()` calls to simulate hot-plug transitions.
    void setKeys(std::set<std::pair<std::uint16_t, std::uint16_t>> keys) {
        m_keys = std::move(keys);
    }

    /// Return an enumerator callable bound to this mock — pass it to
    /// `DeviceRegistry`'s constructor.
    [[nodiscard]] ajazz::core::DeviceRegistry::HidEnumerator asEnumerator() {
        return [this]() { return m_keys; };
    }

private:
    std::set<std::pair<std::uint16_t, std::uint16_t>> m_keys;
};

} // namespace ajazz::tests
