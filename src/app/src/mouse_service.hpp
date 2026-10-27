// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file mouse_service.hpp
 * @brief QML bridge for AJ-series DPI, polling-rate and lift-off settings.
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <type_traits>

class QJSEngine;
class QQmlEngine;

namespace ajazz::app {

/**
 * @brief Pushes mouse settings to the device and persists the host cache.
 */
class MouseService final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(MouseService)
    QML_SINGLETON

public:
    using DeviceLookup = std::function<std::shared_ptr<core::IDevice>(QString const&)>;

    explicit MouseService(DeviceLookup lookup, QObject* parent);
    ~MouseService() override;

    static MouseService* create(QQmlEngine*, QJSEngine*);
    static void registerInstance(MouseService*) noexcept;

    /**
     * @brief Return cached or backend mouse values for QML controls.
     * @return Map with `dpi`, `pollingRate`, `liftOff` and `available`.
     */
    [[nodiscard]] Q_INVOKABLE QVariantMap current(QString const& codename) const;

    /**
     * @brief Push all edited values atomically from the UI batch.
     * @param codename Device codename.
     * @param dpi Ordered DPI values, one per device stage.
     * @param pollingRate Polling rate in Hz.
     * @param liftOff Lift-off distance in millimetres (1.0, 1.5 or 2.0).
     * @return True when every supported write succeeds.
     */
    Q_INVOKABLE bool
    apply(QString const& codename, QVariantList const& dpi, int pollingRate, double liftOff);

signals:
    /// Emitted after all requested writes and persistence succeed.
    void applied(QString const& codename);
    /// Emitted when a capability is missing or a HID write fails.
    void failed(QString const& codename, QString const& message);

private:
    DeviceLookup m_lookup;
};

static_assert(!std::is_default_constructible_v<MouseService>);

} // namespace ajazz::app
