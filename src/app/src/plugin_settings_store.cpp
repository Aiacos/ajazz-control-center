// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_settings_store.cpp
 * @brief Implementation of the shared plugin settings store.
 *
 * Extracted verbatim (behaviour-preserving) from the PIBridge M4 persistence
 * helpers so the Property Inspector and the plugin WebSocket path share one
 * on-disk store. COD-031: Qt-Core only, no nlohmann.
 */
#include "plugin_settings_store.hpp"

#include "ajazz/core/logger.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLatin1Char>
#include <QSaveFile>
#include <QStandardPaths>

namespace ajazz::app::plugin_settings_store {

namespace {

QString pluginDir(QString const& pluginUuid) {
    if (!isSafeComponent(pluginUuid)) {
        return {};
    }
    QString const root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty()) {
        return {};
    }
    return root + QLatin1String("/plugins/") + pluginUuid;
}

QString perContextPath(QString const& pluginUuid, QString const& contextId) {
    if (!isSafeComponent(contextId)) {
        return {};
    }
    QString const dir = pluginDir(pluginUuid);
    if (dir.isEmpty()) {
        return {};
    }
    return dir + QLatin1String("/settings/") + contextId + QLatin1String(".json");
}

QString globalPathFor(QString const& pluginUuid) {
    QString const dir = pluginDir(pluginUuid);
    if (dir.isEmpty()) {
        return {};
    }
    return dir + QLatin1String("/global.json");
}

bool writeJsonAtomic(QString const& path, QByteArray const& json, QString const& whatForLog) {
    if (path.isEmpty()) {
        return false;
    }
    QFileInfo const fi{path};
    QDir parent;
    if (!parent.mkpath(fi.absolutePath())) {
        AJAZZ_LOG_ERROR("plugin-settings",
                        "{}: cannot create directory '{}'",
                        whatForLog.toStdString(),
                        fi.absolutePath().toStdString());
        return false;
    }
    QSaveFile out{path};
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        AJAZZ_LOG_ERROR("plugin-settings",
                        "{}: cannot open '{}' for write: {}",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        out.errorString().toStdString());
        return false;
    }
    if (out.write(json) != json.size() || !out.commit()) {
        AJAZZ_LOG_ERROR("plugin-settings",
                        "{}: write/commit to '{}' failed: {}",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        out.errorString().toStdString());
        return false;
    }
    return true;
}

QString readJsonOrEmpty(QString const& path, QString const& whatForLog) {
    if (path.isEmpty()) {
        return QStringLiteral("{}");
    }
    QFile in{path};
    if (!in.exists()) {
        return QStringLiteral("{}"); // first-load case — not an error
    }
    if (!in.open(QIODevice::ReadOnly)) {
        AJAZZ_LOG_ERROR("plugin-settings",
                        "{}: cannot open '{}' for read: {}",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        in.errorString().toStdString());
        return QStringLiteral("{}");
    }
    if (in.size() > kMaxSettingsBytes) {
        AJAZZ_LOG_ERROR("plugin-settings",
                        "{}: '{}' is {} bytes, exceeds cap; returning empty",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        static_cast<long long>(in.size()));
        return QStringLiteral("{}");
    }
    QByteArray const data = in.readAll();
    in.close();
    if (data.isEmpty()) {
        return QStringLiteral("{}");
    }
    QJsonParseError perr{};
    QJsonDocument::fromJson(data, &perr);
    if (perr.error != QJsonParseError::NoError) {
        AJAZZ_LOG_ERROR("plugin-settings",
                        "{}: '{}' is not valid JSON ({}); returning empty",
                        whatForLog.toStdString(),
                        path.toStdString(),
                        perr.errorString().toStdString());
        return QStringLiteral("{}");
    }
    return QString::fromUtf8(data);
}

/// Validate a candidate settings payload: parses as JSON and within the cap.
/// Returns the UTF-8 bytes on success, or an empty QByteArray on rejection.
QByteArray validatePayload(QString const& json, QString const& whatForLog) {
    QByteArray const bytes = json.toUtf8();
    if (bytes.size() > kMaxSettingsBytes) {
        AJAZZ_LOG_WARN("plugin-settings",
                       "{}: payload {} bytes exceeds cap; refusing",
                       whatForLog.toStdString(),
                       static_cast<long long>(bytes.size()));
        return {};
    }
    QJsonParseError perr{};
    QJsonDocument::fromJson(bytes, &perr);
    if (perr.error != QJsonParseError::NoError) {
        AJAZZ_LOG_WARN("plugin-settings",
                       "{}: payload is not valid JSON ({}); refusing",
                       whatForLog.toStdString(),
                       perr.errorString().toStdString());
        return {};
    }
    return bytes;
}

} // namespace

bool isSafeComponent(QString const& s) {
    if (s.isEmpty() || s.size() > 256) {
        return false;
    }
    for (QChar const c : s) {
        ushort const u = c.unicode();
        if (u < 0x20 || u == 0x7f) {
            return false; // ASCII control chars + NUL
        }
        if (u > 0x7e) {
            return false; // non-ASCII: reject
        }
        if (c == QLatin1Char('/') || c == QLatin1Char('\\')) {
            return false; // path separators on any platform
        }
    }
    if (s == QLatin1String(".") || s == QLatin1String("..")) {
        return false;
    }
    if (s.contains(QLatin1String(".."))) {
        return false;
    }
    return true;
}

QString readContext(QString const& pluginUuid, QString const& contextId) {
    return readJsonOrEmpty(perContextPath(pluginUuid, contextId), QStringLiteral("readContext"));
}

bool writeContext(QString const& pluginUuid, QString const& contextId, QString const& json) {
    QByteArray const bytes = validatePayload(json, QStringLiteral("writeContext"));
    if (bytes.isEmpty()) {
        return false;
    }
    return writeJsonAtomic(
        perContextPath(pluginUuid, contextId), bytes, QStringLiteral("writeContext"));
}

QString readGlobal(QString const& pluginUuid) {
    return readJsonOrEmpty(globalPathFor(pluginUuid), QStringLiteral("readGlobal"));
}

bool writeGlobal(QString const& pluginUuid, QString const& json) {
    QByteArray const bytes = validatePayload(json, QStringLiteral("writeGlobal"));
    if (bytes.isEmpty()) {
        return false;
    }
    return writeJsonAtomic(globalPathFor(pluginUuid), bytes, QStringLiteral("writeGlobal"));
}

} // namespace ajazz::app::plugin_settings_store
