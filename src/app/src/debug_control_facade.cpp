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
#include "builtin_actions_service.hpp"
#include "debug_control_server.hpp"
#include "debug_logging.hpp"
#include "plugin_debug_service.hpp"
#include "profile_controller.hpp"
#include "stream_dock_control_service.hpp"

#ifdef AJAZZ_HAVE_WEBSOCKETS
#include "sd_plugin_server.hpp"
#endif

#include <QColor>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QPainter>
#include <QString>
#include <QVariant>

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

    // ---- Device output control (StreamDockControlService) --------------
    server.registerMethod("device.setActiveDevice",
                          [&app](QJsonObject const& params, QString& err) {
                              auto* control = app.streamDockControl();
                              if (control == nullptr) {
                                  err = QStringLiteral("stream dock control unavailable");
                                  return QJsonObject{};
                              }
                              QString const codename = params.value("codename").toString();
                              if (codename.isEmpty()) {
                                  err = QStringLiteral("missing 'codename'");
                                  return QJsonObject{};
                              }
                              control->setActiveDevice(codename);
                              return QJsonObject{{"activeDevice", codename}};
                          });

    server.registerMethod("device.setBrightness", [&app](QJsonObject const& params, QString& err) {
        auto* control = app.streamDockControl();
        if (control == nullptr) {
            err = QStringLiteral("stream dock control unavailable");
            return QJsonObject{};
        }
        QString const codename = params.value("codename").toString();
        if (codename.isEmpty()) {
            err = QStringLiteral("missing 'codename'");
            return QJsonObject{};
        }
        if (!params.contains("percent")) {
            err = QStringLiteral("missing 'percent'");
            return QJsonObject{};
        }
        int const percent = qBound(0, params.value("percent").toInt(), 100);
        control->setBrightness(codename, percent);
        return QJsonObject{{"codename", codename}, {"percent", percent}};
    });

    server.registerMethod("device.clearAll", [&app](QJsonObject const& params, QString& err) {
        auto* control = app.streamDockControl();
        if (control == nullptr) {
            err = QStringLiteral("stream dock control unavailable");
            return QJsonObject{};
        }
        QString const codename = params.value("codename").toString();
        if (codename.isEmpty()) {
            err = QStringLiteral("missing 'codename'");
            return QJsonObject{};
        }
        control->clearAll(codename);
        return QJsonObject{{"cleared", codename}};
    });

    // device.renderTest {codename, count?=10, main?=false} -> paints a numbered
    // colour swatch onto each key so the BAT key-wire -> physical-surface mapping
    // (commit 037bd8d) can be read off the live panel. Drives the real image path
    // (assignKeyImage: RGBA -> JPEG -> BAT -> 1024-byte chunks -> ULEND) via the
    // public control seam -- NOT a raw HID write, so the wire-format hard rule holds.
    server.registerMethod("device.renderTest", [&app](QJsonObject const& params, QString& err) {
        auto* control = app.streamDockControl();
        if (control == nullptr) {
            err = QStringLiteral("stream dock control unavailable");
            return QJsonObject{};
        }
        QString const codename = params.value("codename").toString();
        if (codename.isEmpty()) {
            err = QStringLiteral("missing 'codename'");
            return QJsonObject{};
        }
        control->setActiveDevice(codename);
        int const count =
            params.contains("count") ? qBound(1, params.value("count").toInt(), 32) : 10;
        for (int i = 1; i <= count; ++i) {
            QImage img(85, 85, QImage::Format_RGBA8888);
            img.fill(QColor::fromHsv(((i - 1) * 360) / count, 220, 235));
            QPainter p(&img);
            p.setRenderHint(QPainter::TextAntialiasing, true);
            p.setPen(Qt::white);
            QFont f = p.font();
            f.setPixelSize(44);
            f.setBold(true);
            p.setFont(f);
            p.drawText(img.rect(), Qt::AlignCenter, QString::number(i));
            p.end();
            control->assignKeyImage(static_cast<std::uint8_t>(i), img);
        }
        bool const withMain = params.value("main").toBool(false);
        if (withMain) {
            QImage strip(800, 100, QImage::Format_RGBA8888);
            strip.fill(QColor(18, 18, 26));
            QPainter p(&strip);
            p.setRenderHint(QPainter::TextAntialiasing, true);
            p.setPen(Qt::white);
            QFont f = p.font();
            f.setPixelSize(52);
            p.setFont(f);
            p.drawText(strip.rect(), Qt::AlignCenter, codename);
            p.end();
            control->assignMainImage(strip);
        }
        // encoders:true -> paint the 4 touch-strip zones (assignEncoderImage 0..3),
        // each a distinct colour labelled E1..E4, so the BAT-wire-1..4 strip path
        // can be verified on the live panel.
        bool const withEncoders = params.value("encoders").toBool(false);
        if (withEncoders) {
            for (int e = 0; e < 4; ++e) {
                QImage z(128, 128, QImage::Format_RGBA8888);
                z.fill(QColor::fromHsv((e * 90) % 360, 200, 230));
                QPainter p(&z);
                p.setRenderHint(QPainter::TextAntialiasing, true);
                p.setPen(Qt::white);
                QFont f = p.font();
                f.setPixelSize(56);
                f.setBold(true);
                p.setFont(f);
                p.drawText(z.rect(), Qt::AlignCenter, QStringLiteral("E%1").arg(e + 1));
                p.end();
                control->assignEncoderImage(static_cast<std::uint8_t>(e), z);
            }
        }
        return QJsonObject{{"rendered", count}, {"main", withMain}, {"encoders", withEncoders}};
    });

    // ---- Input simulation (PluginDebugService) -------------------------
    // Routes synthetic device events through the same path real input takes
    // (PluginDeviceBridge::onDeviceEvent), so it drives actions + plugins.
    server.registerMethod("input.key", [&app](QJsonObject const& params, QString& err) {
        auto* dbg = app.pluginDebug();
        if (dbg == nullptr) {
            err = QStringLiteral("plugin debug service unavailable");
            return QJsonObject{};
        }
        bool const pressed = params.value("pressed").toBool(true);
        dbg->simulateKey(params.value("index").toInt(), pressed);
        return QJsonObject{{"index", params.value("index").toInt()}, {"pressed", pressed}};
    });

    server.registerMethod("input.encoder", [&app](QJsonObject const& params, QString& err) {
        auto* dbg = app.pluginDebug();
        if (dbg == nullptr) {
            err = QStringLiteral("plugin debug service unavailable");
            return QJsonObject{};
        }
        dbg->simulateEncoder(params.value("index").toInt(), params.value("delta").toInt(1));
        return QJsonObject{{"index", params.value("index").toInt()},
                           {"delta", params.value("delta").toInt(1)}};
    });

    server.registerMethod("input.encoderPress", [&app](QJsonObject const& params, QString& err) {
        auto* dbg = app.pluginDebug();
        if (dbg == nullptr) {
            err = QStringLiteral("plugin debug service unavailable");
            return QJsonObject{};
        }
        bool const pressed = params.value("pressed").toBool(true);
        dbg->simulateEncoderPress(params.value("index").toInt(), pressed);
        return QJsonObject{{"index", params.value("index").toInt()}, {"pressed", pressed}};
    });

    server.registerMethod("input.touch", [&app](QJsonObject const& params, QString& err) {
        auto* dbg = app.pluginDebug();
        if (dbg == nullptr) {
            err = QStringLiteral("plugin debug service unavailable");
            return QJsonObject{};
        }
        // phase: 0=down 1=move 2=up (matches DeviceEvent touch phases).
        dbg->simulateTouch(params.value("x").toInt(), params.value("phase").toInt());
        return QJsonObject{{"x", params.value("x").toInt()},
                           {"phase", params.value("phase").toInt()}};
    });

    // ---- Profile control (ProfileController) ---------------------------
    server.registerMethod("profile.list", [&app](QJsonObject const&, QString& err) {
        auto* pc = app.profileController();
        if (pc == nullptr) {
            err = QStringLiteral("profile controller unavailable");
            return QJsonObject{};
        }
        QJsonArray profiles;
        for (auto const& id : pc->knownProfileIds()) {
            profiles.append(QJsonObject{{"id", id}, {"name", pc->profileNameFor(id)}});
        }
        return QJsonObject{{"profiles", profiles},
                           {"active", pc->activeProfileId()},
                           {"activeName", pc->activeProfileName()}};
    });

    server.registerMethod("profile.active", [&app](QJsonObject const&, QString& err) {
        auto* pc = app.profileController();
        if (pc == nullptr) {
            err = QStringLiteral("profile controller unavailable");
            return QJsonObject{};
        }
        return QJsonObject{{"id", pc->activeProfileId()}, {"name", pc->activeProfileName()}};
    });

    server.registerMethod("profile.load", [&app](QJsonObject const& params, QString& err) {
        auto* pc = app.profileController();
        if (pc == nullptr) {
            err = QStringLiteral("profile controller unavailable");
            return QJsonObject{};
        }
        QString const id = params.value("id").toString();
        if (id.isEmpty()) {
            err = QStringLiteral("missing 'id'");
            return QJsonObject{};
        }
        pc->loadProfileById(id);
        return QJsonObject{{"active", pc->activeProfileId()}};
    });

    server.registerMethod("profile.create", [&app](QJsonObject const& params, QString& err) {
        auto* pc = app.profileController();
        if (pc == nullptr) {
            err = QStringLiteral("profile controller unavailable");
            return QJsonObject{};
        }
        QString const name = params.value("name").toString();
        QString const codename = params.value("codename").toString();
        if (name.isEmpty() || codename.isEmpty()) {
            err = QStringLiteral("require 'name' and 'codename'");
            return QJsonObject{};
        }
        return QJsonObject{{"id", pc->createProfile(name, codename)}};
    });

    // profile.commitEncoderBinding {index, actionId, label?, settings?}
    // -> {committed, index, actionId}
    // Drives ProfileController::commitEncoderBinding directly (ActionKind::Plugin=0)
    // so the PLUGIN-19/20 live round-trip is automatable without a real drag-and-drop.
    // Gated behind AJAZZ_DEBUG_CONTROL=1 (this block). No wire-format change.
    server.registerMethod(
        "profile.commitEncoderBinding", [&app](QJsonObject const& params, QString& err) {
            auto* pc = app.profileController();
            if (pc == nullptr) {
                err = QStringLiteral("profile controller unavailable");
                return QJsonObject{};
            }
            int const index = params.value("index").toInt(0);
            QString const actionId = params.value("actionId").toString();
            if (actionId.isEmpty()) {
                err = QStringLiteral("require 'actionId'");
                return QJsonObject{};
            }
            QString const label = params.value("label").toString();
            QString const settings = params.value("settings").toString();
            // ActionKind::Plugin = 0 (profile.hpp:43)
            pc->commitEncoderBinding(
                index, QStringLiteral(""), label, 0 /*ActionKind::Plugin*/, settings, actionId);
            return QJsonObject{{"committed", true}, {"index", index}, {"actionId", actionId}};
        });

    // ---- Action execution (BuiltinActionsService) ---------------------
    // Dangerous: built-in UUIDs include RunCommand/OpenUrl etc.; unknown
    // UUIDs forward to the plugin path. Gated by the channel being on.
    server.registerMethod("action.run", [&app](QJsonObject const& params, QString& err) {
        auto* builtins = app.builtinActions();
        if (builtins == nullptr) {
            err = QStringLiteral("builtin actions service unavailable");
            return QJsonObject{};
        }
        QString const id = params.value("id").toString();
        if (id.isEmpty()) {
            err = QStringLiteral("missing 'id' (action UUID)");
            return QJsonObject{};
        }
        // settings may be a JSON object (re-serialised) or a verbatim string.
        QString settingsJson;
        QJsonValue const settings = params.value("settings");
        if (settings.isObject()) {
            settingsJson = QString::fromUtf8(
                QJsonDocument(settings.toObject()).toJson(QJsonDocument::Compact));
        } else if (settings.isString()) {
            settingsJson = settings.toString();
        } else {
            settingsJson = QStringLiteral("{}");
        }
        builtins->onPluginAction(id.toStdString(), settingsJson.toStdString());
        return QJsonObject{{"dispatched", id}};
    });

    // raw.hidWrite is intentionally a NOT-IMPLEMENTED stub: IDevice exposes
    // no public raw-write seam, and adding one crosses the "RE is the source
    // of truth for wire format" project hard rule (an arbitrary byte write
    // bypasses every opcode/packet-layout invariant). Surfaced so the method
    // is discoverable and returns an honest error rather than silently
    // missing. Wire it deliberately, with an RE cross-check, if ever needed.
    server.registerMethod("raw.hidWrite", [](QJsonObject const&, QString& err) {
        err = QStringLiteral(
            "raw.hidWrite not implemented: no public IDevice raw-write seam; adding one "
            "must go through an RE cross-check (wire-format hard rule)");
        return QJsonObject{};
    });

    // ---- Plugin host (SdPluginServer) ----------------------------------
#ifdef AJAZZ_HAVE_WEBSOCKETS
    server.registerMethod("plugin.list", [&app](QJsonObject const&, QString& err) {
        auto* srv = app.pluginServer();
        if (srv == nullptr) {
            err = QStringLiteral("plugin server unavailable");
            return QJsonObject{};
        }
        return QJsonObject{{"listening", srv->isListening()},
                           {"port", static_cast<int>(srv->serverPort())},
                           {"connectedCount", srv->connectedPluginCount()}};
    });

    // plugin.installedActions {} -> {count, actions, diagnostics}
    // Returns every visible installed action with its full QVariantMap (same
    // shape as PluginCatalogModel::installedActions()) plus the lastScanDiagnostics
    // counters. Enables live PLUGIN-18 verification without a UI drag-and-drop.
    // Gated behind AJAZZ_DEBUG_CONTROL=1 (this block). No secrets in payload.
    server.registerMethod("plugin.installedActions", [&app](QJsonObject const&, QString& err) {
        auto* cat = app.pluginCatalog();
        if (cat == nullptr) {
            err = QStringLiteral("plugin catalog unavailable");
            return QJsonObject{};
        }
        QVariantList const actions = cat->installedActions();
        QJsonArray arr;
        for (QVariant const& v : actions) {
            arr.append(QJsonObject::fromVariantMap(v.toMap()));
        }
        return QJsonObject{{QStringLiteral("count"), static_cast<int>(actions.size())},
                           {QStringLiteral("actions"), arr},
                           {QStringLiteral("diagnostics"),
                            QJsonObject::fromVariantMap(cat->lastScanDiagnostics())}};
    });

    server.registerMethod("plugin.sendEvent", [&app](QJsonObject const& params, QString& err) {
        auto* srv = app.pluginServer();
        if (srv == nullptr) {
            err = QStringLiteral("plugin server unavailable");
            return QJsonObject{};
        }
        QString const uuid = params.value("uuid").toString();
        QString const event = params.value("event").toString();
        if (uuid.isEmpty() || event.isEmpty()) {
            err = QStringLiteral("require 'uuid' and 'event'");
            return QJsonObject{};
        }
        bool const sent = srv->sendEvent(uuid, event, params.value("payload").toObject());
        return QJsonObject{{"sent", sent}};
    });

    // plugin.installFromFile {path, confirm?} -> {installed} — drives the
    // PluginStore install pipeline from the shell so the install->spawn loop is
    // autonomously verifiable (CLAUDE.md debug-channel rule). `confirm:true`
    // supplies userConfirmedUnsigned (Unsigned branch only; a tampered/Refused
    // package is quarantined regardless — CR-01). On success the catalog emits
    // installFinished, which is wired to PluginManager::rediscover().
    server.registerMethod(
        "plugin.installFromFile", [&app](QJsonObject const& params, QString& err) {
            auto* cat = app.pluginCatalog();
            if (cat == nullptr) {
                err = QStringLiteral("plugin catalog unavailable");
                return QJsonObject{};
            }
            QString const path = params.value("path").toString();
            if (path.isEmpty()) {
                err = QStringLiteral("require 'path'");
                return QJsonObject{};
            }
            bool const confirm = params.value("confirm").toBool();
            bool const installed = cat->installFromFile(path, confirm);
            return QJsonObject{{"installed", installed}, {"confirm", confirm}, {"path", path}};
        });

    // plugin.rediscover {} -> {rediscovered, connectedCount} — idempotent
    // re-scan that spawns only newly-installed .sdPlugin plugins with no app
    // restart. connectedCount is sampled immediately; the WS register handshake
    // is async, so poll plugin.list again shortly after to see it climb.
    server.registerMethod("plugin.rediscover", [&app](QJsonObject const&, QString& err) {
        auto* mgr = app.pluginManager();
        if (mgr == nullptr) {
            err = QStringLiteral("plugin manager unavailable");
            return QJsonObject{};
        }
        mgr->rediscover();
        auto* srv = app.pluginServer();
        return QJsonObject{{"rediscovered", true},
                           {"connectedCount", srv != nullptr ? srv->connectedPluginCount() : 0}};
    });
#endif
}

} // namespace ajazz::app
