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
#include <QHash>
#include <QUrl>
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
    QWebEnginePage* activePage = nullptr;
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
    if (profile == nullptr) {
        // Off-the-record == no on-disk persistence. M4 will switch this
        // to a named profile rooted at QStandardPaths::CacheLocation /
        // plugins/<uuid>/ once the persistence layer lands.
        profile = new QWebEngineProfile(pluginUuid, this);
        profile->setHttpUserAgent(QStringLiteral("AjazzControlCenter PropertyInspector/1.0"));
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

    // Security baseline (M2). Tighter scoping (URL request interceptor
    // bound to the PI directory, CDN allowlist) lands in M3 alongside
    // the QWebChannel bridge.
    auto* settings = page->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, false);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, false);
    settings->setAttribute(QWebEngineSettings::PluginsEnabled, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, false);
    settings->setAttribute(QWebEngineSettings::ScreenCaptureEnabled, false);

    page->load(QUrl::fromLocalFile(htmlAbsPath));

    webEngine_->activePage = page;
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
        webEngine_->activePage = nullptr;
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
