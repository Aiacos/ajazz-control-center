// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hid_transport_error.hpp
 * @brief Internal helpers for HID transport error messages.
 */
#pragma once

#include <string>

namespace ajazz::core::detail {

/**
 * @brief Format a failed HID open error with platform-independent text.
 * @param errorNumber Native error code captured when HID open failed.
 * @return Human-readable error message, including the Linux permission hint.
 */
[[nodiscard]] std::string hidOpenErrorMessage(int errorNumber);

} // namespace ajazz::core::detail
