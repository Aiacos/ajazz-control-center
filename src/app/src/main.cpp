// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file main.cpp
 * @brief Application entry point.
 *
 * Sets Qt application metadata from the compile-time branding defines, creates
 * the Application controller, bootstraps all device backends, loads the QML
 * root component, then starts the system tray and the USB hot-plug monitor.
 * If the user has set "start minimized" (default), the QML root window is
 * hidden until the user clicks the tray icon.
 */
#include "ajazz/core/profile.hpp"
#include "ajazz/core/profile_bundle.hpp"
#include "ajazz/core/profile_io.hpp"
#include "app_icon.hpp"
#include "application.hpp"
#include "branding_service.hpp"
#include "opendeck_bridge.hpp"
#include "single_instance_guard.hpp"
#include "tray_controller.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QWindow>

#ifdef AJAZZ_HAVE_WEBENGINE
#include <QtWebEngineQuick/QtWebEngineQuick>
#ifdef AJAZZ_HAVE_WEBUI
#include "opendeck_scheme_handler.hpp"

#include <QWebEngineProfile>
#endif
#endif

#include <csignal>
#include <iostream>
#include <optional>

#ifndef AJAZZ_PRODUCT_NAME
#define AJAZZ_PRODUCT_NAME "AJAZZ Control Center"
#endif
#ifndef AJAZZ_VENDOR_NAME
#define AJAZZ_VENDOR_NAME "Aiacos"
#endif
#ifndef AJAZZ_APP_ID
#define AJAZZ_APP_ID "io.github.Aiacos.AjazzControlCenter"
#endif
#ifndef AJAZZ_APP_VERSION
#define AJAZZ_APP_VERSION "0.0.0"
#endif

int main(int argc, char* argv[]) {
#ifndef _WIN32
    // Ignore SIGPIPE process-wide. The out-of-process plugin host writes to a
    // child via a pipe; if the child exits/crashes mid-IPC, the next ::write()
    // to the closed read end raises SIGPIPE, whose default disposition kills
    // the whole GUI app *before* the write()'s EPIPE return can be handled.
    // Ignoring it lets ::write() return -1/EPIPE so the host's existing error
    // path runs instead. Windows _write() returns EPIPE without a signal.
    ::signal(SIGPIPE, SIG_IGN);
#endif
#ifdef AJAZZ_HAVE_WEBENGINE
    // Qt WebEngine requires its renderer-process initialiser to run BEFORE
    // any QGuiApplication / QApplication is constructed; otherwise the
    // out-of-process renderer fails to spawn and `WebEngineView` shows a
    // blank surface. Only compiled in when CMake found Qt6::WebEngineQuick
    // (see `AJAZZ_BUILD_PROPERTY_INSPECTOR`); minimal Qt installs and
    // headless CI builds compile this branch out and stay on the
    // schema-driven Property Inspector renderer at runtime.
#ifdef AJAZZ_HAVE_WEBUI
    // Register the custom scheme that serves the bundled OpenDeck SPA with a
    // clean web origin (opendeck://app/). MUST precede QtWebEngine init.
    ajazz::app::registerOpenDeckScheme();
#endif
    QtWebEngineQuick::initialize();
#endif
    // Use QApplication (not QGuiApplication) because TrayController relies on
    // QSystemTrayIcon + QMenu which are part of the QtWidgets module.
    QApplication::setOrganizationName(AJAZZ_VENDOR_NAME);
    QApplication::setOrganizationDomain("github.com/Aiacos/ajazz-control-center");
    QApplication::setApplicationName(AJAZZ_PRODUCT_NAME);
    QApplication::setApplicationDisplayName(AJAZZ_PRODUCT_NAME);
    // Single-source of truth: CMake's project(VERSION) via AJAZZ_APP_VERSION. A hardcoded
    // literal here shipped "0.1.0" in every release regardless of the actual tag (issue #88).
    QApplication::setApplicationVersion(QStringLiteral(AJAZZ_APP_VERSION));
    // setDesktopFileName must match the actual basename of the installed
    // .desktop file, *not* the reverse-DNS app id. Linux distros install us
    // as `share/applications/ajazz-control-center.desktop` (see
    // resources/linux/ajazz-control-center.desktop and the CMake install
    // rule), so xdg-desktop-portal and Wayland tray hosts (Quickshell, KWin)
    // can resolve the app's metadata. Passing the reverse-DNS id here makes
    // every portal call fail with `App info not found` and silently strips
    // the SNI tray icon.
    QApplication::setDesktopFileName(QStringLiteral("ajazz-control-center"));
    // Keep running when the last window is closed (tray-only mode).
    QApplication::setQuitOnLastWindowClosed(false);

    QApplication app(argc, argv);
    // Window icon shown in the taskbar, alt-tab list and X11 _NET_WM_ICON.
    // Window icon resolution mirrors the tray (see tray_controller.cpp): the
    // theme name "ajazz-control-center" tells xdg / Wayland compositors and
    // taskbars to look up the system-installed icon, while the multi-size
    // QIcon rasterized from the embedded SVG (same artwork as the README
    // hero) is the embedded fallback for hosts that draw raw pixmaps. macOS
    // picks the bundle icon from app.icns and Windows uses the .rc-embedded
    // app.ico; this call covers Linux/X11/Wayland.
    QApplication::setWindowIcon(QIcon::fromTheme(
        QStringLiteral("ajazz-control-center"),
        ajazz::app::makeAppIcon(QStringLiteral(":/qt/qml/AjazzControlCenter/branding/app.svg"),
                                QStringLiteral(":/qt/qml/AjazzControlCenter/icons/app.svg"))));

    // QtQuick Controls 2 style: "Material" gives a Material Design 3 look that
    // honors light/dark via the Material.theme attached property in Main.qml.
    // Setting the style here (before the QML engine is created) is mandatory;
    // changing it after engine.load() has no effect.
    QQuickStyle::setStyle(QStringLiteral("Material"));

    // ------------------------------------------------------------------
    // CLI flags. Closes #32 (--export-profile / --import-profile) and
    // backs the autostart hook's `--minimized` flag (#35).
    // ------------------------------------------------------------------
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("AJAZZ Control Center"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption minimizedOpt(QStringLiteral("minimized"),
                                    QStringLiteral("Start the GUI minimised to tray."));
    QCommandLineOption exportOpt(
        QStringLiteral("export-profile"),
        QStringLiteral("Export PROFILE_PATH to BUNDLE_PATH (`.ajazzprofile`) and exit."),
        QStringLiteral("profile=bundle"));
    QCommandLineOption importOpt(
        QStringLiteral("import-profile"),
        QStringLiteral("Validate the BUNDLE_PATH (`.ajazzprofile`) and exit."),
        QStringLiteral("bundle"));
    parser.addOption(minimizedOpt);
    parser.addOption(exportOpt);
    parser.addOption(importOpt);
    parser.process(app);

    if (parser.isSet(exportOpt)) {
        // Pair format: "<src.json>=<dst.ajazzprofile>".
        auto const value = parser.value(exportOpt);
        auto const eq = value.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            std::cerr << "--export-profile expects 'src.json=dst.ajazzprofile'\n";
            return 2;
        }
        auto const src = value.left(eq).toStdString();
        auto const dst = value.mid(eq + 1).toStdString();
        try {
            auto const profile = ajazz::core::readProfileFromDisk(src);
            ajazz::core::exportProfileBundle(dst, profile, AJAZZ_VENDOR_NAME);
        } catch (std::exception const& ex) {
            std::cerr << "export failed: " << ex.what() << "\n";
            return 3;
        }
        return 0;
    }
    if (parser.isSet(importOpt)) {
        try {
            auto const bundle =
                ajazz::core::importProfileBundle(parser.value(importOpt).toStdString());
            std::cout << "bundle ok: " << bundle.profile.name << "\n";
        } catch (std::exception const& ex) {
            std::cerr << "import failed: " << ex.what() << "\n";
            return 4;
        }
        return 0;
    }
    bool const forceMinimized = parser.isSet(minimizedOpt);

    // Single-instance gate. If another GUI is already running, ask it to
    // show its window and exit silently — this is what the user expects when
    // they double-click the .desktop entry, the tray icon, or the autostart
    // hook fires while a manual launch is already up.
    auto const socketName = ajazz::app::SingleInstanceGuard::defaultSocketName();
    // A streamdeck:// positional argument means the OS scheme handler spawned
    // us for a plugin deep link (didReceiveDeepLink). When a primary already
    // runs, hand the URL over instead of raising a second window.
    QString deepLinkArg;
    for (QString const& a : app.arguments().mid(1)) {
        if (a.startsWith(QStringLiteral("streamdeck://"))) {
            deepLinkArg = a;
            break;
        }
    }
    if (!deepLinkArg.isEmpty() &&
        ajazz::app::SingleInstanceGuard::forwardDeepLink(socketName, deepLinkArg)) {
        return 0;
    }
    if (deepLinkArg.isEmpty() && ajazz::app::SingleInstanceGuard::tryActivateExisting(socketName)) {
        return 0;
    }
    ajazz::app::SingleInstanceGuard instanceGuard(socketName);
    if (!instanceGuard.isPrimary()) {
        // Couldn't take ownership and couldn't connect either — degraded
        // environment (e.g. /tmp full or sandbox without abstract sockets).
        // Fall through and run; worst case the user gets two windows.
    }

    ajazz::app::Application controller;
    controller.bootstrap();

    QQmlApplicationEngine engine;
    controller.exposeToQml(engine);

    // OpenDeck UI integration: the OpenDeck Svelte SPA is embedded as the
    // STREAMDECK editor pane (qml/OpenDeckPane.qml, mounted by ProfileEditor when
    // the active device is a stream controller). The native shell (Main.qml) is
    // always the root and keeps the device sidebar + the mouse and keyboard
    // editors. We therefore expose the bridge + the bundle flag unconditionally
    // (gated only by the WebEngine/SPA compile-time availability) and serve the
    // SPA over the custom opendeck://app/ scheme, regardless of which device is
    // selected — ProfileEditor decides when to show the pane.
#ifdef AJAZZ_HAVE_WEBUI
    bool const webUiBundlePresent = true;
#else
    bool const webUiBundlePresent = false;
#endif
    engine.rootContext()->setContextProperty(QStringLiteral("AppHasWebUiBundle"),
                                             webUiBundlePresent);
#ifdef AJAZZ_HAVE_WEBENGINE
    engine.rootContext()->setContextProperty(QStringLiteral("OpenDeckBridgeObject"),
                                             controller.openDeckBridge());
#ifdef AJAZZ_HAVE_WEBUI
    // Serve the bundled SPA via the custom scheme so SvelteKit routes from a
    // clean origin and Fetch works. Handler is parented to qApp.
    QWebEngineProfile::defaultProfile()->installUrlSchemeHandler(
        QByteArray(ajazz::app::kOpenDeckScheme), new ajazz::app::OpenDeckSchemeHandler(qApp));
#endif
#endif

    engine.loadFromModule("AjazzControlCenter", QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // Start tray + hot-plug *after* QML is loaded; the tray needs a window.
    controller.startBackgroundServices(engine);

    // Helper used both at startup (when the user explicitly asked for
    // --minimized) and when a secondary launch knocks on our socket.
    auto const showAllWindows = [&engine]() {
        for (QObject* obj : engine.rootObjects()) {
            if (auto* win = qobject_cast<QWindow*>(obj)) {
                win->show();
                win->raise();
                win->requestActivate();
            }
        }
    };

    // Honor the "start minimized to tray" preference only when explicitly
    // requested via --minimized (autostart hook). Manual launches always show
    // the window — relying solely on a tray icon makes the app invisible on
    // GNOME/Wayland without an AppIndicator extension.
    auto* tray = controller.trayController();
    if (tray && forceMinimized && tray->trayAvailable()) {
        for (QObject* obj : engine.rootObjects()) {
            if (auto* win = qobject_cast<QWindow*>(obj)) {
                win->hide();
            }
        }
    }

    // Re-raise on subsequent launches (single-instance contract).
    QObject::connect(
        &instanceGuard, &ajazz::app::SingleInstanceGuard::showRequested, &app, showAllWindows);
#ifdef AJAZZ_HAVE_WEBSOCKETS
    // Deep links: forwarded by scheme-activated secondaries, or carried on our
    // own argv when we ARE the scheme-activated launch (dispatched after the
    // event loop starts so plugins have registered).
    QObject::connect(&instanceGuard,
                     &ajazz::app::SingleInstanceGuard::deepLinkRequested,
                     &controller,
                     &ajazz::app::Application::handleDeepLink);
    if (!deepLinkArg.isEmpty()) {
        QTimer::singleShot(3000, &controller, [&controller, deepLinkArg]() {
            controller.handleDeepLink(deepLinkArg);
        });
    }
#endif

    return app.exec();
}
