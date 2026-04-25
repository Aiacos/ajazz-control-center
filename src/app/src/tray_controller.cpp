// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file tray_controller.cpp
 * @brief Implementation of @ref ajazz::app::TrayController.
 */
#include "tray_controller.hpp"

#include "branding_service.hpp"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QQmlApplicationEngine>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QWindow>

namespace ajazz::app {

TrayController::TrayController(BrandingService* branding, QObject* parent)
    : QObject(parent), branding_(branding) {
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

void TrayController::buildMenu() {
    menu_ = new QMenu();

    auto* show = menu_->addAction(tr("Show window"));
    connect(show, &QAction::triggered, this, &TrayController::showWindowRequested);

    menu_->addSeparator();

    auto* quit = menu_->addAction(tr("Quit"));
    connect(quit, &QAction::triggered, this, &TrayController::quitRequested);

    tray_->setContextMenu(menu_);
}

} // namespace ajazz::app
