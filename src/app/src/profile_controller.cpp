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

QVariantList ProfileController::activeKeyBindings() const {
    QVariantList out;
    for (auto const& [idx, binding] : m_profile.keys) {
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
    auto& binding = m_profile.keys[idx];

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
    auto& binding = m_profile.keys[idx]; // default-constructs if absent (new key)

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
    auto const it = m_profile.keys.find(idx);
    if (it == m_profile.keys.end()) {
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
    auto const it = m_profile.keys.find(idx);
    if (it == m_profile.keys.end()) {
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
        if (auto it = m_profile.keys.find(idx); it != m_profile.keys.end()) {
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
    auto src_copy = m_profile.keys[s];
    m_profile.keys[s] = m_profile.keys[d];
    m_profile.keys[d] = std::move(src_copy);
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
