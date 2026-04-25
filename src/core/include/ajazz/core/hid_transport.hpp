// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hid_transport.hpp
 * @brief Factory for the libhidapi-backed ITransport implementation.
 *
 * Provides the sole public entry point into HidTransport, a concrete
 * ITransport that wraps hid_open / hid_read_timeout / hid_write from
 * libhidapi. Callers should never depend on HidTransport directly;
 * depend only on the ITransport interface.
 *
 * @see ITransport, makeHidTransport
 */
#pragma once

#include "ajazz/core/transport.hpp"

#include <cstdint>
#include <string>

namespace ajazz::core {

/**
 * @brief Construct an HID transport for a USB device.
 *
 * On the first call this function initialises the global hidapi library
 * (hid_init()). Subsequent calls share the same initialisation via a
 * reference-counted atomic. The returned transport is in the closed state;
 * call ITransport::open() before performing I/O.
 *
 * @param vid    USB Vendor ID.
 * @param pid    USB Product ID.
 * @param serial Optional serial number string; empty means "any device"
 *               with matching VID/PID.
 *
 * @return Owning pointer to a closed ITransport backed by libhidapi.
 *
 * @throws std::runtime_error if hidapi itself cannot be initialised.
 */
[[nodiscard]] TransportPtr
makeHidTransport(std::uint16_t vid, std::uint16_t pid, std::string serial = {});

} // namespace ajazz::core
