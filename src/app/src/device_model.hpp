// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device_model.hpp
 * @brief QAbstractListModel exposing registered device descriptors to QML.
 *
 * DeviceModel wraps the flat list returned by DeviceRegistry::enumerate()
 * in a Qt list model so QML ListView and Repeater components can display
 * connected and previously-seen devices without polling C++ directly.
 *
 * @see DeviceRegistry, Application
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QVariantMap>

#include <vector>

namespace ajazz::app {

/**
 * @class DeviceModel
 * @brief List model of all registered device descriptors.
 *
 * Exposed to QML as the `deviceModel` context property. QML delegates
 * access device attributes via the role names defined by roleNames().
 *
 * Call refresh() (also invokable from QML) to re-read the registry after
 * a device is plugged or unplugged.
 *
 * @note Not thread-safe; must be used on the Qt main thread.
 */
class DeviceModel : public QAbstractListModel {
    Q_OBJECT
public:
    /// Custom data roles available to QML delegates.
    enum Roles {
        ModelRole = Qt::UserRole + 1, ///< Human-readable model name ("AJAZZ AKP153").
        CodenameRole,                 ///< Short backend codename ("akp153").
        FamilyRole,                   ///< DeviceFamily integer (StreamDeck, Keyboard, Mouse).
        VidRole,                      ///< USB Vendor ID as integer.
        PidRole,                      ///< USB Product ID as integer.
        ConnectedRole,                ///< True when the device is currently plugged in.
        KeyCountRole,                 ///< Number of LCD/macro keys exposed by the device.
        GridColumnsRole,              ///< Preferred grid column count for the key matrix.
        EncoderCountRole,             ///< Number of rotary encoders.
        DpiStageCountRole,            ///< Number of DPI stages (mice).
        HasRgbRole,                   ///< True when the device exposes RGB lighting.
        HasTouchStripRole,            ///< True when the device exposes a touch strip.
    };

    explicit DeviceModel(QObject* parent = nullptr);

    /**
     * @brief Return the number of rows (registered devices).
     * @param parent Must be invalid; returns 0 for valid parent indices.
     * @return Row count, or 0 if parent is a tree node.
     */
    [[nodiscard]] int rowCount(QModelIndex const& parent = {}) const override;

    /**
     * @brief Return the data for a given index and role.
     * @param index Must be a valid flat index (row < rowCount()).
     * @param role  One of the Roles enum values.
     * @return QVariant wrapping the requested field, or an empty QVariant.
     */
    [[nodiscard]] QVariant data(QModelIndex const& index, int role) const override;

    /**
     * @brief Map role integers to their QML property names.
     * @return Hash from role value to byte-array name used in QML delegates.
     */
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief Re-enumerate the registry and reset the model.
     * @invokable Available from QML as `deviceModel.refresh()`.
     */
    Q_INVOKABLE void refresh();

    /**
     * @brief Look up the descriptor for a codename and return its fields as a
     *        QVariantMap so QML can data-drive grid sizes and tab visibility
     *        without hard-coded constants.
     * @param codename Backend codename (e.g. "akp153", "akp03").
     * @return Map with keys: model, codename, family, keyCount, gridColumns,
     *         encoderCount, dpiStageCount, hasRgb, hasTouchStrip. Empty when
     *         the codename is not registered.
     * @invokable Callable from QML as `deviceModel.capabilitiesFor(codename)`.
     */
    [[nodiscard]] Q_INVOKABLE QVariantMap capabilitiesFor(QString const& codename) const;

private:
    std::vector<core::DeviceDescriptor> m_rows; ///< Snapshot of registered descriptors.
};

} // namespace ajazz::app
