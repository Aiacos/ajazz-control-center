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

#include <type_traits>

class QJSEngine;
class QQmlEngine;
class QDir;

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

    // -------------------------------------------------------------------------
    // Phase 16-02 (PROFILE-01): default path + commit + active-profile save/load
    // -------------------------------------------------------------------------

    /**
     * @brief Resolve the deterministic default save path for a profile id.
     *
     * Returns QStandardPaths::AppDataLocation/profiles/<sanitizedId>.json.
     * The id is sanitized to a safe filename: only [A-Za-z0-9._-] are kept;
     * path separators, "..", drive prefixes, and control characters are stripped.
     * If the sanitized id is empty, falls back to "default".
     * The profiles parent directory is NOT created here — call mkpath() before
     * the first save (Pitfall 5 — done inside saveActiveProfile).
     *
     * @param profileId Profile id (e.g. a UUID string).
     * @return Absolute path, e.g. "/home/user/.local/share/Aiacos/.../profiles/abc.json".
     */
    [[nodiscard]] QString defaultProfilePath(QString const& profileId) const;

    /**
     * @brief Commit a key-binding edit into the active Profile (keys map).
     *
     * Mutates m_profile.keys[keyIndex]:
     *  - state.imagePath = iconPath (nullopt if empty)
     *  - state.text      = label    (nullopt if empty)
     *  - onPress         = single Action{kind, settingsJson}
     *
     * Emits profileChanged() so the control service repaints and QML refreshes.
     * Does NOT save to disk — call saveActiveProfile() / saveProfile() to persist.
     *
     * @param keyIndex    0-based or 1-based key index as used by KeyDesigner
     *                    (Profile::keys are std::uint16_t — stored as-is).
     * @param iconPath    Absolute path or Qt resource URL; empty -> nullopt.
     * @param label       Overlay text; empty -> nullopt.
     * @param actionKind  cast from ajazz::core::ActionKind enum value.
     * @param settingsJson Opaque JSON string forwarded to Action::settingsJson.
     * @invokable Callable from QML as ProfileController.commitKeyBinding(...).
     */
    Q_INVOKABLE void commitKeyBinding(int keyIndex,
                                      QString const& iconPath,
                                      QString const& label,
                                      int actionKind,
                                      QString const& settingsJson);

    /**
     * @brief Save the active profile to its default path.
     *
     * Resolves defaultProfilePath(m_profile.id), creates the parent directory
     * with QDir::mkpath() if needed (Pitfall 5), then calls saveProfile().
     * Emits profileSaved on success or saveFailed on error.
     * If the active profile id is empty, uses "default".
     *
     * @invokable Callable from QML as ProfileController.saveActiveProfile().
     */
    Q_INVOKABLE void saveActiveProfile();

    /**
     * @brief Reload the active profile from its default path.
     *
     * Resolves defaultProfilePath(m_profile.id), then calls loadProfile().
     * Emits profileChanged on success or loadFailed on error.
     * If the active profile id is empty, uses "default".
     *
     * @invokable Callable from QML as ProfileController.loadActiveProfile().
     */
    Q_INVOKABLE void loadActiveProfile();

    /**
     * @brief Reset the active profile's key/encoder bindings to empty defaults and save.
     *
     * Clears all keys, encoders, mouseButtons maps on the active profile while
     * preserving id, name, and deviceCodename. Then calls saveActiveProfile()
     * so the reset is persisted atomically. Emits profileChanged + profileSaved
     * on success. This provides an honest "Restore defaults" behaviour — it never
     * silently no-ops (PROFILE-01 / plan decision for onRestoreDefaultsRequested).
     *
     * @invokable Callable from QML as ProfileController.resetActiveProfile().
     */
    Q_INVOKABLE void resetActiveProfile();

    /**
     * @brief Return the currently loaded profile by const-ref.
     *
     * Provides read-only access to the active profile for sibling C++ services
     * (e.g. StreamDockControlService) that need to iterate key bindings on
     * profileChanged without ProfileController gaining a dependency on the
     * device layer. NOT Q_INVOKABLE — returns a non-QML core type; for C++ use
     * only. The returned reference is valid until the next loadProfile() call.
     *
     * @note Open Question 4 from Phase 14 RESEARCH.md — this minimal getter is
     *       the chosen seam: keeps ProfileController a pure I/O bridge while
     *       allowing the repaint service to iterate Profile::keys directly.
     */
    [[nodiscard]] ajazz::core::Profile const& activeProfile() const noexcept;

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

// See BrandingService static_assert — same QML_SINGLETON dual-instance trap.
static_assert(!std::is_default_constructible_v<ProfileController>,
              "ProfileController must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
