// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin_debug_service.hpp"

#include "ajazz/core/device.hpp"
#include "ajazz/core/logger.hpp"
#include "stream_dock_input_service.hpp"

#ifdef AJAZZ_HAVE_WEBSOCKETS
#include "plugin_device_bridge.hpp"
#include "sd_plugin_server.hpp"
#endif

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QTime>

namespace ajazz::app {

namespace {
PluginDebugService* s_instance = nullptr;

QString shortJson(QJsonObject const& obj) {
    auto const compact = QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
    return compact.size() > 240 ? compact.left(237) + QStringLiteral("...") : compact;
}
} // namespace

PluginDebugService* PluginDebugService::create(QQmlEngine* /*qml*/, QJSEngine* /*js*/) {
    Q_ASSERT_X(s_instance != nullptr,
               "PluginDebugService::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

void PluginDebugService::registerInstance(PluginDebugService* instance) noexcept {
    s_instance = instance;
}

PluginDebugService::PluginDebugService(QObject* parent) : QObject(parent) {}

void PluginDebugService::attach(
#ifdef AJAZZ_HAVE_WEBSOCKETS
    SdPluginServer* server,
    PluginDeviceBridge* bridge,
#endif
    StreamDockInputService* input) {
    m_input = input;

    // Auto-log device input as it flows to the plugin bridge.
    if (m_input != nullptr) {
        QObject::connect(m_input,
                         &StreamDockInputService::deviceEvent,
                         this,
                         [this](QString const& deviceId, ajazz::core::DeviceEvent const& ev) {
                             append(QStringLiteral("dev"),
                                    QStringLiteral("input"),
                                    QStringLiteral("%1 kind=%2 index=%3 value=%4")
                                        .arg(deviceId)
                                        .arg(static_cast<int>(ev.kind))
                                        .arg(ev.index)
                                        .arg(ev.value));
                         });
    }

#ifdef AJAZZ_HAVE_WEBSOCKETS
    m_server = server;
    m_bridge = bridge;
    if (m_server != nullptr) {
        QObject::connect(m_server,
                         &SdPluginServer::actionReceived,
                         this,
                         [this](QString const& uuid, QJsonObject const& action) {
                             append(QStringLiteral("in"),
                                    QStringLiteral("plugin"),
                                    QStringLiteral("%1  %2").arg(uuid, shortJson(action)));
                         });
        QObject::connect(m_server,
                         &SdPluginServer::unhandledEventReceived,
                         this,
                         [this](QString const& uuid, QString const& eventName) {
                             append(QStringLiteral("in"),
                                    QStringLiteral("plugin"),
                                    QStringLiteral("%1  unhandled: %2").arg(uuid, eventName));
                         });
        QObject::connect(
            m_server, &SdPluginServer::pluginRegistered, this, [this](QString const& uuid) {
                append(QStringLiteral("in"),
                       QStringLiteral("plugin"),
                       QStringLiteral("registered: %1").arg(uuid));
            });
        QObject::connect(
            m_server, &SdPluginServer::pluginDisconnected, this, [this](QString const& uuid) {
                append(QStringLiteral("in"),
                       QStringLiteral("plugin"),
                       QStringLiteral("disconnected: %1").arg(uuid));
            });
    }
#endif
    append(
        QStringLiteral("sim"), QStringLiteral("debug"), QStringLiteral("debug console attached"));
}

void PluginDebugService::append(QString const& direction,
                                QString const& category,
                                QString const& text) {
    QString const stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString const line = QStringLiteral("[%1] %2 %3: %4")
                             .arg(stamp, direction.toUpper().leftJustified(3), category, text);
    m_lines.prepend(line);
    while (m_lines.size() > kMaxLines) {
        m_lines.removeLast();
    }
    emit linesChanged();
}

void PluginDebugService::record(QString const& direction,
                                QString const& category,
                                QString const& text) {
    append(direction, category, text);
}

void PluginDebugService::clear() {
    m_lines.clear();
    emit linesChanged();
}

QString PluginDebugService::activeDeviceId() const {
    return m_input != nullptr ? m_input->activeDeviceCodename() : QString{};
}

void PluginDebugService::simulateKey(int keyIndex, bool pressed) {
#ifdef AJAZZ_HAVE_WEBSOCKETS
    ajazz::core::DeviceEvent ev{};
    ev.kind = pressed ? ajazz::core::DeviceEvent::Kind::KeyPressed
                      : ajazz::core::DeviceEvent::Kind::KeyReleased;
    ev.index = static_cast<std::uint16_t>(keyIndex);
    QString const id = activeDeviceId();
    append(QStringLiteral("sim"),
           QStringLiteral("input"),
           QStringLiteral("key %1 %2 -> %3").arg(keyIndex).arg(pressed ? "down" : "up").arg(id));
    if (m_input != nullptr) {
        // Drive the FULL real pipeline (built-in actions + plugin bridge +
        // debug log) via dispatch(), not just the plugin bridge directly.
        m_input->injectSyntheticEvent(ev);
    }
#else
    append(QStringLiteral("sim"),
           QStringLiteral("input"),
           QStringLiteral("key %1: plugin bridge unavailable (no WebSockets build)").arg(keyIndex));
#endif
}

void PluginDebugService::simulateEncoder(int encoderIndex, int delta) {
#ifdef AJAZZ_HAVE_WEBSOCKETS
    ajazz::core::DeviceEvent ev{};
    ev.kind = ajazz::core::DeviceEvent::Kind::EncoderTurned;
    ev.index = static_cast<std::uint16_t>(encoderIndex);
    ev.value = delta;
    QString const id = activeDeviceId();
    append(QStringLiteral("sim"),
           QStringLiteral("input"),
           QStringLiteral("encoder %1 delta %2 -> %3").arg(encoderIndex).arg(delta).arg(id));
    if (m_input != nullptr) {
        // Drive the FULL real pipeline (built-in actions + plugin bridge +
        // debug log) via dispatch(), not just the plugin bridge directly.
        m_input->injectSyntheticEvent(ev);
    }
#else
    Q_UNUSED(encoderIndex)
    Q_UNUSED(delta)
#endif
}

void PluginDebugService::simulateEncoderPress(int encoderIndex, bool pressed) {
#ifdef AJAZZ_HAVE_WEBSOCKETS
    ajazz::core::DeviceEvent ev{};
    ev.kind = pressed ? ajazz::core::DeviceEvent::Kind::EncoderPressed
                      : ajazz::core::DeviceEvent::Kind::EncoderReleased;
    ev.index = static_cast<std::uint16_t>(encoderIndex);
    QString const id = activeDeviceId();
    append(QStringLiteral("sim"),
           QStringLiteral("input"),
           QStringLiteral("encoder %1 %2 -> %3")
               .arg(encoderIndex)
               .arg(pressed ? "press" : "release")
               .arg(id));
    if (m_input != nullptr) {
        // Drive the FULL real pipeline (built-in actions + plugin bridge +
        // debug log) via dispatch(), not just the plugin bridge directly.
        m_input->injectSyntheticEvent(ev);
    }
#else
    Q_UNUSED(encoderIndex)
    Q_UNUSED(pressed)
#endif
}

void PluginDebugService::simulateTouch(int x, int phase) {
#ifdef AJAZZ_HAVE_WEBSOCKETS
    ajazz::core::DeviceEvent ev{};
    ev.kind = phase == 0   ? ajazz::core::DeviceEvent::Kind::TouchDown
              : phase == 1 ? ajazz::core::DeviceEvent::Kind::TouchMove
                           : ajazz::core::DeviceEvent::Kind::TouchUp;
    ev.value = x;
    QString const id = activeDeviceId();
    append(QStringLiteral("sim"),
           QStringLiteral("input"),
           QStringLiteral("touch x=%1 phase=%2 -> %3").arg(x).arg(phase).arg(id));
    if (m_input != nullptr) {
        // Drive the FULL real pipeline (built-in actions + plugin bridge +
        // debug log) via dispatch(), not just the plugin bridge directly.
        m_input->injectSyntheticEvent(ev);
    }
#else
    Q_UNUSED(x)
    Q_UNUSED(phase)
#endif
}

void PluginDebugService::simulatePluginAction(QString const& pluginUuid, QString const& json) {
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        append(QStringLiteral("sim"),
               QStringLiteral("plugin"),
               QStringLiteral("invalid JSON: %1").arg(err.errorString()));
        return;
    }
    append(QStringLiteral("sim"),
           QStringLiteral("plugin"),
           QStringLiteral("%1  %2").arg(pluginUuid, shortJson(doc.object())));
#ifdef AJAZZ_HAVE_WEBSOCKETS
    // Drive the real production fan-out via the server's actionReceived signal
    // (injectAction) instead of calling the bridge directly. The SAME simulation
    // then exercises BOTH the device-bridge visual handler (still reached — the
    // bridge wires server.actionReceived -> onAction) AND the host-level
    // openUrl/logMessage handler in Application, so a plugin->host action path is
    // verifiable end-to-end without a live plugin socket. Falls back to the
    // bridge only if no server is attached (defensive; not expected in the app).
    if (m_server != nullptr) {
        m_server->injectAction(pluginUuid, doc.object());
    } else if (m_bridge != nullptr) {
        m_bridge->onAction(pluginUuid, doc.object());
    }
#endif
}

} // namespace ajazz::app
