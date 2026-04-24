// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ajazz/core/transport.hpp"

#include <cstdint>
#include <string>

namespace ajazz::core {

/// Construct an HID transport for the given USB vendor/product and optional
/// serial number. The transport is returned closed; call `open()` on it
/// before use. Throws `std::runtime_error` if hidapi cannot be initialized.
[[nodiscard]] TransportPtr
makeHidTransport(std::uint16_t vid, std::uint16_t pid, std::string serial = {});

} // namespace ajazz::core
