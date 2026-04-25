// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile_controller.cpp
 * @brief Bridge between QML and the core profile I/O layer.
 *
 * Persistence goes through ajazz::core::readProfileFromDisk /
 * writeProfileToDisk, which provide atomic, fsync-safe writes. Errors are
 * surfaced to QML via the loadFailed() / saveFailed() signals so the GUI
 * can show a user-friendly toast without inspecting the exception type.
 */
#include "profile_controller.hpp"

#include "ajazz/core/profile.hpp"
#include "ajazz/core/profile_io.hpp"

#include <QFileInfo>
#include <QString>

#include <exception>
#include <filesystem>
#include <utility>

namespace ajazz::app {

ProfileController::ProfileController(QObject* parent) : QObject(parent) {}

void ProfileController::loadProfile(QString const& path) {
    try {
        std::filesystem::path const fsPath = path.toStdString();
        m_profile = ajazz::core::readProfileFromDisk(fsPath);
        m_path = path;
        emit profileChanged();
    } catch (ajazz::core::ProfileIoError const& e) {
        emit loadFailed(QString::fromUtf8(e.what()));
    } catch (std::exception const& e) {
        emit loadFailed(QString::fromUtf8(e.what()));
    }
}

void ProfileController::saveProfile(QString const& path) {
    try {
        std::filesystem::path const fsPath = path.toStdString();
        ajazz::core::writeProfileToDisk(fsPath, m_profile);
        m_path = path;
        emit profileSaved(path);
        emit profilesChanged();
    } catch (ajazz::core::ProfileIoError const& e) {
        emit saveFailed(QString::fromUtf8(e.what()));
    } catch (std::exception const& e) {
        emit saveFailed(QString::fromUtf8(e.what()));
    }
}

QStringList ProfileController::knownProfileIds() const {
    QStringList ids;
    if (!m_profile.id.empty()) {
        ids << QString::fromStdString(m_profile.id);
    }
    return ids;
}

QString ProfileController::profileNameFor(QString const& profileId) const {
    if (QString::fromStdString(m_profile.id) == profileId) {
        return QString::fromStdString(m_profile.name);
    }
    return {};
}

void ProfileController::loadProfileById(QString const& profileId) {
    if (profileId.isEmpty()) {
        emit loadFailed(tr("Empty profile id"));
        return;
    }
    if (QString::fromStdString(m_profile.id) == profileId) {
        // Already active: nothing to do, but re-emit so QML refreshes bindings.
        emit profileChanged();
        return;
    }
    // The id\:path index is not yet maintained; the tray submenu currently
    // exposes only the active profile (see issue #24). Surface a clear
    // message so the UI can prompt the user to use the file picker.
    emit loadFailed(tr("Profile '%1' is not in the in-memory library; "
                       "open it from the Profiles page.")
                        .arg(profileId));
}

} // namespace ajazz::app
