// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file property_inspector_controller.cpp
 * @brief Implementation of @ref ajazz::app::PropertyInspectorController.
 *
 * Two compilation modes, controlled by the @c AJAZZ_HAVE_WEBENGINE macro
 * (set by CMake when @c find_package(Qt6 ... WebEngineQuick) succeeds):
 *
 *   * **WebEngine present (M2 path)** — @ref loadInspector creates a
 *     @c QWebEngineProfile scoped to the plugin UUID and a
 *     @c QWebEnginePage that loads the plugin's HTML PI from the file:
 *     scheme. The QML side picks up the page via the @c activePage
 *     Q_PROPERTY. M3 layers the @c QWebChannel `\$SD` bridge on top;
 *     M4 adds settings persistence; M5 connects the bridge to the
 *     plugin host's WebSocket router.
 *
 *   * **WebEngine absent (M1 stub fallback)** — every method is a no-op
 *     that logs the request, @c activePage always returns nullptr,
 *     and QML stays on the schema-driven @c NativePropertyInspector.
 *     This keeps minimal Qt installs and headless CI green.
 *
 * The two paths share the same header surface so QML bindings and
 * @ref Application wiring are identical regardless of the build flag.
 */
#include "property_inspector_controller.hpp"

#include "ajazz/core/logger.hpp"

#include <QObject>
#include <QString>

#ifdef AJAZZ_HAVE_WEBENGINE
#include "pi_bridge.hpp"
#include "pi_url_request_interceptor.hpp"

#include <QFileInfo>
#include <QHash>
#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>

#include <utility>
#endif

namespace ajazz::app {

#ifdef AJAZZ_HAVE_WEBENGINE
/**
 * @brief Owns the per-controller Qt WebEngine state.
 *
 * Defined inline in the .cpp (PIMPL via @c std::unique_ptr in the
 * header) so the header doesn't need to include Qt WebEngine and stays
 * compilable on builds without the dep. The struct holds:
 *
 *   * @c profilesByPluginUuid — one isolated @c QWebEngineProfile per
 *     plugin UUID so two plugins never share cookies / storage / cache.
 *     Profiles are created lazily and parented to the controller, so
 *     they're cleaned up automatically when the controller dies.
 *
 *   * @c activePage — the @c QWebEnginePage currently driving the
 *     `PIWebView`. Owned by the controller; replaced (and the previous
 *     one deleted) on every @ref loadInspector call so we never leak
 *     pages across plugin selections.
 *
 * @note `QHash` over `unordered_map` because the keys are `QString`
 *       (the manifest UUID) and we want `qHash` to do the right thing
 *       without a custom hasher.
 */
struct PropertyInspectorController::WebEngineImpl {
    QHash<QString, QWebEngineProfile*> profilesByPluginUuid;

    /// One URL request interceptor per profile, parented to the profile
    /// (so it dies with the profile). Same key as @c profilesByPluginUuid.
    /// We keep a parallel map so the controller can call @c setPiDir()
    /// when a subsequent loadInspector for the same plugin lands a PI in
    /// a different bundle subdirectory.
    QHash<QString, PIUrlRequestInterceptor*> interceptorsByPluginUuid;

    QWebEnginePage* activePage = nullptr;

    /// QWebChannel installed on @c activePage. Re-created per page so each
    /// PI sees a fresh @c "$SD" object with no leftover JS handlers from
    /// the previously-loaded inspector.
    QWebChannel* channel = nullptr;

    /// `$SD` object exposed to the page's JS. Owned by @c activePage so
    /// it dies with the page.
    PIBridge* bridge = nullptr;
};
#else
/// Empty PIMPL on builds without Qt WebEngine. The unique_ptr stays
/// null; nothing reaches the WebEngine code paths at runtime.
struct PropertyInspectorController::WebEngineImpl {};
#endif

PropertyInspectorController::PropertyInspectorController(QObject* parent) : QObject(parent) {
#ifdef AJAZZ_HAVE_WEBENGINE
    webEngine_ = std::make_unique<WebEngineImpl>();
#endif
}

PropertyInspectorController::~PropertyInspectorController() = default;

// NOLINTNEXTLINE(readability-convert-member-functions-to-static) — must be a
// non-static member because it backs the @c webEngineAvailable Q_PROPERTY.
bool PropertyInspectorController::webEngineAvailable() const noexcept {
#ifdef AJAZZ_HAVE_WEBENGINE
    return true;
#else
    return false;
#endif
}

QWebEnginePage* PropertyInspectorController::activePage() const noexcept {
#ifdef AJAZZ_HAVE_WEBENGINE
    return webEngine_ ? webEngine_->activePage : nullptr;
#else
    return nullptr;
#endif
}

void PropertyInspectorController::loadInspector(QString const& pluginUuid,
                                                QString const& htmlAbsPath,
                                                QString const& actionUuid,
                                                QString const& contextUuid) {
    AJAZZ_LOG_INFO("property-inspector",
                   "loadInspector: plugin={} action={} context={} html={}",
                   pluginUuid.toStdString(),
                   actionUuid.toStdString(),
                   contextUuid.toStdString(),
                   htmlAbsPath.toStdString());

#ifdef AJAZZ_HAVE_WEBENGINE
    if (!webEngine_) {
        return; // Defensive — should never happen post-construction.
    }

    // Resolve or lazily create the per-plugin profile. Using the plugin
    // UUID as the storage key isolates each plugin's cookies / cache /
    // localStorage and lets the host wipe them when the plugin uninstalls.
    auto*& profile = webEngine_->profilesByPluginUuid[pluginUuid];
    bool freshProfile = false;
    if (profile == nullptr) {
        // Off-the-record == no on-disk persistence. M4 will switch this
        // to a named profile rooted at QStandardPaths::CacheLocation /
        // plugins/<uuid>/ once the persistence layer lands.
        profile = new QWebEngineProfile(pluginUuid, this);
        profile->setHttpUserAgent(QStringLiteral("AjazzControlCenter PropertyInspector/1.0"));
        freshProfile = true;
    }

    // URL request interceptor — security baseline. Scoped to the parent
    // directory of the PI HTML file so any file:// resource under the same
    // bundle dir is allowed; anything else (file:// elsewhere, http://,
    // unallow-listed https://, exotic schemes) is blocked at the network
    // layer before Chrome can even fetch it. See `pi_url_policy.hpp` for
    // the full decision tree and the CDN allowlist.
    QString const piDir = QFileInfo{htmlAbsPath}.absolutePath();
    auto*& interceptor = webEngine_->interceptorsByPluginUuid[pluginUuid];
    if (freshProfile || interceptor == nullptr) {
        interceptor = new PIUrlRequestInterceptor(pluginUuid, piDir, profile);
        profile->setUrlRequestInterceptor(interceptor);
    } else {
        interceptor->setPiDir(piDir);
    }

    // Tear down the previous page (if any) before creating a new one so
    // we never leak. The page is parented to the controller, so even if
    // a future change forgets to delete explicitly, the controller's
    // destruction frees it.
    if (auto* prev = webEngine_->activePage) {
        webEngine_->activePage = nullptr;
        prev->deleteLater();
    }

    auto* page = new QWebEnginePage(profile, this);

    // Security baseline (M2 + security-hardening pass). Conservative
    // QWebEngineSettings flags here; the URL request interceptor wired to
    // the per-plugin profile above (`PIUrlRequestInterceptor`) bounds
    // file:// to the PI directory and gates https:// against the CDN
    // allowlist.
    auto* settings = page->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
    settings->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, false);

    // M3 — QWebChannel bridge. The PI HTML must `<script src="qrc:///
    // qtwebchannel/qwebchannel.js">` and instantiate
    // `new QWebChannel(qt.webChannelTransport, ...)` to receive the `$SD`
    // object. We register the bridge BEFORE `page->load(url)` so the
    // channel is ready by the time the page's JS runs.
    auto* bridge = new PIBridge(this, pluginUuid, actionUuid, contextUuid, page);
    auto* channel = new QWebChannel(page);
    channel->registerObject(QStringLiteral("$SD"), bridge);
    page->setWebChannel(channel);

    page->load(QUrl::fromLocalFile(htmlAbsPath));

    webEngine_->activePage = page;
    webEngine_->channel = channel;
    webEngine_->bridge = bridge;
    emit activePageChanged();

    if (!hasHtmlInspector_) {
        hasHtmlInspector_ = true;
        emit hasHtmlInspectorChanged();
    }
#else
    // No WebEngine — keep the M1 stub semantics: log + ensure we report
    // no active HTML inspector so QML stays on the native renderer.
    (void)pluginUuid;
    (void)htmlAbsPath;
    (void)actionUuid;
    (void)contextUuid;
    if (hasHtmlInspector_) {
        hasHtmlInspector_ = false;
        emit hasHtmlInspectorChanged();
    }
#endif
}

void PropertyInspectorController::closeInspector() {
#ifdef AJAZZ_HAVE_WEBENGINE
    if (webEngine_ && webEngine_->activePage) {
        auto* page = webEngine_->activePage;
        // The channel + bridge are parented to the page, so they die with
        // it via Qt's parent-child ownership; we just need to clear our
        // own pointers so the next loadInspector call doesn't see stale
        // references.
        webEngine_->activePage = nullptr;
        webEngine_->channel = nullptr;
        webEngine_->bridge = nullptr;
        page->deleteLater();
        emit activePageChanged();
    }
#endif
    if (hasHtmlInspector_) {
        hasHtmlInspector_ = false;
        emit hasHtmlInspectorChanged();
    }
}

} // namespace ajazz::app
