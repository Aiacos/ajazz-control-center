// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile_controller.cpp
 * @brief ProfileController stub implementation.
 *
 * loadProfile() currently emits profileChanged() unconditionally so QML
 * bindings can be tested end-to-end. saveProfile() is a no-op stub;
 * serialisation via ajazz::core::profileToJson() will be wired up once
 * the full profile I/O pipeline lands.
 */
#include "profile_controller.hpp"

namespace ajazz::app {

ProfileController::ProfileController(QObject* parent) : QObject(parent) {}

void ProfileController::loadProfile(QString const& /*path*/) {
    emit profileChanged();
}

void ProfileController::saveProfile(QString const& /*path*/) {
    // TODO: serialize current profile via ajazz::core::profileToJson.
}

} // namespace ajazz::app
