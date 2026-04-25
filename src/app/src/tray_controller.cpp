// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file tray_controller.cpp
 * @brief Implementation of @ref ajazz::app::TrayController.
 */
#include "tray_controller.hpp"

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
    // Default: start minimized to tray. Power users can flip this to false in
    // Settings → Startup, or via QSettings directly. Boolean is persisted so
    // the next launch honors the user's choice.
    startMinimized_ = settings.value("Window/StartMinimized", true).toBool();
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
    tray_->setIcon(QIcon(branding_ ? branding_->trayIconUrl().toString() : QString()));
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
