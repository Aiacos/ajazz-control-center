// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file lighting_service.hpp
 * @brief QML-facing facade over @ref ajazz::core::IFirmwareLightingCapable.
 *
 * Mirrors @ref TimeSyncService + @ref BatteryService: holds a
 * DeviceLookup callback (so unit tests can inject), surfaces the
 * available firmware modes per codename, and pushes mode + brightness
 * + speed changes through the IFirmwareLightingCapable interface.
 *
 * Only devices that ship a fixed-effect firmware catalogue implement
 * the capability (AK980 PRO 20 modes via opcode 0x13 as of
 * 2026-05-18). Others return an empty mode list and the QML picker
 * falls back to the generic RgbPicker.
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration>

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
 * @class LightingService
 * @brief QML singleton bridging @c IFirmwareLightingCapable to the UI.
 *
 * Exposed as `LightingService` in QML. Pattern follows BrandingService:
 * non-default-constructible (Pitfall 4 build-break lock); QML factory
 * `create()` returns the Application-owned instance registered via
 * `registerInstance()`.
 */
class LightingService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LightingService)
    QML_SINGLETON

public:
    /// Codename -> shared_ptr<IDevice>; same shape as TimeSyncService.
    using DeviceLookup = std::function<std::shared_ptr<core::IDevice>(QString const&)>;

    explicit LightingService(DeviceLookup lookup, QObject* parent);
    ~LightingService() override;

    static LightingService* create(QQmlEngine* qml, QJSEngine* js);
    static void registerInstance(LightingService* instance) noexcept;

    /**
     * @brief Enumerate the firmware-built-in lighting modes for a device.
     *
     * @param codename Device codename to inspect.
     * @return QVariantList of `{"id": int, "name": QString}` maps.
     *         Empty when the device is offline OR does not implement
     *         @c IFirmwareLightingCapable.
     * @invokable Callable from QML as `LightingService.modesFor(codename)`.
     */
    [[nodiscard]] Q_INVOKABLE QVariantList modesFor(QString const& codename) const;

    /**
     * @brief Activate a firmware-built-in lighting mode.
     *
     * @param codename   Device codename.
     * @param modeId     Mode id from @ref modesFor() (e.g. 0..19 on AK980 PRO).
     * @param brightness 0..@ref brightnessMaxFor(codename); clamped.
     * @param speed      0..@ref speedMaxFor(codename); clamped.
     * @return true on success; false if the device is offline / not
     *         IFirmwareLightingCapable / HID write failed.
     * @invokable Callable from QML.
     */
    Q_INVOKABLE bool setMode(QString const& codename, int modeId, int brightness, int speed);

    /// @return Brightness ceiling for this device (typically 5).
    [[nodiscard]] Q_INVOKABLE int brightnessMaxFor(QString const& codename) const;
    /// @return Speed ceiling for this device (typically 5).
    [[nodiscard]] Q_INVOKABLE int speedMaxFor(QString const& codename) const;

private:
    DeviceLookup m_lookup;
};

// Pitfall 4 build-break lock — see BrandingService.
static_assert(
    !std::is_default_constructible_v<LightingService>,
    "LightingService must not be default-constructible — see ctor @note and BrandingService.");

} // namespace ajazz::app
