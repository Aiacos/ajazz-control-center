// SPDX-License-Identifier: GPL-3.0-or-later
/** @file sidecar_protocol.cpp
 *  @brief Implementation of the pure streamdock-host sidecar wire protocol.
 */
#include "sidecar_protocol.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>

namespace ajazz::app::sidecar {

namespace {

/// Serialise a JSON object to a compact single line terminated with '\n'.
QByteArray toLine(QJsonObject const& obj) {
    QByteArray out = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    out.append('\n');
    return out;
}

} // namespace

QByteArray buildPing() {
    return toLine(QJsonObject{{"cmd", "ping"}});
}

QByteArray buildSetBrightness(QString const& serial, std::uint8_t percent) {
    return toLine(QJsonObject{
        {"cmd", "set_brightness"},
        {"serial", serial},
        {"percent", static_cast<int>(std::min<std::uint8_t>(percent, 100))},
    });
}

QByteArray buildKeepAlive(QString const& serial) {
    return toLine(QJsonObject{
        {"cmd", "keep_alive"},
        {"serial", serial},
    });
}

QByteArray buildSetImage(QString const& serial,
                         std::uint8_t key,
                         bool touchzone,
                         std::uint16_t width,
                         std::uint16_t height,
                         std::span<std::uint8_t const> rgba) {
    // QByteArray view over the RGBA span, then standard base64.
    QByteArray const raw(reinterpret_cast<char const*>(rgba.data()),
                         static_cast<qsizetype>(rgba.size()));
    return toLine(QJsonObject{
        {"cmd", "set_image"},
        {"serial", serial},
        {"key", static_cast<int>(key)},
        {"touchzone", touchzone},
        {"width", static_cast<int>(width)},
        {"height", static_cast<int>(height)},
        {"rgba_b64", QString::fromLatin1(raw.toBase64())},
    });
}

QByteArray buildRenderTest(QString const& serial) {
    return toLine(QJsonObject{{"cmd", "render_test"}, {"serial", serial}});
}

std::optional<SidecarEvent> parseEvent(QByteArray const& jsonLine) {
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(jsonLine, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return std::nullopt;
    }
    QJsonObject const obj = doc.object();
    QJsonValue const evVal = obj.value("event");
    if (!evVal.isString()) {
        return std::nullopt;
    }

    SidecarEvent ev{};
    QString const name = evVal.toString();
    ev.serial = obj.value("serial").toString();
    ev.firmware = obj.value("firmware").toString();
    ev.message = obj.value("msg").toString();
    ev.rawHex = obj.value("raw").toString();
    ev.vid = static_cast<std::uint16_t>(obj.value("vid").toInt());
    ev.pid = static_cast<std::uint16_t>(obj.value("pid").toInt());
    ev.deviceCount = obj.value("device_count").toInt();
    ev.code = static_cast<std::uint8_t>(obj.value("code").toInt());
    ev.state = static_cast<std::uint8_t>(obj.value("state").toInt());

    if (name == "connected") {
        ev.type = SidecarEvent::Type::Connected;
    } else if (name == "disconnected") {
        ev.type = SidecarEvent::Type::Disconnected;
    } else if (name == "ready") {
        ev.type = SidecarEvent::Type::Ready;
    } else if (name == "input") {
        ev.type = SidecarEvent::Type::Input;
    } else if (name == "pong") {
        ev.type = SidecarEvent::Type::Pong;
    } else if (name == "ok") {
        ev.type = SidecarEvent::Type::Ok;
    } else if (name == "error" || name == "device_error") {
        ev.type = SidecarEvent::Type::Error;
    } else {
        ev.type = SidecarEvent::Type::Unknown;
    }
    return ev;
}

} // namespace ajazz::app::sidecar
