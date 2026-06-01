// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file streamdeck.hpp
 * @brief Public API of the AJAZZ stream deck backend module.
 *
 * Declares the module bootstrap function, the AKP815 device factory, and the
 * descriptor list for the Stream Dock SKUs driven by the out-of-process
 * mirajazz sidecar. Normal application code calls registerAll() (AKP815) and
 * registers streamDockSidecarDescriptors() against the sidecar factory; the
 * AKP815 factory is exposed separately so unit tests can instantiate it
 * without a real device.
 *
 * @see DeviceRegistry::registerDevice, makeAkp815, streamDockSidecarDescriptors
 */
#pragma once

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/transport.hpp"

#include <memory>
#include <vector>

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
 * @brief Descriptors for every Stream Dock SKU driven by the mirajazz sidecar
 *        (AKP05/N4, AKP03/N3, AKP153/HSV293S). AKP815 is excluded — it is not a
 *        mirajazz device and stays on its C++ backend via @ref registerAll.
 *
 * The application registers these against its out-of-process sidecar factory
 * (`makeSidecarStreamDock`), so the mirajazz backend — not the in-tree C++ wire
 * code — drives these devices. Geometry is literal in the implementation, so
 * the list is independent of the C++ protocol headers.
 *
 * @return One DeviceDescriptor per supported (vendorId, productId).
 */
[[nodiscard]] std::vector<core::DeviceDescriptor> streamDockSidecarDescriptors();

/**
 * @brief Factory for the AJAZZ AKP815 backend.
 *
 * 15-key grid (5 rows × 3 columns) with 100×100 JPEG-encoded keys
 * (`Rot180`, no mirror) and an 854×480 LCD strip. Drives the family v1-API
 * framing via its own `akp815_wire.hpp` builders, with a different
 * `key_image_format` per `[ajazz-sdk]/info.rs::Kind::Akp815`.
 *
 * Implements IDisplayCapable and IFirmwareCapable.
 *
 * @param d  Static descriptor from the registry.
 * @param id Runtime device identifier (VID/PID/serial).
 * @return Closed DevicePtr (shared_ptr alias per ARCH-03); call open()
 *         before I/O. The DeviceRegistry's flyweight cache (D-06) will
 *         hand the same instance to subsequent open(id) calls for the
 *         same (vendorId, productId) until the last shared_ptr drops.
 */
[[nodiscard]] core::DevicePtr makeAkp815(core::DeviceDescriptor const& d, core::DeviceId id);

/**
 * @brief Test-only factory: construct an AKP815 device with an injected
 *        @c ITransport (COD-026 DI seam).
 *
 * Production code uses @ref makeAkp815 above; tests use this overload to
 * substitute a mock that records every write for byte-level wire-format
 * assertions (CAPTURE-04 pattern, COD-026 DI seam).
 */
[[nodiscard]] core::DevicePtr makeAkp815WithTransport(core::DeviceDescriptor const& d,
                                                      core::DeviceId id,
                                                      core::TransportPtr transport);

} // namespace ajazz::streamdeck
