// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ajazz/core/device.hpp"

namespace ajazz::mouse {

void registerAll();

/// AJAZZ AJ-series gaming mice (AJ159, AJ199, AJ339, AJ380 ...). They share
/// a common configuration endpoint (HID interface #1, keyboard-class) that
/// accepts the same command envelope with model-specific payloads.
[[nodiscard]] core::DevicePtr makeAjSeries(core::DeviceDescriptor const& d, core::DeviceId id);

}  // namespace ajazz::mouse
