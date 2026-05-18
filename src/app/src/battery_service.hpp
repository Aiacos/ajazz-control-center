// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file battery_service.hpp
 * @brief Application-layer service that queries battery level on AJAZZ devices
 *        advertising @ref ajazz::core::IBatteryCapable.
 *
 * Mirrors the @ref TimeSyncService design: owns no devices, locates them via
 * a caller-supplied @c DeviceLookup function (production wires this to
 * @c DeviceRegistry::open + @c IBatteryCapable dynamic_cast; unit tests inject
 * mocks). Two entry points:
 *
 *   * @ref queryBattery(codename) — manual one-shot query, invoked by the QML
 *     "Refresh battery" button (or by the implicit poll timer if enabled).
 *   * @ref pollAll() — iterate every registered device codename + query the
 *     ones that advertise @ref ajazz::core::Capability::Battery. Called by
 *     the optional 15-second poll timer (matches vendor cadence per
 *     @c ak980pro_vendor.md §3 — @c FUN_004358c0).
 *
 * Persistent: @c pollEnabled flag is mirrored into
 * @c QSettings("Battery/PollEnabled") on every setter call. Default: enabled
 * (matches user expectation that wireless devices have a battery indicator
 * that "just works").
 *
 * @note Thread-affine: lives on the Qt main thread. Polled queries are
 *       dispatched onto the device's I/O thread by the underlying transport.
 *
 * @see TimeSyncService (sister singleton with identical lookup contract).
 * @see IBatteryCapable (capability mix-in).
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration>

#include <QHash>

#include <chrono>
#include <functional>
#include <memory>
#include <type_traits>

class QJSEngine;
class QQmlEngine;
class QTimer;

namespace ajazz::core {
class IDevice;
} // namespace ajazz::core

namespace ajazz::app {

/**
 * @class BatteryService
 * @brief Polls and surfaces battery charge level for wireless AJAZZ devices.
 *
 * Exposed to QML as a singleton via @c qmlRegisterSingletonInstance — see
 * the @c TimeSyncService pattern + static_assert guard for the Pitfall 4
 * build-break lock.
 */
class BatteryService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(BatteryService)
    QML_SINGLETON

    /// Whether the 15-second auto-poll timer is enabled.
    /// Mirrored to @c QSettings("Battery/PollEnabled") on every change.
    Q_PROPERTY(bool pollEnabled READ pollEnabled WRITE setPollEnabled NOTIFY pollEnabledChanged)

public:
    /// Caller-supplied lookup: codename → @c shared_ptr<IDevice> or null.
    using DeviceLookup = std::function<std::shared_ptr<core::IDevice>(QString const&)>;

    /// Caller-supplied enumeration of currently-connected device codenames
    /// that advertise @c Capability::Battery. Used by @ref pollAll().
    using BatteryDeviceEnumeration = std::function<std::vector<QString>()>;

    /// No-default-ctor (QML singleton must use the static factory — Pitfall 4).
    explicit BatteryService(DeviceLookup lookup,
                            BatteryDeviceEnumeration enumerate,
                            QObject* parent);
    ~BatteryService() override;

    /// QML singleton factory.
    static BatteryService* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory.
    static void registerInstance(BatteryService* instance) noexcept;

    [[nodiscard]] bool pollEnabled() const noexcept;

    /// Set the pollEnabled flag, start/stop the timer, persist to QSettings.
    void setPollEnabled(bool enabled);

    /**
     * @brief Manual one-shot battery query for one device by codename.
     *
     * Invoked from QML by a "Refresh battery" button. Locates the device via
     * the lookup, dynamic-casts to @c IBatteryCapable (Pitfall 2: null-check
     * inline), calls @c batteryPercent(), surfaces the outcome via
     * @ref batteryQueried (charge level present) or @ref batteryUnavailable
     * (no battery / wired / I/O failure).
     *
     * @param codename Device codename (matches @c DeviceDescriptor::codename).
     */
    Q_INVOKABLE void queryBattery(QString const& codename);

    /**
     * @brief Iterate every battery-capable device codename + query it.
     *
     * Called by the 15-second auto-poll @c QTimer when @ref pollEnabled is
     * true. Per-device failures surface via the same signals as @ref queryBattery.
     */
    Q_INVOKABLE void pollAll();

    /// Test-only: number of completed queries since construction (manual + auto).
    [[nodiscard]] int totalQueryCount() const noexcept;

    /**
     * @brief Last successful battery reading for a codename, or -1 if no
     *        reading has been observed since construction (or the most
     *        recent query returned @ref batteryUnavailable).
     *
     * Lets the QML @c BatteryIndicator seed its initial state without
     * waiting for the next 15 s poll tick — when a row is mounted, the
     * indicator queries this once and renders the cached value (or
     * @c "--%" when the cache returns -1). The cache is updated on every
     * @ref batteryQueried signal and cleared on every
     * @ref batteryUnavailable signal.
     *
     * @param codename Device codename.
     * @return 0..100 percent, or -1 if unknown / unavailable.
     */
    [[nodiscard]] Q_INVOKABLE int lastKnownPercent(QString const& codename) const;

signals:
    /// Battery query succeeded; @p percent is the charge level (0..100).
    /// QML BatteryIndicator binds to this signal to refresh per-row state.
    void batteryQueried(QString const& codename, int percent);

    /// Battery query returned no value: device is wired / has no battery / I/O
    /// failure / not capable. QML BatteryIndicator hides the indicator on this.
    void batteryUnavailable(QString const& codename);

    /// Emitted when @ref pollEnabled changes (for QML Q_PROPERTY binding).
    void pollEnabledChanged(bool enabled);

private:
    /// Helper: do the dynamic_cast + batteryPercent() call + signal emission.
    void doQuery(QString const& codename);

    DeviceLookup m_lookup;
    BatteryDeviceEnumeration m_enumerate;
    QTimer* m_pollTimer{nullptr};
    bool m_pollEnabled{true};
    int m_totalQueries{0};
    /// Codename -> last successful reading (0..100). Absent / -1 means unknown.
    /// Populated on every @ref batteryQueried, cleared on every
    /// @ref batteryUnavailable. Surfaced to QML via @ref lastKnownPercent.
    QHash<QString, int> m_lastKnown;
};

// Pitfall 4 build-break: BatteryService MUST NOT be default-constructible so
// the QML engine can't accidentally pick "Constructor" mode and silently spawn
// a duplicate singleton bypassing our static factory. Mirrors TimeSyncService /
// BrandingService.
static_assert(!std::is_default_constructible_v<BatteryService>,
              "BatteryService must NOT be default-constructible — QML_SINGLETON requires "
              "qmlRegisterSingletonInstance via the static create() factory. See Pitfall 4.");

} // namespace ajazz::app
