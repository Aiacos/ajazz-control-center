// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ajazz/core/device.hpp"

#include <QAbstractListModel>
#include <vector>

namespace ajazz::app {

class DeviceModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        ModelRole       = Qt::UserRole + 1,
        CodenameRole,
        FamilyRole,
        VidRole,
        PidRole,
        ConnectedRole,
    };

    explicit DeviceModel(QObject* parent = nullptr);

    [[nodiscard]] int      rowCount(QModelIndex const& parent = {}) const override;
    [[nodiscard]] QVariant data(QModelIndex const& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    std::vector<core::DeviceDescriptor> m_rows;
};

}  // namespace ajazz::app
