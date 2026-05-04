// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file profile_controller.hpp
 * @brief QObject bridge for loading and saving device profiles from QML.
 *
 * ProfileController is exposed to QML as the `profileController` context
 * property. It mediates between the QML UI and the atomic core profile
 * I/O layer (ajazz::core::readProfileFromDisk / writeProfileToDisk).
 *
 * @see Profile, ajazz::core::profile_io
 */
#pragma once

#include "ajazz/core/profile.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>

class QJSEngine;
class QQmlEngine;

namespace ajazz::app {

/**
 * @class ProfileController
 * @brief QML-accessible controller for profile persistence.
 *
 * Provides two invokable slots (loadProfile / saveProfile) and three
 * signals: profileChanged, loadFailed, saveFailed. QML bindings observe
 * profileChanged() to refresh the visual editor; load/save failures
 * surface a translated error string for an in-app toast.
 *
 * @note Not thread-safe; must be used on the Qt main thread.
 */
class ProfileController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ProfileController)
    QML_SINGLETON
public:
    /// QML singleton factory — see BrandingService::create for the pattern.
    static ProfileController* create(QQmlEngine* qml, QJSEngine* js);

    /// Hand the singleton instance to the QML factory.
    static void registerInstance(ProfileController* instance) noexcept;

    // No default on `parent`: see BrandingService — a default-constructible
    // QML_SINGLETON makes Qt 6 pick `Constructor` mode and silently bypass
    // the static `create()` factory, spawning a duplicate QML-side instance.
    explicit ProfileController(QObject* parent);

    /**
     * @brief Load a profile from a JSON file and activate it.
     *
     * Emits profileChanged() on success. On failure emits loadFailed() with
     * a human-readable error message and leaves the in-memory profile
     * untouched.
     *
     * @param path Absolute file system path to the `.json` profile file.
     * @invokable Callable from QML as `profileController.loadProfile(path)`.
     */
    Q_INVOKABLE void loadProfile(QString const& path);

    /**
     * @brief Atomically serialise the current profile to disk.
     *
     * Uses ajazz::core::writeProfileToDisk() which performs a tmpfile +
     * fsync + rename sequence so the destination is never partially
     * written.
     *
     * @param path Absolute file system path for the output `.json` file.
     * @invokable Callable from QML as `profileController.saveProfile(path)`.
     */
    Q_INVOKABLE void saveProfile(QString const& path);

    /**
     * @brief Return the ids of every profile known to this controller.
     *
     * Used by the tray's Switch-profile submenu (#24). The returned ids are
     * stable and refer to the in-memory profile cache; the active profile
     * id (if any) is included.
     */
    [[nodiscard]] QStringList knownProfileIds() const;

    /**
     * @brief Return the user-visible name for a profile id, or empty string
     *        if no such profile is known.
     */
    [[nodiscard]] QString profileNameFor(QString const& profileId) const;

    /**
     * @brief Load a profile selected by stable id from the tray submenu.
     *
     * Currently the in-memory cache only tracks the *active* profile, so this
     * slot is a no-op when @p profileId matches the active id and emits
     * loadFailed() otherwise. Issue #24 ships the submenu wiring; the full
     * id\ \:path index is tracked separately as a follow-up to the profile
     * library work.
     */
    Q_INVOKABLE void loadProfileById(QString const& profileId);

signals:
    /**
     * @signal profileChanged
     * @brief Emitted whenever the active profile is replaced.
     *
     * QML bindings on `profileController.profileChanged` will be notified
     * so the UI can refresh key images and labels.
     */
    void profileChanged();

    /**
     * @signal loadFailed
     * @brief A profile load operation failed; UI should toast the message.
     * @param message Human-readable, possibly developer-facing error string.
     */
    void loadFailed(QString message);

    /**
     * @signal saveFailed
     * @brief A profile save operation failed.
     * @param message Human-readable error string.
     */
    void saveFailed(QString message);

    /**
     * @signal profileSaved
     * @brief Emitted after a successful save. UI may use it for toast/auto-close.
     * @param path Path that was just written.
     */
    void profileSaved(QString path);

    /**
     * @signal profilesChanged
     * @brief Emitted when the *list* of known profiles changes (added,
     *        removed, renamed). Distinct from profileChanged() which only
     *        fires when the *active* profile is swapped.
     */
    void profilesChanged();

private:
    ajazz::core::Profile m_profile{};
    QString m_path;
};

} // namespace ajazz::app
