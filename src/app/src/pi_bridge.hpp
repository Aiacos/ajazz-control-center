// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_bridge.hpp
 * @brief JS-facing bridge object exposed as `\$SD` to Property Inspector pages.
 *
 * Each loaded Property Inspector HTML page gets its own @ref PIBridge,
 * registered on the page's @c QWebChannel under the name @c "$SD" so the
 * page's JavaScript can call e.g. `\$SD.setSettings(JSON.stringify({...}))`
 * exactly like a Stream Deck SDK-2 PI would.
 *
 * The bridge itself does no policy: every Q_INVOKABLE forwards to
 * @ref PropertyInspectorController, which owns the M4 persistence layer
 * and the M5 plugin-host WebSocket router. Keeping the bridge a thin
 * shim means the security boundary (what JS is allowed to ask the host
 * to do) lives in the controller, where it can be audited without
 * grepping through QtWebEngine plumbing.
 *
 * Lifetime: the bridge is parented to the @c QWebEnginePage it serves,
 * so it is destroyed automatically when @ref PropertyInspectorController
 * tears down the page on @ref PropertyInspectorController::closeInspector
 * or when a fresh page is loaded for a different action.
 *
 * Compiled only when @c AJAZZ_HAVE_WEBENGINE is defined (gate set by
 * CMake's @c find_package(Qt6 ... WebEngineQuick) probe). Builds without
 * Qt WebEngine never see this header.
 *
 * @see docs/architecture/PLUGIN-SDK.md (Property Inspector wire protocol —
 *      the JS API mapped here is a subset of the Stream Deck SDK-2 surface).
 */
#pragma once

#include <QObject>
#include <QString>

namespace ajazz::app {

class PropertyInspectorController;

/**
 * @class PIBridge
 * @brief Stream Deck SDK-2-compatible JS API exposed via QWebChannel.
 *
 * Method surface mirrors the `streamDeckClient` / `\$SD` global the
 * Elgato SDK exposes to PI HTML pages, so an existing Stream Deck or
 * OpenDeck PI runs unchanged once it has been pointed at our channel.
 *
 * For M3 every method is a pass-through to the controller that logs the
 * payload via @c AJAZZ_LOG_INFO; later milestones plug the C++ side into
 * actual settings persistence (M4) and the plugin host's WebSocket
 * router (M5). Signals are wired but not yet emitted by the M3 path.
 */
class PIBridge : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PIBridge)

public:
    /**
     * @param controller   Back-pointer for routing JS calls to host logic;
     *                     must outlive the bridge (the controller owns the
     *                     page that owns this bridge).
     * @param pluginUuid   The plugin manifest UUID — keys settings storage
     *                     and the plugin-host route in M5.
     * @param actionUuid   The action UUID (manifest @c Actions[].UUID) —
     *                     used in the @c connected() handshake payload.
     * @param contextUuid  Per-key/per-encoder context UUID — Stream Deck's
     *                     `context` parameter in every wire-protocol event.
     * @param parent       QObject parent for memory management (typically
     *                     the @c QWebEnginePage the bridge serves).
     */
    PIBridge(PropertyInspectorController* controller,
             QString pluginUuid,
             QString actionUuid,
             QString contextUuid,
             QObject* parent = nullptr);

    ~PIBridge() override;

    // -- Stream Deck SDK-2 method surface (callable from PI JS as `$SD.xxx`).

    /// Persist per-context settings JSON for this PI's action context.
    Q_INVOKABLE void setSettings(QString const& json);

    /// Ask the host to emit @c didReceiveSettings with the current
    /// per-context settings JSON. M4 reads from disk; M3 returns `{}`.
    Q_INVOKABLE void getSettings();

    /// Persist plugin-wide settings shared across every action context.
    Q_INVOKABLE void setGlobalSettings(QString const& json);

    /// Ask the host to emit @c didReceiveGlobalSettings with the current
    /// plugin-wide settings JSON.
    Q_INVOKABLE void getGlobalSettings();

    /// Forward an arbitrary JSON payload to the plugin process. M5 routes
    /// it over the plugin-host WebSocket; M3 logs it.
    Q_INVOKABLE void sendToPlugin(QString const& json);

    /// Update the rendered title of one or all key contexts.
    Q_INVOKABLE void setTitle(QString const& title, QString const& context, int target);

    /// Update the rendered image of one or all key contexts.
    /// @c imageData is a base64-encoded PNG/SVG per Stream Deck SDK-2.
    Q_INVOKABLE void setImage(QString const& imageData, QString const& context, int target);

    /// Open a URL in the user's default external browser. Validated against
    /// a host-side allowlist before dispatch.
    Q_INVOKABLE void openUrl(QString const& url);

    /// Append a line to the host's log at @c info severity. Useful for
    /// plugin developers debugging their PI from the browser console.
    Q_INVOKABLE void logMessage(QString const& message);

    /**
     * @brief Generic §8 invoke dispatcher — maps `{event,...}` JSON onto the
     *        existing typed Q_INVOKABLE slots.
     *
     * Called by the cefQuery JS polyfill (pi_cef_shim.hpp) via
     * `channel.objects["$SD"].invoke(opts.request)` — the §8-verbatim mapping
     * (akp_plugin_sdk.md §8: `cefQuery({request,...})` → `bridge.invoke(json).then(...)`).
     *
     * The dispatch table is CLOSED: unknown events log at WARN and return `"{}"`.
     * No default-to-arbitrary path; no C++ outside the fixed typed-slot set is
     * ever reached from this method.
     *
     * @param json A JSON object string carrying at minimum an `"event"` key.
     * @return `"{}"` always (the typed slots emit signals; callers use those).
     */
    Q_INVOKABLE QString invoke(QString const& json);

    /// Deliver a plugin-originated `sendToPropertyInspector` payload to the PI page
    /// (emits the sendToPropertyInspector signal the PI JS is bound to). Called by
    /// Application when the host relays the event from the plugin's WebSocket — the
    /// plugin->PI half of the SDK relay (the PI->plugin half is sendToPlugin).
    void deliverToPropertyInspector(QString const& json);

    /// The plugin UUID this PI bridge belongs to (for relay target matching).
    [[nodiscard]] QString pluginUuid() const { return pluginUuid_; }

signals:
    /// Emitted when @ref getSettings completes (M4) or after the plugin
    /// process pushes new settings (M5). JS subscribes to receive updates.
    void didReceiveSettings(QString json);

    /// Plugin-wide counterpart to @c didReceiveSettings.
    void didReceiveGlobalSettings(QString json);

    /// Forward a payload from the plugin process to this PI's JS.
    void sendToPropertyInspector(QString json);

    /// Emitted when the PI calls sendToPlugin (§4.3). Carries the plugin
    /// UUID and the raw JSON payload so the Application can route the
    /// frame to the correct SdPluginServer::sendEvent target without
    /// exposing a raw SdPluginServer* to the bridge.
    ///
    /// The Application wiring is: connect(bridge, &PIBridge::toPluginRequested,
    /// [server](QString uuid, QString json) { server->sendEvent(uuid, "sendToPlugin",
    ///     QJsonDocument::fromJson(json.toUtf8()).object()); });
    ///
    /// Phase 20 / 17-02 STOP gate: wired at Application layer when
    /// SdPluginServer::sendEvent is present (17-02-SUMMARY.md exists).
    void toPluginRequested(QString pluginUuid, QString json);

    /// Stream Deck SDK-2 handshake event — emitted once the page reports
    /// the channel is ready, carrying the JSON-encoded action info and
    /// app info envelope the SDK pages expect.
    void connected(QString actionInfo, QString appInfo);

private:
    PropertyInspectorController* controller_;
    QString pluginUuid_;
    QString actionUuid_;
    QString contextUuid_;
};

} // namespace ajazz::app
