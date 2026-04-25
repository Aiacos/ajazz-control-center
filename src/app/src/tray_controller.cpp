// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file tray_controller.cpp
 * @brief Implementation of @ref ajazz::app::TrayController.
 */
#include "tray_controller.hpp"

#include "app_icon.hpp"
#include "branding_service.hpp"
#include "profile_controller.hpp"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWindow>

#include <algorithm>

namespace ajazz::app {

TrayController::TrayController(BrandingService* branding,
                               ProfileController* profiles,
                               QObject* parent)
    : QObject(parent), branding_(branding), profiles_(profiles) {
    QSettings settings;
    // Default: show the main window on launch. Starting minimized-to-tray by
    // default makes the app invisible on desktops without a working tray
    // (e.g. GNOME/Wayland without the AppIndicator extension). Power users
    // who actually want the tray-only behavior can flip this in Settings →
    // Startup; the autostart hook also passes --minimized explicitly so login
    // launches still hide the window when that's enabled.
    startMinimized_ = settings.value("Window/StartMinimized", false).toBool();
}

TrayController::~TrayController() = default;

bool TrayController::trayAvailable() const noexcept {
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::setStartMinimized(bool v) {
    if (startMinimized_ == v) {
        return;
    }
    startMinimized_ = v;
    QSettings().setValue("Window/StartMinimized", v);
    emit startMinimizedChanged();
}

void TrayController::ensureTray(QQmlApplicationEngine* engine) {
    if (tray_) {
        return;
    }
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return; // Headless / minimal desktop — nothing to attach to.
    }

    tray_ = new QSystemTrayIcon(this);
    // Build the tray QIcon. Two halves matter for Wayland status bars (niri +
    // Quickshell, KDE Plasma, Sway, etc.):
    //   1. IconName — most SNI tray hosts honor only `IconName` from
    //      org.kde.StatusNotifierItem, looking it up in the user's icon
    //      theme; raw `IconPixmap` data is silently dropped. We construct
    //      the icon via QIcon::fromTheme("ajazz-control-center") so the SNI
    //      property is populated; the system-installed
    //      /usr/share/icons/hicolor/.../ajazz-control-center.svg is what the
    //      host actually paints.
    //   2. IconPixmap fallback — for hosts that DO honor pixmap data (some
    //      X11 trays, GNOME Shell with the AppIndicator extension), we
    //      rasterize the same SVG used as the README hero at the standard
    //      Qt sizes (16/22/24/32/48/64/128/256/512). Embedded in the QIcon
    //      so even hosts without our installed theme entry render something.
    QIcon const fallback =
        makeAppIcon(QStringLiteral(":/qt/qml/AjazzControlCenter/branding/app.svg"),
                    QStringLiteral(":/qt/qml/AjazzControlCenter/icons/app.svg"));
    tray_->setIcon(QIcon::fromTheme(QStringLiteral("ajazz-control-center"), fallback));
    tray_->setToolTip(branding_ ? branding_->productName()
                                : QStringLiteral("AJAZZ Control Center"));
    buildMenu();
    tray_->show();

    // Left-click on the tray icon toggles the main window's visibility — a
    // very common UX shorthand. We resolve the QML-owned window lazily here,
    // not in the ctor, because the engine has not yet loaded the root
    // component when the controller is constructed.
    connect(tray_,
            &QSystemTrayIcon::activated,
            this,
            [this, engine](QSystemTrayIcon::ActivationReason reason) {
                if (reason != QSystemTrayIcon::Trigger) {
                    return;
                }
                if (!engine || engine->rootObjects().isEmpty()) {
                    return;
                }
                auto* root = qobject_cast<QWindow*>(engine->rootObjects().first());
                if (!root) {
                    return;
                }
                if (root->isVisible()) {
                    root->hide();
                } else {
                    emit showWindowRequested();
                    root->show();
                    root->raise();
                    root->requestActivate();
                }
            });
}

void TrayController::setDeviceBattery(QString const& deviceName, int percent) {
    if (!tray_) {
        return; // Tray hasn't been created yet (headless session).
    }
    auto const clamped = std::clamp(percent, 0, 100);
    QString const prefix = clamped < 20 ? tr("⚠ Low battery") : tr("Battery");
    batteryTooltip_ =
        QStringLiteral("%1 %2: %3%").arg(prefix, deviceName, QString::number(clamped));
    auto const baseTip =
        branding_ ? branding_->productName() : QStringLiteral("AJAZZ Control Center");
    tray_->setToolTip(baseTip + QStringLiteral("\n") + batteryTooltip_);
    if (clamped < 20) {
        tray_->showMessage(tr("Low battery"),
                           tr("%1 is at %2%").arg(deviceName, QString::number(clamped)),
                           QSystemTrayIcon::Warning,
                           5000);
    }
}

void TrayController::setPaused(bool paused) {
    if (paused_ == paused) {
        return;
    }
    paused_ = paused;
    if (pauseAction_) {
        pauseAction_->setText(paused_ ? tr("Resume") : tr("Pause"));
    }
    emit pausedChanged(paused_);
}

void TrayController::buildMenu() {
    menu_ = new QMenu();

    // Show / hide the main window.
    auto* show = menu_->addAction(tr("Show window"));
    connect(show, &QAction::triggered, this, &TrayController::showWindowRequested);

    // Pause / Resume — flips state and surfaces it to subscribers (#24, F-33).
    pauseAction_ = menu_->addAction(paused_ ? tr("Resume") : tr("Pause"));
    connect(pauseAction_, &QAction::triggered, this, [this]() { setPaused(!paused_); });

    // Switch profile submenu — populated lazily from ProfileController.
    profileMenu_ = menu_->addMenu(tr("Switch profile"));
    profileMenu_->setEnabled(profiles_ != nullptr);
    rebuildProfileSubmenu();
    if (profiles_) {
        // ProfileController exposes profilesChanged() whenever the underlying
        // model is reloaded; refresh the submenu in lock-step. Functor-style
        // connection avoids the runtime SLOT() string lookup and is safer
        // against typos at compile time.
        connect(profiles_, &ProfileController::profilesChanged, this, [this]() {
            rebuildProfileSubmenu();
        });
    }

    menu_->addSeparator();

    auto* quit = menu_->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, &TrayController::quitRequested);

    tray_->setContextMenu(menu_);
}

void TrayController::rebuildProfileSubmenu() {
    if (!profileMenu_) {
        return;
    }
    profileMenu_->clear();
    if (!profiles_) {
        return;
    }
    auto const ids = profiles_->knownProfileIds();
    if (ids.empty()) {
        auto* empty = profileMenu_->addAction(tr("(no profiles)"));
        empty->setEnabled(false);
        return;
    }
    for (auto const& id : ids) {
        auto const label = profiles_->profileNameFor(id);
        auto* act = profileMenu_->addAction(label.isEmpty() ? id : label);
        connect(act, &QAction::triggered, this, [this, id]() { emit profileSwitchRequested(id); });
    }
}

} // namespace ajazz::app
