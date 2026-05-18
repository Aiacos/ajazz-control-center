// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file lighting_service.cpp
 * @brief Implementation of @ref ajazz::app::LightingService.
 */
#include "lighting_service.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/logger.hpp"

#include <QQmlEngine>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace ajazz::app {

namespace {

LightingService* g_instance = nullptr;

/// Resolve the codename to an IFirmwareLightingCapable* or nullptr.
/// Caller owns the returned shared_ptr lifetime; pin it in a local for
/// the duration of any call into the cap interface (A-04 / D-01).
[[nodiscard]] core::IFirmwareLightingCapable*
resolveCap(LightingService::DeviceLookup const& lookup, QString const& codename,
           std::shared_ptr<core::IDevice>& keepAlive) {
    if (!lookup) {
        return nullptr;
    }
    keepAlive = lookup(codename);
    if (!keepAlive) {
        return nullptr;
    }
    return dynamic_cast<core::IFirmwareLightingCapable*>(keepAlive.get());
}

} // namespace

LightingService::LightingService(DeviceLookup lookup, QObject* parent)
    : QObject(parent), m_lookup(std::move(lookup)) {}

LightingService::~LightingService() = default;

LightingService* LightingService::create(QQmlEngine*, QJSEngine*) {
    if (g_instance != nullptr) {
        QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    }
    return g_instance;
}

void LightingService::registerInstance(LightingService* instance) noexcept {
    g_instance = instance;
}

QVariantList LightingService::modesFor(QString const& codename) const {
    std::shared_ptr<core::IDevice> keepAlive;
    auto* const cap = resolveCap(m_lookup, codename, keepAlive);
    if (cap == nullptr) {
        return {};
    }
    QVariantList out;
    for (auto const& m : cap->availableFirmwareModes()) {
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), static_cast<int>(m.id));
        entry.insert(QStringLiteral("name"), QString::fromStdString(m.name));
        out.append(entry);
    }
    return out;
}

bool LightingService::setMode(QString const& codename, int modeId, int brightness, int speed) {
    std::shared_ptr<core::IDevice> keepAlive;
    auto* const cap = resolveCap(m_lookup, codename, keepAlive);
    if (cap == nullptr) {
        AJAZZ_LOG_WARN("lighting-service",
                       "setMode: device '{}' not connected or not IFirmwareLightingCapable",
                       codename.toStdString());
        return false;
    }
    auto const safeMode = static_cast<std::uint8_t>(std::clamp(modeId, 0, 255));
    auto const safeBrightness = static_cast<std::uint8_t>(
        std::clamp(brightness, 0, static_cast<int>(cap->brightnessMax())));
    auto const safeSpeed =
        static_cast<std::uint8_t>(std::clamp(speed, 0, static_cast<int>(cap->speedMax())));
    return cap->setFirmwareLightingMode(safeMode, safeBrightness, safeSpeed);
}

int LightingService::brightnessMaxFor(QString const& codename) const {
    std::shared_ptr<core::IDevice> keepAlive;
    auto* const cap = resolveCap(m_lookup, codename, keepAlive);
    return cap ? static_cast<int>(cap->brightnessMax()) : 0;
}

int LightingService::speedMaxFor(QString const& codename) const {
    std::shared_ptr<core::IDevice> keepAlive;
    auto* const cap = resolveCap(m_lookup, codename, keepAlive);
    return cap ? static_cast<int>(cap->speedMax()) : 0;
}

} // namespace ajazz::app
