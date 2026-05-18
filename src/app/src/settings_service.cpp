// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file settings_service.cpp
 * @brief Implementation of @ref ajazz::app::SettingsService.
 */
#include "settings_service.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/logger.hpp"

#include <QQmlEngine>

#include <algorithm>
#include <utility>

namespace ajazz::app {

namespace {

SettingsService* g_instance = nullptr;

/// Resolve the codename to an ISettingsCapable* or nullptr. Caller owns
/// the returned shared_ptr lifetime; pin it in a local for the duration
/// of any call into the cap interface (A-04 / D-01 amendment 3 — the
/// same UAF discipline as LightingService / TimeSyncService).
[[nodiscard]] core::ISettingsCapable* resolveCap(SettingsService::DeviceLookup const& lookup,
                                                 QString const& codename,
                                                 std::shared_ptr<core::IDevice>& keepAlive) {
    if (!lookup) {
        return nullptr;
    }
    keepAlive = lookup(codename);
    if (!keepAlive) {
        return nullptr;
    }
    return dynamic_cast<core::ISettingsCapable*>(keepAlive.get());
}

} // namespace

SettingsService::SettingsService(DeviceLookup lookup, QObject* parent)
    : QObject(parent), m_lookup(std::move(lookup)) {}

SettingsService::~SettingsService() = default;

SettingsService* SettingsService::create(QQmlEngine*, QJSEngine*) {
    if (g_instance != nullptr) {
        QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    }
    return g_instance;
}

void SettingsService::registerInstance(SettingsService* instance) noexcept {
    g_instance = instance;
}

std::uint8_t SettingsService::clampResponseLevel(int raw) noexcept {
    // Mirror ProprietaryKeyboard::buildSettingsBatch normalisation: 0 maps
    // to the vendor default (3), valid range is [1..5], everything above
    // clamps to 5. Pulling the clamp out of the backend lets QML preview
    // the persisted value without round-tripping through HID.
    if (raw <= 0) {
        return static_cast<std::uint8_t>(3);
    }
    if (raw >= 5) {
        return static_cast<std::uint8_t>(5);
    }
    return static_cast<std::uint8_t>(raw);
}

bool SettingsService::setSettings(QString const& codename,
                                  int fnSwitch,
                                  int sleepMinutes,
                                  int responseLevel) {
    std::shared_ptr<core::IDevice> keepAlive;
    auto* const cap = resolveCap(m_lookup, codename, keepAlive);
    if (cap == nullptr) {
        AJAZZ_LOG_WARN("settings-service",
                       "setSettings: device '{}' not connected or not ISettingsCapable",
                       codename.toStdString());
        emit settingsFailed(codename,
                            tr("Device is not connected or does not support settings batch"));
        return false;
    }
    core::KeyboardSettings batch{};
    batch.fnLayerSwitch = static_cast<std::uint8_t>(std::clamp(fnSwitch, 0, 1));
    batch.sleepTimerMinutes = static_cast<std::uint8_t>(std::clamp(sleepMinutes, 0, 255));
    batch.keyResponseTimeLevel = clampResponseLevel(responseLevel);
    bool const ok = cap->setKeyboardSettings(batch);
    if (!ok) {
        emit settingsFailed(codename, tr("Transport error while pushing settings"));
        return false;
    }
    emit settingsApplied(codename);
    return true;
}

QVariantMap SettingsService::currentSettings(QString const& codename) const {
    QVariantMap out;
    std::shared_ptr<core::IDevice> keepAlive;
    auto* const cap = resolveCap(m_lookup, codename, keepAlive);
    if (cap == nullptr) {
        // Vendor defaults so QML bindings stay well-typed even when the
        // device is absent (sliders/comboboxes still render). `available`
        // gates the row's visibility on the QML side.
        out.insert(QStringLiteral("available"), false);
        out.insert(QStringLiteral("fnSwitch"), 0);
        out.insert(QStringLiteral("sleepMinutes"), 0);
        out.insert(QStringLiteral("responseLevel"), 3);
        return out;
    }
    auto const cached = cap->keyboardSettings();
    out.insert(QStringLiteral("available"), true);
    out.insert(QStringLiteral("fnSwitch"), static_cast<int>(cached.fnLayerSwitch));
    out.insert(QStringLiteral("sleepMinutes"), static_cast<int>(cached.sleepTimerMinutes));
    out.insert(QStringLiteral("responseLevel"), static_cast<int>(cached.keyResponseTimeLevel));
    return out;
}

} // namespace ajazz::app
