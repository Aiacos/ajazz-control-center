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
#include <QtQmlIntegration>
#include <QVariantMap>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

class QJSEngine;
class QQmlEngine;

namespace ajazz::core {
class DeviceRegistry;
}

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
    QML_NAMED_ELEMENT(DeviceModel)
    QML_SINGLETON
public:
    /// QML singleton factory — see BrandingService::create for the pattern.
    static DeviceModel* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory.
    static void registerInstance(DeviceModel* instance) noexcept;

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
        HasClockRole, ///< True when the device advertises Capability::Clock (scaffolded; Phase 5).
        MaturityRole, ///< Maturity tier from devices.yaml:
                      ///< scaffolded/probed/partial/functional/verified (Phase 8 DEVICES-02).
    };

    /**
     * @brief Construct a DeviceModel bound to a specific DeviceRegistry.
     *
     * @param registry Registry to read descriptors / live HID keys from.
     *        Must outlive this DeviceModel. In production this is the
     *        registry owned by `ajazz::app::Application`; tests can pass
     *        a local instance for isolation (audit finding A1).
     * @param parent QObject parent for memory management.
     */
    explicit DeviceModel(core::DeviceRegistry& registry, QObject* parent = nullptr);

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
     * @brief Re-enumerate the registry and propagate the diff to QML.
     *
     * Diff-driven per D-03: in the common path (row count unchanged), the
     * method computes which rows' connected-state flipped between the
     * previous refresh and now, and emits a per-row
     * `dataChanged(index, index, {ConnectedRole})` signal for each
     * changed row. Selection and scroll position survive automatically
     * — no row index moves, so QML's `currentIndex` stays bound to the
     * same row across hot-plug events.
     *
     * Falls back to `beginResetModel()` / `endResetModel()` only if the
     * row count changes (a brand-new backend was registered between
     * calls — unlikely outside bootstrap, but defensively handled).
     *
     * Row identity is `codename`, not `(vid, pid)` — multiple rebadge
     * VID/PIDs sharing a codename collapse onto one row whose
     * ConnectedRole is the OR across all rebadge keys (D-04). Rows are
     * sorted lexicographically by `(family, codename)` per HOTPLUG-04;
     * sort order is stable across arrival/departure/re-arrival.
     *
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
    /// Refresh m_connected by walking hid_enumerate(); cheap (≈ms) on Linux.
    void refreshLiveEnumeration();

    /// Injected registry — non-owning reference; lifetime guaranteed by
    /// `ajazz::app::Application`, which owns both us and the registry.
    core::DeviceRegistry& m_registry;

    /// Lexicographically-sorted (by family, codename) deduplicated rows.
    ///
    /// One row per `codename` — when multiple registered (vid, pid)
    /// descriptors share a codename (the AKP03 rebadge case, D-04), the
    /// first descriptor encountered is retained as the representative.
    std::vector<core::DeviceDescriptor> m_rows;

    /// Set of (vendorId, productId) pairs currently visible to hidapi.
    /// Populated by refresh() at startup and on every hot-plug event.
    std::set<std::pair<std::uint16_t, std::uint16_t>> m_connected;

    /// Codename → set of (vendorId, productId) keys that share that codename.
    ///
    /// Computed at refresh() time from the full `registry.enumerate()`
    /// result (BEFORE codename collapse). Used by `data(ConnectedRole)`
    /// to OR the per-rebadge connected-state into one row's value: a
    /// codename row is connected iff ANY of its rebadge VID/PIDs
    /// intersects `m_connected`. Closes the D-04 rebadge gap.
    std::map<std::string, std::set<std::pair<std::uint16_t, std::uint16_t>>> m_codename_keys;
};

// QML_SINGLETON dual-instance trap: DeviceModel stays safe today only
// because the required `core::DeviceRegistry&` first ctor parameter is a
// reference (and references have no "default"). A future refactor that
// switches it to a default-nullable pointer silently re-arms the bug —
// Qt 6 picks Constructor over Factory and spawns a duplicate QML-side
// instance. See BrandingService / e221b21.
static_assert(!std::is_default_constructible_v<DeviceModel>,
              "DeviceModel must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
