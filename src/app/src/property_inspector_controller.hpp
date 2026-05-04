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

namespace ajazz::app {

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

private:
    bool hasHtmlInspector_ = false;
    QUrl activeUrl_;

    /// PIMPL holding the WebEngineProfile + WebChannel. Defined only
    /// when AJAZZ_HAVE_WEBENGINE is set; when absent the unique_ptr is
    /// always empty and the controller stays in M1-stub mode at runtime.
    struct WebEngineImpl;
    std::unique_ptr<WebEngineImpl> webEngine_;
};

} // namespace ajazz::app
