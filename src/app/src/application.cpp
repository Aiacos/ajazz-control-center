// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file application.cpp
 * @brief Application class implementation.
 *
 * Connects the backend bootstrap sequence (registerAll calls) to the QML
 * engine by forwarding DeviceModel, ProfileController, BrandingService and
 * TrayController as context properties. Also owns the cross-platform USB
 * hot-plug monitor and marshals its events back to the Qt main thread to
 * refresh the device list.
 */
#include "application.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/hotplug_monitor.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "hotplug_debouncer.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>

#ifdef AJAZZ_PYTHON_HOST
#include "ajazz/plugins/manifest_signer.hpp"
#include "ajazz/plugins/out_of_process_plugin_host.hpp"

#include <QDir>
#include <QStandardPaths>

#include <exception>
#endif

namespace ajazz::app {

Application::Application(QObject* parent)
    : QObject(parent), m_branding(std::make_unique<BrandingService>(this)),
      m_themeService(std::make_unique<ThemeService>(m_branding.get(), this)),
      m_autostart(std::make_unique<AutostartService>(this)),
      // Audit finding A1: the DeviceModel reads from this Application's
      // owned registry (`m_deviceRegistry`), not from a process-wide
      // singleton. The registry is declared first in the header so it
      // is constructed before — and destroyed after — the model that
      // holds a reference to it.
      m_deviceModel(std::make_unique<DeviceModel>(m_deviceRegistry, this)),
      m_profileController(std::make_unique<ProfileController>(this)),
      m_trayController(
          std::make_unique<TrayController>(m_branding.get(), m_profileController.get(), this)),
      m_pluginCatalog(std::make_unique<PluginCatalogModel>(this)),
      m_loadedPlugins(std::make_unique<LoadedPluginsModel>(this)),
      m_propertyInspector(std::make_unique<PropertyInspectorController>(this)),
      // Phase 5 Plan 05-07 / A-04: TimeSyncService is constructed with a
      // DeviceLookup lambda that captures m_deviceRegistry by reference.
      // The lookup returns std::shared_ptr<IDevice> directly per the
      // updated DeviceLookup signature; TimeSyncService::doPush() holds
      // it in a local for the duration of the dynamic_cast → setTime
      // sequence, closing the UAF window from Phase 4 D-06's weak_ptr
      // cache. No more raw-pointer lifetime juggling.
      //
      // Codename → DeviceId resolution: walk m_deviceRegistry.enumerate()
      // for a descriptor whose codename matches; pass (vid, pid) to
      // open(). The empty serial string means open() matches on
      // (vid, pid) only — sufficient for the v1.0 codebase where the
      // descriptor key is (vid, pid) per Phase 4 D-04. Cost is O(N) over
      // descriptors but N is small (~20) and the call is on the GUI
      // thread; the inner registry lookup is O(1) per Phase 4 D-06.
      m_timeSync(std::make_unique<TimeSyncService>(
          [this](QString const& codename) -> std::shared_ptr<core::IDevice> {
              auto const descriptors = m_deviceRegistry.enumerate();
              for (auto const& d : descriptors) {
                  if (QString::fromStdString(d.codename) != codename) {
                      continue;
                  }
                  core::DeviceId const id{
                      .vendorId = d.vendorId, .productId = d.productId, .serial = {}};
                  return m_deviceRegistry.open(id);
              }
              return nullptr;
          },
          this)),
      m_lighting(std::make_unique<LightingService>(
          [this](QString const& codename) -> std::shared_ptr<core::IDevice> {
              // Same DeviceLookup pattern as TimeSyncService above.
              auto const descriptors = m_deviceRegistry.enumerate();
              for (auto const& d : descriptors) {
                  if (QString::fromStdString(d.codename) != codename) {
                      continue;
                  }
                  core::DeviceId const id{
                      .vendorId = d.vendorId, .productId = d.productId, .serial = {}};
                  return m_deviceRegistry.open(id);
              }
              return nullptr;
          },
          this)),
      m_hotplug(std::make_unique<core::HotplugMonitor>()),
      m_debouncer(std::make_unique<HotplugDebouncer>(this)) {
    // 300ms trailing-edge coalescing per D-05 / HOTPLUG-05. The debouncer
    // owns its QTimers and lives on this Application's thread (the GUI
    // thread); its `coalesced` signal is delivered to the DeviceModel on
    // the same thread without an extra hop. Empty HotplugEvent payload
    // is intentionally captured by-value into the lambda — only the
    // act of coalescing matters for refresh(), not the event content.
    QObject::connect(m_debouncer.get(),
                     &HotplugDebouncer::coalesced,
                     m_deviceModel.get(),
                     [this](core::HotplugEvent const&) { m_deviceModel->refresh(); });
}

Application::~Application() {
    // Defensive shutdown ordering — the hot-plug worker queues
    // refresh() lambdas to m_deviceModel via Qt::QueuedConnection. Without
    // care, an in-flight queued event can fire during member destruction
    // and dereference an already-destroyed m_deviceModel.
    if (m_hotplug) {
        // 1. Block further callbacks before joining; an event firing after
        //    setCallback({}) returns is impossible by HotplugMonitor's contract.
        m_hotplug->setCallback({});
        // 2. Join the polling thread; no new events can be posted after this.
        m_hotplug->stop();
    }
    // 3. Drain events already in the main-thread queue that target
    //    m_deviceModel, so they cannot run after its unique_ptr destructor.
    if (m_deviceModel) {
        QCoreApplication::removePostedEvents(m_deviceModel.get());
    }
}

void Application::bootstrap() {
    core::setLogLevel(core::LogLevel::Info);

    // Audit finding A1: pass the owned registry into every backend
    // bootstrap rather than relying on the deprecated `instance()` shim.
    streamdeck::registerAll(m_deviceRegistry);
    keyboard::registerAll(m_deviceRegistry);
    mouse::registerAll(m_deviceRegistry);

    m_deviceModel->refresh();
    AJAZZ_LOG_INFO("app",
                   "bootstrap complete: {} supported devices",
                   static_cast<int>(m_deviceModel->rowCount()));

#ifdef AJAZZ_PYTHON_HOST
    initPluginHost();
#endif
}

#ifdef AJAZZ_PYTHON_HOST
void Application::initPluginHost() {
    plugins::OutOfProcessHostConfig config;
    config.childScript = AJAZZ_PLUGIN_HOST_SCRIPT;
    config.pythonPath = {std::filesystem::path{AJAZZ_PLUGIN_PYTHONPATH}};

    plugins::ManifestSignerConfig verifier;
    verifier.verifierScript = AJAZZ_PLUGIN_VERIFIER_SCRIPT;
    verifier.trustedPublishersFile = AJAZZ_PLUGIN_TRUST_ROOTS;
    config.manifestVerifier = std::move(verifier);

    try {
        auto host = std::make_unique<plugins::OutOfProcessPluginHost>(std::move(config));

        // User-level plugin search path: XDG `AppLocalDataLocation`, e.g.
        // `~/.local/share/ajazz-control-center/plugins` on Linux. Created
        // lazily so a fresh checkout doesn't error on the first launch.
        QString const userPluginsQ =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
            QStringLiteral("/plugins");
        QDir().mkpath(userPluginsQ);
        host->addSearchPath(userPluginsQ.toStdString());
        host->loadAll();

        // Wire the host pointer FIRST, then the row data — that way any
        // QML "Reload" affordance (which calls @c LoadedPluginsModel::refresh
        // via the host pointer) cannot observe a freshly-populated model
        // backed by a null host. Today this ordering is unreachable because
        // the QML engine isn't loaded until @c exposeToQml runs after
        // @c bootstrap, but the previous order encoded a fragile
        // assumption that future bootstrap reorganisation could violate.
        // (REVIEW WR-03)
        m_loadedPlugins->setPluginHost(host.get());
        m_loadedPlugins->setPlugins(host->plugins());
        m_pluginHost = std::move(host);

        AJAZZ_LOG_INFO("app",
                       "plugin host ready: {} loaded from {}",
                       m_loadedPlugins->rowCountSimple(),
                       userPluginsQ.toStdString());
    } catch (std::exception const& e) {
        // Common failure modes: python3 missing, child script missing
        // (broken install), `cryptography` not installed (the
        // verifier exec fails which makes loadAll/list_plugins
        // appear to throw via the IPC contract). Logging keeps the
        // user-visible app alive — the "Loaded" drawer stays empty.
        AJAZZ_LOG_WARN("app", "plugin host disabled: {}", e.what());
    }
}
#endif

void Application::exposeToQml(QQmlApplicationEngine& engine) {
    // Services registered as QML singletons via QML_NAMED_ELEMENT + QML_SINGLETON.
    // Hand the app-owned instances to their factories before the engine loads.
    BrandingService::registerInstance(m_branding.get());
    ThemeService::registerInstance(m_themeService.get());
    AutostartService::registerInstance(m_autostart.get());
    TrayController::registerInstance(m_trayController.get());
    DeviceModel::registerInstance(m_deviceModel.get());
    ProfileController::registerInstance(m_profileController.get());
    PluginCatalogModel::registerInstance(m_pluginCatalog.get());
    LoadedPluginsModel::registerInstance(m_loadedPlugins.get());
    PropertyInspectorController::registerInstance(m_propertyInspector.get());
    TimeSyncService::registerInstance(m_timeSync.get());
    LightingService::registerInstance(m_lighting.get());
    // Wire the periodic auto-sync enumerator now that DeviceModel is
    // registered + connected to live hotplug. The TimeSyncService timer
    // (15 min interval) calls this back to enumerate IClockCapable
    // devices when autoSync is on. Capturing m_deviceModel.get() is safe
    // because DeviceModel is owned by Application for the same lifetime
    // as TimeSyncService (both Application members destroyed in
    // construction-order reverse).
    auto* const deviceModel = m_deviceModel.get();
    m_timeSync->setConnectedCodenameEnumerator(
        [deviceModel]() { return deviceModel->connectedCodenames(); });
    // No more setContextProperty calls — every service is now a QML
    // singleton, statically resolvable by qmllint.
    Q_UNUSED(engine);
}

void Application::startBackgroundServices(QQmlApplicationEngine& engine) {
    // Tray must be created after the QML engine has loaded the root window so
    // the menu's Show/Hide actions have a window to operate on.
    m_trayController->ensureTray(&engine);

    // Quit signal: route to the global Qt application so we shut down cleanly
    // even when the main window is hidden to the tray.
    QObject::connect(
        m_trayController.get(), &TrayController::quitRequested, qApp, &QCoreApplication::quit);

    // Tray submenu "Switch profile": forward to the profile controller. The
    // controller owns the load semantics; the tray just emits the requested id.
    QObject::connect(m_trayController.get(),
                     &TrayController::profileSwitchRequested,
                     m_profileController.get(),
                     &ProfileController::loadProfileById);

    // USB hot-plug: callback runs on a background thread; marshal to the GUI
    // thread before touching the QAbstractListModel.
    m_hotplug->setCallback([this](core::HotplugEvent const& ev) { onHotplug(ev); });
    if (!m_hotplug->start()) {
        AJAZZ_LOG_INFO("app", "hot-plug monitor unavailable on this platform/session");
    }
}

void Application::onHotplug(core::HotplugEvent const& ev) {
    AJAZZ_LOG_INFO("app",
                   "hot-plug {}: {:04x}:{:04x}",
                   ev.action == core::HotplugAction::Arrived ? "+" : "-",
                   static_cast<int>(ev.vid),
                   static_cast<int>(ev.pid));
    // Route through the debouncer (300ms trailing-edge coalescing per
    // D-05 / HOTPLUG-05). The debouncer marshals onto its owning
    // (GUI) thread internally and emits `coalesced` once per stable
    // (vid, pid, serial) transition; that signal drives refresh().
    // No direct invokeMethod here — the debouncer owns thread safety.
    m_debouncer->observe(ev);

    // Phase 5 Plan 05-07 / A-04: forward arrivals to TimeSyncService's
    // 300 ms-debounced auto-sync hook. The debouncer above coalesces
    // the OS-side burst (composite USB = 2 events per connect); this
    // separate QTimer-singleShot inside onDeviceArrivedDebounced
    // re-validates capability + connectedness at firing time
    // (Pitfall 2). Total: Phase 4 D-05 300 ms + Phase 5 A-04 300 ms ≈
    // 600 ms plug-in → auto-sync fire — within the design doc budget.
    //
    // Resolve VID/PID back to a codename via DeviceRegistry::enumerate
    // (same path the DeviceLookup uses on its way back the other
    // direction). If no descriptor matches the arrived VID/PID, drop
    // silently — the device isn't one we know about.
    if (ev.action == core::HotplugAction::Arrived) {
        auto const descriptors = m_deviceRegistry.enumerate();
        for (auto const& d : descriptors) {
            if (d.vendorId == ev.vid && d.productId == ev.pid) {
                m_timeSync->onDeviceArrivedDebounced(QString::fromStdString(d.codename));
                break;
            }
        }
    }
}

} // namespace ajazz::app
