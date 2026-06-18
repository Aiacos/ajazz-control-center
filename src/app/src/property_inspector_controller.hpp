// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file property_inspector_controller.hpp
 * @brief Controller that hosts plugin-authored Property Inspector HTML pages.
 *
 * The Property Inspector ("PI") is the per-action settings panel a plugin
 * author ships as HTML + JS, modelled on the Stream Deck SDK so that
 * existing Stream Deck / OpenDeck plugins keep their PI assets untouched.
 *
 * This controller owns the Qt WebEngine plumbing — one isolated
 * @c QQuickWebEngineProfile per plugin UUID, a @c QQmlWebChannel that
 * exposes a @c \$SD bridge object to the page's JS, and a URL request
 * interceptor scoped to the PI directory. It is exposed to QML as the
 * @c PropertyInspectorController singleton and consumed by
 * @c PropertyInspector.qml.
 *
 * Why the QML wrapper types: QtWebEngineQuick's @c WebEngineView (the
 * QML element) does **not** expose a @c page property — its only
 * QML-bindable handles to the underlying Chromium page are
 * @c profile (@c QQuickWebEngineProfile*), @c webChannel
 * (@c QQmlWebChannel*) and @c url (@c QUrl). Earlier revisions tried to
 * own a @c QWebEnginePage* directly and assign it via QML, but that
 * binding was silently inert at runtime (and tripped a qmllint
 * @c missing-property warning).
 *
 * Build gating: the actual WebEngine code paths only compile when
 * @c AJAZZ_HAVE_WEBENGINE is defined (driven by a successful
 * @c find_package(Qt6 ... WebEngineQuick WebChannelQuick) probe in CMake).
 * When the macro is absent every method becomes a no-op so the rest of
 * the application links unchanged on minimal Qt installs that don't ship
 * Qt WebEngine.
 *
 * @see docs/architecture/PLUGIN-SDK.md (the over-the-wire protocol the
 *      bridge eventually forwards messages to)
 * @see TODO.md "Property Inspector embedding" — milestone tracking.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QtCore/qmetatype.h>
#include <QtQmlIntegration>
#include <QUrl>

#include <memory>
#include <type_traits>

class QJSEngine;
class QQmlEngine;

// Qt WebEngine and the QML-side WebChannel are optional (see
// AJAZZ_BUILD_PROPERTY_INSPECTOR in CMake). When the macro is set we
// pull in the real `QQuickWebEngineProfile` and `QQmlWebChannel`
// headers so moc, qmltyperegistrar and qmllint all see fully-defined
// types — that is what makes the QML bindings in `PIWebView.qml`
// (`profile:`, `webChannel:`, `url:`) statically resolvable.
//
// When the macro is unset we keep forward-decls + opaque-pointer shims
// so the rest of the application links unchanged on minimal Qt installs
// that don't ship Qt WebEngine. `Q_DECLARE_OPAQUE_POINTER` tells
// QMetaType to skip the "must be a fully-defined type" assert triggered
// by the matching `Q_PROPERTY(... *)` lines below.
#if defined(AJAZZ_HAVE_WEBENGINE)
#include <QtWebChannelQuick/QQmlWebChannel>
#include <QtWebEngineQuick/QQuickWebEngineProfile>
#else
class QQuickWebEngineProfile;
class QQmlWebChannel;
Q_DECLARE_OPAQUE_POINTER(QQuickWebEngineProfile*)
Q_DECLARE_OPAQUE_POINTER(QQmlWebChannel*)
#endif

// `PIBridge*` is exposed by the `activeBridgeChanged` signal below. Its full
// definition (pi_bridge.hpp) is included only in the .cpp (and only under the
// AJAZZ_HAVE_WEBENGINE guard), so moc always sees an incomplete type — including
// in the offscreen QML test target, which compiles this controller without the
// WebEngine define. `Q_DECLARE_OPAQUE_POINTER` tells QMetaType to skip the
// "must be a fully-defined type" assert, exactly as the WebEngine pointer types
// above do. Must be at global scope before the Q_OBJECT that references it.
namespace ajazz::app {
class PIBridge;
}
Q_DECLARE_OPAQUE_POINTER(ajazz::app::PIBridge*)

namespace ajazz::app {

// Forward declaration for the activeBridgeChanged signal. The full definition is
// in pi_bridge.hpp (included only in the .cpp via the AJAZZ_HAVE_WEBENGINE guard).
class PIBridge;

/**
 * @class PropertyInspectorController
 * @brief Lifecycle owner of the Property Inspector WebEngine surface.
 *
 * The controller owns the per-plugin profile (cookie / cache / storage
 * isolation), the per-context @c QQmlWebChannel (with the @c \$SD
 * @ref PIBridge registered), and the URL of the PI HTML entry point. It
 * exposes those three handles via Q_PROPERTY so @c PIWebView.qml can
 * bind them onto a single @c WebEngineView, which then constructs and
 * displays the page itself.
 */
class PropertyInspectorController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PropertyInspectorController)
    QML_SINGLETON
    Q_DISABLE_COPY_MOVE(PropertyInspectorController)

    /// True iff the build linked Qt WebEngine and the controller can host
    /// HTML PI pages. QML uses this to swap between the WebEngine renderer
    /// and the schema-driven native fallback.
    Q_PROPERTY(bool webEngineAvailable READ webEngineAvailable CONSTANT)

    /// True while a PI HTML page is loaded and visible. Cleared by
    /// @ref closeInspector.
    Q_PROPERTY(bool hasHtmlInspector READ hasHtmlInspector NOTIFY hasHtmlInspectorChanged)

    /// QML-friendly profile for the active inspector. Bound by
    /// @c PIWebView.qml to its @c WebEngineView.profile. Always nullptr
    /// in builds without Qt WebEngine.
    Q_PROPERTY(
        QQuickWebEngineProfile* activeProfile READ activeProfile NOTIFY activeInspectorChanged)

    /// QML-friendly web channel scoped to the active inspector. Already
    /// has the @c \$SD bridge registered; bound by @c PIWebView.qml to
    /// its @c WebEngineView.webChannel. Always nullptr in builds without
    /// Qt WebEngine.
    Q_PROPERTY(QQmlWebChannel* activeChannel READ activeChannel NOTIFY activeInspectorChanged)

    /// Absolute @c file:// URL to the PI HTML entry. Bound by
    /// @c PIWebView.qml to its @c WebEngineView.url. Empty when no
    /// inspector is loaded.
    Q_PROPERTY(QUrl activeUrl READ activeUrl NOTIFY activeInspectorChanged)

    /// JS the PIWebView runs once the PI document finishes loading: the modern
    /// Elgato bootstrap `connectElgatoStreamDeckSocket(port, context,
    /// "registerPropertyInspector", info, actionInfo)` (T024). Empty when there
    /// is no WS port or no active inspector — the legacy `$SD`/cefQuery bridge
    /// then carries the PI alone. A modern PI that only does `new WebSocket(...)`
    /// stays dead without this call.
    Q_PROPERTY(QString activeBootstrapJs READ activeBootstrapJs NOTIFY activeInspectorChanged)

public:
    /// QML singleton factory — see BrandingService::create for the pattern.
    static PropertyInspectorController* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory.
    static void registerInstance(PropertyInspectorController* instance) noexcept;

    // No default on `parent`: see BrandingService — a default-constructible
    // QML_SINGLETON makes Qt 6 pick `Constructor` mode and silently bypass
    // the static `create()` factory, spawning a duplicate QML-side instance.
    explicit PropertyInspectorController(QObject* parent);
    ~PropertyInspectorController() override;

    [[nodiscard]] bool webEngineAvailable() const noexcept;
    [[nodiscard]] bool hasHtmlInspector() const noexcept { return hasHtmlInspector_; }
    [[nodiscard]] QQuickWebEngineProfile* activeProfile() const noexcept;
    [[nodiscard]] QQmlWebChannel* activeChannel() const noexcept;
    [[nodiscard]] QUrl activeUrl() const noexcept { return activeUrl_; }
    [[nodiscard]] QString activeBootstrapJs() const noexcept { return activeBootstrapJs_; }

    /// Inject the loopback WebSocket port the SdPluginServer listens on. The
    /// modern-PI bootstrap (T024) needs it to point the PI's WebSocket at the
    /// host. Set by Application after the server starts; 0 disables the bootstrap
    /// (the legacy $SD bridge still works).
    void setWebSocketPort(std::uint16_t port) noexcept { wsPort_ = port; }

    /**
     * @brief Load the Property Inspector for an action context.
     *
     * @param pluginUuid    The plugin's UUID (manifest @c UUID); used to scope
     *                      the WebEngineProfile so plugins cannot read each
     *                      other's cookies / storage.
     * @param htmlAbsPath   Absolute filesystem path to the PI HTML entry
     *                      (resolved by the manifest reader from
     *                      @c PropertyInspectorPath, action-level override
     *                      preferred over the top-level one).
     * @param actionUuid    The action's UUID (manifest @c Actions[].UUID).
     * @param contextUuid   The per-key/per-encoder context UUID minted by
     *                      the host so settings persistence is keyed
     *                      uniquely.
     */
    Q_INVOKABLE void loadInspector(QString const& pluginUuid,
                                   QString const& htmlAbsPath,
                                   QString const& actionUuid,
                                   QString const& contextUuid);

    /// Tear down the active PI page (selection cleared / inspector closed).
    Q_INVOKABLE void closeInspector();

signals:
    void hasHtmlInspectorChanged();
    /// Fires whenever any of @c activeProfile / @c activeChannel /
    /// @c activeUrl changes. The three move together (a fresh inspector
    /// load swaps all of them) so a single notify keeps the QML side
    /// from seeing intermediate states where the channel and URL
    /// disagree about which inspector they belong to.
    void activeInspectorChanged();

    /// Emitted (only when AJAZZ_HAVE_WEBENGINE is defined) after a fresh
    /// @c loadInspector completes and the new @c PIBridge object is live on
    /// @c activeChannel. The Application should connect the bridge's
    /// @c toPluginRequested signal to @c SdPluginServer::sendEvent in the
    /// lambda connected here (Phase 20 / 17-02 STOP gate relay wiring).
    ///
    /// @note The bridge is owned by the active QWebChannel (parented to this
    ///       controller). Callers MUST NOT cache the pointer — it becomes
    ///       dangling on the next @c loadInspector or @c closeInspector call.
    ///
    /// @warning This signal is NOT emitted in non-WebEngine builds (the
    ///          AJAZZ_HAVE_WEBENGINE guard applies). Safe to connect
    ///          unconditionally from Application (no emission = no call).
    void activeBridgeChanged(ajazz::app::PIBridge* bridge);

    /// PI-04: emitted when a PI page opens (loadInspector completes and the
    /// new bridge is live). Application routes this to
    /// SdPluginServer::sendEvent(propertyInspectorDidAppear) so the owning
    /// plugin learns its PI is visible. Routed through Application (not a raw
    /// SdPluginServer* here) to preserve the audited indirection that the
    /// toPluginRequested / activeBridgeChanged seams already use.
    void inspectorOpened(QString pluginUuid, QString actionUuid, QString contextUuid);

    /// PI-04: emitted when a PI page closes (closeInspector) AND on the
    /// PI->PI switch edge (loadInspector tears down the previous channel to
    /// load a new one). Application routes this to
    /// SdPluginServer::sendEvent(propertyInspectorDidDisappear).
    void inspectorClosed(QString pluginUuid, QString actionUuid, QString contextUuid);

private:
    bool hasHtmlInspector_ = false;
    QUrl activeUrl_;

    /// T024: loopback WS port (0 = bootstrap disabled) and the per-load modern-PI
    /// bootstrap JS built in loadInspector / cleared in closeInspector.
    std::uint16_t wsPort_ = 0;
    QString activeBootstrapJs_;

    /// PI-04: identity of the currently-loaded inspector, captured at
    /// loadInspector time so the disappear event can be emitted with the
    /// correct plugin/action/context on the PI->PI switch teardown (where the
    /// outgoing bridge is already gone) and on closeInspector. Set in both
    /// WebEngine and stub builds; used only by the appear/disappear emits.
    QString activePluginUuid_;
    QString activeActionUuid_;
    QString activeContextUuid_;

    /// PIMPL holding the WebEngineProfile + WebChannel. Defined only
    /// when AJAZZ_HAVE_WEBENGINE is set; when absent the unique_ptr is
    /// always empty and the controller stays in M1-stub mode at runtime.
    struct WebEngineImpl;
    std::unique_ptr<WebEngineImpl> webEngine_;
};

// See BrandingService static_assert — same QML_SINGLETON dual-instance trap.
// (Q_DISABLE_COPY_MOVE already prevents the other ways the type could be
// default-constructed; this is belt-and-braces.)
static_assert(
    !std::is_default_constructible_v<PropertyInspectorController>,
    "PropertyInspectorController must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
