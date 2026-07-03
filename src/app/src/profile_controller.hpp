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
 * @brief Pure case-insensitive match of a foreground app id against a profile's
 *        application hints (Phase 34-04 APROF-02).
 *
 * Returns true when @p appId equals any entry of @p hints ignoring ASCII case.
 * Empty @p appId or empty @p hints never match. This is the kernel of
 * ProfileController::resolveProfileForApp, exposed as a free function so the
 * matching contract can be unit-tested in isolation (no disk/library state).
 * The app id is treated purely as a comparison token — never as a path, command
 * or pattern (T-34-04-03 tampering mitigation).
 */
[[nodiscard]] bool appIdMatchesHints(QString const& appId, QStringList const& hints);

class ProfileController; // fwd for the switchToProfile token resolver

/**
 * @brief Resolve an inbound switchToProfile token to a known profile id
 *        (EVENT-03, Phase 34-04).
 *
 * The token arriving from a plugin over the WS is UNTRUSTED (T-34-04-03): this
 * treats it purely as a lookup key (never evaluated). Resolution order: an exact
 * profile-id match against @p ctrl.knownProfileIds() wins; otherwise a name-or-id
 * match scoped to @p deviceToken via @p ctrl.profilesForDevice(). Returns the
 * resolved profile id, or an empty string when the token (after V5 length-bound
 * + trim by the caller) matches no known profile — the caller then rejects the
 * request without activating anything. Read-only with respect to @p ctrl.
 *
 * @param ctrl         Profile controller providing the known-profile index.
 * @param profileToken Caller-bounded/trimmed token (id or user-visible name).
 * @param deviceToken  Optional device scope for the name fallback.
 * @return Resolved profile id, or "" when unresolvable.
 */
[[nodiscard]] QString resolveSwitchToProfileToken(ProfileController const& ctrl,
                                                  QString const& profileToken,
                                                  QString const& deviceToken);

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

    // -------------------------------------------------------------------------
    // Phase 34-05 (APROF-03): foreground-capability surface for the
    // Wayland/GNOME capability-warning chip in SettingsPage.qml.
    //
    // The active IActiveWindowWatcher backend reports capabilityAvailable():
    // true on a wlr-foreign-toplevel Wayland compositor / X11 / Win / macOS,
    // false on a degraded desktop (GNOME/KDE without the foreign-toplevel
    // global). Application injects that capability into the controller via
    // setForegroundCapabilityAvailable() so the QML chip + the live debug
    // channel (qml.get) can read it WITHOUT a raw watcher pointer in QML.
    //
    // The chip is visible-on-ABSENT (degradation), so foregroundCapabilityWarning
    // carries the non-empty UI-SPEC detail copy the chip's tooltip + a headless
    // qml.get assert against (success criterion 2). NOTIFY so the chip binding
    // re-evaluates when Application updates the capability after the watcher binds.
    // -------------------------------------------------------------------------
    Q_PROPERTY(bool foregroundCapabilityAvailable READ foregroundCapabilityAvailable WRITE
                   setForegroundCapabilityAvailable NOTIFY foregroundCapabilityChanged)
    Q_PROPERTY(QString foregroundCapabilityWarning READ foregroundCapabilityWarning CONSTANT)
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

    /// Rename a profile BY ID (active or not) and persist. The OpenDeck SPA's
    /// ProfileManager renames arbitrary non-selected profiles; routing its
    /// rename_profile through renameActiveProfile() renamed the wrong profile
    /// (audit 1.1, 2026-07-02). Active id -> renameActiveProfile(); otherwise
    /// read-modify-write the library entry on disk. Emits profilesChanged().
    /// @return true when a profile was renamed.
    Q_INVOKABLE bool renameProfile(QString const& profileId, QString const& newName);

    /// Delete a profile by id (removes the file + index entry). If the active
    /// profile is deleted, activates another profile for the same device, or a
    /// fresh "Default" if none remain. Emits profilesChanged() (and
    /// profileChanged() when the active profile changed).
    Q_INVOKABLE void deleteProfile(QString const& profileId);

    /// Duplicate a profile (by id, or the active one when @p profileId is empty)
    /// under @p newName with a fresh UUID, persist it, and make it active.
    /// Returns the new id (or "" on failure).
    Q_INVOKABLE QString duplicateProfile(QString const& profileId, QString const& newName);

    /// Export a profile (by @p profileId, or the active one when empty) to
    /// @p destPath (a filesystem path or a file:// URL) as a shareable JSON file
    /// — the native-format slice of marketplace "profile install" (Delta G).
    /// Returns true on success; emits saveFailed() on error.
    Q_INVOKABLE bool exportProfile(QString const& profileId, QString const& destPath);

    /// Import a profile from @p srcPath (filesystem path or file:// URL). A fresh
    /// id is assigned so it never collides with an existing library entry and the
    /// name is tagged "(imported)"; the device codename is preserved. The imported
    /// profile is made active. Returns the new id ("" on failure; emits
    /// loadFailed()). Emits profileChanged() + profilesChanged().
    Q_INVOKABLE QString importProfile(QString const& srcPath);

    /// Ensure the active profile belongs to @p deviceCodename: loads that
    /// device's first known profile, or creates a "Default" one when none
    /// exist. Called when the selected device changes so the editor always
    /// edits a device-scoped profile.
    Q_INVOKABLE void activateDeviceProfile(QString const& deviceCodename);

    // -------------------------------------------------------------------------
    // Phase 34-04 (APROF-02): per-app profile auto-switch resolution.
    //
    // The foreground-window watcher (active_window_watcher.hpp) reports the
    // focused application's identity token (app_id / WM_CLASS / image-base /
    // bundle-id). resolveProfileForApp() maps that token to the profile that
    // should be active, by matching it case-insensitively against each profile's
    // Profile::applicationHints, with a default-profile fallback. Application
    // wires the watcher's onChange callback to this resolver and then to
    // loadProfileById, behind an idempotent guard (no re-activation when the
    // resolved profile is already active — CR WR-01 / T-34-04-01 DoS mitigation).
    // -------------------------------------------------------------------------

    /// Resolve the profile id that should be active for the foreground app
    /// @p appId on device @p deviceCodename.
    ///
    /// Matching is case-insensitive against each candidate profile's
    /// Profile::applicationHints (Profile::applicationHints, profile.hpp:192).
    /// Scope: when @p deviceCodename is non-empty, only profiles for that device
    /// are considered; an empty codename considers every known profile. On no
    /// hint match the result is the device's DEFAULT profile id (the first known
    /// profile for the device, sorted by name — mirroring activateDeviceProfile's
    /// fallback), or the active profile id when none exist. Returns an empty
    /// string only when there is genuinely no profile to switch to.
    ///
    /// Read-only: rescans the on-disk library and reads each candidate profile's
    /// hints; it does NOT change the active profile (Application does that via
    /// loadProfileById after the idempotent guard). The token is treated purely
    /// as a lookup key — never evaluated or shelled out (T-34-04-03).
    ///
    /// @param appId          Foreground application identity token.
    /// @param deviceCodename Device scope; empty considers every profile.
    /// @param allowDefaultFallback When true (default), a non-matching app falls
    ///        back to the device-default profile (first by name) per the LOCKED
    ///        34-CONTEXT decision ("a default-profile fallback applies when no
    ///        hint matches"). The focus-driven AUTO-SWITCH path passes FALSE so
    ///        an unmapped foreground change is a no-op and does NOT clobber a
    ///        manual selection (CR WR-01): without a mapping the active profile
    ///        stays put, which is what the SettingsPage copy promises. Callers
    ///        that want device-default semantics (e.g. device connect) leave it
    ///        true.
    /// @return The profile id to activate, or "" when no profile is applicable
    ///         (always "" on no match when @p allowDefaultFallback is false).
    [[nodiscard]] Q_INVOKABLE QString resolveProfileForApp(QString const& appId,
                                                           QString const& deviceCodename,
                                                           bool allowDefaultFallback = true) const;

    /// Read the active profile's application hints (one token per entry).
    [[nodiscard]] Q_INVOKABLE QStringList applicationHints() const;

    /// Replace the active profile's Profile::applicationHints with @p hints
    /// (empty/whitespace tokens are dropped), persist, and emit profilesChanged()
    /// so resolveProfileForApp() and the assign-profile UI see the new mapping.
    /// The APROF-03 assign-profile surface (Plan 05) writes through this; it also
    /// backs the auto-switch resolver test fixtures. Mirrors the
    /// commitKeyBinding/commitEncoderBinding writer shape (mutate + emit + save).
    Q_INVOKABLE void setApplicationHints(QStringList const& hints);

    // -------------------------------------------------------------------------
    // Phase 34-05 (APROF-03): per-app mapping writer + remover for the
    // assign-profile UI. These target a SPECIFIC profile by id (not just the
    // active profile like setApplicationHints) so the SettingsPage surface can
    // map an application name to any of the device's profiles without first
    // having to activate it. Mirrors the commitKeyBinding/commitEncoderBinding
    // writer shape: mutate the addressed profile -> persist -> emit
    // profilesChanged(). The app name is bounded + treated purely as a match
    // token (V5 / T-34-05-01 — never evaluated or shelled out).
    // -------------------------------------------------------------------------

    /// Maximum accepted length of a user-entered application-name match token
    /// (V5 input bound, T-34-05-01). Longer input is rejected as a no-op.
    static constexpr int kMaxAppNameLength = 256;

    /// Add @p appName to @p profileId's Profile::applicationHints and persist.
    ///
    /// The app name is trimmed and length-bounded to kMaxAppNameLength (V5);
    /// an empty/whitespace/over-long name, or an unknown @p profileId, is a
    /// rejected no-op (logged, never crashes). A case-insensitive duplicate
    /// already present on the target profile is also a no-op. When @p profileId
    /// is the active profile the in-memory copy is updated in place; otherwise
    /// the profile is read from disk, mutated and written back. Emits
    /// profilesChanged() on a successful add so resolveProfileForApp() and the
    /// assign-profile UI observe the new mapping.
    ///
    /// @param profileId Target profile's stable id.
    /// @param appName   Foreground application-name match token (user input).
    /// @return true when a mapping was added; false on any rejected no-op.
    Q_INVOKABLE bool addAppProfileMapping(QString const& profileId, QString const& appName);

    /// Remove @p appName (case-insensitive) from @p profileId's application
    /// hints and persist. Unknown profile, empty name, or a name not present is
    /// a no-op (returns false). Emits profilesChanged() when a mapping was
    /// actually removed.
    Q_INVOKABLE bool removeAppProfileMapping(QString const& profileId, QString const& appName);

    /// All current app->profile mappings across every known profile, as a
    /// QVariantList of {profileId, profileName, deviceCodename, appName} maps,
    /// one entry per (profile, hint) pair, sorted by profile name then app name.
    /// Drives the assign-profile mapping list in SettingsPage.qml. Read-only:
    /// rescans the on-disk library and reads each profile's hints.
    [[nodiscard]] Q_INVOKABLE QVariantList appProfileMappings() const;

    /// Whether the active foreground-window watcher exposes a foreground API
    /// (drives the visible-on-absent capability-warning chip). Defaults true;
    /// Application lowers it when the watcher reports a degraded desktop.
    [[nodiscard]] bool foregroundCapabilityAvailable() const noexcept;

    /// Inject the watcher's capability into the controller (Application seam).
    /// Emits foregroundCapabilityChanged() when the value actually changes so
    /// the chip binding re-evaluates. Also Q_INVOKABLE so the live debug
    /// channel can force the capability-absent path for headless verification.
    Q_INVOKABLE void setForegroundCapabilityAvailable(bool available);

    /// Non-empty human-readable detail for the capability-warning chip
    /// (UI-SPEC copy). Exposed so the chip tooltip + a headless qml.get can
    /// read the warning text without hover (success criterion 2).
    [[nodiscard]] QString foregroundCapabilityWarning() const;

    /// Active profile's key bindings as a QVariantList of
    /// {index, iconSource, label, actionKind, actionId} maps (only populated
    /// keys). Lets the QML editor rebuild its preview model after a profile
    /// switch (DeviceView listens to profileChanged()).
    [[nodiscard]] Q_INVOKABLE QVariantList activeKeyBindings() const;

    /// Active profile's ENCODER (dial) bindings as a QVariantList of
    /// {index, iconSource, label, actionKind, actionId} maps (only populated
    /// encoders). The keypad analog above is activeKeyBindings(); this lets the
    /// QML editor resolve the action bound to the selected dial so the Property
    /// Inspector can configure it (the encoder's onPress is the bound action).
    [[nodiscard]] Q_INVOKABLE QVariantList activeEncoderBindings() const;

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
     * @brief Bind a dial to system volume control (Stream Deck + style).
     *
     * Sets up the encoder's directional chains so the dial drives the OS volume
     * via the built-in @c com.hotspot.streamdock.system.volume action (which
     * routes to the input synthesizer's media keys — Linux uinput KEY_VOLUMEUP/
     * DOWN/MUTE, Windows SendInput, macOS CGEvent):
     *   - clockwise  (onCw)    -> volume up
     *   - counter-cw (onCcw)   -> volume down
     *   - press      (onPress) -> mute toggle
     * The segment label is set to "Volume". Emits profileChanged(); does NOT save
     * (downstream persistence handles it). This is the first directional encoder
     * binding — drag-drop of the library "Volume" action routes here instead of
     * the onPress-only @ref commitEncoderBinding.
     *
     * @param encoderIndex 0-based dial index (validated like commitEncoderBinding).
     * @invokable Callable from QML (the dial drop handler) and the debug channel.
     */
    Q_INVOKABLE void commitEncoderVolume(int encoderIndex);

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
     * @brief Clear every key/encoder binding owned by an uninstalled plugin.
     *
     * Feature 002 US3/T037 (spec edge case): when a plugin is uninstalled while
     * one of its actions is bound to a key or a dial, the affected control MUST
     * revert to an unbound state without crashing the editor. A binding is owned
     * by @p pluginUuid when its action id (the onPress front, or the OpenDeck
     * `instance` id) equals the plugin uuid or is a dotted child of it (the same
     * owner-prefix rule ContextRegistry uses). Matching bindings are erased from
     * the active profile (root keys + encoders). Emits profileChanged() once if
     * anything changed; does NOT save to disk (downstream persistence handles it).
     *
     * @param pluginUuid reverse-DNS plugin id (e.g. "com.elgato.counter").
     * @return number of bindings cleared.
     * @invokable Callable from QML / the debug channel for verification.
     */
    Q_INVOKABLE int clearBindingsForPlugin(QString const& pluginUuid);

    // -------------------------------------------------------------------------
    // Phase 29-03 (PLUGIN-23): Multi-action editing — append / reorder / remove
    // on a key's onPress action vector.
    // -------------------------------------------------------------------------

    /**
     * @brief Append a new Action onto key[keyIndex].onPress WITHOUT clearing
     *        existing entries.
     *
     * Called by KeyBindingList.qml when the user drops a NEW library action
     * onto a key that already has one action — the "additive" path. The
     * "replace-all" path remains commitKeyBinding().
     *
     * keyIndex validation mirrors commitKeyBinding (range [0, 65534]).
     * actionKind validation mirrors commitKeyBinding ([0, BackToParent]).
     * Out-of-range arguments are no-ops (logged, not asserted).
     *
     * Emits profileChanged(). Does NOT save to disk.
     *
     * @param keyIndex    0-based key index.
     * @param actionKind  cast from ajazz::core::ActionKind enum value.
     * @param settingsJson Opaque JSON string forwarded to Action::settingsJson.
     * @param actionId    Dotted action identifier (for ActionKind::Plugin).
     * @invokable Callable from QML as ProfileController.appendKeyAction(...).
     */
    Q_INVOKABLE void appendKeyAction(int keyIndex,
                                     int actionKind,
                                     QString const& settingsJson,
                                     QString const& actionId);

    /**
     * @brief Move the action at fromPos to toPos within key[keyIndex].onPress.
     *
     * Implements drag-reorder for the per-key binding list (KeyBindingList.qml).
     * Out-of-range (fromPos or toPos outside [0, onPress.size()-1]) is a no-op.
     * Equal indices are a no-op.
     *
     * Uses std::rotate to shift the element in O(n) without allocations.
     * Emits profileChanged(). Does NOT save to disk.
     *
     * @param keyIndex  0-based key index.
     * @param fromPos   Current 0-based position of the action to move.
     * @param toPos     Target 0-based position.
     * @invokable Callable from QML as ProfileController.reorderKeyAction(...).
     */
    Q_INVOKABLE void reorderKeyAction(int keyIndex, int fromPos, int toPos);

    /**
     * @brief Erase the action at pos within key[keyIndex].onPress.
     *
     * Implements drag-to-trash for individual actions in the binding list.
     * pos out of range [0, onPress.size()-1] is a no-op. Removing the last
     * action leaves an empty onPress (key reads as empty/cleared).
     *
     * Emits profileChanged(). Does NOT save to disk.
     *
     * @param keyIndex  0-based key index.
     * @param pos       0-based position to erase.
     * @invokable Callable from QML as ProfileController.removeKeyActionAt(...).
     */
    Q_INVOKABLE void removeKeyActionAt(int keyIndex, int pos);

    /**
     * @brief Clear an encoder (dial) slot's binding, including its touch-strip
     * segment (the EncoderBinding owns the segment on dial devices).
     *
     * Erases the whole encoder binding so the slot reads as empty, persists via
     * saveActiveProfile(), and emits profileChanged(). No-op when the slot is
     * already empty or @p encoderIndex is out of range. Closes the
     * encoder/touch gap in the OpenDeck `remove_instance` command.
     *
     * @param encoderIndex 0-based dial index.
     * @param pos          0-based action position (reserved; an encoder slot
     *                     carries a single OpenDeck instance, so the whole
     *                     binding is cleared).
     * @invokable Callable as ProfileController.removeEncoderActionAt(...).
     */
    Q_INVOKABLE void removeEncoderActionAt(int encoderIndex, int pos);

    /**
     * @brief Clear a legacy touch-strip-zone slot's binding.
     *
     * Retained for read-compat profiles (dial devices fold the strip segment
     * into the EncoderBinding). Persists + emits profileChanged(); no-op when
     * empty or out of range.
     *
     * @param zoneIndex 0-based touch-zone index.
     * @param pos       0-based action position (reserved; see removeEncoderActionAt).
     * @invokable Callable as ProfileController.removeTouchZoneActionAt(...).
     */
    Q_INVOKABLE void removeTouchZoneActionAt(int zoneIndex, int pos);

    // -------------------------------------------------------------------------
    // Phase 32-03 (BIND-05/07): Toggle Action currentState cycle.
    // -------------------------------------------------------------------------

    /**
     * @brief Advance a binding's `instance.currentState` one step (mod N) and persist.
     *
     * Toggle Action semantics (BIND-05/07): pressing a Toggle cycles its
     * @ref ajazz::core::ActionInstance::currentState through ALL
     * @ref ajazz::core::ActionInstance::states (N > 2 supported):
     * `currentState = (currentState + 1) % states.size()`. The new index is
     * PERSISTED to the profile JSON (Q1 user decision) so it survives a restart,
     * via the SAME save path commitKeyBinding's callers use (saveActiveProfile()),
     * and @ref profileChanged is emitted so the repaint/context reconcile fires.
     *
     * Safe no-op (currentState unchanged) when:
     *   - the addressed binding has no @ref ajazz::core::Binding::instance, OR
     *   - the instance carries fewer than two states (a single-state or empty
     *     instance has nothing to cycle).
     *
     * @param controller "Keypad" for a key binding, "Encoder" for an encoder
     *                   binding (case-insensitive). Any other value is a no-op.
     * @param index      0-based control index (key index or encoder index).
     * @invokable Callable from QML as ProfileController.cycleInstanceState(...).
     */
    Q_INVOKABLE void cycleInstanceState(QString const& controller, int index);

    /**
     * @brief Set a binding's `instance.currentState` to a SPECIFIC index (vs the
     * advance-by-one @ref cycleInstanceState), persist it, and emit
     * profileChanged() so the canvas + device repaint the chosen state.
     *
     * Backs the OpenDeck `set_state` command (the InstanceEditor selects which
     * state of a multi-state action is live). Clamped into
     * [0, states.size()-1]; a no-op when the slot has no instance or @p
     * stateIndex is out of range.
     *
     * @param controller "Keypad" or "Encoder" (case-insensitive; touch-zone
     *                   bindings register under "Encoder", as in cycleInstanceState).
     * @param index      0-based control index.
     * @param stateIndex 0-based state to make current.
     * @invokable Callable as ProfileController.setInstanceCurrentState(...).
     */
    Q_INVOKABLE void setInstanceCurrentState(QString const& controller, int index, int stateIndex);

    /**
     * @brief Persist an edited per-state visual (OpenDeck set_state contract).
     *
     * The SPA's InstanceEditor fires `set_state {context, index, state}` with
     * the FULL edited ActionState on every edit; upstream writes
     * `states[index] = state` and never touches current_state. Writes
     * @p visual into the binding's instance states[@p stateIndex] (creating
     * the instance/states slots as needed), or into the legacy single
     * KeyState for controllers without instances ("TouchZone"). Persists via
     * saveActiveProfile(). NOT invokable from QML (core::KeyState parameter);
     * called by OpenDeckBridge.
     *
     * @return true when a binding was found and updated.
     */
    bool setInstanceStateVisual(QString const& controller,
                                int index,
                                int stateIndex,
                                ajazz::core::KeyState const& visual);

    /**
     * @brief Mirror a Property-Inspector settings write into the binding.
     *
     * Sets @p settingsJson on EVERY Plugin-kind step of the addressed
     * binding's chains (all of them — a dial builtin replicates its action
     * across onCw/onCcw/onPress) and on the OpenDeck instance's settings,
     * then persists. This is what makes PI edits reach the press-time chain
     * of BUILTIN actions (which run on the binding's settingsJson, not the
     * plugin settings store) and survive restarts in the profile.
     * @p controller is "Keypad" | "Encoder" | "TouchZone".
     *
     * @return true when a binding was found and updated.
     */
    bool updateBindingSettings(QString const& controller, int index, QString const& settingsJson);

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
     * @brief Atomically swap (or move) two KEY bindings, whole-Binding.
     *
     * The keypad analog of swapEncoderBindings, added for the "move a bound
     * action to another button" gesture (goal: drag an occupied key onto
     * another key). Unlike the previous QML-side workaround in DeviceView's
     * onKeySwapRequested — which re-issued two commitKeyBinding() calls and so
     * collapsed a multi-action onPress chain down to a single action and dropped
     * onRelease/onLongPress — this moves the ENTIRE core::Binding (the full
     * onPress vector incl. multi-action, onRelease, onLongPress, and the visual
     * KeyState) so no chain data is lost on a move/swap.
     *
     * Swapping with an empty destination effectively MOVES the binding there and
     * clears the source (operator[] default-constructs an empty Binding for an
     * absent key, which is the correct "moved away" state). The subsequent
     * profileChanged() drives the bridge's context reconcile so the plugin
     * receives willDisappear(old key) + willAppear(new key).
     *
     * Out-of-range indices (negative or > uint16_t max - 1) are logged and
     * ignored; equal indices are a no-op. Emits profileChanged() once on success.
     *
     * @invokable Callable from QML as ProfileController.swapKeyBindings(...).
     */
    Q_INVOKABLE void swapKeyBindings(int srcIndex, int dstIndex);

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
     * @brief Move or copy a binding between slots, including across controllers.
     *
     * Backs the OpenDeck SPA's `move_instance` command in FULL: drag-move
     * (retain=false — source cleared) and copy/paste (retain=true — source
     * kept). The destination slot is OVERWRITTEN (SPA semantics: it assigns the
     * returned instance into the slot unconditionally).
     *
     * Controllers: "Keypad" (active-page key map), "Encoder", "TouchZone".
     * Same-controller transfers copy the whole binding struct (multi-action
     * chains, per-event chains, state, instance all survive). Cross-controller
     * transfers map the primary action chain (Keypad.onPress <-> Encoder.onPress
     * <-> TouchZone.onTap) plus the visual KeyState; fields with no counterpart
     * (Encoder onCw/onCcw, TouchZone-bound instance) are dropped — a lossy but
     * user-initiated conversion, mirroring upstream OpenDeck.
     *
     * Persists via saveActiveProfile() and emits profileChanged() on success.
     *
     * @param srcController "Keypad" | "Encoder" | "TouchZone".
     * @param srcIndex      0-based slot in the source collection.
     * @param dstController "Keypad" | "Encoder" | "TouchZone".
     * @param dstIndex      0-based slot in the destination collection.
     * @param retain        true = copy (keep source), false = move (clear source).
     * @return              true on success; false when the source slot is empty,
     *                      a controller name is unknown, or an index is invalid.
     *
     * @invokable Callable from QML as ProfileController.transferBinding(...).
     */
    Q_INVOKABLE bool transferBinding(QString const& srcController,
                                     int srcIndex,
                                     QString const& dstController,
                                     int dstIndex,
                                     bool retain);

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

    // -------------------------------------------------------------------------
    // Pages / Folders (Elgato-parity Delta A). A profile's root key map is
    // Profile::keys; nested folder pages live in Profile::pages keyed by id (see
    // profile.hpp). The editor tracks which page is currently shown via a
    // navigation stack ([root] at the bottom); the key read/write methods above
    // (activeKeyBindings/commitKeyBinding/appendKeyAction/reorderKeyAction/
    // removeKeyActionAt/swapKeyBindings) resolve against that active page instead
    // of always editing root. Encoders/touch-zones stay profile-global (a
    // ProfilePage carries keys only), matching the Elgato model where folders are
    // a key-grid concept. The device-render side (repaintPage) is already
    // page-aware; this brings the editor in line.
    // -------------------------------------------------------------------------

    /// Id of the page the editor is currently showing ("root" at the top level).
    [[nodiscard]] Q_INVOKABLE QString activePageId() const;

    /// User-visible name of the active page ("Home" for root).
    [[nodiscard]] Q_INVOKABLE QString activePageName() const;

    /// Breadcrumb from root to the active page as a QVariantList of {id, name}
    /// maps. Always starts with the root entry; drives the editor breadcrumb bar.
    [[nodiscard]] Q_INVOKABLE QVariantList pageBreadcrumb() const;

    /// Navigate the editor INTO an existing folder page (push onto the nav stack).
    /// No-op (logged) if @p pageId is unknown. Emits profileChanged() so the
    /// canvas re-syncs to the folder's keys.
    Q_INVOKABLE void enterFolder(QString const& pageId);

    /// Pop one level off the editor nav stack (return to the parent page). No-op
    /// at root. Emits profileChanged().
    Q_INVOKABLE void goBackPage();

    /// Reset the editor nav stack to the root page. Emits profileChanged().
    Q_INVOKABLE void goToRootPage();

    /// Create a folder on @p keyIndex of the CURRENT page: makes a fresh
    /// ProfilePage (generated id), binds the key to an OpenFolder action targeting
    /// it, seeds the new page with a BackToParent key at index 0 so the device can
    /// return, and persists. Returns the new page id ("" on invalid index). Emits
    /// profileChanged() + profileSaved(). The editor does NOT auto-enter the new
    /// folder (caller decides) so the just-created OpenFolder key stays visible.
    Q_INVOKABLE QString createFolderOnKey(int keyIndex, QString const& name);

    // -------------------------------------------------------------------------
    // Toggle Action (multi-state key) editor (Elgato-parity Delta C). A key
    // becomes a built-in Toggle when it carries an ActionInstance with id
    // kToggleActionId and >= 2 states; the input service cycles currentState on
    // press and PluginDeviceBridge::renderToggleState repaints states[currentState]
    // (image + title) -- that runtime path already exists. These verbs make it
    // reachable from the editor (no UI existed to create/configure toggle states).
    // -------------------------------------------------------------------------

    /// Replace the active page key's toggle states wholesale. @p states is a list
    /// of {title, image} maps. Fewer than two states removes the toggle (the key
    /// reverts to a normal single-state binding). The live currentState is
    /// preserved across an edit (clamped). Persists + emits profileChanged().
    Q_INVOKABLE void commitToggleStates(int keyIndex, QVariantList states);

    /// The active page key's toggle states as a QVariantList of {title, image}
    /// maps; empty when the key is not a toggle. Drives the Inspector editor.
    [[nodiscard]] Q_INVOKABLE QVariantList toggleStatesForKey(int keyIndex) const;

    /// 0-based active state index for a toggle key (0 when not a toggle).
    [[nodiscard]] Q_INVOKABLE int toggleCurrentState(int keyIndex) const;

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
     * @signal bindingSettingsUpdated
     * @brief A binding's plugin settings changed (PI edit round-trip) — the
     *        profile structure is UNCHANGED.
     *
     * Deliberately distinct from profileChanged(): mirroring a per-keystroke
     * PI settings write as a full profile reload made the SPA remount the
     * Property Inspector iframe on every character typed, wiping the field
     * (found live 2026-07-03 on the starterpack Run Command PI). Consumers
     * that only cache settings may listen here; the SPA bridge must NOT
     * resend the profile for it.
     */
    void bindingSettingsUpdated();

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

    /**
     * @signal foregroundCapabilityChanged
     * @brief Emitted when the foreground-window capability flips (Phase 34-05).
     *
     * Drives the visibility binding of the Wayland/GNOME capability-warning
     * chip in SettingsPage.qml so it appears/disappears as Application injects
     * the active watcher's capabilityAvailable() state.
     */
    void foregroundCapabilityChanged();

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

    /// Resolve the key-binding map for the page the editor is currently showing
    /// (Delta A). "root"/empty -> Profile::keys; otherwise Profile::pages[id].keys.
    /// An unknown active page id falls back to root (safe — never default-creates
    /// a page via operator[]). The per-page key read/write methods route through
    /// this so editing follows the active folder.
    [[nodiscard]] std::unordered_map<std::uint16_t, ajazz::core::Binding>& activeKeyMap();
    [[nodiscard]] std::unordered_map<std::uint16_t, ajazz::core::Binding> const&
    activeKeyMap() const;

    /// Reset the editor navigation stack to [root]. Called whenever a new profile
    /// becomes active (load/switch/reset) so the canvas opens at the top level
    /// rather than a stale folder from the previous profile.
    void resetPageNav();

    ajazz::core::Profile m_profile{};

    /// Editor page-navigation stack (Delta A). Always non-empty: index 0 is the
    /// implicit root ("root"); each enterFolder() pushes a child page id, goBack()
    /// pops. activePageId() == m_pageStack.back(). NOT persisted (a profile has no
    /// "currently-open folder" on disk; it always loads at root).
    QStringList m_pageStack{QStringLiteral("root")};
    QString m_path;
    QHash<QString, ProfileMeta> m_library; ///< id -> on-disk profile metadata.

    /// Phase 34-05 (APROF-03): foreground-window capability injected by
    /// Application from the active IActiveWindowWatcher. Defaults true so the
    /// warning chip is hidden until a degraded desktop is detected (fail safe).
    bool m_foregroundCapabilityAvailable{true};
};

// See BrandingService static_assert — same QML_SINGLETON dual-instance trap.
static_assert(!std::is_default_constructible_v<ProfileController>,
              "ProfileController must not be default-constructible — see BrandingService.");

} // namespace ajazz::app
