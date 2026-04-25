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

namespace ajazz::mouse {

/**
 * @brief Register all supported mouse devices with the global DeviceRegistry.
 *
 * Inserts VID/PID descriptors for every known AJ-series model.
 * Safe to call multiple times.
 */
void registerAll();

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
 */
[[nodiscard]] core::DevicePtr makeAjSeries(core::DeviceDescriptor const& d, core::DeviceId id);

} // namespace ajazz::mouse
