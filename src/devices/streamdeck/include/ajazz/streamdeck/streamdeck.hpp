// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file streamdeck.hpp
 * @brief Public API of the AJAZZ stream deck backend module.
 *
 * Declares the module bootstrap function and the concrete device factories
 * for all supported AKP-family decks. Normal application code only calls
 * registerAll(); the factories are exposed separately so unit tests and
 * plugin consumers can instantiate specific backends without a real device.
 *
 * @see DeviceRegistry::registerDevice, makeAkp153, makeAkp03, makeAkp05
 */
#pragma once

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"

#include <memory>

namespace ajazz::core {
class DeviceRegistry;
}

namespace ajazz::streamdeck {

/**
 * @brief Register all stream deck backends with the given DeviceRegistry.
 *
 * Must be called once from `Application::bootstrap()` (or from the test
 * harness) before any device enumeration or open() call. Calling it more
 * than once is safe; DeviceRegistry silently skips duplicate VID/PID
 * registrations.
 *
 * @param registry Registry to populate (audit finding A1 replaced the
 *        implicit singleton lookup with constructor injection).
 */
void registerAll(core::DeviceRegistry& registry);

/**
 * @brief Factory for the AJAZZ AKP153 / Mirabox HSV293S backend.
 *
 * 15-key grid with 85×85 JPEG display per key. Implements IDisplayCapable
 * and IFirmwareCapable.
 *
 * @param d  Static descriptor from the registry.
 * @param id Runtime device identifier (VID/PID/serial).
 * @return Closed DevicePtr; call open() before I/O.
 */
[[nodiscard]] core::DevicePtr makeAkp153(core::DeviceDescriptor const& d, core::DeviceId id);

/**
 * @brief Factory for the AJAZZ AKP03 / Mirabox N3 backend.
 *
 * 6-key grid (72×72 PNG) plus one rotary encoder. Implements
 * IDisplayCapable and IEncoderCapable.
 *
 * @param d  Static descriptor from the registry.
 * @param id Runtime device identifier (VID/PID/serial).
 * @return Closed DevicePtr; call open() before I/O.
 */
[[nodiscard]] core::DevicePtr makeAkp03(core::DeviceDescriptor const& d, core::DeviceId id);

/**
 * @brief Factory for the AJAZZ AKP05 / AKP05E backend.
 *
 * 15-key grid (85×85 JPEG), 4 encoder LCDs, touch strip, and main LCD.
 * Implements IDisplayCapable and IEncoderCapable.
 *
 * @param d  Static descriptor from the registry.
 * @param id Runtime device identifier (VID/PID/serial).
 * @return Closed DevicePtr; call open() before I/O.
 */
[[nodiscard]] core::DevicePtr makeAkp05(core::DeviceDescriptor const& d, core::DeviceId id);

} // namespace ajazz::streamdeck
