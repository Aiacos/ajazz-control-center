// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file property_inspector_controller.cpp
 * @brief Implementation of @ref ajazz::app::PropertyInspectorController.
 *
 * This is the M1 stub: API surface only, every public slot is a no-op. The
 * point of M1 is to introduce the controller into the build (CMake gating,
 * @c Application wiring, QML context property) so the integration plumbing
 * is in place before M2 wires up Qt WebEngine and an actual PI page load.
 */
#include "property_inspector_controller.hpp"

#include "ajazz/core/logger.hpp"

namespace ajazz::app {

PropertyInspectorController::PropertyInspectorController(QObject* parent) : QObject(parent) {}

PropertyInspectorController::~PropertyInspectorController() = default;

bool PropertyInspectorController::webEngineAvailable() const noexcept {
#if defined(AJAZZ_HAVE_WEBENGINE)
    return true;
#else
    return false;
#endif
}

void PropertyInspectorController::loadInspector(QString const& pluginUuid,
                                                QString const& htmlAbsPath,
                                                QString const& actionUuid,
                                                QString const& contextUuid) {
    // M1: log the request and stay in the native-inspector path. M2 will
    // create the QWebEngineProfile + QWebEnginePage and load the HTML.
    AJAZZ_LOG_INFO("property-inspector",
                   "loadInspector requested (M1 stub): plugin={} action={} context={} html={}",
                   pluginUuid.toStdString(),
                   actionUuid.toStdString(),
                   contextUuid.toStdString(),
                   htmlAbsPath.toStdString());
    if (hasHtmlInspector_) {
        hasHtmlInspector_ = false;
        emit hasHtmlInspectorChanged();
    }
}

void PropertyInspectorController::closeInspector() {
    if (!hasHtmlInspector_) {
        return;
    }
    hasHtmlInspector_ = false;
    emit hasHtmlInspectorChanged();
}

} // namespace ajazz::app
