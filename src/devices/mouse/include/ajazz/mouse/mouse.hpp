// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mouse.hpp
 * @brief Public factory and registration entry-point for AJAZZ mouse backends.
 *
 * Currently a single backend (AJ-series) covers all supported gaming mice via
 * a shared command envelope documented in docs/protocols/mouse/aj_series.md.
 */
#pragma once

#include "ajazz/core/device.hpp"
#include "ajazz/core/transport.hpp"

namespace ajazz::core {
class DeviceRegistry;
}

namespace ajazz::mouse {

/**
 * @brief Register all supported mouse devices with the given DeviceRegistry.
 *
 * Inserts VID/PID descriptors for every known AJ-series model.
 * Safe to call multiple times.
 *
 * @param registry Registry to populate (audit finding A1 replaced the
 *        implicit singleton lookup with constructor injection).
 */
void registerAll(core::DeviceRegistry& registry);

/**
 * @brief Create an AJ-series gaming mouse backend.
 *
 * Targets the AJ159, AJ199, AJ339 Pro, AJ380, and compatible models.
 * All share the same 64-byte feature-report command envelope on HID
 * interface #1 (keyboard-class). See docs/protocols/mouse/aj_series.md
 * for the byte-level wire format and checksum algorithm.
 *
 * @param d   Device descriptor (model name, VID/PID, …).
 * @param id  Runtime identity including the USB serial string.
 * @return    Heap-allocated IDevice implementing IMouseCapable and IRgbCapable.
 *            Returned via DevicePtr (shared_ptr alias per ARCH-03); the
 *            DeviceRegistry flyweight cache (D-06) shares the same
 *            instance across consumers.
 */
[[nodiscard]] core::DevicePtr makeAjSeries(core::DeviceDescriptor const& d, core::DeviceId id);

/**
 * @brief Test-only factory: construct an AJ-series mouse with an injected
 *        @c ITransport (typically a `MockTransport` from
 *        `tests/unit/fixtures/mock_transport.hpp`).
 *
 * Parallels the COD-026 DI test constructor on the anonymous-namespace
 * `AjSeriesMouse` class inside `aj_series.cpp`, exposing it across
 * translation-unit boundaries. Production code uses `makeAjSeries()` above,
 * which builds a real HID transport from `(vendorId, productId, serial)`;
 * tests use this overload to substitute a mock that records every write
 * for byte-level wire-format assertions.
 *
 * @param d         Static model descriptor from the device registry.
 * @param id        Runtime USB identifier of the specific device.
 * @param transport Owned `ITransport` implementation. Ownership transfers
 *                  to the returned device.
 * @return          Heap-allocated `IDevice` implementing `IMouseCapable`
 *                  and `IRgbCapable`. Identical surface to `makeAjSeries()`.
 *
 * @see CAPTURE-04 (.planning/phases/09-research-captures-hygiene/09-04-PLAN.md)
 * @see ajazz::tests::MockTransport
 */
[[nodiscard]] core::DevicePtr makeAjSeriesWithTransport(core::DeviceDescriptor const& d,
                                                        core::DeviceId id,
                                                        core::TransportPtr transport);

} // namespace ajazz::mouse
