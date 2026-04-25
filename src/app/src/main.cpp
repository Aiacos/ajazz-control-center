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
#include "single_instance_guard.hpp"
#include "tray_controller.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QWindow>

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

int main(int argc, char* argv[]) {
    // Use QApplication (not QGuiApplication) because TrayController relies on
    // QSystemTrayIcon + QMenu which are part of the QtWidgets module.
    QApplication::setOrganizationName(AJAZZ_VENDOR_NAME);
    QApplication::setOrganizationDomain("github.com/Aiacos/ajazz-control-center");
    QApplication::setApplicationName(AJAZZ_PRODUCT_NAME);
    QApplication::setApplicationDisplayName(AJAZZ_PRODUCT_NAME);
    QApplication::setApplicationVersion("0.1.0");
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
    if (ajazz::app::SingleInstanceGuard::tryActivateExisting(socketName)) {
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
    engine.loadFromModule("AjazzControlCenter", "Main");
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

    return app.exec();
}
