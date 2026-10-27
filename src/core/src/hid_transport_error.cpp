// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hid_transport_error.cpp
 * @brief Portable HID transport error formatting.
 */
#include "hid_transport_error.hpp"

#include <cerrno>
#include <system_error>

namespace ajazz::core::detail {

std::string hidOpenErrorMessage(int errorNumber) {
    std::string message = "hid_open failed";
    if (errorNumber == 0) {
        return message;
    }

    message += ": ";
    message += std::error_code(errorNumber, std::generic_category()).message();
    if (errorNumber == EACCES) {
        message += " (check the installed AJAZZ udev rule and hidraw ACLs)";
    }
    return message;
}

} // namespace ajazz::core::detail
