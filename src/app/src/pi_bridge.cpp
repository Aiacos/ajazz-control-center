// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_bridge.cpp
 * @brief Implementation of @ref ajazz::app::PIBridge.
 *
 * M4 milestone — `setSettings` / `getSettings` / `setGlobalSettings` /
 * `getGlobalSettings` are wired through to on-disk JSON files. Other
 * Q_INVOKABLE methods remain logging stubs until later milestones:
 *
 *   * @c setSettings / @c getSettings → on-disk JSON in
 *     @c QStandardPaths::AppDataLocation/plugins/<plugin>/settings/<context>.json
 *     (this milestone — written via @c QSaveFile, atomic on Linux/macOS/Windows).
 *
 *   * @c setGlobalSettings / @c getGlobalSettings → plugin-wide JSON in
 *     @c QStandardPaths::AppDataLocation/plugins/<plugin>/global.json (this
 *     milestone, same atomic-rename pattern).
 *
 *   * @c sendToPlugin / @c sendToPropertyInspector → JSON frames over the
 *     plugin-host WebSocket connection (lands in M5).
 *
 *   * @c openUrl → @c QDesktopServices::openUrl after validating against
 *     the host-side allowlist (security pass alongside M5).
 */
#include "pi_bridge.hpp"

#include "ajazz/core/logger.hpp"
#include "property_inspector_controller.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>

#include <utility>

namespace ajazz::app {

namespace {

/// Per-context settings size cap. A runaway PI calling setSettings() in a
/// tight loop must not be able to fill the user's disk; 1 MiB is two orders
/// of magnitude above any realistic PI form payload.
constexpr qint64 kMaxSettingsBytes = 1LL << 20; // 1 MiB

/**
 * @brief Defence against path-traversal: reject any uuid that could escape
 *        the plugin sandbox if naively concatenated into a filesystem path.
 *
 * The PI is plugin-authored HTML so the @c pluginUuid and @c contextUuid
 * forwarded by the bridge are effectively untrusted: a hostile (or buggy)
 * plugin could pass `..`, embedded slashes, NUL or control bytes. Any of
 * those would let it write outside its own per-plugin directory, so we
 * refuse them up-front rather than try to scrub them.
 *
 * Stream Deck SDK-2 UUIDs are reverse-DNS strings (`com.elgato.foo`); we
 * additionally accept the hex-style context UUIDs the host mints. Both
 * fit comfortably inside the conservative whitelist below.
 */
bool isSafeUuidComponent(QString const& s) {
    if (s.isEmpty() || s.size() > 256) {
        return false;
    }
    for (QChar const c : s) {
        ushort const u = c.unicode();
        if (u < 0x20 || u == 0x7f) {
            return false; // ASCII control chars + NUL
        }
        if (c == QLatin1Char('/') || c == QLatin1Char('\\')) {
            return false; // path separators on any platform
        }
    }
    if (s == QLatin1String(".") || s == QLatin1String("..")) {
        return false;
    }
    if (s.contains(QLatin1String(".."))) {
        return false; // blocks "foo/../bar" even after slash check above
    }
    return true;
}

/**
 * @brief Resolve the directory all per-plugin settings live under.
 *
 * `<AppDataLocation>/plugins/<pluginUuid>/`. Created on demand. Returns an
 * empty QString if the plugin uuid is rejected by @ref isSafeUuidComponent
 * or if the AppDataLocation cannot be resolved (which would indicate a
 * broken Qt environment).
 */
QString pluginDir(QString const& pluginUuid) {
    if (!isSafeUuidComponent(pluginUuid)) {
        return {};
    }
    QString const root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        return {};
    }
    return root + QLatin1String("/plugins/") + pluginUuid;
}

/// Path of the per-context settings file. Empty on validation failure.
QString perContextPath(QString const& pluginUuid, QString const& contextUuid) {
    if (!isSafeUuidComponent(contextUuid)) {
        return {};
    }
    QString const dir = pluginDir(pluginUuid);
    if (dir.isEmpty()) {
        return {};
    }
    return dir + QLatin1String("/settings/") + contextUuid + QLatin1String(".json");
}

/// Path of the plugin-wide settings file. Empty on validation failure.
QString globalPath(QString const& pluginUuid) {
    QString const dir = pluginDir(pluginUuid);
    if (dir.isEmpty()) {
        return {};
    }
    return dir + QLatin1String("/global.json");
}

/**
 * @brief Atomic JSON write via @c QSaveFile (write-to-temp + rename).
 *
 * @c QSaveFile already implements the same atomic-rename pattern that
 * @ref ajazz::core::profile_io uses (write to `*.<rand>` sibling, fsync,
 * rename over the destination). Returns true on success, logs and returns
 * false on any failure so the caller can preserve the M3 contract of
 * "logged error, no exception across the JS bridge".
 *
 * The caller is expected to have already validated that @p json parses
 * and is below the size cap; this function only deals with the I/O side.
 */
bool writeJsonAtomic(QString const& path, QByteArray const& json, QString const& whatForLog) {
    QFileInfo const fi{path};
    QDir parent;
    if (!parent.mkpath(fi.absolutePath())) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "{}: cannot create directory '{}'",
                        whatForLog.toStdString(),
                        fi.absolutePath().toStdString());
        return false;
    }

    QSaveFile out{path};
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "{}: cannot open '{}' for write: {}",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        out.errorString().toStdString());
        return false;
    }
    if (out.write(json) != json.size()) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "{}: short write to '{}': {}",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        out.errorString().toStdString());
        return false;
    }
    if (!out.commit()) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "{}: commit to '{}' failed: {}",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        out.errorString().toStdString());
        return false;
    }
    return true;
}

/**
 * @brief Read the settings file if present, otherwise return `{}`.
 *
 * Errors (corrupt JSON, I/O failure on a file that does exist) are logged
 * but still resolve to `{}` so the PI's JS event loop never hangs waiting
 * for a @c didReceiveSettings reply that never arrives.
 */
QString readJsonOrEmpty(QString const& path, QString const& whatForLog) {
    if (path.isEmpty()) {
        return QStringLiteral("{}");
    }
    QFile in{path};
    if (!in.exists()) {
        return QStringLiteral("{}"); // first-load case — not an error
    }
    if (!in.open(QIODevice::ReadOnly)) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "{}: cannot open '{}' for read: {}",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        in.errorString().toStdString());
        return QStringLiteral("{}");
    }
    QByteArray const data = in.readAll();
    in.close();
    if (data.isEmpty()) {
        return QStringLiteral("{}");
    }
    // Parse just to validate; on failure log and substitute `{}` so a
    // half-written file from a previous crash can't poison the PI page.
    QJsonParseError perr;
    QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "{}: '{}' is not valid JSON ({}); returning empty",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        perr.errorString().toStdString());
        return QStringLiteral("{}");
    }
    return QString::fromUtf8(data);
}

} // namespace

PIBridge::PIBridge(PropertyInspectorController* controller,
                   QString pluginUuid,
                   QString actionUuid,
                   QString contextUuid,
                   QObject* parent)
    : QObject(parent), controller_(controller), pluginUuid_(std::move(pluginUuid)),
      actionUuid_(std::move(actionUuid)), contextUuid_(std::move(contextUuid)) {}

PIBridge::~PIBridge() = default;

void PIBridge::setSettings(QString const& json) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "setSettings: plugin={} action={} context={} payload-bytes={}",
                   pluginUuid_.toStdString(),
                   actionUuid_.toStdString(),
                   contextUuid_.toStdString(),
                   static_cast<int>(json.size()));
    (void)controller_; // routed through the bridge directly in M4.

    QByteArray const utf8 = json.toUtf8();
    if (utf8.size() > kMaxSettingsBytes) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "setSettings: payload {} bytes exceeds {}-byte cap; refusing",
                        static_cast<long long>(utf8.size()),
                        static_cast<long long>(kMaxSettingsBytes));
        return;
    }
    QJsonParseError perr;
    QJsonDocument::fromJson(utf8, &perr);
    if (perr.error != QJsonParseError::NoError) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "setSettings: invalid JSON ({}); refusing",
                        perr.errorString().toStdString());
        return;
    }
    QString const path = perContextPath(pluginUuid_, contextUuid_);
    if (path.isEmpty()) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "setSettings: invalid plugin/context uuid (plugin='{}' context='{}'); "
                        "refusing",
                        pluginUuid_.toStdString(),
                        contextUuid_.toStdString());
        return;
    }
    if (writeJsonAtomic(path, utf8, QStringLiteral("setSettings"))) {
        AJAZZ_LOG_INFO("pi-bridge", "setSettings: persisted to '{}'", path.toStdString());
    }
}

void PIBridge::getSettings() {
    AJAZZ_LOG_INFO("pi-bridge",
                   "getSettings: plugin={} context={}",
                   pluginUuid_.toStdString(),
                   contextUuid_.toStdString());
    QString const path = perContextPath(pluginUuid_, contextUuid_);
    if (path.isEmpty()) {
        AJAZZ_LOG_ERROR("pi-bridge", "getSettings: invalid plugin/context uuid; emitting empty");
        emit didReceiveSettings(QStringLiteral("{}"));
        return;
    }
    emit didReceiveSettings(readJsonOrEmpty(path, QStringLiteral("getSettings")));
}

void PIBridge::setGlobalSettings(QString const& json) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "setGlobalSettings: plugin={} payload-bytes={}",
                   pluginUuid_.toStdString(),
                   static_cast<int>(json.size()));

    QByteArray const utf8 = json.toUtf8();
    if (utf8.size() > kMaxSettingsBytes) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "setGlobalSettings: payload {} bytes exceeds {}-byte cap; refusing",
                        static_cast<long long>(utf8.size()),
                        static_cast<long long>(kMaxSettingsBytes));
        return;
    }
    QJsonParseError perr;
    QJsonDocument::fromJson(utf8, &perr);
    if (perr.error != QJsonParseError::NoError) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "setGlobalSettings: invalid JSON ({}); refusing",
                        perr.errorString().toStdString());
        return;
    }
    QString const path = globalPath(pluginUuid_);
    if (path.isEmpty()) {
        AJAZZ_LOG_ERROR("pi-bridge",
                        "setGlobalSettings: invalid plugin uuid '{}'; refusing",
                        pluginUuid_.toStdString());
        return;
    }
    if (writeJsonAtomic(path, utf8, QStringLiteral("setGlobalSettings"))) {
        AJAZZ_LOG_INFO("pi-bridge", "setGlobalSettings: persisted to '{}'", path.toStdString());
    }
}

void PIBridge::getGlobalSettings() {
    AJAZZ_LOG_INFO("pi-bridge", "getGlobalSettings: plugin={}", pluginUuid_.toStdString());
    QString const path = globalPath(pluginUuid_);
    if (path.isEmpty()) {
        AJAZZ_LOG_ERROR("pi-bridge", "getGlobalSettings: invalid plugin uuid; emitting empty");
        emit didReceiveGlobalSettings(QStringLiteral("{}"));
        return;
    }
    emit didReceiveGlobalSettings(readJsonOrEmpty(path, QStringLiteral("getGlobalSettings")));
}

void PIBridge::sendToPlugin(QString const& json) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "sendToPlugin: plugin={} action={} payload-bytes={}",
                   pluginUuid_.toStdString(),
                   actionUuid_.toStdString(),
                   static_cast<int>(json.size()));
    // M5 routes to the plugin process via PluginHostController.
}

void PIBridge::setTitle(QString const& title, QString const& context, int target) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "setTitle: context='{}' target={} title='{}'",
                   context.toStdString(),
                   target,
                   title.toStdString());
}

void PIBridge::setImage(QString const& imageData, QString const& context, int target) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "setImage: context='{}' target={} payload-bytes={}",
                   context.toStdString(),
                   target,
                   static_cast<int>(imageData.size()));
}

void PIBridge::openUrl(QString const& url) {
    AJAZZ_LOG_INFO("pi-bridge", "openUrl (deferred to security pass): {}", url.toStdString());
    // Deliberately not invoking QDesktopServices::openUrl yet — landing it
    // unguarded would let any plugin shell out to the user's browser
    // unprompted. The security pass alongside M5 adds a host-side
    // allowlist check + a per-plugin confirmation prompt the first time
    // a PI calls openUrl().
}

void PIBridge::logMessage(QString const& message) {
    AJAZZ_LOG_INFO("pi-bridge[{}]", "{}", contextUuid_.toStdString(), message.toStdString());
}

} // namespace ajazz::app
