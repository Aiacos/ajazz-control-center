// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file debug_control_facade.cpp
 * @brief Phase 2a control-method registrations: log + state + inventory.
 */
#include "debug_control_facade.hpp"

#include "ajazz/core/device_registry.hpp"
#include "ajazz/core/log_sinks.hpp"
#include "ajazz/core/logger.hpp"
#include "application.hpp"
#include "debug_control_server.hpp"
#include "debug_logging.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace ajazz::app {
namespace {

/// Lowercase, trimmed level name for JSON (the core levelLabel is padded).
QString levelToString(core::LogLevel level) {
    switch (level) {
    case core::LogLevel::Trace:
        return QStringLiteral("trace");
    case core::LogLevel::Debug:
        return QStringLiteral("debug");
    case core::LogLevel::Info:
        return QStringLiteral("info");
    case core::LogLevel::Warn:
        return QStringLiteral("warn");
    case core::LogLevel::Error:
        return QStringLiteral("error");
    case core::LogLevel::Critical:
        return QStringLiteral("critical");
    }
    return QStringLiteral("info");
}

QJsonObject recordToJson(core::LogRecord const& rec) {
    return QJsonObject{
        {"ts", static_cast<double>(rec.epochMs)},
        {"level", levelToString(rec.level)},
        {"module", QString::fromStdString(rec.module)},
        {"msg", QString::fromStdString(rec.message)},
    };
}

QJsonObject descriptorToJson(core::DeviceDescriptor const& d, bool connected) {
    return QJsonObject{
        {"codename", QString::fromStdString(d.codename)},
        {"model", QString::fromStdString(d.model)},
        {"vendorId", static_cast<int>(d.vendorId)},
        {"productId", static_cast<int>(d.productId)},
        {"family", static_cast<int>(d.family)},
        {"keyCount", static_cast<int>(d.keyCount)},
        {"gridColumns", static_cast<int>(d.gridColumns)},
        {"keyRows", static_cast<int>(d.keyRows)},
        {"encoderCount", static_cast<int>(d.encoderCount)},
        {"hasTouchStrip", d.hasTouchStrip},
        {"touchZoneCount", static_cast<int>(d.touchZoneCount)},
        {"hasRgb", d.hasRgb},
        {"hasBattery", d.hasBattery},
        {"hasSettings", d.hasSettings},
        {"connected", connected},
    };
}

} // namespace

void registerDebugControlMethods(DebugControlServer& server, Application& app) {
    // ---- Logs ----------------------------------------------------------
    server.registerMethod("log.tail", [&app](QJsonObject const& params, QString& error) {
        auto* ring = app.logRing();
        if (ring == nullptr) {
            error = QStringLiteral("log ring not initialised");
            return QJsonObject{};
        }
        core::LogLevel const minLevel =
            parseLogLevel(params.value("level").toString(), core::LogLevel::Trace);
        auto const sinceMs = static_cast<long long>(params.value("sinceMs").toDouble(0));

        auto records = ring->snapshot(minLevel, sinceMs);

        // Optional tail limit: keep only the newest `limit` records.
        int const limit = params.value("limit").toInt(0);
        if (limit > 0 && static_cast<int>(records.size()) > limit) {
            records.erase(records.begin(), records.end() - limit);
        }

        QJsonArray lines;
        for (auto const& rec : records) {
            lines.append(recordToJson(rec));
        }
        long long const cursor = records.empty() ? sinceMs : records.back().epochMs;
        return QJsonObject{{"lines", lines}, {"cursor", static_cast<double>(cursor)}};
    });

    server.registerMethod("log.setLevel", [](QJsonObject const& params, QString& error) {
        QString const name = params.value("level").toString();
        if (name.isEmpty()) {
            error = QStringLiteral("missing 'level'");
            return QJsonObject{};
        }
        // Sentinel detects an unrecognised name (parse falls back to it).
        core::LogLevel const parsed = parseLogLevel(name, core::LogLevel::Critical);
        if (parsed == core::LogLevel::Critical && name.toLower() != QStringLiteral("critical") &&
            name.toLower() != QStringLiteral("crit")) {
            error = QStringLiteral("unknown level: ") + name;
            return QJsonObject{};
        }
        core::setLogLevel(parsed);
        return QJsonObject{{"level", levelToString(parsed)}};
    });

    server.registerMethod("log.clear", [&app](QJsonObject const&, QString& error) {
        auto* ring = app.logRing();
        if (ring == nullptr) {
            error = QStringLiteral("log ring not initialised");
            return QJsonObject{};
        }
        ring->clear();
        return QJsonObject{{"cleared", true}};
    });

    server.registerMethod("log.file", [&app](QJsonObject const&, QString&) {
        return QJsonObject{{"path", app.logFilePath()}};
    });

    // ---- Inventory + state --------------------------------------------
    server.registerMethod("device.list", [&app](QJsonObject const&, QString&) {
        auto const descriptors = app.deviceRegistry().enumerate();
        auto const connectedKeys = app.deviceRegistry().enumerateConnectedHidKeys();
        QJsonArray arr;
        for (auto const& d : descriptors) {
            bool const connected =
                connectedKeys.find({d.vendorId, d.productId}) != connectedKeys.end();
            arr.append(descriptorToJson(d, connected));
        }
        return QJsonObject{{"devices", arr}};
    });

    server.registerMethod("state", [&app, &server](QJsonObject const&, QString&) {
        auto const descriptors = app.deviceRegistry().enumerate();
        auto const connectedKeys = app.deviceRegistry().enumerateConnectedHidKeys();
        int connectedCount = 0;
        for (auto const& d : descriptors) {
            if (connectedKeys.find({d.vendorId, d.productId}) != connectedKeys.end()) {
                ++connectedCount;
            }
        }
        return QJsonObject{
            {"logFile", app.logFilePath()},
            {"logLevel", levelToString(core::logLevel())},
            {"deviceCount", static_cast<int>(descriptors.size())},
            {"connectedCount", connectedCount},
            {"methodCount", static_cast<int>(server.methodNames().size())},
        };
    });
}

} // namespace ajazz::app
