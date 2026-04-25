// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_bridge.cpp
 * @brief Implementation of @ref ajazz::app::PIBridge.
 *
 * M3 milestone — every Q_INVOKABLE logs the call and returns. Subsequent
 * milestones connect the methods to real host services:
 *
 *   * @c setSettings / @c getSettings → on-disk JSON in
 *     @c QStandardPaths::AppDataLocation/plugins/<uuid>/settings/<context>.json
 *     (lands in M4).
 *
 *   * @c sendToPlugin / @c sendToPropertyInspector → JSON frames over the
 *     plugin-host WebSocket connection (lands in M5).
 *
 *   * @c openUrl → @c QDesktopServices::openUrl after validating against
 *     the host-side allowlist (security pass alongside M5).
 *
 * Until those land, this is a working stub: the bridge appears on the JS
 * side as @c \$SD with the full method surface, plugin authors can target
 * it during development, and host-side log lines confirm round-trip.
 */
#include "pi_bridge.hpp"

#include "ajazz/core/logger.hpp"
#include "property_inspector_controller.hpp"

#include <utility>

namespace ajazz::app {

PIBridge::PIBridge(PropertyInspectorController* controller,
                   QString pluginUuid,
                   QString actionUuid,
                   QString contextUuid,
                   QObject* parent)
    : QObject(parent), controller_(controller), pluginUuid_(std::move(pluginUuid)),
      actionUuid_(std::move(actionUuid)), contextUuid_(std::move(contextUuid)) {}

PIBridge::~PIBridge() = default;

void PIBridge::setSettings(QString const& json) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "setSettings: plugin={} action={} context={} payload-bytes={}",
                   pluginUuid_.toStdString(),
                   actionUuid_.toStdString(),
                   contextUuid_.toStdString(),
                   static_cast<int>(json.size()));
    // M4 will write `json` to AppDataLocation/plugins/<plugin>/settings/<context>.json.
    (void)controller_; // referenced once M4 wires the persistence layer through.
}

void PIBridge::getSettings() {
    AJAZZ_LOG_INFO("pi-bridge",
                   "getSettings: plugin={} context={}",
                   pluginUuid_.toStdString(),
                   contextUuid_.toStdString());
    // M4 reads from disk; M3 returns an empty document so PI pages that
    // call $SD.getSettings() at startup don't hang waiting for a reply.
    emit didReceiveSettings(QStringLiteral("{}"));
}

void PIBridge::setGlobalSettings(QString const& json) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "setGlobalSettings: plugin={} payload-bytes={}",
                   pluginUuid_.toStdString(),
                   static_cast<int>(json.size()));
}

void PIBridge::getGlobalSettings() {
    AJAZZ_LOG_INFO("pi-bridge", "getGlobalSettings: plugin={}", pluginUuid_.toStdString());
    emit didReceiveGlobalSettings(QStringLiteral("{}"));
}

void PIBridge::sendToPlugin(QString const& json) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "sendToPlugin: plugin={} action={} payload-bytes={}",
                   pluginUuid_.toStdString(),
                   actionUuid_.toStdString(),
                   static_cast<int>(json.size()));
    // M5 routes to the plugin process via PluginHostController.
}

void PIBridge::setTitle(QString const& title, QString const& context, int target) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "setTitle: context='{}' target={} title='{}'",
                   context.toStdString(),
                   target,
                   title.toStdString());
}

void PIBridge::setImage(QString const& imageData, QString const& context, int target) {
    AJAZZ_LOG_INFO("pi-bridge",
                   "setImage: context='{}' target={} payload-bytes={}",
                   context.toStdString(),
                   target,
                   static_cast<int>(imageData.size()));
}

void PIBridge::openUrl(QString const& url) {
    AJAZZ_LOG_INFO("pi-bridge", "openUrl (deferred to security pass): {}", url.toStdString());
    // Deliberately not invoking QDesktopServices::openUrl yet — landing it
    // unguarded would let any plugin shell out to the user's browser
    // unprompted. The security pass alongside M5 adds a host-side
    // allowlist check + a per-plugin confirmation prompt the first time
    // a PI calls openUrl().
}

void PIBridge::logMessage(QString const& message) {
    AJAZZ_LOG_INFO("pi-bridge[{}]", "{}", contextUuid_.toStdString(), message.toStdString());
}

} // namespace ajazz::app
