// SPDX-License-Identifier: GPL-3.0-or-later
/** @file mouse_service.cpp @brief Implementation of MouseService. */
#include "mouse_service.hpp"

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/logger.hpp"

#include <QQmlEngine>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace ajazz::app {
namespace {
MouseService* g_instance = nullptr;

std::shared_ptr<core::IDevice> resolve(MouseService::DeviceLookup const& lookup,
                                       QString const& codename) {
    return lookup ? lookup(codename) : nullptr;
}

QString key(QString const& codename, QString const& field) {
    return QStringLiteral("Mouse/") + codename + QLatin1Char('/') + field;
}

QVariantList defaultDpi(int count) {
    QVariantList out;
    for (int i = 0; i < count; ++i) {
        out.append(800 + i * 400);
    }
    return out;
}
} // namespace

MouseService::MouseService(DeviceLookup lookup, QObject* parent)
    : QObject(parent), m_lookup(std::move(lookup)) {}

MouseService::~MouseService() = default;

MouseService* MouseService::create(QQmlEngine*, QJSEngine*) {
    if (g_instance != nullptr) {
        QQmlEngine::setObjectOwnership(g_instance, QQmlEngine::CppOwnership);
    }
    return g_instance;
}

void MouseService::registerInstance(MouseService* instance) noexcept {
    g_instance = instance;
}

QVariantMap MouseService::current(QString const& codename) const {
    QVariantMap out;
    auto device = resolve(m_lookup, codename);
    auto* mouse = device ? dynamic_cast<core::IMouseCapable*>(device.get()) : nullptr;
    if (mouse == nullptr) {
        out.insert(QStringLiteral("available"), false);
        out.insert(QStringLiteral("dpi"), QVariantList{});
        out.insert(QStringLiteral("pollingRate"), 125);
        out.insert(QStringLiteral("liftOff"), 1.5);
        return out;
    }
    QVariantList dpi;
    for (auto const& stage : mouse->getDpiStages()) {
        dpi.append(static_cast<int>(stage.dpi));
    }
    if (dpi.isEmpty()) {
        dpi = defaultDpi(mouse->dpiStageCount());
    }
    QSettings stored;
    auto const savedDpi = stored.value(key(codename, QStringLiteral("dpi")));
    if (savedDpi.isValid()) {
        dpi = savedDpi.toList();
    }
    auto const savedRate = stored.value(key(codename, QStringLiteral("pollingRate")));
    auto const savedLift = stored.value(key(codename, QStringLiteral("liftOff")));
    out.insert(QStringLiteral("available"), true);
    out.insert(QStringLiteral("dpi"), dpi);
    out.insert(QStringLiteral("pollingRate"),
               savedRate.isValid() ? savedRate.toInt() : static_cast<int>(mouse->pollRateHz()));
    auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(device.get());
    auto const lod =
        settings ? settings->mouseSettings().liftOffDistance : core::LiftOffDistance::Mm1;
    out.insert(QStringLiteral("liftOff"),
               savedLift.isValid() ? savedLift.toDouble()
                                   : (lod == core::LiftOffDistance::Mm2 ? 2.0 : 1.0));
    return out;
}

bool MouseService::apply(QString const& codename,
                         QVariantList const& dpi,
                         int pollingRate,
                         double liftOff) {
    auto device = resolve(m_lookup, codename);
    auto* mouse = device ? dynamic_cast<core::IMouseCapable*>(device.get()) : nullptr;
    if (mouse == nullptr) {
        const auto message = tr("Device is not connected or does not support mouse settings");
        AJAZZ_LOG_WARN("mouse-service", "apply: {}", message.toStdString());
        emit failed(codename, message);
        return false;
    }
    std::vector<core::DpiStage> stages;
    stages.reserve(static_cast<std::size_t>(mouse->dpiStageCount()));
    for (int i = 0; i < dpi.size() && i < mouse->dpiStageCount(); ++i) {
        auto const value = std::clamp(dpi.at(i).toInt(), 50, 42000);
        stages.push_back(core::DpiStage{static_cast<std::uint16_t>(value), {}});
    }
    while (stages.size() < mouse->dpiStageCount()) {
        auto const index = static_cast<int>(stages.size());
        stages.push_back(core::DpiStage{static_cast<std::uint16_t>(800 + index * 400), {}});
    }
    mouse->setDpiStages(stages);
    mouse->setPollRateHz(static_cast<std::uint16_t>(std::clamp(pollingRate, 125, 8000)));
    if (auto* settings = dynamic_cast<core::IMouseSettingsCapable*>(device.get())) {
        auto currentSettings = settings->mouseSettings();
        currentSettings.liftOffDistance =
            liftOff >= 1.75 ? core::LiftOffDistance::Mm2 : core::LiftOffDistance::Mm1;
        if (!settings->setMouseSettings(currentSettings)) {
            emit failed(codename, tr("Mouse settings write failed"));
            return false;
        }
    }
    QSettings stored;
    QVariantList normalizedDpi;
    for (auto const& stage : stages) {
        normalizedDpi.append(static_cast<int>(stage.dpi));
    }
    stored.setValue(key(codename, QStringLiteral("dpi")), normalizedDpi);
    stored.setValue(key(codename, QStringLiteral("pollingRate")), pollingRate);
    stored.setValue(key(codename, QStringLiteral("liftOff")), liftOff);
    stored.sync();
    AJAZZ_LOG_INFO("mouse-service",
                   "applied codename={} dpiStages={} pollingRate={} liftOff={}",
                   codename.toStdString(),
                   stages.size(),
                   pollingRate,
                   liftOff);
    emit applied(codename);
    return true;
}
} // namespace ajazz::app
