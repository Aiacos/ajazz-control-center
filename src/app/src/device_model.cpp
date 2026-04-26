// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device_model.cpp
 * @brief DeviceModel QAbstractListModel implementation.
 *
 * refresh() calls DeviceRegistry::enumerate() to rebuild the static catalog
 * and hid_enumerate() to discover currently-plugged devices, wraps the
 * result in a beginResetModel()/endResetModel() pair so QML views rebuild
 * cleanly. ConnectedRole reflects the live presence of each (vid, pid).
 */
#include "device_model.hpp"

#include "ajazz/core/device_registry.hpp"
#include "ajazz/core/logger.hpp"

#include <algorithm>

namespace ajazz::app {

DeviceModel::DeviceModel(core::DeviceRegistry& registry, QObject* parent)
    : QAbstractListModel(parent), m_registry(registry) {}

int DeviceModel::rowCount(QModelIndex const& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant DeviceModel::data(QModelIndex const& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    auto const& d = m_rows[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ModelRole:
        return QString::fromStdString(d.model);
    case CodenameRole:
        return QString::fromStdString(d.codename);
    case FamilyRole:
        return static_cast<int>(d.family);
    case VidRole:
        return d.vendorId;
    case PidRole:
        return d.productId;
    case ConnectedRole: {
        // Live HID presence test: a registry row is "connected" iff hidapi's
        // last enumeration saw a device with the same (vendorId, productId).
        // Refreshed by refresh() at startup and on every hot-plug event.
        auto const key = std::make_pair(d.vendorId, d.productId);
        return m_connected.find(key) != m_connected.end();
    }
    case KeyCountRole:
        return static_cast<int>(d.keyCount);
    case GridColumnsRole:
        return static_cast<int>(d.gridColumns);
    case EncoderCountRole:
        return static_cast<int>(d.encoderCount);
    case DpiStageCountRole:
        return static_cast<int>(d.dpiStageCount);
    case HasRgbRole:
        return d.hasRgb;
    case HasTouchStripRole:
        return d.hasTouchStrip;
    default:
        return {};
    }
}

QHash<int, QByteArray> DeviceModel::roleNames() const {
    return {
        {ModelRole, "model"},
        {CodenameRole, "codename"},
        {FamilyRole, "family"},
        {VidRole, "vid"},
        {PidRole, "pid"},
        {ConnectedRole, "connected"},
        {KeyCountRole, "keyCount"},
        {GridColumnsRole, "gridColumns"},
        {EncoderCountRole, "encoderCount"},
        {DpiStageCountRole, "dpiStageCount"},
        {HasRgbRole, "hasRgb"},
        {HasTouchStripRole, "hasTouchStrip"},
    };
}

void DeviceModel::refresh() {
    beginResetModel();
    m_rows = m_registry.enumerate();
    refreshLiveEnumeration();
    endResetModel();
}

void DeviceModel::refreshLiveEnumeration() {
    // The actual hidapi call lives in core/DeviceRegistry so the app target
    // does not need to link hidapi directly. On Linux the result reflects the
    // /dev/hidraw* nodes the calling user can open (uaccess ACL); on Windows
    // and macOS it reflects every device the OS has enumerated.
    m_connected = m_registry.enumerateConnectedHidKeys();
    int matching = 0;
    for (auto const& d : m_rows) {
        if (m_connected.count(std::make_pair(d.vendorId, d.productId)) > 0) {
            ++matching;
        }
    }
    AJAZZ_LOG_INFO("device-model",
                   "live enumeration: {} hid device(s) total, {} of {} supported model(s) online",
                   static_cast<int>(m_connected.size()),
                   matching,
                   static_cast<int>(m_rows.size()));
}

QVariantMap DeviceModel::capabilitiesFor(QString const& codename) const {
    auto const target = codename.toStdString();
    auto it = std::find_if(m_rows.begin(), m_rows.end(), [&](core::DeviceDescriptor const& d) {
        return d.codename == target;
    });
    if (it == m_rows.end()) {
        return {};
    }
    QVariantMap m;
    m.insert(QStringLiteral("model"), QString::fromStdString(it->model));
    m.insert(QStringLiteral("codename"), QString::fromStdString(it->codename));
    m.insert(QStringLiteral("family"), static_cast<int>(it->family));
    m.insert(QStringLiteral("keyCount"), static_cast<int>(it->keyCount));
    m.insert(QStringLiteral("gridColumns"), static_cast<int>(it->gridColumns));
    m.insert(QStringLiteral("encoderCount"), static_cast<int>(it->encoderCount));
    m.insert(QStringLiteral("dpiStageCount"), static_cast<int>(it->dpiStageCount));
    m.insert(QStringLiteral("hasRgb"), it->hasRgb);
    m.insert(QStringLiteral("hasTouchStrip"), it->hasTouchStrip);
    return m;
}

} // namespace ajazz::app
