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

#include "ajazz/core/logger.hpp"
#include "ajazz/core/profile.hpp"
#include "ajazz/core/profile_io.hpp"

#include <QDir>
#include <QFileInfo>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QString>

#include <exception>
#include <filesystem>
#include <limits>
#include <utility>

namespace ajazz::app {

namespace {

/// Pointer set by ProfileController::registerInstance, consumed by ::create.
ProfileController* s_profileControllerInstance = nullptr;

} // namespace

ProfileController* ProfileController::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_profileControllerInstance != nullptr,
               "ProfileController::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_profileControllerInstance, QQmlEngine::CppOwnership);
    return s_profileControllerInstance;
}

void ProfileController::registerInstance(ProfileController* instance) noexcept {
    s_profileControllerInstance = instance;
}

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

ajazz::core::Profile const& ProfileController::activeProfile() const noexcept {
    return m_profile;
}

// ---------------------------------------------------------------------------
// Phase 16-02 (PROFILE-01): default path + commit + active-profile save/load
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Sanitize a profile id to a safe filename component.
 *
 * Keeps only [A-Za-z0-9._-]; strips everything else (path separators, "..",
 * drive prefix, control chars). If the result is empty, returns "default".
 * Mirrors the threat-model mitigation for T-16b-01.
 */
QString sanitizeProfileId(QString const& profileId) {
    QString result;
    result.reserve(profileId.size());
    for (QChar const c : profileId) {
        ushort const u = c.unicode();
        bool const safe = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                          (u >= '0' && u <= '9') || u == '.' || u == '_' || u == '-';
        if (safe) {
            result += c;
        }
    }
    // Strip leading dots to prevent hidden-file/traversal remnants like ".."
    while (!result.isEmpty() && result[0] == QLatin1Char('.')) {
        result.remove(0, 1);
    }
    if (result.isEmpty()) {
        return QStringLiteral("default");
    }
    return result;
}

} // anonymous namespace

QString ProfileController::defaultProfilePath(QString const& profileId) const {
    QString const appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QString const safe = sanitizeProfileId(profileId);
    return appData + QStringLiteral("/profiles/") + safe + QStringLiteral(".json");
}

void ProfileController::commitKeyBinding(int keyIndex,
                                         QString const& iconPath,
                                         QString const& label,
                                         int actionKind,
                                         QString const& settingsJson) {
    // Validate keyIndex: must be in [0, 65534] (65535 is the uint16_t overflow sentinel).
    if (keyIndex < 0 ||
        keyIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitKeyBinding: keyIndex {} out of valid range [0, 65534], ignoring",
                       keyIndex);
        return;
    }
    // Validate actionKind: must map to a defined ActionKind value (0..BackToParent).
    constexpr int kMaxActionKind = static_cast<int>(ajazz::core::ActionKind::BackToParent);
    if (actionKind < 0 || actionKind > kMaxActionKind) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitKeyBinding: actionKind {} out of range [0, {}], ignoring",
                       actionKind,
                       kMaxActionKind);
        return;
    }

    auto const idx = static_cast<std::uint16_t>(keyIndex);
    auto& binding = m_profile.keys[idx];

    binding.state.imagePath =
        iconPath.isEmpty() ? std::nullopt : std::optional<std::string>{iconPath.toStdString()};

    binding.state.text =
        label.isEmpty() ? std::nullopt : std::optional<std::string>{label.toStdString()};

    ajazz::core::Action act{};
    act.kind = static_cast<ajazz::core::ActionKind>(actionKind);
    act.settingsJson = settingsJson.toStdString();
    binding.onPress = {std::move(act)};

    emit profileChanged();
}

void ProfileController::saveActiveProfile() {
    QString const id =
        m_profile.id.empty() ? QStringLiteral("default") : QString::fromStdString(m_profile.id);
    QString const path = defaultProfilePath(id);

    // Pitfall 5: parent directory must exist before writeProfileToDisk.
    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    saveProfile(path);
}

void ProfileController::loadActiveProfile() {
    QString const id =
        m_profile.id.empty() ? QStringLiteral("default") : QString::fromStdString(m_profile.id);
    loadProfile(defaultProfilePath(id));
}

void ProfileController::resetActiveProfile() {
    // Clear all binding maps; preserve identity fields (id, name, deviceCodename).
    m_profile.keys.clear();
    m_profile.encoders.clear();
    m_profile.mouseButtons.clear();
    // Keep pages intact; user may have folder navigation they did not author here.
    emit profileChanged();
    saveActiveProfile();
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
