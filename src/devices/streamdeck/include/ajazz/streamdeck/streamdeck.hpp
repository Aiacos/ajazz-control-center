// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"

#include <memory>

namespace ajazz::streamdeck {

/// Register every stream deck backend with the global DeviceRegistry.
/// Called once from `main()` (or from the test harness).
void registerAll();

/// Concrete factories exposed for tests and plugin consumers.
[[nodiscard]] core::DevicePtr makeAkp153(core::DeviceDescriptor const& d, core::DeviceId id);
[[nodiscard]] core::DevicePtr makeAkp03(core::DeviceDescriptor const& d, core::DeviceId id);
[[nodiscard]] core::DevicePtr makeAkp05(core::DeviceDescriptor const& d, core::DeviceId id);

} // namespace ajazz::streamdeck
