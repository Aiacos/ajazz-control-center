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

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration>
#include <QVariantList>

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
    // Multi-profile library (Workstream D). Profiles are device-scoped JSON
    // files under AppDataLocation/profiles/. The controller keeps an in-memory
    // id->{name, deviceCodename, path} index, rebuilt by refreshProfileLibrary()
    // (scan on construction + after any create/rename/delete/duplicate). This
    // is the id:path index issue #24 deferred; loadProfileById() now resolves
    // through it instead of failing.
    // -------------------------------------------------------------------------

    /// Rescan AppDataLocation/profiles for *.json and rebuild the in-memory
    /// index. Emits profilesChanged(). Safe to call repeatedly.
    Q_INVOKABLE void refreshProfileLibrary();

    /// Known profiles for a device as a QVariantList of {id, name} maps, sorted
    /// by name. An empty @p deviceCodename returns every known profile. Drives
    /// the editor's profile-switcher dropdown.
    [[nodiscard]] Q_INVOKABLE QVariantList profilesForDevice(QString const& deviceCodename) const;

    /// Active profile identity, surfaced for the switcher UI.
    [[nodiscard]] Q_INVOKABLE QString activeProfileId() const;
    [[nodiscard]] Q_INVOKABLE QString activeProfileName() const;

    /// Create a fresh, empty profile for @p deviceCodename with a generated
    /// UUID, persist it, index it, and make it active. Returns the new id (or
    /// "" on save failure). Emits profileChanged() + profilesChanged().
    Q_INVOKABLE QString createProfile(QString const& name, QString const& deviceCodename);

    /// Rename the active profile and persist. Emits profilesChanged().
    Q_INVOKABLE void renameActiveProfile(QString const& newName);

    /// Delete a profile by id (removes the file + index entry). If the active
    /// profile is deleted, activates another profile for the same device, or a
    /// fresh "Default" if none remain. Emits profilesChanged() (and
    /// profileChanged() when the active profile changed).
    Q_INVOKABLE void deleteProfile(QString const& profileId);

    /// Duplicate a profile (by id, or the active one when @p profileId is empty)
    /// under @p newName with a fresh UUID, persist it, and make it active.
    /// Returns the new id (or "" on failure).
    Q_INVOKABLE QString duplicateProfile(QString const& profileId, QString const& newName);

    /// Ensure the active profile belongs to @p deviceCodename: loads that
    /// device's first known profile, or creates a "Default" one when none
    /// exist. Called when the selected device changes so the editor always
    /// edits a device-scoped profile.
    Q_INVOKABLE void activateDeviceProfile(QString const& deviceCodename);

    /// Active profile's key bindings as a QVariantList of
    /// {index, iconSource, label, actionKind, actionId} maps (only populated
    /// keys). Lets the QML editor rebuild its preview model after a profile
    /// switch (DeviceView listens to profileChanged()).
    [[nodiscard]] Q_INVOKABLE QVariantList activeKeyBindings() const;

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
     * @param keyIndex    0-based key index (matches Profile::keys map keys). The
     *                    paint service adds 1 when converting to the device's
     *                    1-based scheme. Passing a 1-based index will silently
     *                    paint the wrong key. Must be in [0, 65534]; out-of-range
     *                    values are rejected with a warning (no-op).
     * @param iconPath    Absolute path or Qt resource URL; empty -> nullopt.
     * @param label       Overlay text; empty -> nullopt.
     * @param actionKind  cast from ajazz::core::ActionKind enum value.
     * @param settingsJson Opaque JSON string forwarded to Action::settingsJson.
     * @param actionId    Dotted action identifier forwarded to Action::id. Only
     *                    meaningful for ActionKind::Plugin (the plugin-host
     *                    dispatch key, e.g. "com.elgato.obs.togglemute"); empty
     *                    for built-in kinds. Optional so existing 5-arg QML
     *                    callers keep working.
     * @invokable Callable from QML as ProfileController.commitKeyBinding(...).
     */
    Q_INVOKABLE void commitKeyBinding(int keyIndex,
                                      QString const& iconPath,
                                      QString const& label,
                                      int actionKind,
                                      QString const& settingsJson,
                                      QString const& actionId = {});

    /**
     * @brief Commit a rotary-encoder binding into the active Profile (encoders map).
     *
     * Phase 26 CR-04: adds the Q_INVOKABLE so EncoderDial.qml drop handlers can
     * persist a binding via the same pattern as commitKeyBinding and
     * commitTouchZoneBinding.
     *
     * Mutates m_profile.encoders[encoderIndex]:
     *  - state.imagePath = iconPath (nullopt if empty)
     *  - state.text      = label    (nullopt if empty)
     *  - onPress         = single Action{kind, settingsJson}
     *    (library drag-drop assigns to onPress; full CW/CCW editor is a follow-up)
     *
     * Emits profileChanged() so QML drop-targets and the repaint service update.
     * Does NOT save to disk — call saveActiveProfile() to persist.
     *
     * @param encoderIndex 0-based encoder index (maps to uint16 key in encoders
     *                     map). AKP05/N4 exposes 4 encoders (indices 0..3). Values
     *                     outside [0, 65534] are rejected with a warning (no-op).
     * @param iconPath     Absolute path or Qt resource URL; empty -> nullopt.
     * @param label        Overlay text; empty -> nullopt.
     * @param actionKind   cast from ajazz::core::ActionKind enum value.
     * @param settingsJson Opaque JSON string forwarded to Action::settingsJson.
     * @invokable Callable from QML as ProfileController.commitEncoderBinding(...).
     */
    Q_INVOKABLE void commitEncoderBinding(int encoderIndex,
                                          QString const& iconPath,
                                          QString const& label,
                                          int actionKind,
                                          QString const& settingsJson,
                                          QString const& actionId = {});

    /**
     * @brief Commit a touch-strip-zone binding into the active Profile (touchZones map).
     *
     * REQ-26-B touch strip zone binding (Phase 26 D-11).
     *
     * Mutates m_profile.touchZones[zoneIndex]:
     *  - state.imagePath = iconPath (nullopt if empty)
     *  - state.text      = label    (nullopt if empty)
     *  - onTap           = single Action{kind, settingsJson}
     *
     * Emits profileChanged() so QML drop-targets and the repaint service update.
     * Does NOT save to disk — call saveActiveProfile() to persist.
     *
     * @param zoneIndex   0-based touch-zone index (maps to uint8 key in touchZones
     *                    map). AKP05/N4 exposes 4 zones (indices 0..3). Out-of-range
     *                    values [0, 255] are rejected with a warning (no-op).
     * @param iconPath    Absolute path or Qt resource URL; empty -> nullopt.
     * @param label       Overlay text; empty -> nullopt.
     * @param actionKind  cast from ajazz::core::ActionKind enum value.
     * @param settingsJson Opaque JSON string forwarded to Action::settingsJson.
     * @invokable Callable from QML as ProfileController.commitTouchZoneBinding(...).
     */
    Q_INVOKABLE void commitTouchZoneBinding(int zoneIndex,
                                            QString const& iconPath,
                                            QString const& label,
                                            int actionKind,
                                            QString const& settingsJson,
                                            QString const& actionId = {});

    /**
     * @brief Atomically swap two encoder bindings.
     *
     * Fixes the Phase 26 UI-REVIEW.md data-loss bug where
     * DeviceView.qml onEncoderSwapRequested was calling commitEncoderBinding
     * with empty params on both sides — destroying both bindings instead of
     * swapping. The QML side lacked a read-back path; this method does the
     * swap atomically on the C++ side where m_profile.encoders is directly
     * accessible.
     *
     * Out-of-range indices (negative or > uint16_t max - 1) are logged and
     * ignored. Equal indices are a no-op. Missing source or destination
     * bindings (not yet in the map) are treated as empty defaults — swapping
     * with an empty slot effectively moves the populated binding.
     *
     * Emits profileChanged() exactly once on success.
     *
     * @invokable Callable from QML as ProfileController.swapEncoderBindings(...).
     */
    Q_INVOKABLE void swapEncoderBindings(int srcIndex, int dstIndex);

    /**
     * @brief Atomically swap two touch-zone bindings.
     *
     * Touch-zone analog of swapEncoderBindings. Same rationale + safety
     * envelope; fixes the corresponding DeviceView.qml onZoneSwapRequested
     * data-loss bug (Phase 26 UI-REVIEW.md).
     *
     * @invokable Callable from QML as ProfileController.swapTouchZoneBindings(...).
     */
    Q_INVOKABLE void swapTouchZoneBindings(int srcIndex, int dstIndex);

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
    /// One indexed profile on disk. `id` is the profile's stable id (or the
    /// sanitized filename stem for legacy files with an empty id).
    struct ProfileMeta {
        QString id;
        QString name;
        QString deviceCodename;
        QString path;
    };

    /// AppDataLocation/profiles. Created lazily on first save.
    [[nodiscard]] QString profilesDir() const;

    /// Rescan the profiles directory and rebuild m_library. Does NOT emit;
    /// callers decide whether to emit profilesChanged().
    void rescanLibrary();

    ajazz::core::Profile m_profile{};
    QString m_path;
    QHash<QString, ProfileMeta> m_library; ///< id -> on-disk profile metadata.
};

// See BrandingService static_assert — same QML_SINGLETON dual-instance trap.
static_assert(!std::is_default_constructible_v<ProfileController>,
              "ProfileController must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
