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
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QString>
#include <QUuid>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
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

ProfileController::ProfileController(QObject* parent) : QObject(parent) {
    rescanLibrary();
}

void ProfileController::loadProfile(QString const& path) {
    try {
        std::filesystem::path const fsPath = path.toStdString();
        m_profile = ajazz::core::readProfileFromDisk(fsPath);
        m_path = path;
        resetPageNav(); // Delta A: a freshly-loaded profile always opens at root.
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
        // WR-04: profilesChanged() is NOT emitted here. saveProfile() is a low-level
        // write that does not alter the list of known profiles -- it only mutates the
        // content of an existing entry. Callers that introduce a new profile id into
        // the library (e.g. saveActiveProfile on first save) emit profilesChanged()
        // themselves after verifying that the id is actually new.
    } catch (ajazz::core::ProfileIoError const& e) {
        emit saveFailed(QString::fromUtf8(e.what()));
    } catch (std::exception const& e) {
        emit saveFailed(QString::fromUtf8(e.what()));
    }
}

QStringList ProfileController::knownProfileIds() const {
    QStringList ids = m_library.keys();
    // The active profile may be brand new (created but the library not yet
    // rescanned in this const path); make sure it is always listed.
    QString const activeId = QString::fromStdString(m_profile.id);
    if (!activeId.isEmpty() && !ids.contains(activeId)) {
        ids << activeId;
    }
    ids.sort();
    return ids;
}

QString ProfileController::profileNameFor(QString const& profileId) const {
    auto const it = m_library.find(profileId);
    if (it != m_library.end()) {
        return it->name;
    }
    if (QString::fromStdString(m_profile.id) == profileId) {
        return QString::fromStdString(m_profile.name);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Multi-profile library (Workstream D)
// ---------------------------------------------------------------------------

QString ProfileController::profilesDir() const {
    QString const appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appData + QStringLiteral("/profiles");
}

void ProfileController::rescanLibrary() {
    m_library.clear();
    QDir const dir(profilesDir());
    if (!dir.exists()) {
        return;
    }
    QStringList const files =
        dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files | QDir::Readable);
    for (QString const& file : files) {
        QString const path = dir.filePath(file);
        try {
            ajazz::core::Profile const p =
                ajazz::core::readProfileFromDisk(std::filesystem::path{path.toStdString()});
            ProfileMeta meta;
            // Legacy files may carry an empty id; key off the filename stem so
            // they still appear and remain loadable (the stem == sanitized id).
            QString const stem = QFileInfo(file).completeBaseName();
            meta.id = p.id.empty() ? stem : QString::fromStdString(p.id);
            meta.name = p.name.empty() ? meta.id : QString::fromStdString(p.name);
            meta.deviceCodename = QString::fromStdString(p.deviceCodename);
            meta.path = path;
            m_library.insert(meta.id, meta);
        } catch (std::exception const&) {
            // Skip unreadable / malformed profile files rather than aborting the
            // whole scan; a corrupt file must not hide the rest of the library.
            continue;
        }
    }
}

void ProfileController::refreshProfileLibrary() {
    rescanLibrary();
    emit profilesChanged();
}

QVariantList ProfileController::profilesForDevice(QString const& deviceCodename) const {
    std::vector<ProfileMeta> matches;
    for (auto const& meta : m_library) {
        if (deviceCodename.isEmpty() || meta.deviceCodename == deviceCodename) {
            matches.push_back(meta);
        }
    }
    std::sort(matches.begin(), matches.end(), [](ProfileMeta const& a, ProfileMeta const& b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });

    QVariantList out;
    for (auto const& meta : matches) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), meta.id);
        m.insert(QStringLiteral("name"), meta.name);
        out.append(m);
    }
    return out;
}

QString ProfileController::activeProfileId() const {
    return QString::fromStdString(m_profile.id);
}

QString ProfileController::activeProfileName() const {
    return QString::fromStdString(m_profile.name);
}

QString ProfileController::createProfile(QString const& name, QString const& deviceCodename) {
    ajazz::core::Profile fresh{};
    fresh.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    fresh.name = name.isEmpty() ? std::string{"Profile"} : name.toStdString();
    fresh.deviceCodename = deviceCodename.toStdString();

    m_profile = std::move(fresh);
    m_path.clear(); // force saveActiveProfile to treat this as a new id
    resetPageNav(); // Delta A: open the new profile at root.
    emit profileChanged();

    saveActiveProfile(); // persists + emits profileSaved/profilesChanged on new id
    rescanLibrary();
    emit profilesChanged();
    return QString::fromStdString(m_profile.id);
}

void ProfileController::renameActiveProfile(QString const& newName) {
    if (newName.isEmpty()) {
        return;
    }
    m_profile.name = newName.toStdString();
    emit profileChanged();
    saveActiveProfile();
    rescanLibrary();
    emit profilesChanged();
}

void ProfileController::deleteProfile(QString const& profileId) {
    auto const it = m_library.find(profileId);
    QString const path = (it != m_library.end()) ? it->path : defaultProfilePath(profileId);

    QFile file(path);
    if (file.exists() && !file.remove()) {
        emit saveFailed(tr("Could not delete profile file: %1").arg(path));
        return;
    }

    bool const wasActive = (QString::fromStdString(m_profile.id) == profileId);
    QString const deviceCodename = (it != m_library.end())
                                       ? it->deviceCodename
                                       : QString::fromStdString(m_profile.deviceCodename);

    rescanLibrary();
    emit profilesChanged();

    if (wasActive) {
        // Drop the just-deleted profile from memory first, otherwise
        // activateDeviceProfile() sees its stale deviceCodename still matching
        // and keeps the deleted profile active. With an empty active profile it
        // loads another profile for the device, or creates a fresh default.
        m_profile = ajazz::core::Profile{};
        m_path.clear();
        activateDeviceProfile(deviceCodename);
    }
}

QString ProfileController::duplicateProfile(QString const& profileId, QString const& newName) {
    // Resolve the source profile: an explicit id from the library, else the
    // active profile.
    ajazz::core::Profile source;
    if (!profileId.isEmpty()) {
        auto const it = m_library.find(profileId);
        if (it == m_library.end()) {
            emit loadFailed(tr("Profile '%1' not found").arg(profileId));
            return {};
        }
        try {
            source =
                ajazz::core::readProfileFromDisk(std::filesystem::path{it->path.toStdString()});
        } catch (std::exception const& e) {
            emit loadFailed(QString::fromUtf8(e.what()));
            return {};
        }
    } else {
        source = m_profile;
    }

    source.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    source.name = newName.isEmpty() ? (source.name + " copy") : newName.toStdString();

    m_profile = std::move(source);
    m_path.clear();
    resetPageNav(); // Delta A: open the duplicated profile at root.
    emit profileChanged();
    saveActiveProfile();
    rescanLibrary();
    emit profilesChanged();
    return QString::fromStdString(m_profile.id);
}

void ProfileController::activateDeviceProfile(QString const& deviceCodename) {
    rescanLibrary();

    // Already editing a profile for this device? Keep it.
    if (!m_profile.id.empty() &&
        QString::fromStdString(m_profile.deviceCodename) == deviceCodename) {
        return;
    }

    // Load the first known profile for this device (sorted by name).
    QVariantList const forDevice = profilesForDevice(deviceCodename);
    if (!forDevice.isEmpty()) {
        QString const id = forDevice.first().toMap().value(QStringLiteral("id")).toString();
        loadProfileById(id);
        return;
    }

    // None yet: create a default device-scoped profile.
    createProfile(tr("Default"), deviceCodename);
}

// ---------------------------------------------------------------------------
// Phase 34-04 (APROF-02): per-app profile auto-switch resolution
// ---------------------------------------------------------------------------

bool appIdMatchesHints(QString const& appId, QStringList const& hints) {
    if (appId.isEmpty() || hints.isEmpty()) {
        return false;
    }
    for (QString const& hint : hints) {
        if (!hint.isEmpty() && hint.compare(appId, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString resolveSwitchToProfileToken(ProfileController const& ctrl,
                                    QString const& profileToken,
                                    QString const& deviceToken) {
    if (profileToken.trimmed().isEmpty()) {
        return {};
    }
    // Exact profile-id match wins.
    if (ctrl.knownProfileIds().contains(profileToken)) {
        return profileToken;
    }
    // Fall back to a name-or-id match scoped to the device.
    QVariantList const candidates = ctrl.profilesForDevice(deviceToken);
    for (auto const& v : candidates) {
        auto const m = v.toMap();
        if (m.value(QStringLiteral("name")).toString() == profileToken ||
            m.value(QStringLiteral("id")).toString() == profileToken) {
            return m.value(QStringLiteral("id")).toString();
        }
    }
    return {};
}

QString ProfileController::resolveProfileForApp(QString const& appId,
                                                QString const& deviceCodename,
                                                bool allowDefaultFallback) const {
    // Read directly off disk: m_library is a lightweight {id,name,device,path}
    // index that does NOT carry applicationHints, so each candidate profile is
    // read to inspect its hints. resolveProfileForApp is invoked on a debounced
    // (~180ms) foreground change, not per frame, so the per-call reads are cheap
    // relative to the switch they gate. const: never mutates the active profile.
    QDir const dir(profilesDir());
    if (!dir.exists()) {
        return {};
    }
    QStringList const files =
        dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files | QDir::Readable);

    QString fallbackId; // default profile for the device (first by name)
    QString fallbackName;
    for (QString const& file : files) {
        QString const path = dir.filePath(file);
        ajazz::core::Profile p;
        try {
            p = ajazz::core::readProfileFromDisk(std::filesystem::path{path.toStdString()});
        } catch (std::exception const&) {
            continue; // skip malformed files (mirrors rescanLibrary)
        }
        if (!deviceCodename.isEmpty() &&
            QString::fromStdString(p.deviceCodename) != deviceCodename) {
            continue; // out of device scope
        }

        QString const stem = QFileInfo(file).completeBaseName();
        QString const id = p.id.empty() ? stem : QString::fromStdString(p.id);

        // Direct applicationHints match wins immediately.
        QStringList hints;
        hints.reserve(static_cast<qsizetype>(p.applicationHints.size()));
        for (auto const& h : p.applicationHints) {
            hints.append(QString::fromStdString(h));
        }
        if (appIdMatchesHints(appId, hints)) {
            return id;
        }

        // Track the default (first by name) for the fallback.
        QString const name = p.name.empty() ? id : QString::fromStdString(p.name);
        if (fallbackId.isEmpty() || name.localeAwareCompare(fallbackName) < 0) {
            fallbackId = id;
            fallbackName = name;
        }
    }

    // No hint matched. WR-01: the focus-driven auto-switch caller passes
    // allowDefaultFallback=false, so an unmapped foreground change resolves to
    // "" -> the Application idempotent guard no-ops -> the user's current
    // (possibly manually-chosen) profile is preserved. This is what the
    // SettingsPage copy promises ("Without a mapping, your active profile stays
    // put"). The LOCKED 34-CONTEXT default-profile fallback is retained for
    // callers that opt in (allowDefaultFallback=true, e.g. device-connect
    // semantics): they still get the device default (first by name), else the
    // active profile.
    if (!allowDefaultFallback) {
        return {};
    }
    if (!fallbackId.isEmpty()) {
        return fallbackId;
    }
    return QString::fromStdString(m_profile.id);
}

QStringList ProfileController::applicationHints() const {
    QStringList out;
    out.reserve(static_cast<qsizetype>(m_profile.applicationHints.size()));
    for (auto const& h : m_profile.applicationHints) {
        out.append(QString::fromStdString(h));
    }
    return out;
}

void ProfileController::setApplicationHints(QStringList const& hints) {
    m_profile.applicationHints.clear();
    for (QString const& h : hints) {
        QString const trimmed = h.trimmed();
        if (!trimmed.isEmpty()) {
            m_profile.applicationHints.push_back(trimmed.toStdString());
        }
    }
    saveActiveProfile(); // persist so resolveProfileForApp() reads the new mapping
    rescanLibrary();
    emit profilesChanged();
}

bool ProfileController::addAppProfileMapping(QString const& profileId, QString const& appName) {
    // V5 / T-34-05-01: the app name is UNTRUSTED user input. Trim + length-bound
    // and treat strictly as a comparison token (never a path/command/pattern).
    QString const token = appName.trimmed();
    if (token.isEmpty() || token.size() > kMaxAppNameLength) {
        AJAZZ_LOG_WARN("profile",
                       "addAppProfileMapping: rejected app-name (empty or > {} chars)",
                       kMaxAppNameLength);
        return false;
    }
    if (profileId.isEmpty()) {
        return false;
    }

    auto const alreadyHasToken = [&token](std::vector<std::string> const& hints) {
        return std::any_of(hints.begin(), hints.end(), [&token](std::string const& h) {
            return QString::fromStdString(h).compare(token, Qt::CaseInsensitive) == 0;
        });
    };

    // Active profile: mutate in place + persist via the same path the active
    // writers use (saveActiveProfile), so the in-memory copy stays authoritative.
    if (QString::fromStdString(m_profile.id) == profileId) {
        if (alreadyHasToken(m_profile.applicationHints)) {
            return false; // case-insensitive duplicate — no-op
        }
        m_profile.applicationHints.push_back(token.toStdString());
        saveActiveProfile();
        rescanLibrary();
        emit profilesChanged();
        return true;
    }

    // Non-active profile: read off disk via the library index, mutate, write back.
    auto const it = m_library.constFind(profileId);
    if (it == m_library.constEnd()) {
        AJAZZ_LOG_WARN("profile", "addAppProfileMapping: unknown profile id");
        return false;
    }
    QString const path = it->path;
    try {
        ajazz::core::Profile p =
            ajazz::core::readProfileFromDisk(std::filesystem::path{path.toStdString()});
        if (alreadyHasToken(p.applicationHints)) {
            return false;
        }
        p.applicationHints.push_back(token.toStdString());
        ajazz::core::writeProfileToDisk(std::filesystem::path{path.toStdString()}, p);
    } catch (std::exception const& ex) {
        AJAZZ_LOG_WARN("profile", "addAppProfileMapping: I/O error: {}", ex.what());
        return false;
    }
    rescanLibrary();
    emit profilesChanged();
    return true;
}

bool ProfileController::removeAppProfileMapping(QString const& profileId, QString const& appName) {
    QString const token = appName.trimmed();
    if (token.isEmpty() || profileId.isEmpty()) {
        return false;
    }

    auto const eraseToken = [&token](std::vector<std::string>& hints) -> bool {
        auto const newEnd =
            std::remove_if(hints.begin(), hints.end(), [&token](std::string const& h) {
                return QString::fromStdString(h).compare(token, Qt::CaseInsensitive) == 0;
            });
        if (newEnd == hints.end()) {
            return false; // nothing matched
        }
        hints.erase(newEnd, hints.end());
        return true;
    };

    if (QString::fromStdString(m_profile.id) == profileId) {
        if (!eraseToken(m_profile.applicationHints)) {
            return false;
        }
        saveActiveProfile();
        rescanLibrary();
        emit profilesChanged();
        return true;
    }

    auto const it = m_library.constFind(profileId);
    if (it == m_library.constEnd()) {
        return false;
    }
    QString const path = it->path;
    try {
        ajazz::core::Profile p =
            ajazz::core::readProfileFromDisk(std::filesystem::path{path.toStdString()});
        if (!eraseToken(p.applicationHints)) {
            return false;
        }
        ajazz::core::writeProfileToDisk(std::filesystem::path{path.toStdString()}, p);
    } catch (std::exception const& ex) {
        AJAZZ_LOG_WARN("profile", "removeAppProfileMapping: I/O error: {}", ex.what());
        return false;
    }
    rescanLibrary();
    emit profilesChanged();
    return true;
}

QVariantList ProfileController::appProfileMappings() const {
    // One row per (profile, hint) pair across every known profile, read off disk
    // (the m_library index does not carry applicationHints). Sorted by profile
    // name then app name for a stable list in SettingsPage.qml.
    QVariantList out;
    QDir const dir(profilesDir());
    if (!dir.exists()) {
        return out;
    }
    QStringList const files =
        dir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files | QDir::Readable);
    for (QString const& file : files) {
        QString const path = dir.filePath(file);
        ajazz::core::Profile p;
        try {
            p = ajazz::core::readProfileFromDisk(std::filesystem::path{path.toStdString()});
        } catch (std::exception const&) {
            continue;
        }
        QString const stem = QFileInfo(file).completeBaseName();
        QString const id = p.id.empty() ? stem : QString::fromStdString(p.id);
        QString const name = p.name.empty() ? id : QString::fromStdString(p.name);
        for (auto const& h : p.applicationHints) {
            QVariantMap m;
            m.insert(QStringLiteral("profileId"), id);
            m.insert(QStringLiteral("profileName"), name);
            m.insert(QStringLiteral("deviceCodename"), QString::fromStdString(p.deviceCodename));
            m.insert(QStringLiteral("appName"), QString::fromStdString(h));
            out.append(m);
        }
    }
    std::sort(out.begin(), out.end(), [](QVariant const& a, QVariant const& b) {
        QVariantMap const ma = a.toMap();
        QVariantMap const mb = b.toMap();
        int const byName =
            ma.value(QStringLiteral("profileName"))
                .toString()
                .localeAwareCompare(mb.value(QStringLiteral("profileName")).toString());
        if (byName != 0) {
            return byName < 0;
        }
        return ma.value(QStringLiteral("appName"))
                   .toString()
                   .localeAwareCompare(mb.value(QStringLiteral("appName")).toString()) < 0;
    });
    return out;
}

bool ProfileController::foregroundCapabilityAvailable() const noexcept {
    return m_foregroundCapabilityAvailable;
}

void ProfileController::setForegroundCapabilityAvailable(bool available) {
    if (m_foregroundCapabilityAvailable == available) {
        return;
    }
    m_foregroundCapabilityAvailable = available;
    AJAZZ_LOG_INFO("profile",
                   "foreground capability {} (APROF-03 chip {})",
                   available ? "available" : "ABSENT",
                   available ? "hidden" : "VISIBLE");
    emit foregroundCapabilityChanged();
}

QString ProfileController::foregroundCapabilityWarning() const {
    // UI-SPEC capability-warning detail copy (ASCII-only; the chip tooltip + a
    // headless qml.get assert this is non-empty on the capability-absent path).
    return tr("Your desktop environment does not expose a foreground-window API, "
              "so per-app profile switching is unavailable here. Profiles can still "
              "be switched manually. (Wayland compositors without "
              "zwlr-foreign-toplevel, e.g. GNOME or KDE, are affected.)");
}

QVariantList ProfileController::activeKeyBindings() const {
    QVariantList out;
    for (auto const& [idx, binding] : activeKeyMap()) {
        QVariantMap m;
        m.insert(QStringLiteral("index"), static_cast<int>(idx));
        m.insert(QStringLiteral("iconSource"),
                 binding.state.imagePath ? QString::fromStdString(*binding.state.imagePath)
                                         : QString{});
        m.insert(QStringLiteral("label"),
                 binding.state.text ? QString::fromStdString(*binding.state.text) : QString{});
        // First onPress step defines the action kind / plugin id for the tile
        // (back-compat top-level fields — unchanged from Phase 16).
        int kind = 0;
        QString actionId;
        if (!binding.onPress.empty()) {
            kind = static_cast<int>(binding.onPress.front().kind);
            actionId = QString::fromStdString(binding.onPress.front().id);
        }
        m.insert(QStringLiteral("actionKind"), kind);
        m.insert(QStringLiteral("actionId"), actionId);

        // Delta A: surface folder keys so the canvas can show the folder look and
        // navigate INTO the child page on activation. A key is a folder when its
        // first onPress step is an OpenFolder action; its target page id lives in
        // settingsJson as {"target":"<id>"}.
        bool isFolder = false;
        QString folderTarget;
        if (!binding.onPress.empty() &&
            binding.onPress.front().kind == ajazz::core::ActionKind::OpenFolder) {
            isFolder = true;
            auto const doc = QJsonDocument::fromJson(
                QByteArray::fromStdString(binding.onPress.front().settingsJson));
            if (doc.isObject()) {
                folderTarget = doc.object().value(QStringLiteral("target")).toString();
            }
        }
        m.insert(QStringLiteral("isFolder"), isFolder);
        m.insert(QStringLiteral("folderTarget"), folderTarget);

        // PLUGIN-23: full onPress list exposed as "actionList" — a nested
        // QVariantList of {actionKind, actionId, label, iconSource} maps.
        // QML KeyBindingList uses this to render each action row.
        QVariantList actionList;
        for (auto const& act : binding.onPress) {
            QVariantMap am;
            am.insert(QStringLiteral("actionKind"), static_cast<int>(act.kind));
            am.insert(QStringLiteral("actionId"), QString::fromStdString(act.id));
            am.insert(QStringLiteral("label"), QString::fromStdString(act.label));
            // iconSource for an individual action step is not stored in the core
            // model (imagePath belongs to KeyState, not Action); leave empty so
            // QML falls back to the plugin-catalog icon lookup.
            am.insert(QStringLiteral("iconSource"), QString{});
            actionList.append(am);
        }
        m.insert(QStringLiteral("actionList"), actionList);

        out.append(m);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Pages / Folders (Delta A)
// ---------------------------------------------------------------------------

std::unordered_map<std::uint16_t, ajazz::core::Binding>& ProfileController::activeKeyMap() {
    if (m_pageStack.isEmpty() || m_pageStack.last() == QStringLiteral("root")) {
        return m_profile.keys;
    }
    auto it = m_profile.pages.find(m_pageStack.last().toStdString());
    if (it != m_profile.pages.end()) {
        return it->second.keys;
    }
    return m_profile.keys; // Unknown active page -> safe fallback to root.
}

std::unordered_map<std::uint16_t, ajazz::core::Binding> const&
ProfileController::activeKeyMap() const {
    if (m_pageStack.isEmpty() || m_pageStack.last() == QStringLiteral("root")) {
        return m_profile.keys;
    }
    auto it = m_profile.pages.find(m_pageStack.last().toStdString());
    if (it != m_profile.pages.end()) {
        return it->second.keys;
    }
    return m_profile.keys;
}

void ProfileController::resetPageNav() {
    m_pageStack = QStringList{QStringLiteral("root")};
}

QString ProfileController::activePageId() const {
    return m_pageStack.isEmpty() ? QStringLiteral("root") : m_pageStack.last();
}

QString ProfileController::activePageName() const {
    QString const id = activePageId();
    if (id == QStringLiteral("root")) {
        return tr("Home");
    }
    auto const it = m_profile.pages.find(id.toStdString());
    if (it != m_profile.pages.end() && !it->second.name.empty()) {
        return QString::fromStdString(it->second.name);
    }
    return tr("Folder");
}

QVariantList ProfileController::pageBreadcrumb() const {
    QVariantList out;
    for (QString const& id : m_pageStack) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), id);
        if (id == QStringLiteral("root")) {
            m.insert(QStringLiteral("name"), tr("Home"));
        } else {
            auto const it = m_profile.pages.find(id.toStdString());
            m.insert(QStringLiteral("name"),
                     (it != m_profile.pages.end() && !it->second.name.empty())
                         ? QString::fromStdString(it->second.name)
                         : tr("Folder"));
        }
        out.append(m);
    }
    return out;
}

void ProfileController::enterFolder(QString const& pageId) {
    if (pageId.isEmpty() || pageId == QStringLiteral("root")) {
        goToRootPage();
        return;
    }
    if (m_profile.pages.find(pageId.toStdString()) == m_profile.pages.end()) {
        AJAZZ_LOG_WARN(
            "profile-controller", "enterFolder: unknown page '{}', ignoring", pageId.toStdString());
        return;
    }
    if (m_pageStack.last() == pageId) {
        return; // Already showing this folder.
    }
    m_pageStack.append(pageId);
    emit profileChanged(); // Canvas re-syncs to the folder's keys via activeKeyBindings().
}

void ProfileController::goBackPage() {
    if (m_pageStack.size() <= 1) {
        return; // Already at root.
    }
    m_pageStack.removeLast();
    emit profileChanged();
}

void ProfileController::goToRootPage() {
    if (m_pageStack.size() == 1 && m_pageStack.last() == QStringLiteral("root")) {
        return;
    }
    resetPageNav();
    emit profileChanged();
}

QString ProfileController::createFolderOnKey(int keyIndex, QString const& name) {
    if (keyIndex < 0 ||
        keyIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "createFolderOnKey: keyIndex {} out of valid range [0, 65534], ignoring",
                       keyIndex);
        return {};
    }

    QString const pageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    std::string const folderName =
        name.trimmed().isEmpty() ? std::string{"Folder"} : name.trimmed().toStdString();

    // Create the new child page, seeded with a BackToParent key at index 0 so the
    // device can navigate back out (Elgato folders always carry a return key).
    ajazz::core::ProfilePage page;
    page.id = pageId.toStdString();
    page.name = folderName;
    {
        ajazz::core::Action back;
        back.kind = ajazz::core::ActionKind::BackToParent;
        back.label = "Back";
        ajazz::core::Binding backBinding;
        backBinding.onPress.push_back(back);
        backBinding.state.text = std::string{"Back"};
        page.keys.emplace(static_cast<std::uint16_t>(0), std::move(backBinding));
    }
    m_profile.pages.emplace(page.id, std::move(page));

    // Track the parent->child relationship when the parent is a real ProfilePage
    // (a child folder). The root page lives in Profile::keys and has no
    // ProfilePage wrapper, so root-level folders are reachable purely via the
    // OpenFolder key below (children tracking is best-effort, not load-bearing).
    QString const parentId = activePageId();
    if (parentId != QStringLiteral("root")) {
        if (auto pit = m_profile.pages.find(parentId.toStdString()); pit != m_profile.pages.end()) {
            pit->second.children.push_back(pageId.toStdString());
        }
    }

    // Bind the key on the CURRENT page to an OpenFolder action targeting the new
    // page (settingsJson carries {"target":"<id>"} per profile.hpp Action docs).
    ajazz::core::Action open;
    open.kind = ajazz::core::ActionKind::OpenFolder;
    open.settingsJson = std::string{"{\"target\":\""} + pageId.toStdString() + "\"}";
    open.label = folderName;
    auto& binding = activeKeyMap()[static_cast<std::uint16_t>(keyIndex)];
    binding.onPress = {open};
    binding.onRelease.clear();
    binding.onLongPress.clear();
    binding.state.text = std::string{folderName};
    binding.state.imagePath.reset(); // Folder uses the built-in folder look in QML.

    saveActiveProfile();
    emit profileChanged();
    return pageId;
}

QVariantList ProfileController::activeEncoderBindings() const {
    // Keypad analog: activeKeyBindings(). The dial's bound action is its onPress
    // front (commitEncoderBinding assigns there); CW/CCW chains are a follow-up,
    // so the tile/PI key off onPress exactly like a key does.
    QVariantList out;
    for (auto const& [idx, binding] : m_profile.encoders) {
        QVariantMap m;
        m.insert(QStringLiteral("index"), static_cast<int>(idx));
        m.insert(QStringLiteral("iconSource"),
                 binding.state.imagePath ? QString::fromStdString(*binding.state.imagePath)
                                         : QString{});
        m.insert(QStringLiteral("label"),
                 binding.state.text ? QString::fromStdString(*binding.state.text) : QString{});
        int kind = 0;
        QString actionId;
        if (!binding.onPress.empty()) {
            kind = static_cast<int>(binding.onPress.front().kind);
            actionId = QString::fromStdString(binding.onPress.front().id);
        }
        m.insert(QStringLiteral("actionKind"), kind);
        m.insert(QStringLiteral("actionId"), actionId);
        out.append(m);
    }
    return out;
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
    // IN-02: cap at 200 characters to stay comfortably under filename limits on all
    // platforms (Linux 255 bytes, macOS 255 UTF-8 chars, Windows non-extended 260
    // total path chars). A profile id with 300 safe chars produces a 305-char
    // filename ("<id>.json") that can silently exceed PATH_MAX on Windows.
    if (result.size() > 200) {
        result.truncate(200);
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
                                         QString const& settingsJson,
                                         QString const& actionId) {
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
    auto& binding = activeKeyMap()[idx];

    binding.state.imagePath =
        iconPath.isEmpty() ? std::nullopt : std::optional<std::string>{iconPath.toStdString()};

    binding.state.text =
        label.isEmpty() ? std::nullopt : std::optional<std::string>{label.toStdString()};

    ajazz::core::Action act{};
    act.kind = static_cast<ajazz::core::ActionKind>(actionKind);
    act.id = actionId.toStdString();
    act.settingsJson = settingsJson.toStdString();
    binding.onPress = {std::move(act)};

    emit profileChanged();
}

void ProfileController::commitEncoderBinding(int encoderIndex,
                                             QString const& iconPath,
                                             QString const& label,
                                             int actionKind,
                                             QString const& settingsJson,
                                             QString const& actionId) {
    // Validate encoderIndex: must be in [0, 65534] (uint16_t range; mirrors commitKeyBinding).
    if (encoderIndex < 0 ||
        encoderIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitEncoderBinding: encoderIndex {} out of valid range [0, 65534],"
                       " ignoring",
                       encoderIndex);
        return;
    }
    // Validate actionKind.
    constexpr int kMaxActionKind = static_cast<int>(ajazz::core::ActionKind::BackToParent);
    if (actionKind < 0 || actionKind > kMaxActionKind) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitEncoderBinding: actionKind {} out of range [0, {}], ignoring",
                       actionKind,
                       kMaxActionKind);
        return;
    }

    auto const idx = static_cast<std::uint16_t>(encoderIndex);
    auto& binding = m_profile.encoders[idx];

    binding.state.imagePath =
        iconPath.isEmpty() ? std::nullopt : std::optional<std::string>{iconPath.toStdString()};

    binding.state.text =
        label.isEmpty() ? std::nullopt : std::optional<std::string>{label.toStdString()};

    // Library drag-drop assigns to onPress; CW/CCW editors are a follow-up (Phase 26 D-09).
    ajazz::core::Action act{};
    act.kind = static_cast<ajazz::core::ActionKind>(actionKind);
    act.id = actionId.toStdString();
    act.settingsJson = settingsJson.toStdString();
    binding.onPress = {std::move(act)};

    emit profileChanged();
}

void ProfileController::commitEncoderVolume(int encoderIndex) {
    if (encoderIndex < 0 ||
        encoderIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitEncoderVolume: encoderIndex {} out of range [0, 65534], ignoring",
                       encoderIndex);
        return;
    }

    // Built-in volume action; routed by ActionEngine -> BuiltinActionsService ->
    // input synthesizer media keys (Linux uinput KEY_VOLUMEUP/DOWN/MUTE).
    constexpr char const* kVolumeAction = "com.hotspot.streamdock.system.volume";
    auto makeStep = [&](char const* settingsJson) {
        ajazz::core::Action a{};
        a.kind = ajazz::core::ActionKind::Plugin;
        a.id = kVolumeAction;
        a.settingsJson = settingsJson;
        return a;
    };

    auto const idx = static_cast<std::uint16_t>(encoderIndex);
    auto& binding = m_profile.encoders[idx];
    // Directional chains: CW -> up, CCW -> down, press -> mute (Stream Deck + dial).
    binding.onCw = {makeStep(R"({"direction":"up"})")};
    binding.onCcw = {makeStep(R"({"direction":"down"})")};
    binding.onPress = {makeStep(R"({"key":"Mute"})")};
    binding.state.text = std::optional<std::string>{"Volume"};
    binding.state.imagePath = std::nullopt; // segment falls back to label + glyph

    AJAZZ_LOG_INFO("profile-controller", "commitEncoderVolume: dial {} -> system volume", idx);
    emit profileChanged();
}

int ProfileController::clearBindingsForPlugin(QString const& pluginUuid) {
    if (pluginUuid.isEmpty()) {
        return 0;
    }
    // Owner-prefix rule (mirrors ContextRegistry / plugin_device_bridge.cpp:295):
    // an action id belongs to the plugin when it equals the plugin uuid or is a
    // dotted child of it ("com.foo" owns "com.foo" and "com.foo.action").
    auto const owns = [&pluginUuid](std::string const& id) {
        QString const qid = QString::fromStdString(id);
        return qid == pluginUuid || qid.startsWith(pluginUuid + QLatin1Char('.'));
    };
    // A binding belongs to the plugin if the action the tile/PI keys off (the
    // onPress front, or the OpenDeck-shaped instance id) is owned by it.
    auto const bindingOwned = [&owns](auto const& binding) {
        if (!binding.onPress.empty() && owns(binding.onPress.front().id)) {
            return true;
        }
        if (binding.instance.has_value() && owns(binding.instance->id)) {
            return true;
        }
        return false;
    };

    int cleared = 0;
    for (auto it = m_profile.keys.begin(); it != m_profile.keys.end();) {
        if (bindingOwned(it->second)) {
            it = m_profile.keys.erase(it);
            ++cleared;
        } else {
            ++it;
        }
    }
    for (auto it = m_profile.encoders.begin(); it != m_profile.encoders.end();) {
        if (bindingOwned(it->second)) {
            it = m_profile.encoders.erase(it);
            ++cleared;
        } else {
            ++it;
        }
    }

    if (cleared > 0) {
        AJAZZ_LOG_INFO("profile-controller",
                       "clearBindingsForPlugin: '{}' -> cleared {} binding(s) (plugin uninstalled)",
                       pluginUuid.toStdString(),
                       cleared);
        emit profileChanged();
    }
    return cleared;
}

void ProfileController::commitTouchZoneBinding(int zoneIndex,
                                               QString const& iconPath,
                                               QString const& label,
                                               int actionKind,
                                               QString const& settingsJson,
                                               QString const& actionId) {
    // Validate zoneIndex: must be in [0, 255] (uint8_t range; touchZoneCount is uint8).
    // Out-of-range values indicate a runaway QML caller (T-26-09 mitigation).
    if (zoneIndex < 0 || zoneIndex > 255) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitTouchZoneBinding: zoneIndex {} out of valid range [0, 255], ignoring",
                       zoneIndex);
        return;
    }
    // Validate actionKind: must map to a defined ActionKind value (0..BackToParent).
    constexpr int kMaxActionKind = static_cast<int>(ajazz::core::ActionKind::BackToParent);
    if (actionKind < 0 || actionKind > kMaxActionKind) {
        AJAZZ_LOG_WARN("profile-controller",
                       "commitTouchZoneBinding: actionKind {} out of range [0, {}], ignoring",
                       actionKind,
                       kMaxActionKind);
        return;
    }

    auto const idx = static_cast<std::uint8_t>(zoneIndex);
    auto& binding = m_profile.touchZones[idx];

    binding.state.imagePath =
        iconPath.isEmpty() ? std::nullopt : std::optional<std::string>{iconPath.toStdString()};

    binding.state.text =
        label.isEmpty() ? std::nullopt : std::optional<std::string>{label.toStdString()};

    ajazz::core::Action act{};
    act.kind = static_cast<ajazz::core::ActionKind>(actionKind);
    act.id = actionId.toStdString();
    act.settingsJson = settingsJson.toStdString();
    binding.onTap = {std::move(act)};

    emit profileChanged();
}

// ---------------------------------------------------------------------------
// Phase 29-03 (PLUGIN-23): Multi-action verbs — append / reorder / remove
// ---------------------------------------------------------------------------

void ProfileController::appendKeyAction(int keyIndex,
                                        int actionKind,
                                        QString const& settingsJson,
                                        QString const& actionId) {
    // Validate keyIndex: must be in [0, 65534] (mirrors commitKeyBinding).
    if (keyIndex < 0 ||
        keyIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "appendKeyAction: keyIndex {} out of valid range [0, 65534], ignoring",
                       keyIndex);
        return;
    }
    // Validate actionKind: must map to a defined ActionKind value.
    constexpr int kMaxActionKind = static_cast<int>(ajazz::core::ActionKind::BackToParent);
    if (actionKind < 0 || actionKind > kMaxActionKind) {
        AJAZZ_LOG_WARN("profile-controller",
                       "appendKeyAction: actionKind {} out of range [0, {}], ignoring",
                       actionKind,
                       kMaxActionKind);
        return;
    }

    auto const idx = static_cast<std::uint16_t>(keyIndex);
    auto& binding = activeKeyMap()[idx]; // default-constructs if absent (new key)

    ajazz::core::Action act{};
    act.kind = static_cast<ajazz::core::ActionKind>(actionKind);
    act.id = actionId.toStdString();
    act.settingsJson = settingsJson.toStdString();
    binding.onPress.push_back(std::move(act)); // additive — does NOT clear existing actions

    emit profileChanged();
}

void ProfileController::reorderKeyAction(int keyIndex, int fromPos, int toPos) {
    // Validate keyIndex.
    if (keyIndex < 0 ||
        keyIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "reorderKeyAction: keyIndex {} out of valid range [0, 65534], ignoring",
                       keyIndex);
        return;
    }

    auto const idx = static_cast<std::uint16_t>(keyIndex);
    auto& keyMap = activeKeyMap();
    auto const it = keyMap.find(idx);
    if (it == keyMap.end()) {
        return; // No binding for this key — no-op.
    }

    auto& onPress = it->second.onPress;
    auto const sz = static_cast<int>(onPress.size());

    // Validate positions against the current vector size (out-of-range = no-op,
    // mitigates T-29-05).
    if (fromPos < 0 || fromPos >= sz || toPos < 0 || toPos >= sz) {
        AJAZZ_LOG_WARN("profile-controller",
                       "reorderKeyAction: pos ({}, {}) out of range [0, {}), ignoring",
                       fromPos,
                       toPos,
                       sz);
        return;
    }
    if (fromPos == toPos) {
        return; // No-op for self-move.
    }

    // Use std::rotate to shift the element at fromPos to toPos in O(n).
    if (fromPos < toPos) {
        // Moving forward: rotate the sub-range [fromPos, toPos+1) left by 1.
        std::rotate(
            onPress.begin() + fromPos, onPress.begin() + fromPos + 1, onPress.begin() + toPos + 1);
    } else {
        // Moving backward: rotate the sub-range [toPos, fromPos+1) right by 1
        // (= left by n-1, or equivalently rotate so the last element becomes first).
        std::rotate(
            onPress.begin() + toPos, onPress.begin() + fromPos, onPress.begin() + fromPos + 1);
    }

    emit profileChanged();
}

void ProfileController::removeKeyActionAt(int keyIndex, int pos) {
    // Validate keyIndex.
    if (keyIndex < 0 ||
        keyIndex > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "removeKeyActionAt: keyIndex {} out of valid range [0, 65534], ignoring",
                       keyIndex);
        return;
    }

    auto const idx = static_cast<std::uint16_t>(keyIndex);
    auto& keyMap = activeKeyMap();
    auto const it = keyMap.find(idx);
    if (it == keyMap.end()) {
        return; // No binding for this key — no-op.
    }

    auto& onPress = it->second.onPress;
    auto const sz = static_cast<int>(onPress.size());

    // Validate pos (T-29-05 range guard).
    if (pos < 0 || pos >= sz) {
        AJAZZ_LOG_WARN("profile-controller",
                       "removeKeyActionAt: pos {} out of range [0, {}), ignoring",
                       pos,
                       sz);
        return;
    }

    onPress.erase(onPress.begin() + pos);
    // Removing the last action leaves onPress empty — key reads as cleared.

    emit profileChanged();
}

void ProfileController::cycleInstanceState(QString const& controller, int index) {
    // BIND-05/07: advance a Toggle Action binding's instance.currentState one step
    // (mod N over ALL states, N > 2 supported), PERSIST it (Q1 user decision: the
    // new state survives a restart), and emit profileChanged() so the repaint /
    // context reconcile fires. The render of states[currentState] + the
    // state-change willAppear are driven by the input-service render hook
    // (StreamDockInputService::dispatchToggle) -- this mutator owns ONLY the state
    // mutation + persistence (RESEARCH Finding 3).
    if (index < 0 || index > static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1)) {
        AJAZZ_LOG_WARN("profile-controller",
                       "cycleInstanceState: index {} out of valid range [0, 65534], ignoring",
                       index);
        return;
    }
    auto const idx = static_cast<std::uint16_t>(index);

    // Resolve the binding's instance for the addressed controller. "Keypad" ->
    // keys, "Encoder" -> encoders (touch-zone bindings register under "Encoder"
    // in the bridge, so they share this path). Case-insensitive compare so the
    // wire controller strings ("Keypad"/"Encoder") match regardless of source.
    std::optional<ajazz::core::ActionInstance>* instanceSlot = nullptr;
    if (controller.compare(QStringLiteral("Keypad"), Qt::CaseInsensitive) == 0) {
        auto& keyMap = activeKeyMap();
        if (auto it = keyMap.find(idx); it != keyMap.end()) {
            instanceSlot = &it->second.instance;
        }
    } else if (controller.compare(QStringLiteral("Encoder"), Qt::CaseInsensitive) == 0) {
        if (auto it = m_profile.encoders.find(idx); it != m_profile.encoders.end()) {
            instanceSlot = &it->second.instance;
        }
    } else {
        AJAZZ_LOG_WARN("profile-controller",
                       "cycleInstanceState: unknown controller '{}', ignoring",
                       controller.toStdString());
        return;
    }

    // No binding, no instance, or fewer than two states -> clean no-op (do NOT
    // create an instance; currentState stays put). T-32-08: the modulo guarantees
    // currentState stays in [0, states.size()).
    if (instanceSlot == nullptr || !instanceSlot->has_value()) {
        return;
    }
    auto& inst = **instanceSlot;
    if (inst.states.size() <= 1) {
        return;
    }
    inst.currentState = (inst.currentState + 1u) % static_cast<std::uint32_t>(inst.states.size());

    // Persist (Q1): route through the same save path commitKeyBinding's callers
    // use so the advance survives a restart, then notify observers.
    saveActiveProfile();
    emit profileChanged();
}

void ProfileController::swapEncoderBindings(int srcIndex, int dstIndex) {
    // Fixes UI-REVIEW.md Phase 26 encoder-swap data-loss bug.
    // Validate both indices.
    constexpr int kMaxIdx = static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1);
    if (srcIndex < 0 || srcIndex > kMaxIdx || dstIndex < 0 || dstIndex > kMaxIdx) {
        AJAZZ_LOG_WARN("profile-controller",
                       "swapEncoderBindings: index out of range (src={}, dst={}), ignoring",
                       srcIndex,
                       dstIndex);
        return;
    }
    if (srcIndex == dstIndex) {
        return; // No-op for self-swap.
    }
    auto const s = static_cast<std::uint16_t>(srcIndex);
    auto const d = static_cast<std::uint16_t>(dstIndex);
    // operator[] on an absent key default-constructs an empty binding, which is
    // the correct semantics for "swap with empty slot moves the binding".
    auto src_copy = m_profile.encoders[s];
    m_profile.encoders[s] = m_profile.encoders[d];
    m_profile.encoders[d] = std::move(src_copy);
    emit profileChanged();
}

void ProfileController::swapKeyBindings(int srcIndex, int dstIndex) {
    // Keypad analog of swapEncoderBindings. Moves the WHOLE core::Binding so a
    // multi-action onPress chain (PLUGIN-23) survives a move/swap intact — the
    // old QML two-commit path collapsed it to a single action.
    constexpr int kMaxIdx = static_cast<int>(std::numeric_limits<std::uint16_t>::max() - 1);
    if (srcIndex < 0 || srcIndex > kMaxIdx || dstIndex < 0 || dstIndex > kMaxIdx) {
        AJAZZ_LOG_WARN("profile-controller",
                       "swapKeyBindings: index out of range (src={}, dst={}), ignoring",
                       srcIndex,
                       dstIndex);
        return;
    }
    if (srcIndex == dstIndex) {
        return; // No-op for self-swap.
    }
    auto const s = static_cast<std::uint16_t>(srcIndex);
    auto const d = static_cast<std::uint16_t>(dstIndex);
    // operator[] on an absent key default-constructs an empty Binding, which is
    // the correct semantics for "swap with an empty slot moves the binding".
    // Page-aware (Delta A): swap within whichever page the editor is showing.
    auto& keyMap = activeKeyMap();
    auto src_copy = keyMap[s];
    keyMap[s] = keyMap[d];
    keyMap[d] = std::move(src_copy);
    emit profileChanged();
}

void ProfileController::swapTouchZoneBindings(int srcIndex, int dstIndex) {
    // Fixes UI-REVIEW.md Phase 26 touch-zone-swap data-loss bug. Same shape as
    // swapEncoderBindings; touchZone indices use uint8_t range [0, 255].
    if (srcIndex < 0 || srcIndex > 255 || dstIndex < 0 || dstIndex > 255) {
        AJAZZ_LOG_WARN("profile-controller",
                       "swapTouchZoneBindings: index out of range (src={}, dst={}), ignoring",
                       srcIndex,
                       dstIndex);
        return;
    }
    if (srcIndex == dstIndex) {
        return;
    }
    auto const s = static_cast<std::uint8_t>(srcIndex);
    auto const d = static_cast<std::uint8_t>(dstIndex);
    auto src_copy = m_profile.touchZones[s];
    m_profile.touchZones[s] = m_profile.touchZones[d];
    m_profile.touchZones[d] = std::move(src_copy);
    emit profileChanged();
}

void ProfileController::saveActiveProfile() {
    QString const id =
        m_profile.id.empty() ? QStringLiteral("default") : QString::fromStdString(m_profile.id);
    QString const path = defaultProfilePath(id);

    // Pitfall 5: parent directory must exist before writeProfileToDisk.
    // WR-03: check mkpath() return value and emit saveFailed with an accurate
    // message on failure rather than proceeding to writeProfileToDisk (which
    // would throw ProfileIoError with a less informative "write failed" message).
    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        emit saveFailed(tr("Cannot create profiles directory: %1").arg(dir.absolutePath()));
        return;
    }

    // WR-04: emit profilesChanged() only when this is the first save of this id
    // (i.e., the path was not previously persisted). In-place re-saves do not
    // change the profile library list and must not trigger library-rebuild listeners.
    bool const isNewId = m_path != path;
    saveProfile(path);
    // saveProfile() sets m_path on success; if m_path now equals path, the save
    // succeeded. Only emit profilesChanged when the id is new to the library.
    if (isNewId && m_path == path) {
        emit profilesChanged();
    }
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
    m_profile.touchZones.clear(); // Phase 26 D-11 map — must be cleared alongside keys/encoders
                                  // so "Restore defaults" does not silently leave stale touch-strip
                                  // bindings that survive to the next saveActiveProfile() call.
    // WR-02: also clear per-page key bindings while preserving the page structure
    // (folder navigation entries). Without this, root keys are cleared but folder
    // pages still carry old bindings -- the device shows folder key images after a
    // carousel navigation, contradicting the "Restore defaults" intent.
    for (auto& [id, page] : m_profile.pages) {
        page.keys.clear();
    }
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
    // Resolve through the in-memory library index (issue #24, now implemented).
    auto it = m_library.find(profileId);
    if (it == m_library.end()) {
        // Rebuild once in case the library is stale (e.g. a profile created in
        // another window) before giving up.
        rescanLibrary();
        it = m_library.find(profileId);
    }
    if (it == m_library.end()) {
        emit loadFailed(tr("Profile '%1' is not in the library").arg(profileId));
        return;
    }
    loadProfile(it->path); // emits profileChanged() on success / loadFailed() on error
}

} // namespace ajazz::app
