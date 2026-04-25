// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile_controller.hpp
 * @brief QObject bridge for loading and saving device profiles from QML.
 *
 * ProfileController is exposed to QML as the `profileController` context
 * property. It mediates between the QML UI and the core profile I/O
 * functions (ajazz::core::profileToJson / app-layer reader).
 *
 * @see Profile, Application
 */
#pragma once

#include <QObject>

namespace ajazz::app {

/**
 * @class ProfileController
 * @brief QML-accessible controller for profile persistence.
 *
 * Provides two invokable slots and a signal that QML bindings can observe
 * to react when the active profile changes. The implementation is
 * currently a stub pending the full profile I/O pipeline.
 *
 * @note Not thread-safe; must be used on the Qt main thread.
 */
class ProfileController : public QObject {
    Q_OBJECT
public:
    explicit ProfileController(QObject* parent = nullptr);

    /**
     * @brief Load a profile from a JSON file and activate it.
     *
     * Emits profileChanged() on success. The implementation is currently
     * a stub; full JSON parsing is planned for a future milestone.
     *
     * @param path Absolute file system path to the `.json` profile file.
     * @invokable Callable from QML as `profileController.loadProfile(path)`.
     */
    Q_INVOKABLE void loadProfile(QString const& path);

    /**
     * @brief Serialise the current profile and write it to a file.
     *
     * Uses ajazz::core::profileToJson() for serialisation. The implementation
     * is currently a stub.
     *
     * @param path Absolute file system path for the output `.json` file.
     * @invokable Callable from QML as `profileController.saveProfile(path)`.
     */
    Q_INVOKABLE void saveProfile(QString const& path);

signals:
    /**
     * @signal profileChanged
     * @brief Emitted whenever the active profile is replaced.
     *
     * QML bindings on `profileController.profileChanged` will be notified
     * so the UI can refresh key images and labels.
     */
    void profileChanged();
};

} // namespace ajazz::app
