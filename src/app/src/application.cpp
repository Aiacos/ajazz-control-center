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

#include "active_window_watcher_factory.hpp"
#ifdef AJAZZ_HAVE_WEBSOCKETS
#include "app_event_dispatch.hpp"
#endif
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/hotplug_monitor.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "ajazz/mouse/mouse.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "debug_control_facade.hpp"
#include "debug_control_server.hpp"
#include "debug_logging.hpp"
#include "hotplug_debouncer.hpp"
#include "node_runner.hpp"
#include "sidecar_stream_dock_device.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#if defined(AJAZZ_HAVE_WEBENGINE)
#include "plugin_asset_server.hpp"
#endif

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

namespace {

/// F4: vendor actions that SdPluginServer routes (so they pass the auth gate and
/// reach an actionReceived consumer) but for which the host has no implementation.
/// `setBackground`/`clearIcon` are handled by the device bridge and are NOT here;
/// the visual/settings families are handled elsewhere too. This is the residual
/// set that would otherwise be a silent no-op — logged explicitly instead.
/// `sendToDevice` (raw-HID forwarding) stays unimplemented by the RE hard rule.
bool isUnsupportedVendorAction(QString const& event) {
    static QString const kUnsupported[] = {
        QStringLiteral("sendToDevice"),
        QStringLiteral("openTouchbarSecondaryMenu"),
        QStringLiteral("exitTouchbarSecondaryMenu"),
        QStringLiteral("enterGatheringEvent"),
        QStringLiteral("registrationScreenSaverEvent"),
        QStringLiteral("unRegistrationScreenSaverEvent"),
        QStringLiteral("lockScreen"),
        QStringLiteral("unLockScreen"),
        QStringLiteral("getScreenshot"),
        QStringLiteral("getSystemAudioVolume"),
        QStringLiteral("getUserInfo"),
        QStringLiteral("setAcImgTop"),
        QStringLiteral("onSwitchToFolderProfile"),
        QStringLiteral("onSwitchFromFolderProfile"),
        QStringLiteral("deleteAction"),
        QStringLiteral("stopBackground"),
        QStringLiteral("exitFullScreen"),
        QStringLiteral("getDetectedSensorsData"),
        QStringLiteral("startAudioCapture"),
        QStringLiteral("stopAudioCapture"),
        QStringLiteral("sendUserInfo"),
    };
    return std::any_of(std::begin(kUnsupported), std::end(kUnsupported), [&](QString const& e) {
        return e == event;
    });
}

} // namespace

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
      m_openDeckBridge(std::make_unique<OpenDeckBridge>(m_deviceModel.get(),
                                                        m_profileController.get(),
                                                        m_pluginCatalog.get(),
                                                        this)),
      m_loadedPlugins(std::make_unique<LoadedPluginsModel>(this)),
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
              // plugin executor: Phase 21-03 registry short-circuit (PLUGIN-12).
              // Captures `this` so the lambda can reach m_builtinActions AFTER the
              // Application constructor has finished. The lambda is only called from
              // the Qt event loop (GUI thread) — after all members are constructed.
              // Built-in UUIDs (com.hotspot.streamdock.*) short-circuit to the registry
              // via BuiltinActionsService::onPluginAction; third-party UUIDs pass
              // through to the Phase-19 logged stub (the prior Phase-15 fallback is
              // now injected into BuiltinActionsService as m_fallback).
              execs.plugin = [this](std::string_view id, std::string_view settingsJson) {
                  if (m_builtinActions) {
                      m_builtinActions->onPluginAction(id, settingsJson);
                  }
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
      // Phase 21-03 (PLUGIN-12): BuiltinActionsService — the built-in in-process action
      // dispatcher. Replaces the Phase-15 plugin executor stub with the registry short-circuit.
      // Injection seams:
      //   - BrightnessSink -> StreamDockControlService::setBrightness (codename from active device)
      //   - NavigateSink   -> StreamDockControlService::navigatePage (page carousel)
      //   - OpenUrlFn      -> reuse the app's existing openUrl path via QDesktopServices
      //   - engine         -> the ActionEngine owned by m_streamDockInput (via engine())
      //   - fallback       -> the Phase-15 logged stub (Phase-19 bridge path is wired via the
      //                       execs.plugin lambda that calls onPluginAction; non-builtin UUIDs
      //                       fall through to this logged no-op stub pending Phase-19 re-wire).
      //
      // -Wreorder: declared after m_streamDockInput so its engine() accessor is valid.
      m_builtinActions(std::make_unique<BuiltinActionsService>(
          // BrightnessSink: brightness is set on the first connected Stream Dock (codename
          // recorded by m_streamDockControl at setActiveDevice time). For now route to the
          // control service with a fixed codename lookup via m_streamDockControl's held handle.
          // Phase 25 will wire the codename from the active device codename.
          [this](int level) {
              // m_streamDockControl holds the active codename; call setBrightness with it.
              // The service clamps 0..100 internally; we pass the already-clamped level.
              auto const codename =
                  m_streamDockInput ? m_streamDockInput->activeDeviceCodename() : QString{};
              if (!codename.isEmpty()) {
                  m_streamDockControl->setBrightness(codename, level);
              }
          },
          // NavigateSink: route to the page carousel (Phase-16 / StreamDockControlService).
          [this](int direction) {
              if (m_streamDockControl) {
                  m_streamDockControl->navigatePage(direction);
              }
          },
          // OpenUrlFn: reuse the app's existing QDesktopServices openUrl path with scheme
          // validation (WR-01 from Phase-20). The BuiltinActionsService handler also
          // validates the scheme; this is a double-validation defence-in-depth.
          [](std::string_view url) {
              QUrl const qurl(QString::fromUtf8(url.data(), static_cast<qsizetype>(url.size())));
              if (qurl.scheme() != QStringLiteral("http") &&
                  qurl.scheme() != QStringLiteral("https")) {
                  AJAZZ_LOG_WARN("input",
                                 "openUrl (builtin): rejected non-http(s) URL scheme '{}'",
                                 qurl.scheme().toStdString());
                  return;
              }
              QDesktopServices::openUrl(qurl);
          },
          // ActionEngine*: the engine owned by m_streamDockInput (moved-in; always non-null).
          m_streamDockInput ? m_streamDockInput->engine() : nullptr,
          // ActionEngine plugin executor (Phase 19+): plugin actions are dispatched
          // by PluginDeviceBridge via the deviceEvent signal path, NOT by the
          // ActionEngine directly. The engine executor is therefore a deliberate
          // no-op: the bridge handles routing after dispatch() emits deviceEvent.
          // Prior stale log "ignored (plugin host arrives Phase 19)" removed — Phase 19
          // has shipped; the no-op is correct behaviour, not a future TODO.
          [](std::string_view /*id*/, std::string_view /*settingsJson*/) {},
          this)),
      // Phase 34 (APROF-01): foreground-window watcher. Plan 03 wired the real per-OS
      // backends behind app::makeActiveWindowWatcher(), which selects the Wayland (wlr-
      // foreign-toplevel) vs X11/EWMH backend at RUNTIME by session type on Linux (Win/
      // macOS compile-guarded), falling back to the recording stub on unsupported desktops
      // / when the feature gate is off. Constructed here on the GUI thread after the
      // QGuiApplication exists (Pitfall 5). Declared after m_builtinActions to keep the
      // init list in member-declaration order (-Wreorder). The window.setForeground debug
      // RPC injects synthetic foreground changes through the StubActiveWindowWatcher seam
      // when the stub backend is active.
      m_activeWindowWatcher(app::makeActiveWindowWatcher()),
#ifdef AJAZZ_HAVE_WEBSOCKETS
      // Phase 17 / Phase 19-02: SdPluginServer — Elgato-compatible WebSocket plugin
      // server (loopback-only). Constructed after m_streamDockInput to keep the init
      // list in member-declaration order (-Wreorder). Port 0 = OS-assigned; the actual
      // port is queryable via m_pluginServer->serverPort() after start().
      // start() is called in startBackgroundServices() so the Qt event loop is running.
      m_pluginServer(std::make_unique<SdPluginServer>(this)),
      // Phase 19-02 (PLUGIN-10): PluginDeviceBridge — wires SdPluginServer::actionReceived
      // to the StreamDockControlService paint path. Constructed after m_pluginServer +
      // m_streamDockControl + m_streamDockInput (all non-owning seam pointers; lifetime
      // guaranteed by member-declaration order: these members are destroyed AFTER the bridge).
      m_pluginBridge(std::make_unique<PluginDeviceBridge>(m_pluginServer.get(),
                                                          m_streamDockControl.get(),
                                                          m_streamDockInput.get(),
                                                          this)),
#endif
      // Debug console: always constructed (logging works without WebSockets).
      // attach() wires the live taps once the server/bridge/input exist.
      m_pluginDebug(std::make_unique<PluginDebugService>(this)),
      m_hotplug(std::make_unique<core::HotplugMonitor>()),
      m_debouncer(std::make_unique<HotplugDebouncer>(this)) {
    // Wire the debug console's protocol taps + simulation seams.
    m_pluginDebug->attach(
#ifdef AJAZZ_HAVE_WEBSOCKETS
        m_pluginServer.get(),
        m_pluginBridge.get(),
#endif
        m_streamDockInput.get());
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

    // OpenDeck UI (Phase 2B): give the bridge the live device-image path
    // (update_image -> set_image) and push backend changes to the web UI as
    // OpenDeck events. Set after construction (StreamDockControlService is
    // declared after the bridge in the member list, so the init list can't pass
    // it).
    m_openDeckBridge->setStreamDockControl(m_streamDockControl.get());
    // Same init-order reason: the input service is constructed after the bridge,
    // so inject it here for trigger_virtual_press' synthetic press path.
    m_openDeckBridge->setInputService(m_streamDockInput.get());
    // make_info (PI bootstrap Info, audit 5.3): lazy m_pluginManager read —
    // it is constructed later in startBackgroundServices.
    m_openDeckBridge->setInfoJsonResolver([this](QString const& plugin) -> QString {
        return m_pluginManager ? m_pluginManager->infoJsonForPlugin(plugin) : QString{};
    });
    // reload_plugin (audit 5.4): developer-mode Reload in the SPA — tear down
    // the running plugin, then respawn it from disk. Same lazy-null pattern as
    // the resolver above (m_pluginManager comes up in startBackgroundServices).
    m_openDeckBridge->setPluginReloader([this](QString const& id) {
        if (m_pluginManager) {
            m_pluginManager->unloadPlugin(id);
            m_pluginManager->rediscover();
        }
    });
    // show_settings_interface (audit 5.4): deliver the OpenDeck
    // `showSettingsInterface` event to the plugin over its WebSocket.
    m_openDeckBridge->setPluginEventSender([this](QString const& plugin, QJsonObject const& ev) {
        return m_pluginServer ? m_pluginServer->sendEvent(plugin, ev) : false;
    });
    // "Start at login" (audit 5.2): the SPA settings checkbox drives the OS
    // autolaunch through the same AutostartService the native QML page uses.
    m_openDeckBridge->setAutolaunchSetter([this](bool on) {
        if (m_autostart) {
            m_autostart->setLaunchOnLogin(on);
        }
    });
    // Reopened-PI settings (audit 6.4): overlay each instance's settings with
    // the live bridge-registry record so the PI shows what the plugin runs on.
    m_openDeckBridge->setInstanceSettingsResolver([this](QString const& ctx) -> QString {
        return m_pluginBridge ? m_pluginBridge->settingsJsonForContext(ctx) : QString{};
    });
    // opendeck.switchprofile / profile.switch (builtin parity audit): resolve
    // the target NAME against the active device's library and activate it —
    // the same lookup set_selected_profile performs for the SPA dropdown.
    if (m_builtinActions && m_profileController) {
        m_builtinActions->setProfileSwitcher([this](QString const& name) {
            QString const device =
                QString::fromStdString(m_profileController->activeProfile().deviceCodename);
            for (QVariant const& v : m_profileController->profilesForDevice(device)) {
                QVariantMap const m = v.toMap();
                if (m.value(QStringLiteral("name")).toString() == name) {
                    QString const id = m.value(QStringLiteral("id")).toString();
                    if (id != m_profileController->activeProfileId()) {
                        m_profileController->loadProfileById(id);
                    }
                    return;
                }
            }
            AJAZZ_LOG_WARN("builtin",
                           "profile.switch: no profile named '{}' for device '{}'",
                           name.toStdString(),
                           device.toStdString());
        });
    }
    QObject::connect(m_profileController.get(),
                     &ProfileController::profileChanged,
                     m_openDeckBridge.get(),
                     &OpenDeckBridge::notifyProfileChanged);
    QObject::connect(m_deviceModel.get(),
                     &DeviceModel::modelReset,
                     m_openDeckBridge.get(),
                     &OpenDeckBridge::notifyDevicesChanged);

    // Phase 14 Plan 14-02 (DISPLAY-08): wire profileChanged -> repaintFromProfile so
    // loading a profile repaints every bound key on the active Stream Deck.
    QObject::connect(m_profileController.get(),
                     &ProfileController::profileChanged,
                     m_streamDockControl.get(),
                     &StreamDockControlService::repaintFromProfile);

    // Phase 23 Plan 23-02 (DISPLAY-10 repaint): wire profileChanged ->
    // repaintEncodersFromProfile so encoder overlays also come back on profile load.
    // Reuses the SAME m_profileController signal and the SAME m_streamDockControl
    // handle + coalesced drain (no second monitor, accessor, or QTimer -- RESEARCH A5).
    // Keys and encoder overlays repaint together on every profileChanged emission.
    //
    // IN-02 ordering invariant: this connection is registered AFTER the
    // repaintFromProfile connection above. Qt delivers direct-connection slots in
    // registration order on the same thread, so repaintFromProfile fires first.
    // repaintFromProfile resets m_carouselIndex = 0 before calling repaintPage;
    // repaintEncodersFromProfile must NOT reset m_carouselIndex (it does not today).
    // If either method is later changed to read or write m_carouselIndex, the
    // ordering of these two connects becomes load-bearing -- reorder explicitly
    // rather than relying on registration order alone.
    QObject::connect(m_profileController.get(),
                     &ProfileController::profileChanged,
                     m_streamDockControl.get(),
                     &StreamDockControlService::repaintEncodersFromProfile);

    // Phase 21 (T-21-lunbo): wire profileChanged -> resetLunBoCursors so LunBo
    // per-key carousel positions start fresh when a new profile loads.
    // Without this, stale cursor positions from the previous profile persist and
    // the first action after a profile change is determined by the leftover index.
    QObject::connect(m_profileController.get(),
                     &ProfileController::profileChanged,
                     m_builtinActions.get(),
                     &BuiltinActionsService::resetLunBoCursors);

    // Phase 16 Plan 16-03 (PROFILE-02): wire pageNavRequested -> navigatePage so
    // a touch-strip swipe drives the top-level-page carousel and repaints the
    // new page via repaintPage(newPageId). The control service holds the carousel
    // index and the profile accessor; it is the single page-state authority for
    // the flat carousel (Decision 2). ActionEngine pushPage/popPage remain the
    // authority for vertical folder nesting (OpenFolder/BackToParent).
    QObject::connect(m_streamDockInput.get(),
                     &StreamDockInputService::pageNavRequested,
                     m_streamDockControl.get(),
                     &StreamDockControlService::navigatePage);

    // Phase 32-03 (BIND-05/07): Toggle Action cycle hook. A built-in Toggle press
    // resolves at StreamDockInputService::dispatch (dispatchToggle); the state
    // mutation needs the mutable Profile, owned only by ProfileController. Wire the
    // cycle hook to cycleInstanceState so the press advances currentState (mod N)
    // and persists it. The render hook (states[currentState] + state-change
    // willAppear) is wired below in the WEBSOCKETS block, where the bridge lives.
    //
    // Pitfall 3 / IN-02: this is NOT a profileChanged slot -- it is a direct hook
    // invoked synchronously from the toggle dispatch path, so it does not perturb
    // the repaint-before-context-registration connection ordering at lines 456-487.
    m_streamDockInput->setToggleCycleHook([this](QString const& controller, int index) {
        m_profileController->cycleInstanceState(controller, index);
    });

#ifdef AJAZZ_HAVE_WEBSOCKETS
    // Phase 19-03 (PLUGIN-10): outbound device -> plugin event routing.
    //
    // 1. Inject the profile accessor so populateContextsForActivePage can
    //    enumerate bound plugin actions (willAppear on connect/registration).
    m_pluginBridge->setProfileAccessor(
        [this]() -> core::Profile const& { return m_profileController->activeProfile(); });

    // 1a. Inject the manifest state-image resolver so the bridge can auto-render
    //     a multi-state action's declared States[index].Image on setState. Read
    //     m_pluginManager lazily (it is constructed later, in
    //     startBackgroundServices); setState only fires long after startup, and
    //     the null guard degrades gracefully if discovery has not run.
    m_pluginBridge->setStateImageResolver(
        [this](QString const& actionUuid, int stateIndex) -> QString {
            return m_pluginManager ? m_pluginManager->stateImagePath(actionUuid, stateIndex)
                                   : QString{};
        });

    // 1a-bis. Inject the stored action-owner resolver (PluginManager::ownerForAction)
    //     so the bridge maps an action UUID to its owning plugin via the discovered
    //     manifests (OpenDeck stored-owner model) instead of requiring the action
    //     UUID to be a dotted prefix of the plugin UUID. Lazy m_pluginManager read
    //     with the same null-guard rationale as the state-image resolver above.
    m_pluginBridge->setActionOwnerResolver([this](QString const& actionUuid) -> QString {
        return m_pluginManager ? m_pluginManager->ownerForAction(actionUuid) : QString{};
    });

    // 1a-ter. Inject the manifest default-Settings resolver so a first-run
    //     instance appears with the manifest action's `Settings` defaults
    //     (vendor StreamDock parity — MiraBox SDVueSDK draw code throws on
    //     empty settings and the 1 Hz repaint never starts). Same lazy
    //     m_pluginManager read + null-guard as the resolvers above.
    m_pluginBridge->setDefaultSettingsResolver([this](QString const& actionUuid) -> QString {
        return m_pluginManager ? m_pluginManager->defaultSettingsForAction(actionUuid) : QString{};
    });

    // 1a-quinquies. Inject the device-geometry resolver (PLUGIN-GAP-ANALYSIS F2)
    //     so the bridge sources key columns/rows, the Elgato DeviceType, and the
    //     model name from the connected device's own core::DeviceDescriptor
    //     instead of the former AKP05E-hardcoded keyCols=5. Same O(N) codename
    //     walk over m_deviceRegistry.enumerate() as the DeviceLookup lambdas
    //     above (N ~20, resolved on plugin events, off the hot path). An unknown
    //     codename yields the AKP05E DeviceGeometry default — identical to the
    //     pre-F2 behaviour, so single-device setups are unchanged.
    m_pluginBridge->setDeviceGeometryResolver(
        [this](QString const& codename) -> ajazz::app::DeviceGeometry {
            auto const descriptors = m_deviceRegistry.enumerate();
            for (auto const& d : descriptors) {
                if (QString::fromStdString(d.codename) != codename) {
                    continue;
                }
                ajazz::app::DeviceGeometry g;
                g.keyCols = static_cast<std::uint8_t>(d.gridColumns);
                // keyRows: prefer the explicit descriptor field; fall back to
                // keyCount/gridColumns when 0 (the AKP815 deferred sentinel).
                g.keyRows = static_cast<std::uint8_t>(
                    d.keyRows != 0 ? d.keyRows
                                   : (d.gridColumns != 0 ? d.keyCount / d.gridColumns : 0));
                g.keyCount = d.keyCount;
                g.encoderCount = d.encoderCount;
                // Elgato DeviceType: a device with dials or a touch strip is
                // modelled as Stream Deck + (7); a pure key grid as the classic
                // Stream Deck (0). See elgato_plugin_protocol.md §6.3.
                g.elgatoType = (d.hasTouchStrip || d.encoderCount > 0) ? 7 : 0;
                g.model = QString::fromStdString(d.model);
                return g;
            }
            return ajazz::app::DeviceGeometry{}; // unknown codename -> AKP05E default
        });

    // 1a-quarter. Inject the encoder layout resolver so dial actions get their
    //     manifest layout + icon rendered on the strip zone at mount, and
    //     setFeedback/setFeedbackLayout drive the built-in layouts at runtime.
    m_pluginBridge->setEncoderLayoutResolver(
        [this](QString const& actionUuid) -> std::pair<QString, QString> {
            return m_pluginManager ? m_pluginManager->encoderLayoutInfo(actionUuid)
                                   : std::pair<QString, QString>{};
        });

    // 1a-ter. Inject the action state-metadata resolver so the bridge can apply
    //     the Elgato/OpenDeck automatic state cycle on keyUp (2-state actions
    //     advance unless DisableAutomaticStates). Same lazy null-guard pattern.
    m_pluginBridge->setActionStateMetaResolver(
        [this](QString const& actionUuid) -> std::pair<int, bool> {
            return m_pluginManager ? m_pluginManager->actionStateMeta(actionUuid)
                                   : std::pair<int, bool>{0, false};
        });

    // 1b. Wire deviceActivated -> input-service codename + bridge.onDeviceConnected
    //     (GAP-28B fix): StreamDockControlService::setActiveDevice now emits
    //     deviceActivated on every successful open. By wiring it here we ensure
    //     ALL callers — QML auto-select, debug RPC, and the hot-plug path — share
    //     a single propagation path instead of each one having to know about the
    //     input service's codename field and the bridge's onDeviceConnected seam.
    //
    //     Idempotency: setActiveDeviceCodename just overwrites m_activeDeviceId
    //     (string assignment — always safe).  onDeviceConnected re-populates
    //     contexts and re-sends willAppear for all live bindings — also safe.
    //
    //     The hot-plug path previously had explicit calls to setActiveDeviceCodename
    //     and onDeviceConnected after setActiveDevice; those are removed (see
    //     onHotplug below) so they only fire once via this signal.
    QObject::connect(m_streamDockControl.get(),
                     &StreamDockControlService::deviceActivated,
                     m_streamDockInput.get(),
                     [this](QString const& codename) {
                         // 1. Record the active-device codename (the id string used to tag
                         //    every emitted DeviceEvent). setActiveDevice(handle) below does
                         //    NOT carry this — it only sizes the poll/encoder state.
                         m_streamDockInput->setActiveDeviceCodename(codename);

                         // 2. Share the held flyweight handle with the input service so it can
                         //    poll real hardware AND size its encoder-count guard
                         //    (m_encoderCount, from descriptor.encoderCount). Without this the
                         //    dispatch() guard `encIndex >= m_encoderCount` drops EVERY encoder
                         //    event — real or synthetic — because m_encoderCount stays 0.
                         //
                         //    This previously lived ONLY in the hot-plug Arrived branch, so a
                         //    device present at startup (Linux udev does not replay coldplug as
                         //    an Arrived event; macOS drains the initial set, so it worked there)
                         //    never had its input service sized. Moving it onto the shared
                         //    deviceActivated channel makes QML auto-select, the debug RPC, and
                         //    hot-plug all size the input service identically (GAP-28B spirit).
                         //
                         //    The DeviceRegistry flyweight guarantees open() with the same
                         //    (vid, pid) returns the SAME backend shared_ptr the control service
                         //    already holds (ARCH-03 single-handle invariant — no second HID
                         //    session). Resolve codename -> DeviceId via enumerate().
                         for (auto const& d : m_deviceRegistry.enumerate()) {
                             if (QString::fromStdString(d.codename) == codename &&
                                 d.family == core::DeviceFamily::StreamDeck) {
                                 core::DeviceId const devId{.vendorId = d.vendorId,
                                                            .productId = d.productId,
                                                            .serial = {}};
                                 m_streamDockInput->setActiveDevice(m_deviceRegistry.open(devId));
                                 break;
                             }
                         }
                     });
    QObject::connect(m_streamDockControl.get(),
                     &StreamDockControlService::deviceActivated,
                     m_pluginBridge.get(),
                     &PluginDeviceBridge::onDeviceConnected);

    // 2. DeviceEvent tap: StreamDockInputService::deviceEvent -> bridge::onDeviceEvent.
    //    The input service emits the raw DeviceEvent after dispatching the ActionChain.
    //    The bridge maps it to §4.4 plugin events via sendEvent (T-19-leak: byCoord lookup).
    QObject::connect(m_streamDockInput.get(),
                     &StreamDockInputService::deviceEvent,
                     m_pluginBridge.get(),
                     &PluginDeviceBridge::onDeviceEvent);

    // 2a. Phase 32-03 (BIND-07): Toggle Action render hook. After the cycle hook
    //     advances currentState (wired above), the input service calls this hook to
    //     repaint states[currentState] on the control (reusing the bridge's existing
    //     assignKeyImage/assignEncoderImage setState path) and to emit a state-change
    //     willAppear for any plugin that owns the context. Direct hook (not a
    //     profileChanged slot) so the Pitfall-3 connection ordering is untouched.
    m_streamDockInput->setToggleRenderHook(
        [this](QString const& controller, int index, core::ActionInstance const& inst) {
            m_pluginBridge->renderToggleState(controller, index, inst);
        });

    // 3. Plugin lifecycle: pluginRegistered / pluginDisconnected -> bridge lifecycle.
    //    Populates contexts + willAppear on registration; retires on disconnect.
    QObject::connect(m_pluginServer.get(),
                     &SdPluginServer::pluginRegistered,
                     m_pluginBridge.get(),
                     &PluginDeviceBridge::onPluginRegistered);
    QObject::connect(m_pluginServer.get(),
                     &SdPluginServer::pluginDisconnected,
                     m_pluginBridge.get(),
                     &PluginDeviceBridge::onPluginDisconnected);

    // 3b. Live key visuals -> OpenDeck web UI. A plugin setImage/setTitle paints
    //     the physical key via the bridge; this mirror pushes the same frame to
    //     the SPA canvas as an "update_state" event so the on-screen key tracks
    //     the hardware (without it the SPA shows only the bind-time icon).
    QObject::connect(m_pluginBridge.get(),
                     &PluginDeviceBridge::liveInstanceVisual,
                     m_openDeckBridge.get(),
                     &OpenDeckBridge::notifyLiveInstanceVisual);

    // 3c. audit 4.9: the remaining SPA event mirrors — showAlert/showOk feedback
    //     glyphs ("show_alert"/"show_ok"), physical press state ("key_moved"),
    //     and the plugin deviceBrightness extension ("device_brightness").
    QObject::connect(m_pluginBridge.get(),
                     &PluginDeviceBridge::instanceFeedback,
                     m_openDeckBridge.get(),
                     &OpenDeckBridge::notifyInstanceFeedback);
    QObject::connect(m_pluginBridge.get(),
                     &PluginDeviceBridge::keyPressMirror,
                     m_openDeckBridge.get(),
                     &OpenDeckBridge::notifyKeyPress);
    QObject::connect(m_pluginBridge.get(),
                     &PluginDeviceBridge::deviceBrightnessRequested,
                     m_openDeckBridge.get(),
                     &OpenDeckBridge::notifyDeviceBrightness);

    // 3d. PI settings -> profile binding mirror: a Property Inspector
    //     setSettings persists to the plugin settings store AND into the
    //     binding, so builtin actions (which execute on the binding's
    //     settingsJson) pick up PI edits, and the value survives restarts in
    //     the profile. Gated on the active profile's device.
    QObject::connect(
        m_pluginBridge.get(),
        &PluginDeviceBridge::bindingSettingsPersisted,
        m_profileController.get(),
        [this](QString const& deviceId,
               QString const& controller,
               int index,
               QString const& settingsJson) {
            if (QString::fromStdString(m_profileController->activeProfile().deviceCodename) !=
                deviceId) {
                return;
            }
            m_profileController->updateBindingSettings(controller, index, settingsJson);
        });

    // 3a-F3. Property Inspector second-connection model (canonical doc §5).
    //   The server resolves which plugin owns a PI's instance context via this
    //   resolver, backed by the bridge's ContextRegistry (the trusted
    //   context→plugin map). It routes sendToPlugin from a stock PI to the owner
    //   and lets us forward propertyInspectorDidAppear/…DidDisappear back.
    // Audit 2.4: one context namespace on the PI<->plugin relays — the server
    // canonicalizes SPA dot-form contexts to the wire `#` id via the bridge.
    m_pluginServer->setContextCanonicalizer([this](QString const& context) -> QString {
        return m_pluginBridge ? m_pluginBridge->canonicalContextId(context) : context;
    });
    m_pluginServer->setContextOwnerResolver([this](QString const& context) -> QString {
        // Accept the SPA "device.profile.controller.position" context the PI
        // registers with, not only the bridge wire id — otherwise the PI's owner
        // never resolves and its settings/relay routing silently breaks.
        auto const ctx = m_pluginBridge->lookupContext(context);
        return ctx.has_value() ? ctx->pluginUuid : QString{};
    });
    //   B7: source the vendor passHello.deviceInfo from the bridge's per-device
    //   geometry (the same shape deviceDidConnect sends) so a plugin reading
    //   geometry at hello gets the real grid, not an empty {}.
    m_pluginServer->setDeviceInfoResolver(
        [this](QString const& deviceId) { return m_pluginBridge->deviceInfoFor(deviceId); });
    //   On PI connect/disconnect, tell the owning plugin its PI is visible/hidden.
    //   These events carry {action,context,device,event} and NO payload (§3).
    auto const sendPiLifecycle =
        [this](QString const& context, QString const& owner, QString const& eventName) {
            if (owner.isEmpty()) {
                return; // unresolved context — nothing to notify
            }
            // T023 dedup: a modern PI also registers its own WebSocket, so the
            // inspectorOpened path (below) fires for the same open. Gate on the
            // instance context so propertyInspectorDidAppear/…DidDisappear is sent
            // exactly once regardless of which emit site fires first.
            bool const isAppear = eventName == QStringLiteral("propertyInspectorDidAppear");
            bool const shouldEmit = isAppear ? m_piAppearGate.noteAppear(context)
                                             : m_piAppearGate.noteDisappear(context);
            if (!shouldEmit) {
                return; // duplicate appear, or unpaired disappear — suppress
            }
            auto const ctx = m_pluginBridge->lookupContext(context);
            if (!ctx.has_value()) {
                return;
            }
            QJsonObject const ev{
                {QStringLiteral("action"), ctx->actionUUID},
                // Canonical wire id: the PI registered with the SPA dot form,
                // which the plugin cannot match against its willAppear
                // contexts (audit 2.4).
                {QStringLiteral("context"), ajazz::app::ContextRegistry::deriveContextId(*ctx)},
                {QStringLiteral("device"), ctx->deviceId},
                {QStringLiteral("event"), eventName},
            };
            m_pluginServer->sendEvent(owner, ev);
        };
    QObject::connect(m_pluginServer.get(),
                     &SdPluginServer::propertyInspectorRegistered,
                     this,
                     [sendPiLifecycle](QString const& context, QString const& owner) {
                         sendPiLifecycle(
                             context, owner, QStringLiteral("propertyInspectorDidAppear"));
                     });
    QObject::connect(m_pluginServer.get(),
                     &SdPluginServer::propertyInspectorDisconnected,
                     this,
                     [sendPiLifecycle](QString const& context, QString const& owner) {
                         sendPiLifecycle(
                             context, owner, QStringLiteral("propertyInspectorDidDisappear"));
                     });

    // 3b. Host-level (context-free) plugin system events. The device bridge
    //     deliberately ignores everything that is not a key-visual action
    //     (isVisualAction), so openUrl / logMessage — which Elgato and OpenDeck
    //     handle at the HOST, not the device — would otherwise be silently
    //     dropped. Wire them here, reusing the same strict http(s)-only scheme
    //     guard as the built-in openUrl executor above (WR-01: never
    //     QUrl::fromUserInput; reject any non-http(s) scheme so a hostile plugin
    //     cannot smuggle file:// or custom-scheme launches through the WS).
    QObject::connect(
        m_pluginServer.get(),
        &SdPluginServer::actionReceived,
        this,
        [this](QString const& pluginUuid, QJsonObject const& action) {
            QString const event = action.value(QStringLiteral("event")).toString();
            QJsonObject const payload = action.value(QStringLiteral("payload")).toObject();
            if (event == QStringLiteral("switchToProfile") ||
                event == QStringLiteral("switchProfile")) {
                // EVENT-03: inbound host command — a plugin asks the host to
                // activate a profile. Two wire shapes reach here:
                //   - Elgato "switchToProfile": profile/device inside "payload"
                //   - OpenDeck "switchProfile" (starterpack switch_profile.rs):
                //     profile/device at the ENVELOPE top level
                // The "profile" token is UNTRUSTED (T-34-04-03 tampering): treat
                // it purely as a lookup key, never evaluated/shelled, and
                // V5-bound its length before use so a hostile plugin cannot
                // smuggle a huge or malformed token. The optional "device"
                // scopes resolution to that device's profiles.
                constexpr int kMaxTokenChars = 256; // V5 payload bound
                QString profileToken =
                    payload.value(QStringLiteral("profile")).toString().left(kMaxTokenChars);
                if (profileToken.isEmpty()) {
                    profileToken =
                        action.value(QStringLiteral("profile")).toString().left(kMaxTokenChars);
                }
                QString deviceToken =
                    payload.value(QStringLiteral("device")).toString().left(kMaxTokenChars);
                if (deviceToken.isEmpty()) {
                    deviceToken =
                        action.value(QStringLiteral("device")).toString().left(kMaxTokenChars);
                }
                if (profileToken.trimmed().isEmpty()) {
                    AJAZZ_LOG_WARN("plugin",
                                   "switchToProfile: rejected empty/blank profile token from "
                                   "plugin {}",
                                   pluginUuid.toStdString());
                    return; // reject: no crash, no activation
                }
                // Resolve the token to a known profile id (exact id, then name
                // scoped to the device — RESEARCH Open Q3). Unresolvable -> reject.
                QString const resolvedId =
                    resolveSwitchToProfileToken(*m_profileController, profileToken, deviceToken);
                if (resolvedId.isEmpty()) {
                    AJAZZ_LOG_WARN("plugin",
                                   "switchToProfile: token '{}' from plugin {} matched no known "
                                   "profile — ignored",
                                   profileToken.toStdString(),
                                   pluginUuid.toStdString());
                    return; // bad token: no crash, no activation
                }
                AJAZZ_LOG_INFO("plugin",
                               "switchToProfile: plugin {} -> profile '{}'",
                               pluginUuid.toStdString(),
                               resolvedId.toStdString());
                m_profileController->loadProfileById(resolvedId);
                return;
            }
            if (event == QStringLiteral("openUrl")) {
                QString const url = payload.value(QStringLiteral("url")).toString();
                QUrl const qurl(url);
                if (qurl.scheme() != QStringLiteral("http") &&
                    qurl.scheme() != QStringLiteral("https")) {
                    AJAZZ_LOG_WARN("plugin",
                                   "openUrl: rejected non-http(s) URL from plugin {} (scheme '{}')",
                                   pluginUuid.toStdString(),
                                   qurl.scheme().toStdString());
                    return;
                }
                AJAZZ_LOG_INFO("plugin",
                               "openUrl: plugin {} -> {}",
                               pluginUuid.toStdString(),
                               url.toStdString());
                QDesktopServices::openUrl(qurl);
            } else if (event == QStringLiteral("logMessage")) {
                QString const msg = payload.value(QStringLiteral("message")).toString();
                // Bound the logged size: a hostile plugin could otherwise spam the
                // unified log / ring buffer via logMessage() loops (mirrors the
                // PIBridge::logMessage cap).
                constexpr int kMaxLogChars = 2048;
                QString const bounded =
                    msg.length() > kMaxLogChars
                        ? msg.left(kMaxLogChars) + QStringLiteral("...(truncated)")
                        : msg;
                AJAZZ_LOG_INFO(
                    "plugin", "[{}] {}", pluginUuid.toStdString(), bounded.toStdString());
            } else if (isUnsupportedVendorAction(event)) {
                // F4: these vendor actions are ROUTED by SdPluginServer (so they
                // pass the auth gate and reach a consumer) but have no handler in
                // either the device bridge (not isVisualAction) or here. Previously
                // they vanished silently, leaving a plugin author with no signal
                // that the call did nothing. Surface an explicit WARN so the
                // unsupported call is visible in the unified log / debug console
                // instead of being an invisible no-op. `sendToDevice` raw-HID
                // forwarding stays deliberately unimplemented (RE hard rule); it is
                // logged here, never executed.
                AJAZZ_LOG_WARN("plugin",
                               "unsupported action '{}' requested by plugin {} — not implemented "
                               "(no-op)",
                               event.toStdString(),
                               pluginUuid.toStdString());
            }
        });

    // 4. Page navigation: StreamDockControlService::pageNavigated -> bridge::onActivePageChanged.
    //    WR-02: fired from navigatePage() (connected to pageNavRequested in Phase 16) after
    //    the carousel index advances. The bridge retires old-page contexts (willDisappear)
    //    and populates new-page contexts (willAppear) so plugin lifecycles track page changes.
    QObject::connect(m_streamDockControl.get(),
                     &StreamDockControlService::pageNavigated,
                     m_pluginBridge.get(),
                     &PluginDeviceBridge::onActivePageChanged);

    // 5. Wire profileChanged -> populateContextsForActivePage so a drag-drop
    //    binding registers an ActionContext in the bridge immediately (PLUGIN-19).
    //
    //    IN-02 ordering invariant: registered AFTER the repaint connections at
    //    lines 452-483 so repaintFromProfile fires before context registration
    //    (repaint first, then register; same ordering as onDeviceConnected).
    //
    //    Guard (T-28-09): no-op when activeDeviceId() is empty (startup, no device
    //    connected yet). Do NOT fall back to a hardcoded codename — register nothing
    //    until a real device connect has set m_activeDeviceId via onDeviceConnected.
    QObject::connect(m_profileController.get(),
                     &ProfileController::profileChanged,
                     m_pluginBridge.get(),
                     [this]() {
                         if (!m_pluginBridge->activeDeviceId().isEmpty()) {
                             m_pluginBridge->populateContextsForActivePage(
                                 m_pluginBridge->activeDeviceId());
                         }
                     });

#if defined(AJAZZ_HAVE_WEBENGINE)
    // OpenDeck asset webserver: the SPA loads every non-data icon from
    // http://localhost:<get_port_base + 2>/<path> (ports.ts getWebserverUrl).
    // get_port_base is the 57116 stub (opendeck_bridge.cpp), so serve on 57118.
    // Installed-plugin icons are emitted as `__pluginasset__/<dir>/<rel>` paths
    // that resolve to userPluginsDir()/<dir>/<rel> here.
    m_pluginAssetServer = std::make_unique<PluginAssetServer>(this);
    m_pluginAssetServer->start(57118);
#endif // AJAZZ_HAVE_WEBENGINE
#endif // AJAZZ_HAVE_WEBSOCKETS
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
    // Unify every log source into one queryable stream BEFORE any subsystem
    // logs: a tee that fans stderr + an append-only file + an in-memory ring,
    // plus a Qt message-handler bridge so qDebug/qCDebug land in the same
    // place. The ring backs the out-of-process debug log channel. The level
    // honours the AJAZZ_LOG_LEVEL env override (default Info, as before).
    m_logFilePath = DebugLogging::defaultLogFilePath();
    m_logRing = DebugLogging::install(m_logFilePath, logLevelFromEnv(core::LogLevel::Info));
    AJAZZ_LOG_INFO("app", "logging unified: file={}", m_logFilePath.toStdString());

    // Audit finding A1: pass the owned registry into every backend
    // bootstrap (constructor injection — there is no registry singleton).
    //
    // experiment/mirajazz Slice 4c: the AKP05E (0x0300:0x3004) is driven by the
    // out-of-process mirajazz sidecar backend by default (verified live via the
    // debug channel). Registered BEFORE registerAll so it wins the (VID,PID)
    // slot ahead of registerAll (which now registers only the AKP815 carve-out).
    // Set AJAZZ_NO_SIDECAR to skip registering the sidecar SKUs entirely — there
    // is NO in-tree C++ fallback for AKP03/05/153 anymore (their wire backends
    // were removed in Slice D), so those families are simply unsupported when it
    // is set; AKP815 is unaffected.
    if (!qEnvironmentVariableIsSet("AJAZZ_NO_SIDECAR")) {
        auto const sidecarDevices = streamdeck::streamDockSidecarDescriptors();
        for (auto const& d : sidecarDevices) {
            m_deviceRegistry.registerDevice(d, &makeSidecarStreamDock);
        }
        AJAZZ_LOG_INFO("bootstrap",
                       "{} Stream Dock SKUs routed to mirajazz sidecar backend",
                       static_cast<int>(sidecarDevices.size()));
    }
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
    // T037 (002): when a plugin is uninstalled, revert any key/dial bound to its
    // actions to unbound (the control must not reference a gone plugin or crash).
    // Both members are constructor-owned, so wire it unconditionally here.
    QObject::connect(
        m_pluginCatalog.get(),
        &PluginCatalogModel::pluginUninstalled,
        m_profileController.get(),
        [this](QString const& uuid) { m_profileController->clearBindingsForPlugin(uuid); });
    // Audit 3.1: an uninstall must also STOP the running plugin (exitApp ->
    // kill -> m_live erase) — before this, a "removed" plugin kept painting the
    // device and the stale m_live key blocked any re-install until restart.
    // Audit 3.2: a re-install/update REPLACES the dir; tear the old copy down
    // first (bindings preserved — pluginWillBeReplaced is NOT pluginUninstalled)
    // so the post-install rediscover() respawns the fresh code. m_pluginManager
    // is created later (startBackgroundServices), hence the lazy null guard.
    QObject::connect(m_pluginCatalog.get(),
                     &PluginCatalogModel::pluginUninstalled,
                     this,
                     [this](QString const& uuid) {
                         if (m_pluginManager) {
                             m_pluginManager->unloadPlugin(uuid);
                         }
                     });
    QObject::connect(m_pluginCatalog.get(),
                     &PluginCatalogModel::pluginWillBeReplaced,
                     this,
                     [this](QString const& dirName) {
                         if (m_pluginManager) {
                             m_pluginManager->unloadPlugin(dirName);
                         }
                     });
    PluginDebugService::registerInstance(m_pluginDebug.get());
    LoadedPluginsModel::registerInstance(m_loadedPlugins.get());
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

#ifdef AJAZZ_HAVE_WEBSOCKETS
    // Phase 19-02 / Phase 17: start the WebSocket plugin server on the loopback
    // port the embedded OpenDeck SPA expects. The SPA's getWebSocketPort()
    // returns get_port_base() (the 57116 stub in opendeck_bridge.cpp) and a
    // Property Inspector opens its own WebSocket to ws://localhost:<that port>;
    // it also derives the plugin-asset webserver as base+2 (57118). An OS-assigned
    // port (start(0)) bound the server somewhere else, so PIs could never connect
    // and setSettings never reached the plugin (configured actions stayed inert).
    // Pin to 57116 so PI WS, spawned-plugin -port (serverPort()), and the asset
    // server all agree. (Upstream OpenDeck likewise uses a fixed PORT_BASE.)
    constexpr std::uint16_t kPluginServerPort = 57116;
    if (!m_pluginServer->start(kPluginServerPort)) {
        AJAZZ_LOG_WARN("app", "SdPluginServer failed to start — plugin functionality disabled");
    } else {
        AJAZZ_LOG_INFO("app",
                       "SdPluginServer listening on port {}",
                       static_cast<int>(m_pluginServer->serverPort()));

        // Elgato .sdPlugin (node/html/native) discovery + spawn. The manager
        // must be created AFTER the server is listening because spawn() reads
        // serverPort() for the child's -port argv. User-level install dir:
        // XDG AppLocalDataLocation/plugins (same tree PluginCatalogModel
        // installs into). This is the runtime that makes installed Stream
        // Dock plugins actually run + register over the WebSocket.
        QString const pluginsDir =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
            QStringLiteral("/plugins");
        QDir().mkpath(pluginsDir);
        m_pluginManager = std::make_unique<PluginManager>(
            pluginsDir, m_pluginServer.get(), makeDefaultNodeProbe());

        // F1 (PLUGIN-GAP-ANALYSIS): feed the -info.devices[] array from the
        // currently-connected devices so device-aware plugins can target keys.
        // Built from DeviceModel::connectedCodenames() + the descriptor geometry,
        // matching the deviceDidConnect deviceInfo (same id=codename, size, and
        // Elgato DeviceType). MUST be set before discover()/spawn() below, which
        // call buildInfoJson(). See elgato_plugin_protocol.md §2.3.
        m_pluginManager->setDevicesInfoProvider([this]() -> QJsonArray {
            QJsonArray devices;
            auto const descriptors = m_deviceRegistry.enumerate();
            for (QString const& codename : m_deviceModel->connectedCodenames()) {
                for (auto const& d : descriptors) {
                    if (QString::fromStdString(d.codename) != codename) {
                        continue;
                    }
                    // Only renderable Stream Deck surfaces belong in the plugin
                    // devices[] — a keyboard/mouse (keyCount==0) is not addressable
                    // by a .sdPlugin and would appear as a useless 0x0 device.
                    if (d.keyCount == 0) {
                        break;
                    }
                    int const rows = d.keyRows != 0
                                         ? d.keyRows
                                         : (d.gridColumns != 0 ? d.keyCount / d.gridColumns : 0);
                    int const type = (d.hasTouchStrip || d.encoderCount > 0) ? 7 : 0;
                    devices.append(QJsonObject{
                        {QStringLiteral("id"), codename},
                        {QStringLiteral("name"), QString::fromStdString(d.model)},
                        {QStringLiteral("type"), type},
                        {QStringLiteral("size"),
                         QJsonObject{{QStringLiteral("columns"), d.gridColumns},
                                     {QStringLiteral("rows"), rows}}},
                    });
                    break;
                }
            }
            return devices;
        });

        // HOST-01 (Phase 30-03): construct the UnifiedPluginHost aggregator now that both
        // sub-hosts are known. m_pluginHost may still be nullptr if AJAZZ_PYTHON_HOST is
        // not set or the Python host failed to start — the aggregator degrades gracefully.
        // Declaration order in application.hpp guarantees m_pluginHost2 is destroyed BEFORE
        // m_pluginManager (so the aggregator's raw pointer is always valid while it lives).
#ifdef AJAZZ_PYTHON_HOST
        plugins::IPluginHost* pythonHost = m_pluginHost.get();
#else
        plugins::IPluginHost* pythonHost = nullptr;
#endif
        m_pluginHost2 = std::make_unique<UnifiedPluginHost>(m_pluginManager.get(), pythonHost);

        auto const runnable = m_pluginManager->discover();
        AJAZZ_LOG_INFO(
            "app", "plugin discovery: {} runnable plugin(s)", static_cast<int>(runnable.size()));
        for (auto const& manifest : runnable) {
            m_pluginManager->spawn(manifest);
        }

        // WINPLG (CR-01): re-point the loaded-plugins model at the MERGED inventory.
        // The model was first filled (in initPluginHost) from the Python host ONLY,
        // whose PluginInfo entries always carry winClass==0 (NotWindowsOnly) → the
        // WINPLG platform-status chip was always hidden. The only producer of a real
        // winClass is PluginManager (stamped at scan time in discover()), surfaced via
        // UnifiedPluginHost::plugins() which MERGES the .sdPlugin and Python inventories.
        //
        // Re-wiring here is correct + safe (each hazard verified against REVIEW CR-01):
        //   - setPlugins() does a full beginResetModel/endResetModel REPLACE (not an
        //     append), so this swaps the Python-only list for the complete merged list
        //     — no double-population. Python entries remain present because
        //     UnifiedPluginHost::plugins() appends them.
        //   - spawn() inserts into PluginManager::m_live synchronously, so
        //     m_pluginHost2->plugins() already returns the .sdPlugin entries here — no
        //     need to await async WebSocket registration.
        //   - setPluginHost() re-points a future QML "Reload" (refresh()) at the merged
        //     host, which is the correct behaviour for this page.
        // m_pluginHost2 is always constructed above (independent of AJAZZ_PYTHON_HOST);
        // the UnifiedPluginHost takes a nullable Python host, so this works whether or
        // not the Python host is present.
        if (m_loadedPlugins && m_pluginHost2) {
            m_loadedPlugins->setPluginHost2(m_pluginHost2.get());
            m_loadedPlugins->setPlugins(m_pluginHost2->plugins());
            AJAZZ_LOG_INFO("app",
                           "loaded-plugins model re-wired to unified host: {} plugin(s)",
                           m_loadedPlugins->rowCountSimple());
        }

        // Plan 27-02 (PLUGIN-15): trigger a re-scan when a plugin is installed
        // from the GUI so it runs live with NO app restart (D-27-3 idempotency).
        // Guard on ok==true so a failed or refused install does not cause a scan.
        // m_pluginCatalog is constructed before startBackgroundServices() is called
        // (it is an Application constructor member) so the pointer is always valid here.
        //
        // WR-04 invariant: this fires on bare installFinished(ok=true), which also
        // covers idempotent successes — the install() "already installed" case
        // (plugin_catalog_model.cpp) and openUpstream-only fallbacks where nothing
        // was promoted locally. That is SAFE because PluginManager::rediscover() is
        // idempotent (diffs against m_live, spawns only newly-added dirs) and trusts
        // every dir it reaches to be PRE-VERIFIED: each promotion path quarantines
        // any Refused/tampered or unconsented-Unsigned package before it lands in the
        // plugins dir, so rediscover() never sees an unverified dir here. See the
        // contract comment at PluginManager::rediscover().
        QObject::connect(m_pluginCatalog.get(),
                         &PluginCatalogModel::installFinished,
                         m_pluginManager.get(),
                         [this](QString const& /*uuid*/, bool ok, QString const& /*error*/) {
                             if (ok) {
                                 m_pluginManager->rediscover();
                                 // Refresh the loaded-plugins model so a freshly
                                 // installed .sdPlugin (and its trust/platform
                                 // chips) appears WITHOUT an app restart.
                                 // rediscover() spawns synchronously into m_live,
                                 // so the merged m_pluginHost2->plugins() already
                                 // includes the new entry here (audit WARNING-2).
                                 if (m_loadedPlugins) {
                                     m_loadedPlugins->refresh();
                                 }
                             }
                         });
    }
#endif

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

    // Phase 34-04 (APROF-02 / APROF-04): start the foreground-window watcher and
    // wire its debounced onChange callback to the per-app profile auto-switch.
    //
    // The watcher (m_activeWindowWatcher) is the real per-OS backend (Wayland
    // wlr-foreign-toplevel / X11 EWMH / Win32 / macOS) on a supported desktop, or
    // the recording stub elsewhere; its onChange already arrives debounced
    // (ActiveWindowDebouncer, ~180ms trailing-edge). Started here so the Qt event
    // loop is running (Pitfall 5: never at static-init time).
    //
    // onChange flow (APROF-02): resolve the foreground appId against the active
    // device's profiles via ProfileController::resolveProfileForApp (case-
    // insensitive applicationHints match + default-profile fallback), then —
    // behind an IDEMPOTENT GUARD (T-34-04-01 DoS mitigation, CR WR-01 pattern:
    // skip when the resolved profile is already active so a focus thrash cannot
    // repaint/switch the device) — activate it via loadProfileById. Activation
    // drives the EXISTING profileChanged -> populateContextsForActivePage
    // reconcile (wired above): willDisappear(outgoing)+willAppear(incoming). No
    // new lifecycle path is introduced here (RESEARCH Pattern 3 — reuse, never
    // re-implement willAppear/willDisappear).
    if (m_activeWindowWatcher) {
        // APROF-03: surface the active backend's foreground capability to QML so
        // the Wayland/GNOME capability-warning chip (SettingsPage.qml) shows when
        // the desktop has no foreground-window API. Injected through the
        // ProfileController seam (no raw watcher pointer in QML). The Wayland
        // backend may bind its global slightly after start(); the value here is
        // the post-start capability, and the debug channel can force the absent
        // path via setForegroundCapabilityAvailable() for headless verification.
        if (m_profileController) {
            m_profileController->setForegroundCapabilityAvailable(
                m_activeWindowWatcher->capabilityAvailable());
        }
        m_activeWindowWatcher->start([this](core::ActiveWindowInfo info) {
            QString const appId = QString::fromStdString(info.appId);

            // APROF-04 lifecycle fan-out: a foreground CHANGE means the previous
            // app lost focus (best-effort applicationDidTerminate) and the new app
            // gained it (applicationDidLaunch). Delivered to REGISTERED plugins
            // only (V4 / T-34-04-02 — never broadcast) with a length-bounded
            // payload (V5). Skipped when the app id did not actually change.
#ifdef AJAZZ_HAVE_WEBSOCKETS
            if (appId != m_lastForegroundApp) {
                if (!m_lastForegroundApp.isEmpty()) {
                    dispatchApplicationTerminate(m_lastForegroundApp);
                }
                if (!appId.isEmpty()) {
                    dispatchApplicationLaunch(appId);
                }
                m_lastForegroundApp = appId;
            }
#endif

            // APROF-02 auto-switch. Scope resolution to the active device so we
            // never switch to another device's profile (m_activeDeviceId carries
            // the device codename; empty until a device connects).
            QString const deviceCodename =
                m_pluginBridge ? m_pluginBridge->activeDeviceId() : QString{};
            // WR-01: allowDefaultFallback=false on the focus-driven auto-switch
            // path. An unmapped foreground app resolves to "" (no-op) instead of
            // force-switching to the device default, so a manual profile choice
            // survives a focus change to an unmapped app. The LOCKED default-
            // profile fallback (34-CONTEXT) is preserved as the resolver's
            // opt-in default for callers that want device-default semantics.
            QString const resolved = m_profileController->resolveProfileForApp(
                appId, deviceCodename, /*allowDefaultFallback=*/false);

            // Idempotent guard (T-34-04-01): no-op when there is nothing to switch
            // to or the resolved profile is already active. loadProfileById would
            // otherwise re-emit profileChanged and trigger a redundant reconcile.
            if (resolved.isEmpty() || resolved == m_profileController->activeProfileId()) {
                return;
            }
            AJAZZ_LOG_INFO("app",
                           "auto-switch: foreground '{}' -> profile '{}'",
                           appId.toStdString(),
                           resolved.toStdString());
            m_profileController->loadProfileById(resolved);
        });
    }

    // USB hot-plug: callback runs on a background thread; marshal to the GUI
    // thread before touching the QAbstractListModel.
    m_hotplug->setCallback([this](core::HotplugEvent const& ev) { onHotplug(ev); });
    if (!m_hotplug->start()) {
        AJAZZ_LOG_INFO("app", "hot-plug monitor unavailable on this platform/session");
    }

    // Opt-in debug control channel. Off unless AJAZZ_DEBUG_CONTROL is set;
    // when on, it binds an owner-only Unix domain socket under XDG_RUNTIME_DIR
    // and exposes the log/state/control surface to the out-of-process
    // `scripts/ajazz-debug` client. Started last so every subsystem it talks
    // to already exists.
    if (DebugControlServer::enabledFromEnv()) {
        m_debugControl = std::make_unique<DebugControlServer>(this);
        registerDebugControlMethods(*m_debugControl, *this);
        registerQmlControlMethods(*m_debugControl, engine);
        if (!m_debugControl->start(DebugControlServer::defaultSocketPath())) {
            AJAZZ_LOG_WARN("app", "debug control channel requested but failed to start");
            m_debugControl.reset();
        }
    }
}

#ifdef AJAZZ_HAVE_WEBSOCKETS
void Application::dispatchSystemWake() {
    // EVENT-03: synthetic/OS wake -> systemDidWakeUp to registered plugins only.
    // Delegates to the unit-tested fan-out helper (registered-only, V4).
    if (m_pluginBridge == nullptr) {
        return;
    }
    dispatchSystemWakeTo(m_pluginServer.get(), m_pluginBridge->registeredPlugins());
}

void Application::dispatchApplicationLaunch(QString const& appId) {
    // APROF-04: applicationDidLaunch to REGISTERED plugins only (V4 /
    // T-34-04-02 — never broadcast); length-bounded payload (V5). WR-02: gate the
    // per-plugin fan-out on each plugin's manifest ApplicationsToMonitor list so a
    // plugin that monitors only "obs" is not spammed with every focus change.
    if (m_pluginBridge == nullptr) {
        return;
    }
    dispatchApplicationLaunchTo(
        m_pluginServer.get(), m_pluginBridge->registeredPlugins(), appId, appMonitorFilter());
}

void Application::dispatchApplicationTerminate(QString const& appId) {
    if (m_pluginBridge == nullptr) {
        return;
    }
    dispatchApplicationTerminateTo(
        m_pluginServer.get(), m_pluginBridge->registeredPlugins(), appId, appMonitorFilter());
}

ajazz::app::PluginAppMonitorFilter Application::appMonitorFilter() const {
    // WR-02: a plugin receives applicationDidLaunch/Terminate only when its
    // ApplicationsToMonitor list covers the app (empty list = monitor all). When
    // there is no PluginManager (e.g. WS-disabled builds reach here only via the
    // guarded callers), fall back to an empty filter (deliver to all registered).
    if (m_pluginManager == nullptr) {
        return {};
    }
    PluginManager* mgr = m_pluginManager.get();
    return [mgr](QString const& uuid, QString const& appId) {
        return mgr->monitorsApplication(uuid, appId);
    };
}
#endif // AJAZZ_HAVE_WEBSOCKETS

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
                            // Let the control service open the device and light the panel.
                            //    setActiveDevice() emits deviceActivated(codename) on success,
                            //    which Application wires (in the constructor body above) to:
                            //      - StreamDockInputService::setActiveDeviceCodename
                            //      - StreamDockInputService::setActiveDevice(handle)  (sizes the
                            //        input service + starts the poll pump; shared flyweight handle)
                            //      - PluginDeviceBridge::onDeviceConnected
                            //    No explicit input-service call needed here anymore: the handle
                            //    share moved onto the shared deviceActivated channel so coldplug
                            //    (startup-present) devices are sized identically (see constructor).
                            Q_UNUSED(devId)
                            m_streamDockControl->setActiveDevice(codename);
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
                core::DeviceId const devId{
                    .vendorId = d.vendorId, .productId = d.productId, .serial = {}};
                QMetaObject::invokeMethod(
                    m_streamDockInput.get(),
                    [this, codename = QString::fromStdString(d.codename), devId] {
                        m_streamDockInput->setActiveDevice(nullptr);
                        m_streamDockInput->setActiveDeviceCodename({});
                        // VERIFY-OP-2: evict the flyweight cache slot so the post-replug
                        // open() (driven by the Arrived path's setActiveDevice) builds a
                        // FRESH backend on the new /dev/hidrawN node instead of returning
                        // the cached one bound to the now-dead node. Runs on the GUI thread
                        // (this lambda) so the stale close() serialises with the keep-alive
                        // / poll / drain QTimers rather than racing them.
                        m_deviceRegistry.invalidateOpenDevice(devId);
#ifdef AJAZZ_HAVE_WEBSOCKETS
                        // Phase 19-03: notify the bridge so it retires contexts
                        // and sends deviceDidDisconnect to registered plugins.
                        m_pluginBridge->onDeviceDisconnected(codename);
#endif
                    },
                    Qt::QueuedConnection);
                break;
            }
        }
    }
}

} // namespace ajazz::app
