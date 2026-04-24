// SPDX-License-Identifier: GPL-3.0-or-later
#include "profile_controller.hpp"

namespace ajazz::app {

ProfileController::ProfileController(QObject* parent) : QObject(parent) {}

void ProfileController::loadProfile(QString const& /*path*/) {
    emit profileChanged();
}

void ProfileController::saveProfile(QString const& /*path*/) {
    // TODO: serialize current profile via ajazz::core::profileToJson.
}

}  // namespace ajazz::app
