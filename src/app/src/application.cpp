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
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#ifdef AJAZZ_PYTHON_HOST
#include "ajazz/plugins/manifest_signer.hpp"
#include "ajazz/plugins/out_of_process_plugin_host.hpp"

#if defined(__linux__)
#include "ajazz/plugins/linux_bwrap_sandbox.hpp"
#elif defined(__APPLE__)
#include "ajazz/plugins/macos_sandbox_exec_sandbox.hpp"
#endif

#include <QDir>
#include <QStandardPaths>

#include <exception>
#include <filesystem>
#include <set>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h> // access(X_OK) for vetted interpreter resolution
#endif
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
      // 2026-05-18 issue #57: SettingsService bridges the AK-series
      // settings batch (opcode 0x07 sub 0x10) to QML. Same DeviceLookup
      // pattern as LightingService / TimeSyncService — codename →
      // shared_ptr<IDevice> via DeviceRegistry::open, with the cap
      // dynamic_cast happening inside the service so unknown / wired
      // devices return the vendor-default tuple instead of crashing.
      m_settings(std::make_unique<SettingsService>(
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
      // 2026-05-18 P3.d: BatteryService gets the same DeviceLookup pattern
      // (codename -> shared_ptr<IDevice>) used by TimeSyncService and
      // LightingService. The enumerator returns the codenames of currently-
      // connected devices whose descriptor advertises Capability::Battery,
      // so the 15-s poll only touches devices that can actually respond
      // (AK980 PRO today). m_deviceModel is constructed before this member
      // per the declaration order in application.hpp, so capturing it by
      // pointer is safe; the model itself is single-threaded (GUI thread)
      // and so is BatteryService's QTimer callback.
      m_battery(std::make_unique<BatteryService>(
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
          [this]() -> std::vector<QString> {
              // Intersect "connected codenames" (live HID enumeration) with
              // "descriptor.hasBattery" (advertised capability). This keeps
              // the poll loop O(N) over the small registry rather than
              // chasing every codename through DeviceRegistry::open().
              auto const connected = m_deviceModel->connectedCodenames();
              std::vector<QString> out;
              out.reserve(connected.size());
              auto const descriptors = m_deviceRegistry.enumerate();
              for (auto const& codename : connected) {
                  auto const target = codename.toStdString();
                  for (auto const& d : descriptors) {
                      if (d.codename == target && d.hasBattery) {
                          out.push_back(codename);
                          break;
                      }
                  }
              }
              return out;
          },
          this)),
      // 2026-05-18 / docs/architecture/APP-AUTO-UPDATE.md: AppUpdateService
      // bridges GitHub Releases to the QML banner. It self-disables when
      // FLATPAK_ID is set (Flathub owns updates), otherwise polls every
      // 24 h with a 5 s initial debounce so the splash isn't blocked.
      // Owned here so its QTimer / QNetworkAccessManager live on the GUI
      // thread for the same lifetime as the other QML singletons.
      m_appUpdate(std::make_unique<AppUpdateService>(this)),
      m_firmwareUpdate(std::make_unique<FirmwareUpdateService>(
          this,
          // Same codename -> shared_ptr<IDevice> DeviceLookup as BatteryService,
          // so the Firmware tab can read the running firmware version.
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
          })),
      // Phase 14 Plan 14-02: StreamDockControlService — app-layer panel paint path
      // (DISPLAY-06/07/08, DOCK-01/02). Same DeviceLookup pattern as TimeSyncService:
      // codename -> shared_ptr<IDevice> via DeviceRegistry::open (flyweight). The
      // service holds the returned shared_ptr for the session (single HID handle,
      // ARCH-03). ProfileAccessor captures m_profileController.get() so repaint on
      // profileChanged iterates the loaded Profile::keys without coupling the service
      // to ProfileController's full API. Declared after m_firmwareUpdate to keep the
      // init list in member-declaration order (-Wreorder).
      m_streamDockControl(std::make_unique<StreamDockControlService>(
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
          [this]() -> core::Profile const& { return m_profileController->activeProfile(); },
          this)),
      // Phase 15 Plan 15-02: QtExecutor, ActionEngine, StreamDockInputService.
      //
      // Construction order MUST match member-declaration order in application.hpp
      // (GCC -Wreorder is -Werror). The three members are declared in this order:
      // m_qtExecutor -> m_actionEngine -> m_streamDockInput.
      //
      // m_qtExecutor: non-blocking Sleep Executor (audit A2). Owned here so it
      // outlives m_actionEngine (qt_executor.hpp lifetime note).
      m_qtExecutor(std::make_unique<QtExecutor>(this)),
      // m_actionEngine: the first ActionEngine instantiation in the app.
      // ActionExecutors:
      //   - keyPress:   STUBBED with a log line (OS key injection is cross-platform
      //                 uinput/SendInput/CGEvent, deferred to Phase 21 per T-15-03 /
      //                 Open Question 4 / Assumption A5).
      //   - runCommand: QProcess::startDetached(program, args) -- never system() or
      //                 a shell string (T-15-02 mitigated). settingsJson is parsed
      //                 minimally/defensively; Phase 20 owns the full PI schema.
      //   - openUrl:    QDesktopServices::openUrl (standard Qt cross-platform).
      //   - plugin:     STUBBED with a log line (Phase 19 seam, T-15-03 accepted).
      //
      // m_qtExecutor is passed as the shared_ptr<Executor> so Sleep/delayMs defer
      // via QTimer::singleShot instead of blocking the poll thread (T-15-04 / A2).
      m_actionEngine(std::make_unique<core::ActionEngine>(
          [&] {
              core::ActionExecutors execs;
              // keyPress executor: STUB (Phase 21 / T-15-03 accepted)
              execs.keyPress = [](std::string_view settingsJson) {
                  AJAZZ_LOG_INFO("input",
                                 "keyPress action ignored (OS key injection arrives Phase 21): {}",
                                 settingsJson);
              };
              // runCommand executor: QProcess::startDetached with an explicit argv list.
              // Never system() / never a shell string (T-15-02). settingsJson is
              // expected to be a JSON object with at least a "program" key and an
              // optional "args" array.  Phase 20 owns the full PI schema; Phase 15
              // parses minimally: if the JSON does not parse, log and skip.
              execs.runCommand = [](std::string_view settingsJson) {
                  // Minimal / defensive parse: look for "program":"..." substring.
                  // Full structured parse (QJsonDocument) lands in Phase 20 when the
                  // PI schema for RunCommand is finalised.  Until then, attempt a
                  // best-effort extract to preserve runCommand usability with simple
                  // bindings authored against the schema preview.
                  auto const json = QString::fromUtf8(settingsJson.data(),
                                                      static_cast<qsizetype>(settingsJson.size()));
                  auto const doc = QJsonDocument::fromJson(json.toUtf8());
                  if (!doc.isObject()) {
                      AJAZZ_LOG_INFO("input",
                                     "runCommand: malformed settingsJson (not a JSON object): {}",
                                     settingsJson);
                      return;
                  }
                  auto const obj = doc.object();
                  auto const program = obj.value(QStringLiteral("program")).toString();
                  if (program.isEmpty()) {
                      AJAZZ_LOG_INFO("input",
                                     "runCommand: missing 'program' key in settingsJson: {}",
                                     settingsJson);
                      return;
                  }
                  QStringList args;
                  auto const argsVal = obj.value(QStringLiteral("args"));
                  if (argsVal.isArray()) {
                      for (auto const& a : argsVal.toArray()) {
                          args << a.toString();
                      }
                  }
                  // QProcess::startDetached: no blocking, no shell string, explicit argv.
                  if (!QProcess::startDetached(program, args)) {
                      AJAZZ_LOG_INFO("input",
                                     "runCommand: QProcess::startDetached failed for program: {}",
                                     program.toStdString());
                  }
              };
              // openUrl executor: QDesktopServices::openUrl (standard Qt cross-platform).
              // WR-01: use strict QUrl construction (not QUrl::fromUserInput) and
              // validate the scheme before dispatching. QUrl::fromUserInput converts
              // bare local paths (e.g. "/etc/passwd", "~/secret.pdf") into file://
              // URLs, which would silently open arbitrary local files -- especially
              // dangerous when profiles can be loaded from external/plugin sources.
              // Only http and https are permitted.
              execs.openUrl = [](std::string_view url) {
                  QUrl const qurl(
                      QString::fromUtf8(url.data(), static_cast<qsizetype>(url.size())));
                  if (qurl.scheme() != QStringLiteral("http") &&
                      qurl.scheme() != QStringLiteral("https")) {
                      AJAZZ_LOG_WARN("input",
                                     "openUrl: rejected non-http(s) URL scheme '{}'",
                                     qurl.scheme().toStdString());
                      return;
                  }
                  QDesktopServices::openUrl(qurl);
              };
              // plugin executor: STUB (Phase 19 seam, T-15-03 accepted).
              execs.plugin = [](std::string_view id, std::string_view /*settingsJson*/) {
                  AJAZZ_LOG_INFO(
                      "input", "plugin action {} ignored (plugin host arrives Phase 19)", id);
              };
              return execs;
          }(),
          // QtExecutor as the shared_ptr<Executor>: Sleep defers via QTimer::singleShot,
          // never blocking the GUI/poll thread (T-15-04 / audit A2).
          std::shared_ptr<core::Executor>(m_qtExecutor.get(), [](core::Executor*) {}))),
      // m_streamDockInput: constructed with the ProfileAccessor seam (mirrors
      // StreamDockControlService precedent) and the pre-built ActionEngine.
      // The service receives the active device handle via setActiveDevice() in the
      // onHotplug arrival path (see below), not at construction time.
      m_streamDockInput(std::make_unique<StreamDockInputService>(
          [this]() -> core::Profile const& { return m_profileController->activeProfile(); },
          std::move(m_actionEngine),
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

    // Phase 14 Plan 14-02 (DISPLAY-08): wire profileChanged -> repaintFromProfile so
    // loading a profile repaints every bound key on the active Stream Deck.
    QObject::connect(m_profileController.get(),
                     &ProfileController::profileChanged,
                     m_streamDockControl.get(),
                     &StreamDockControlService::repaintFromProfile);

    // Phase 15 Plan 15-02 (INPUT-03/04/05):
    //
    // pageNavRequested: connect to a logged sink for Phase 15.
    // Phase 16 replaces this lambda with the real page-model consumer
    // (active-page navigation via the page model owned by Phase 16).
    QObject::connect(
        m_streamDockInput.get(), &StreamDockInputService::pageNavRequested, this, [](int dir) {
            AJAZZ_LOG_INFO("input", "page nav intent {} (page model arrives Phase 16)", dir);
        });
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
    // bootstrap (constructor injection — there is no registry singleton).
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

    // User-level plugin search path: XDG `AppLocalDataLocation`, e.g.
    // `~/.local/share/ajazz-control-center/plugins` on Linux. Created
    // lazily so a fresh checkout doesn't error on the first launch.
    // Computed BEFORE the sandbox so it can be added to the read-only
    // allowlist (the child must be able to read plugin code it loads).
    QString const userPluginsQ =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
        QStringLiteral("/plugins");
    QDir().mkpath(userPluginsQ);

    // SECURITY (CWE-200): give the sandbox an explicit read-only
    // allowlist instead of leaving it unset (which fell through to the
    // no-op sandbox — plugins ran with FULL host authority) or binding
    // the host root. The child needs exactly: the python package dir
    // (so `import ajazz_plugins` resolves) and the user-plugins dir (so
    // it can load plugin code). The sandbox adds the system trees and
    // the child-script parent on top; `$HOME` stays hidden.
    [[maybe_unused]] std::vector<std::filesystem::path> readablePaths{
        std::filesystem::path{AJAZZ_PLUGIN_PYTHONPATH},
        std::filesystem::path{userPluginsQ.toStdString()},
    };
#if !defined(_WIN32)
    // Resolve the interpreter to a vetted absolute path (CWE-426
    // defense-in-depth: the sandboxed inner command then does not depend on
    // $PATH resolution inside the namespace), and bind its install prefix so
    // a non-/usr interpreter (conda/pyenv) stays reachable once the sandbox
    // scopes the filesystem. For the default /usr/bin/python3 the prefix is
    // /usr, already in the baseline binds (deduped by the sandbox).
    for (char const* candidate : {"/usr/bin/python3", "/usr/local/bin/python3", "/bin/python3"}) {
        if (::access(candidate, X_OK) == 0) {
            config.pythonExecutable = candidate;
            readablePaths.push_back(std::filesystem::path{candidate}.parent_path().parent_path());
            break;
        }
    }
#endif
#if defined(__linux__)
    // LinuxBwrapSandbox falls back to a no-op passthrough when `bwrap`
    // is not on PATH, so wiring it unconditionally is safe on systems
    // without bubblewrap.
    config.sandbox =
        std::make_unique<plugins::LinuxBwrapSandbox>(std::set<std::string>{}, readablePaths);
#elif defined(__APPLE__)
    config.sandbox =
        std::make_unique<plugins::MacosSandboxExecSandbox>(std::set<std::string>{}, readablePaths);
#endif
    // Windows: WindowsAppContainerSandbox requires capability SIDs that
    // are out of scope for this fix; leave config.sandbox unset (no-op)
    // until the AppContainer wiring lands.

    try {
        auto host = std::make_unique<plugins::OutOfProcessPluginHost>(std::move(config));

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
    SettingsService::registerInstance(m_settings.get());
    BatteryService::registerInstance(m_battery.get());
    AppUpdateService::registerInstance(m_appUpdate.get());
    FirmwareUpdateService::registerInstance(m_firmwareUpdate.get());
    // Phase 16 Plan 16-01 (DISPLAY-09): QML_SINGLETON exposure for the Stream Dock
    // control service -- brightness slider + clear-all button in the Keys tab.
    // Registers the same Application-owned instance so QML talks to the held handle,
    // not a separate instance (CLAUDE.md QML_SINGLETON gotcha / Pitfall 2).
    StreamDockControlService::registerInstance(m_streamDockControl.get());
    // Before the vendor firmware tool is launched, drop our HID handle for the
    // matching device family so the vendor flasher can claim the USB interface
    // uncontested (FIRMWARE-UPDATES.md §Launch vendor app). The shared backend
    // instances stay alive (flyweight); the next open() — typically the
    // post-flash re-enumeration — reopens the transport.
    QObject::connect(m_firmwareUpdate.get(),
                     &FirmwareUpdateService::aboutToLaunchVendorTool,
                     this,
                     [this](FirmwareUpdateService::Family family) {
                         core::DeviceFamily coreFamily = core::DeviceFamily::Unknown;
                         switch (family) {
                         case FirmwareUpdateService::StreamDock:
                             coreFamily = core::DeviceFamily::StreamDeck;
                             break;
                         case FirmwareUpdateService::Keyboard:
                             coreFamily = core::DeviceFamily::Keyboard;
                             break;
                         case FirmwareUpdateService::MouseAj159:
                         case FirmwareUpdateService::MouseAj199:
                             coreFamily = core::DeviceFamily::Mouse;
                             break;
                         case FirmwareUpdateService::Unknown:
                             return; // nothing to release
                         }
                         m_deviceRegistry.closeOpenDevicesInFamily(coreFamily);
                     });
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

                // Phase 14 Plan 14-02 (DISPLAY-06): when a Stream Dock arrives, call
                // setActiveDevice so the panel lights and the held handle is refreshed.
                // Phase 14 simplification: first connected Stream Dock wins; full
                // active-device selection UI is Phase 16.
                //
                // Phase 15 Plan 15-02 (INPUT-03/04/05): after the control service opens
                // the device, share the SAME held handle with the input service (ARCH-03
                // single-handle invariant — no second open()). The DeviceRegistry flyweight
                // guarantees that open() with the same (vid, pid, serial) returns the same
                // backend shared_ptr that the control service holds. Both services hold a
                // reference to the same shared_ptr<IDevice>; neither creates a second HID
                // session.
                if (d.family == core::DeviceFamily::StreamDeck) {
                    core::DeviceId const devId{
                        .vendorId = d.vendorId, .productId = d.productId, .serial = {}};
                    QTimer::singleShot(
                        std::chrono::milliseconds(300),
                        m_streamDockControl.get(),
                        [this, codename = QString::fromStdString(d.codename), devId] {
                            // 1. Let the control service open the device and light the panel.
                            m_streamDockControl->setActiveDevice(codename);
                            // 2. Share the held handle with the input service (ARCH-03).
                            //    The flyweight open() returns the same shared_ptr<IDevice>
                            //    that the control service holds; no second HID open occurs.
                            auto handle = m_deviceRegistry.open(devId);
                            m_streamDockInput->setActiveDevice(std::move(handle));
                        });
                }
                break;
            }
        }
    } else if (ev.action == core::HotplugAction::Removed) {
        // Phase 15 Plan 15-02: on Stream Deck departure, stop the input poll pump
        // and release the held handle to avoid use-after-free (T-15-05 mitigated).
        // StreamDockInputService::setActiveDevice(nullptr) stops the timer and
        // resets m_device (zombie-contract: no-op if already null).
        auto const descriptors = m_deviceRegistry.enumerate();
        for (auto const& d : descriptors) {
            if (d.vendorId == ev.vid && d.productId == ev.pid &&
                d.family == core::DeviceFamily::StreamDeck) {
                // CR-02: onHotplug runs on the HotplugMonitor background thread.
                // setActiveDevice is NOT thread-safe (touches QTimers and m_device
                // on the GUI thread). Marshal via Qt::QueuedConnection so it executes
                // on the GUI thread -- matching the Arrived path's QTimer::singleShot.
                QMetaObject::invokeMethod(
                    m_streamDockInput.get(),
                    [this] { m_streamDockInput->setActiveDevice(nullptr); },
                    Qt::QueuedConnection);
                break;
            }
        }
    }
}

} // namespace ajazz::app
