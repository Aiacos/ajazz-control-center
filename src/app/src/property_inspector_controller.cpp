// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file property_inspector_controller.cpp
 * @brief Implementation of @ref ajazz::app::PropertyInspectorController.
 *
 * Two compilation modes, controlled by the @c AJAZZ_HAVE_WEBENGINE macro
 * (set by CMake when @c find_package(Qt6 ... WebEngineQuick WebChannelQuick)
 * succeeds):
 *
 *   * **WebEngine present** — @ref loadInspector creates a
 *     @c QQuickWebEngineProfile scoped to the plugin UUID, attaches a
 *     URL-request interceptor, builds a fresh @c QQmlWebChannel with
 *     the @c \$SD bridge registered as @c \"$SD\", and exposes the
 *     trio @c (activeProfile, activeChannel, activeUrl) via
 *     Q_PROPERTY. @c PIWebView.qml binds those onto a single
 *     @c WebEngineView, which then constructs and displays the page
 *     itself (Qt-recommended: @c WebEngineView does not expose @c page
 *     to QML — see TODO history).
 *
 *   * **WebEngine absent (M1 stub fallback)** — every method is a no-op
 *     that logs the request, the active properties always read nullptr /
 *     empty, and QML stays on the schema-driven @c NativePropertyInspector.
 *     This keeps minimal Qt installs and headless CI green.
 *
 * The two paths share the same header surface so QML bindings and
 * @ref Application wiring are identical regardless of the build flag.
 */
#include "property_inspector_controller.hpp"

#include "ajazz/core/logger.hpp"

#include <QObject>
#include <QQmlEngine>
#include <QString>

#ifdef AJAZZ_HAVE_WEBENGINE
#include "pi_bootstrap.hpp"
#include "pi_bridge.hpp"
#include "pi_cef_shim.hpp"
#include "pi_url_request_interceptor.hpp"
#include "plugin_mirabox_shim.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QtWebChannelQuick/QQmlWebChannel>
#include <QtWebEngineQuick/QQuickWebEngineProfile>
// MAINTAINER NOTE (WR-01): QQuickWebEngineScriptCollection is only forward-declared in the
// public qquickwebengineprofile.h header; the complete definition lives in this private
// header. Required to call userScripts()->insert(). If this breaks on a Qt minor bump,
// verify the private header path and update Qt6::WebEngineQuickPrivate in CMakeLists.txt.
// The same pattern is used in plugin_manager.cpp.
static_assert(QT_VERSION >= QT_VERSION_CHECK(6, 7, 0),
              "qquickwebenginescriptcollection_p.h layout may have changed; verify include path");
#include <QtWebEngineQuick/private/qquickwebenginescriptcollection_p.h>

#include <utility>
#endif

namespace ajazz::app {

namespace {

/// Pointer set by PropertyInspectorController::registerInstance, consumed by ::create.
PropertyInspectorController* s_propertyInspectorInstance = nullptr;

} // namespace

PropertyInspectorController* PropertyInspectorController::create(QQmlEngine* /*qml*/,
                                                                 QJSEngine* /*js*/) {
    Q_ASSERT_X(s_propertyInspectorInstance != nullptr,
               "PropertyInspectorController::create",
               "registerInstance() must be called before the QML engine loads");
    QQmlEngine::setObjectOwnership(s_propertyInspectorInstance, QQmlEngine::CppOwnership);
    return s_propertyInspectorInstance;
}

void PropertyInspectorController::registerInstance(PropertyInspectorController* instance) noexcept {
    s_propertyInspectorInstance = instance;
}

#ifdef AJAZZ_HAVE_WEBENGINE
/**
 * @brief Owns the per-controller Qt WebEngine state.
 *
 * Defined inline in the .cpp (PIMPL via @c std::unique_ptr in the
 * header) so the header doesn't need to include Qt WebEngine and stays
 * compilable on builds without the dep. The struct holds:
 *
 *   * @c profilesByPluginUuid — one isolated @c QQuickWebEngineProfile
 *     per plugin UUID so two plugins never share cookies / storage /
 *     cache. Profiles are created lazily and parented to the controller,
 *     so they're cleaned up automatically when the controller dies.
 *
 *   * @c interceptorsByPluginUuid — parallel map of URL interceptors,
 *     parented to their owning profile so they share its lifetime.
 *
 *   * @c activeProfile / @c activeChannel — the per-inspector handles
 *     bound by QML. The channel is re-created on every @ref loadInspector
 *     call so each PI sees a fresh @c \"$SD\" object with no leftover JS
 *     handlers from the previously-loaded inspector.
 *
 * @note `QHash` over `unordered_map` because the keys are `QString`
 *       (the manifest UUID) and we want `qHash` to do the right thing
 *       without a custom hasher.
 */
struct PropertyInspectorController::WebEngineImpl {
    QHash<QString, QQuickWebEngineProfile*> profilesByPluginUuid;

    /// One URL request interceptor per profile, parented to the profile
    /// (so it dies with the profile). Same key as @c profilesByPluginUuid.
    /// We keep a parallel map so the controller can call @c setPiDir()
    /// when a subsequent loadInspector for the same plugin lands a PI in
    /// a different bundle subdirectory.
    QHash<QString, PIUrlRequestInterceptor*> interceptorsByPluginUuid;

    /// Profile currently bound to QML. Borrowed from
    /// @c profilesByPluginUuid; never owned twice.
    QQuickWebEngineProfile* activeProfile = nullptr;

    /// Web channel scoped to the current inspector. Owned by the
    /// controller; re-created on every @ref loadInspector call so the
    /// previous PI's JS handlers don't leak into the next PI.
    QQmlWebChannel* activeChannel = nullptr;

    /// `$SD` bridge registered on @c activeChannel. Parented to the
    /// channel so it dies with the channel.
    PIBridge* activeBridge = nullptr;
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

QQuickWebEngineProfile* PropertyInspectorController::activeProfile() const noexcept {
#ifdef AJAZZ_HAVE_WEBENGINE
    return webEngine_ ? webEngine_->activeProfile : nullptr;
#else
    return nullptr;
#endif
}

QQmlWebChannel* PropertyInspectorController::activeChannel() const noexcept {
#ifdef AJAZZ_HAVE_WEBENGINE
    return webEngine_ ? webEngine_->activeChannel : nullptr;
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

    // PI-04 (CR WR-01): identical-reload guard. A single key selection drives
    // maybeLoadInspector() from THREE QML handlers (onBindingChanged /
    // onKeyIndexChanged / onEncoderIndexChanged), so loadInspector can be
    // re-entered for the SAME plugin/action/context/url that is already live.
    // Without this guard the unconditional teardown below would emit a spurious
    // propertyInspectorDidDisappear + DidAppear pair to the plugin for an
    // inspector that never actually went away. No-op when nothing changed.
    if (webEngine_->activeChannel != nullptr && pluginUuid == activePluginUuid_ &&
        actionUuid == activeActionUuid_ && contextUuid == activeContextUuid_ &&
        activeUrl_ == QUrl::fromLocalFile(htmlAbsPath)) {
        return;
    }

    // Resolve or lazily create the per-plugin profile. Using the plugin
    // UUID as the storage key isolates each plugin's cookies / cache /
    // localStorage and lets the host wipe them when the plugin uninstalls.
    auto*& profile = webEngine_->profilesByPluginUuid[pluginUuid];
    bool freshProfile = false;
    if (profile == nullptr) {
        // QQuickWebEngineProfile defaults to an off-the-record (in-memory)
        // profile when no storageName is set, which is exactly what we
        // want for M2/M3. M4 will rename + persist when settings storage
        // lands.
        profile = new QQuickWebEngineProfile(this);
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

    // Inject JS polyfill shims into the per-plugin profile's user-script
    // collection once per fresh profile (T-20-SHIM-ONCE: guarded on
    // freshProfile so re-loading another PI for the same plugin does NOT
    // insert a duplicate script). Both shims must be present before the
    // PI's own <script> tags run — DocumentCreation injection point
    // guarantees this (akp_plugin_sdk.md §8; 18-RESEARCH.md Pitfall 4).
    //
    //   * makeCefQueryShim()  — maps window.cefQuery({request,...}) to the
    //     QWebChannel $SD bridge (PLUGIN-09 / pi_cef_shim.hpp). Required by
    //     every standard Elgato PI that calls cefQuery instead of QWebChannel
    //     directly.
    //
    //   * makeMiraboxShim()   — aliases connectMiraBoxSDSocket to the Elgato
    //     API so Mirabox-targeted PIs keep working unchanged (PLUGIN-11 /
    //     plugin_mirabox_shim.hpp).
    if (freshProfile) {
        profile->userScripts()->insert(makeCefQueryShim());
        profile->userScripts()->insert(makeMiraboxShim());
    }

    // Tear down the previous channel + bridge (if any) before creating a
    // new pair so the next PI sees a fresh `$SD` and we never leak. Both
    // are parented to the controller / channel so deleteLater() cascades
    // safely.
    if (auto* prev = webEngine_->activeChannel) {
        // PI-04: a direct PI->PI switch tears down the previous channel WITHOUT
        // calling closeInspector, so the outgoing PI would never receive
        // propertyInspectorDidDisappear. Emit it here from the captured prior
        // identity before nulling so switching between two PIs fires disappear
        // for the first (research Pitfall 2 / Gap 1c edge case).
        if (!activePluginUuid_.isEmpty()) {
            emit inspectorClosed(activePluginUuid_, activeActionUuid_, activeContextUuid_);
        }
        webEngine_->activeChannel = nullptr;
        webEngine_->activeBridge = nullptr;
        prev->deleteLater();
    }

    auto* channel = new QQmlWebChannel(this);
    auto* bridge = new PIBridge(this, pluginUuid, actionUuid, contextUuid, channel);
    channel->registerObject(QStringLiteral("$SD"), bridge);

    webEngine_->activeProfile = profile;
    webEngine_->activeChannel = channel;
    webEngine_->activeBridge = bridge;
    activeUrl_ = QUrl::fromLocalFile(htmlAbsPath);

    // T024: build the modern-PI bootstrap the PIWebView runs once the document
    // loads. A modern PI defines connectElgatoStreamDeckSocket and waits for the
    // host to call it (exactly like the HTML-plugin self-bootstrap in
    // plugin_manager.cpp); without this call a WS-only PI never registers. The
    // legacy $SD/cefQuery bridge above still serves PIs that use cefQuery. The
    // didAppear dedup (PiAppearGate, T023) keeps the two registration paths from
    // double-firing propertyInspectorDidAppear.
    activeBootstrapJs_ = buildModernPiBootstrapJs(wsPort_, pluginUuid, actionUuid, contextUuid);

    emit activeInspectorChanged();
    // Notify the Application so it can wire bridge->toPluginRequested to
    // SdPluginServer::sendEvent (Phase 20 / 17-02 STOP gate relay wiring).
    emit activeBridgeChanged(bridge);

    if (!hasHtmlInspector_) {
        hasHtmlInspector_ = true;
        emit hasHtmlInspectorChanged();
    }

    // §7 handshake: emit the action's persisted settings on load so the PI
    // receives its current per-context settings as soon as it is ready
    // (akp_plugin_sdk.md §7: host replies passHello + didReceiveSettings).
    // getSettings() reads from disk and emits didReceiveSettings synchronously.
    // passHello is Phase-17's deliverable (SdPluginServer::sendEvent); deferred
    // per the 20-03 STOP gate (17-02-SUMMARY.md exists but the bridge<->server
    // connection wiring is done at Application construction, not here).
    bridge->getSettings();

    // PI-04: record the live inspector identity and announce the open. The
    // Application seam routes this to SdPluginServer::sendEvent(
    // propertyInspectorDidAppear). Captured here so the matching disappear can
    // carry the correct identity even after the bridge is torn down.
    activePluginUuid_ = pluginUuid;
    activeActionUuid_ = actionUuid;
    activeContextUuid_ = contextUuid;
    emit inspectorOpened(pluginUuid, actionUuid, contextUuid);
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
    if (webEngine_ && webEngine_->activeChannel != nullptr) {
        auto* channel = webEngine_->activeChannel;
        // PI-04: announce the close while the plugin/context identity is still
        // known (before teardown), so the owning plugin receives
        // propertyInspectorDidDisappear via the Application seam.
        if (!activePluginUuid_.isEmpty()) {
            emit inspectorClosed(activePluginUuid_, activeActionUuid_, activeContextUuid_);
        }
        activePluginUuid_.clear();
        activeActionUuid_.clear();
        activeContextUuid_.clear();
        webEngine_->activeProfile = nullptr;
        webEngine_->activeChannel = nullptr;
        webEngine_->activeBridge = nullptr;
        channel->deleteLater();
        activeUrl_.clear();
        activeBootstrapJs_.clear(); // T024: no PI loaded ⇒ nothing to bootstrap
        emit activeInspectorChanged();
    }
#endif
    if (hasHtmlInspector_) {
        hasHtmlInspector_ = false;
        emit hasHtmlInspectorChanged();
    }
}

} // namespace ajazz::app
