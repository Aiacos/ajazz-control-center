// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_debug_service.hpp
 * @brief QML-exposed debug console for the plugin/device protocol.
 *
 * PluginDebugService is a developer-facing harness that makes the otherwise
 * invisible plugin runtime observable and drivable from the UI:
 *
 *   * **Log** — a rolling, timestamped, direction-tagged transcript of plugin
 *     protocol traffic (inbound actions from plugins, host->plugin events,
 *     device input events, Property Inspector settings). Application taps the
 *     existing SdPluginServer / StreamDockInputService signals into record().
 *
 *   * **Simulate commands** — inject synthetic device input (key / encoder /
 *     touch) into the same PluginDeviceBridge path real hardware uses, so a
 *     plugin's keyDown/keyUp/dialRotate handlers can be exercised WITHOUT a
 *     physical device (critical on the AKP05E demo unit, whose input path is
 *     unreachable — see CLAUDE.md).
 *
 *   * **Simulate responses** — inject a synthetic plugin->host action
 *     (setImage / setTitle / setSettings / ...) so the host-side handling and
 *     device repaint can be verified without a live plugin process.
 *
 * Exposed to QML as the `PluginDebug` singleton; the DebugConsole.qml drawer
 * renders the log and the simulate controls. The bridge/server-touching paths
 * compile to safe no-ops when Qt WebSockets is absent (AJAZZ_HAVE_WEBSOCKETS
 * undefined), so the console still shows the log on minimal builds.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>

#include <type_traits>

class QJSEngine;
class QQmlEngine;

namespace ajazz::app {

#ifdef AJAZZ_HAVE_WEBSOCKETS
class SdPluginServer;
class PluginDeviceBridge;
#endif
class StreamDockInputService;

/**
 * @class PluginDebugService
 * @brief Observable + drivable harness for the plugin/device protocol.
 */
class PluginDebugService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PluginDebug)
    QML_SINGLETON

    /// Rolling transcript, newest first, capped at @ref kMaxLines entries.
    Q_PROPERTY(QStringList lines READ lines NOTIFY linesChanged)
    /// Convenience for the QML empty-state.
    Q_PROPERTY(int count READ count NOTIFY linesChanged)

public:
    /// QML singleton factory — see BrandingService::create for the pattern.
    static PluginDebugService* create(QQmlEngine* qml, QJSEngine* js);
    /// Hand the singleton instance to the QML factory.
    static void registerInstance(PluginDebugService* instance) noexcept;

    // No default `parent` — same QML_SINGLETON dual-instance trap as the other
    // services (a default-constructible singleton bypasses create()).
    explicit PluginDebugService(QObject* parent);

    [[nodiscard]] QStringList lines() const { return m_lines; }
    [[nodiscard]] int count() const { return static_cast<int>(m_lines.size()); }

    /// Wire the live protocol objects this service taps + drives. Non-owning;
    /// all must outlive the service (they are Application-owned). Connects the
    /// auto-log taps. Safe to call with nullptrs (logging-only mode).
    void attach(
#ifdef AJAZZ_HAVE_WEBSOCKETS
        SdPluginServer* server,
        PluginDeviceBridge* bridge,
#endif
        StreamDockInputService* input);

    // ---- Logging ----------------------------------------------------------

    /// Append one transcript line. @p direction is a short tag (e.g. "in",
    /// "out", "dev", "sim", "pi"); @p category groups by subsystem; @p text is
    /// the human-readable detail. Thread-affine to the GUI thread.
    Q_INVOKABLE void record(QString const& direction, QString const& category, QString const& text);

    /// Clear the transcript.
    Q_INVOKABLE void clear();

    // ---- Simulate commands (host/device -> plugin) ------------------------

    /// Inject a synthetic key press/release for @p keyIndex (1-based, as the
    /// device reports). Routes through PluginDeviceBridge::onDeviceEvent so a
    /// bound plugin receives keyDown/keyUp exactly as from real hardware.
    Q_INVOKABLE void simulateKey(int keyIndex, bool pressed);

    /// Inject a synthetic encoder rotation (@p delta signed ticks) or press.
    Q_INVOKABLE void simulateEncoder(int encoderIndex, int delta);
    Q_INVOKABLE void simulateEncoderPress(int encoderIndex, bool pressed);

    /// Inject a synthetic touch-strip event at X coordinate @p x (0..255).
    /// @p phase: 0 = down, 1 = move, 2 = up.
    Q_INVOKABLE void simulateTouch(int x, int phase);

    // ---- Simulate responses (plugin -> host) ------------------------------

    /// Inject a synthetic plugin->host action JSON (e.g.
    /// {"event":"setTitle","context":"...","payload":{"title":"Hi"}}) for the
    /// plugin @p pluginUuid, as if the plugin had sent it over the socket.
    Q_INVOKABLE void simulatePluginAction(QString const& pluginUuid, QString const& json);

    /// The codename of the device input is currently routed to (for the UI to
    /// show what simulateKey targets). Empty when no device is active.
    [[nodiscard]] Q_INVOKABLE QString activeDeviceId() const;

signals:
    void linesChanged();

private:
    void append(QString const& direction, QString const& category, QString const& text);

    static constexpr int kMaxLines = 500;

    QStringList m_lines;
    StreamDockInputService* m_input = nullptr; // non-owning
#ifdef AJAZZ_HAVE_WEBSOCKETS
    SdPluginServer* m_server = nullptr;     // non-owning
    PluginDeviceBridge* m_bridge = nullptr; // non-owning
#endif
};

// See BrandingService static_assert — same QML_SINGLETON dual-instance trap.
static_assert(!std::is_default_constructible_v<PluginDebugService>,
              "PluginDebugService must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
