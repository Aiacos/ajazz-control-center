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
    };
}

void DeviceModel::refresh() {
    beginResetModel();
    m_rows = core::DeviceRegistry::instance().enumerate();
    endResetModel();
}

} // namespace ajazz::app
