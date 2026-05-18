// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file settings_service.hpp
 * @brief QML-facing facade over @ref ajazz::core::ISettingsCapable.
 *
 * Mirrors @ref LightingService + @ref BatteryService + @ref TimeSyncService:
 * holds a DeviceLookup callback (so unit tests can inject), surfaces the
 * current AK-series settings batch (fn-layer / sleep timer / response time)
 * per codename, and pushes new values through the @c ISettingsCapable
 * interface in one short-report envelope.
 *
 * Currently implemented by ProprietaryKeyboard (AK980 PRO) — issue #57.
 * Other devices return an empty availability map and the QML row stays
 * hidden so non-capable devices don't show empty fake-functional widgets.
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQmlIntegration>

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

class QJSEngine;
class QQmlEngine;

namespace ajazz::core {
class IDevice;
} // namespace ajazz::core

namespace ajazz::app {

/**
 * @class SettingsService
 * @brief QML singleton bridging @c ISettingsCapable to the UI.
 *
 * Exposed as `SettingsService` in QML. Pattern follows LightingService /
 * TimeSyncService: non-default-constructible (Pitfall 4 build-break lock);
 * QML factory `create()` returns the Application-owned instance registered
 * via `registerInstance()`.
 */
class SettingsService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(SettingsService)
    QML_SINGLETON

public:
    /// Codename -> shared_ptr<IDevice>; same shape as TimeSyncService /
    /// LightingService / BatteryService.
    using DeviceLookup = std::function<std::shared_ptr<core::IDevice>(QString const&)>;

    explicit SettingsService(DeviceLookup lookup, QObject* parent);
    ~SettingsService() override;

    static SettingsService* create(QQmlEngine* qml, QJSEngine* js);
    static void registerInstance(SettingsService* instance) noexcept;

    /**
     * @brief Push a new settings batch to the device.
     *
     * Resolves @p codename to an @c ISettingsCapable backend via the
     * DeviceLookup, clamps the field values to the vendor-supported
     * range (delegated to the backend), and fires the 4-packet envelope
     * (START / SETTINGS-DATA / SAVE / FINISH).
     *
     * @param codename       Device codename (matches @c DeviceDescriptor::codename).
     * @param fnSwitch       Fn-layer behaviour: 0 = hold-only, 1 = toggle.
     * @param sleepMinutes   Idle minutes before backlight sleep; 0 = never.
     *                       Vendor UI exposes 0 / 1 / 3 / 5 / 10 / 30.
     * @param responseLevel  Key-response time level in [1..5]; 0 normalises
     *                       to vendor default 3, out-of-range clamps to 5.
     * @return true when the wire packets all went out; false on transport
     *         error, missing device, or non-capable backend.
     * @invokable Callable from QML as
     *            `SettingsService.setSettings(codename, fn, sleep, resp)`.
     */
    Q_INVOKABLE bool
    setSettings(QString const& codename, int fnSwitch, int sleepMinutes, int responseLevel);

    /**
     * @brief Read the host-side cached settings for a device.
     *
     * Returns the vendor-default tuple (fn=0, sleep=0, response=3) before
     * the first push has landed, then the last pushed values for every
     * subsequent read. The QML row uses this on @c Component.onCompleted
     * to seed its initial state without a HID round-trip.
     *
     * @param codename Device codename.
     * @return QVariantMap with keys {`available`, `fnSwitch`,
     *         `sleepMinutes`, `responseLevel`}. @c available is true iff
     *         the device is connected AND implements @c ISettingsCapable;
     *         when false the numeric fields default to the vendor tuple
     *         so QML bindings stay well-typed.
     * @invokable Callable from QML as `SettingsService.currentSettings(codename)`.
     */
    [[nodiscard]] Q_INVOKABLE QVariantMap currentSettings(QString const& codename) const;

    /**
     * @brief Clamp a raw QML response-level into the wire-valid range.
     *
     * Pure helper, exposed publicly so the small unit test can pin the
     * normalisation contract without instantiating a full mock device.
     * Matches @c ProprietaryKeyboard::buildSettingsBatch semantics:
     * 0 → 3 (vendor default), [1..5] passes through, anything > 5 → 5.
     *
     * @param raw Raw level (may be 0 or out-of-range).
     * @return Wire-valid response level in [1..5].
     */
    [[nodiscard]] static std::uint8_t clampResponseLevel(int raw) noexcept;

signals:
    /// Emitted on a successful @c setSettings push. QML uses this to fire
    /// a confirmation toast and to refresh derived bindings (e.g. the
    /// initial-state read on a sibling row that watches the same codename).
    void settingsApplied(QString const& codename);

    /// Emitted on a failed @c setSettings push. @p message is human-
    /// readable and safe to surface in a toast.
    void settingsFailed(QString const& codename, QString const& message);

private:
    DeviceLookup m_lookup;
};

// Pitfall 4 build-break lock — see BrandingService.
// QML_SINGLETON + default-constructible class → Qt 6 picks Constructor
// mode and silently bypasses our static create() factory, spawning a
// duplicate QML-side instance. The non-defaulted ctor + this assertion
// together force every caller to pass a DeviceLookup AND a parent (QML
// routes through create() instead).
static_assert(
    !std::is_default_constructible_v<SettingsService>,
    "SettingsService must not be default-constructible — see ctor @note and BrandingService.");

} // namespace ajazz::app
