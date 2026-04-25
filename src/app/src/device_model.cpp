// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device_model.cpp
 * @brief DeviceModel QAbstractListModel implementation.
 *
 * refresh() calls DeviceRegistry::enumerate() and wraps the result in a
 * beginResetModel()/endResetModel() pair so QML views rebuild cleanly.
 *
 * @note ConnectedRole currently returns false for all rows; live USB
 *       enumeration via hid_enumerate() is planned for a future milestone.
 */
#include "device_model.hpp"

#include "ajazz/core/device_registry.hpp"

#include <algorithm>

namespace ajazz::app {

DeviceModel::DeviceModel(QObject* parent) : QAbstractListModel(parent) {}

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
    case ConnectedRole:
        return false; // TODO: live enumeration via hid_enumerate
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
    m_rows = core::DeviceRegistry::instance().enumerate();
    endResetModel();
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
