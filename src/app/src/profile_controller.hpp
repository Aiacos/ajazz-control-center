// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

namespace ajazz::app {

class ProfileController : public QObject {
    Q_OBJECT
public:
    explicit ProfileController(QObject* parent = nullptr);

    Q_INVOKABLE void loadProfile(QString const& path);
    Q_INVOKABLE void saveProfile(QString const& path);

signals:
    void profileChanged();
};

} // namespace ajazz::app
