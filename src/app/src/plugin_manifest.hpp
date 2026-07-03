// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_manifest.hpp
 * @brief PluginManifest struct + parsePluginManifest() + manifestRunnableHere() declarations.
 *
 * This is a pure Qt-Core-only parser for the Elgato Stream Deck v6 manifest schema plus
 * AJAZZ extensions (IsK1Pro, RunAsAdministrator, FSize/FFamily, Nodejs.Version, PUUID,
 * Controllers incl. Knob/SecondaryScreen). It deliberately has ZERO dependency on
 * Qt WebEngine, Qt WebSockets, or any third-party JSON library (COD-031 boundary).
 *
 * JSON key names come verbatim from docs/protocols/streamdeck/akp_plugin_sdk.md §2 and §2.1.
 * The schema documentation is the source of truth; C++ field names are distinct from JSON keys.
 *
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-01 (PLUGIN-06)
 */
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace ajazz::app {

/**
 * @brief Coarse Windows-plugin classification verdict (WINPLG-01).
 *
 * Defined here (above PluginManifest) so the struct can carry a cached
 * @c winClass member with an in-class initializer. Produced by
 * @ref classifyWindowsPlugin from a parsed manifest + its on-disk bundle; drives
 * the native-run gate (@ref supportsCurrentPlatform) and the UI status chip.
 * The verdict is cached at scan time (PluginManager::discover()) — it MUST NOT be
 * recomputed lazily at launch (locked CONTEXT decision: classify at install/scan).
 *
 * Mapping to the plugins-tier int cache (PluginInfo::winClass), kept explicit so
 * the UI model can mirror it without a cross-tier enum dependency:
 *   NotWindowsOnly = 0, WsOnlyIpc = 1, VendorDll = 2.
 */
enum class WinPluginClass : int {
    NotWindowsOnly = 0, ///< OS array absent or has no "windows" entry — not a win-only plugin.
    WsOnlyIpc = 1, ///< Win-only WebSocket/IPC plugin (.js/.html/.cjs, no PE) — runs natively.
    VendorDll =
        2, ///< Win-only vendor-DLL/.exe plugin (PE present) — needs Windows (Wine deferred).
};

/// A single image-state entry for an action key (per akp_plugin_sdk.md §2 States[i]).
struct PluginActionState {
    QString image;          ///< States[i].Image
    QString fontSize;       ///< States[i].FontSize (or FSize Mirabox synonym)
    QString fontFamily;     ///< States[i].FontFamily (or FFamily Mirabox synonym)
    QString fontStyle;      ///< States[i].FontStyle
    QString titleColor;     ///< States[i].TitleColor
    QString titleAlignment; ///< States[i].TitleAlignment
    QString name;           ///< States[i].Name (Elgato standard, per schema)
    QString title;          ///< States[i].Title
    bool showTitle{true};   ///< States[i].ShowTitle (default true)
};

/// Encoder block descriptor for dial-capable actions (per akp_plugin_sdk.md §2 Encoder object).
struct PluginEncoderBlock {
    QString icon;                        ///< Encoder.Icon
    QString layout;                      ///< Encoder.layout ($A0/$A1/path)
    QString triggerDescriptionRotate;    ///< Encoder.TriggerDescription.Rotate
    QString triggerDescriptionPush;      ///< Encoder.TriggerDescription.Push
    QString triggerDescriptionTouch;     ///< Encoder.TriggerDescription.Touch
    QString triggerDescriptionLongTouch; ///< Encoder.TriggerDescription.LongTouch
};

/// A single action descriptor within a plugin (per akp_plugin_sdk.md §2 Actions[]).
struct PluginAction {
    QString uuid;                  ///< UUID (reverse-DNS)
    QString name;                  ///< Name (display label, may be CJK)
    QString icon;                  ///< Icon path
    QString tooltip;               ///< Tooltip (optional)
    QString propertyInspectorPath; ///< PropertyInspectorPath (optional)
    QStringList controllers; ///< Controllers — subset of Keypad/Knob/Information/SecondaryScreen
    std::vector<PluginActionState> states; ///< States array (empty [{}] is valid)
    bool isK1Pro{false};                   ///< IsK1Pro (AJAZZ extension, per-action level)
    bool visibleInActionsList{true};       ///< VisibleInActionsList (default true)
    bool disableAutomaticStates{false};    ///< DisableAutomaticStates (default false)
    std::string defaultSettings;           ///< Settings object as raw compact JSON string
    PluginEncoderBlock encoderBlock;       ///< Encoder object (empty for non-dial actions)
};

/// OS compatibility entry (per akp_plugin_sdk.md §2 OS array element).
struct PluginOsRequirement {
    QString platform;       ///< OS[].Platform — "mac" | "windows" (vendor never emits "linux")
    QString minimumVersion; ///< OS[].MinimumVersion (may be empty)
};

/// Bundled-profile declaration (per elgato_plugin_protocol.md §1.4 Profiles[]).
/// `name` is a path to a `.streamDeckProfile` inside the bundle with the
/// extension omitted (e.g. "Profiles/Spotify"); the user-visible profile name
/// is its basename. A plugin may `switchToProfile` only to a profile it ships.
struct PluginProfileDecl {
    QString name;                            ///< Name (required)
    int deviceType{-1};                      ///< DeviceType (§6.3 enum, required)
    bool readonly{false};                    ///< Readonly (default false)
    bool dontAutoSwitchWhenInstalled{false}; ///< DontAutoSwitchWhenInstalled (default false)
    bool autoInstall{true};                  ///< AutoInstall (default true)
};

/**
 * @brief Parsed representation of a plugin's manifest.json.
 *
 * Field names intentionally differ from JSON keys — see §2 in akp_plugin_sdk.md for the
 * exact JSON spelling of each key. The parser reads the JSON key names precisely as
 * documented; these C++ names are for readability only.
 */
struct PluginManifest {
    // --- Required fields (per §2) ---
    QString name;         ///< Name
    QString author;       ///< Author
    QString version;      ///< Version
    QString icon;         ///< Icon
    QString category;     ///< Category
    QString categoryIcon; ///< CategoryIcon
    QString description;  ///< Description
    int sdkVersion{0};    ///< SDKVersion (int, always 1 in shipped manifests)

    // --- OS / Software compatibility ---
    std::vector<PluginOsRequirement> os; ///< OS array
    QString softwareMinimumVersion;      ///< Software.MinimumVersion

    // --- Code paths (default + platform overrides) ---
    QString codePath;    ///< CodePath
    QString codePathWin; ///< CodePathWin (Windows override)
    QString codePathMac; ///< CodePathMac (macOS override)
    QString codePathLin; ///< CodePathLin (Linux override; OpenAction/OpenDeck native plugins)

    // --- Optional fields ---
    QString url;           ///< URL (author homepage)
    QString apiVersion;    ///< APIVersion (AJAZZ extension, author hint)
    QString puuid;         ///< PUUID (AJAZZ extension, plugin UUID alias)
    QString nodejsVersion; ///< Nodejs.Version (empty if no Nodejs object)

    // --- AJAZZ extension flags ---
    bool runAsAdministrator{false}; ///< RunAsAdministrator
    bool isK1Pro{false};            ///< IsK1Pro (top-level; per-action IsK1Pro is in PluginAction)
    /// HasSettingsInterface (OpenDeck extension): the plugin accepts a
    /// `showSettingsInterface` event; gates the SPA's per-plugin Settings
    /// button (audit 4.8).
    bool hasSettingsInterface{false};

    // --- Application monitoring (Elgato ApplicationsToMonitor) ---
    /// ApplicationsToMonitor — the apps this plugin asks to be told about via
    /// applicationDidLaunch/applicationDidTerminate. Elgato declares this as an
    /// object keyed by platform ({"mac":[...], "windows":[...]}); we also accept
    /// a bare array. Tokens are normalized (base name, ".exe"/".app" stripped,
    /// lowercased) to the same app-identity contract the watcher emits, so a
    /// focus-derived appId can be matched against this list (WR-02). An EMPTY
    /// list means "monitor everything" (the focus-based approximation delivers to
    /// the plugin for every app), preserving backwards-compatible fan-out.
    QStringList applicationsToMonitor;

    // --- Bundled profiles (Elgato Profiles[]) ---
    /// Profiles this plugin ships (§1.4). Parsed so switchToProfile can resolve
    /// a shipped-profile name; the .streamDeckProfile layout content itself is
    /// NOT imported yet (the ZIP-internal format is undocumented in our RE
    /// corpus — see PRODUCTION-READINESS.md blocker 1 for the bounded scope).
    std::vector<PluginProfileDecl> profiles;

    // --- Actions ---
    std::vector<PluginAction> actions; ///< Actions array

    // --- Runtime-populated (NOT from JSON) ---
    /// Absolute path of the `.sdPlugin` directory this manifest was loaded
    /// from. Set by PluginManager::discover() after parsing; empty when the
    /// manifest was parsed from a byte buffer (unit tests). spawn() uses it as
    /// the child process working directory so a relative CodePath resolves and
    /// the plugin's own relative resource paths work.
    QString sourceDir;

    /// Cached Windows-plugin classification verdict (WINPLG-01/02). Stamped by
    /// PluginManager::discover() at scan time — when @c sourceDir (the bundle
    /// dir) is still available for the bounded PE-magic scan — and carried
    /// through spawn() into the live inventory so PluginManager::plugins() can
    /// hand it to the UI model without re-scanning. Defaults to NotWindowsOnly
    /// for byte-buffer-parsed manifests (unit tests) that never went through
    /// discover(). NOT read from JSON.
    WinPluginClass winClass{WinPluginClass::NotWindowsOnly};
};

/// Bitmask of drop-target affordances derived from a Controllers QStringList.
/// "Knob" and "Encoder" both map to Dial; absent/[] defaults to Key only.
/// "Information" is ignored (not a physical drop surface).
enum class Affordance : int { Key = 1, Dial = 2, TouchZone = 4 };

/// Compute the bitmask of drop-target affordances from a Controllers QStringList.
/// Key=1, Dial=2, TouchZone=4. Empty or absent Controllers list defaults to Key only.
/// ["Information"]-only returns 0 (non-draggable — no physical drop surface).
[[nodiscard]] int affordanceMask(QStringList const& controllers) noexcept;

/// Resolve a switchToProfile token against a manifest's shipped Profiles[]
/// declarations (§1.4: a plugin may switch only to a profile it ships).
/// Matches the token against each declaration's full `Name` and its basename
/// (the `Name` is a bundle-relative path with the extension omitted), exact
/// first, then case-insensitively. Returns the canonical user-visible profile
/// name (the basename) or an empty string when nothing matches.
[[nodiscard]] QString matchShippedProfileName(PluginManifest const& manifest, QString const& token);

/// Normalize an application token (a foreground appId or an ApplicationsToMonitor
/// entry) to the watcher's app-identity contract: path base name, trailing
/// ".exe"/".app" stripped, lowercased. Empty/whitespace input yields "". Used so
/// a manifest's ApplicationsToMonitor list can be matched against a focus-derived
/// appId (WR-02).
[[nodiscard]] QString normalizeApplicationToken(QString const& raw);

/**
 * @brief Parse a manifest.json byte array into a PluginManifest.
 *
 * Returns @c std::nullopt when:
 *   - @p json is not valid JSON, or
 *   - any REQUIRED top-level key is absent: Name, Author, Version, SDKVersion, OS,
 *     at least one of {CodePath, CodePathWin, CodePathMac}, and Actions.
 *
 * All optional and AJAZZ-extension fields are silently ignored when absent.
 * The parser never throws; hostile/oversized input returns nullopt (T-18-MANIFEST).
 *
 * @param json  Raw UTF-8 content of a manifest.json file.
 * @return      Populated PluginManifest on success; std::nullopt on any parse failure.
 */
[[nodiscard]] std::optional<PluginManifest> parsePluginManifest(QByteArray const& json);

/**
 * @brief Decide whether a plugin is runnable on the current (or requested) platform.
 *
 * Rules (applied in order):
 *
 *   1. **OS gate**: if the manifest's OS array is non-empty and contains no entry whose
 *      Platform matches @p platform, return @c false — EXCEPT when @p platform is "linux"
 *      and there is no "linux" entry at all (the LOCKED Linux OS-accept policy, Assumption A1):
 *      in that case return @c true. Vendor manifests only ever list "mac"/"windows"; a strict
 *      match would block every real plugin on our primary OS.
 *
 *   2. **MinimumVersion gate**: if @c manifest.softwareMinimumVersion is non-empty and
 *      QVersionNumber(appVer) < QVersionNumber(softwareMinimumVersion), return @c false.
 *
 *   3. Otherwise return @c true.
 *
 * @param manifest  Parsed manifest to evaluate.
 * @param platform  Current platform string: "linux" | "mac" | "windows".
 * @param appVer    Running application version string (e.g. "0.1.0"). Tests inject this
 *                  directly rather than reading QCoreApplication::applicationVersion().
 * @return          @c true if the plugin is considered runnable; @c false otherwise.
 */
[[nodiscard]] bool manifestRunnableHere(PluginManifest const& manifest,
                                        QString const& platform,
                                        QString const& appVer);

/**
 * @brief Check ONLY the @c Software.MinimumVersion floor (no OS gate).
 *
 * Returns @c false when @c manifest.softwareMinimumVersion is non-empty and
 * QVersionNumber(@p appVer) < QVersionNumber(softwareMinimumVersion); @c true
 * otherwise (including when no minimum is declared).
 *
 * This factors out gate (2) of @ref manifestRunnableHere so the WINPLG-02
 * native-run override (which deliberately bypasses the OS gate for win-only
 * WS/IPC plugins) can still honour the version floor — a cross-platform-runnable
 * plugin is NOT exempt from version gating (WR-01). Plain (non-win) plugins keep
 * going through @ref manifestRunnableHere, which applies both gates.
 *
 * @param manifest  Parsed manifest to evaluate.
 * @param appVer    Running (emulated Stream Deck) version string.
 * @return          @c true if the version floor is satisfied (or absent).
 */
[[nodiscard]] bool manifestVersionGatePasses(PluginManifest const& manifest, QString const& appVer);

/**
 * @brief Return the compile-time platform string for the current build target.
 *
 * Maps Q_OS_MACOS -> "mac", Q_OS_WIN -> "windows", anything else -> "linux".
 * This is the value callers should pass to manifestRunnableHere() for the real host.
 */
[[nodiscard]] QString currentPlatformString();

/**
 * @brief Classify a Windows plugin as WS-only-IPC vs vendor-DLL (WINPLG-01).
 *
 * Pure, never throws. Heuristic (locked CONTEXT decision):
 *
 *   1. If the OS array is empty OR contains no "windows" entry -> NotWindowsOnly.
 *   2. Primary signal: the effective code path (codePathWin if non-empty, else
 *      codePath) ending (case-insensitive) in ".exe"/".dll" -> VendorDll.
 *   3. Corroborator: scan @p bundleDir for a file beginning with the PE/DOS magic
 *      bytes "MZ" (0x4D 0x5A). Any such file -> VendorDll (overrides a mislabeled
 *      manifest that declares e.g. ".js" but ships a .dll). The scan reads ONLY the
 *      first 2 bytes of each file and is bounded by a file-count cap — magic bytes
 *      only, NO full PE parse and NEVER any execution (T-35-01-01/02).
 *   4. Otherwise (win-only, suffix .js/.html/.cjs, no PE) -> WsOnlyIpc.
 *
 * @param m         Parsed manifest to classify.
 * @param bundleDir Absolute path of the extracted .sdPlugin bundle dir. May be empty
 *                  or non-existent — the suffix signal alone still classifies; no crash.
 * @return          The WinPluginClass verdict.
 */
[[nodiscard]] WinPluginClass classifyWindowsPlugin(PluginManifest const& m,
                                                   QString const& bundleDir);

/**
 * @brief Decide whether a classified plugin may run natively on @p platform (WINPLG-02).
 *
 * Pure, never throws. Complements @ref manifestRunnableHere: the caller accepts a
 * plugin when manifestRunnableHere() OR supportsCurrentPlatform() is true.
 *
 *   - WsOnlyIpc    -> true on every platform (a WS/IPC win-only plugin runs natively
 *                     on Linux/macOS without Wine — the locked native-run decision).
 *   - VendorDll    -> true only when @p platform == "windows". Wine detection is
 *                     DEFERRED (WINPLG-03 launch) — treated as false this phase, so a
 *                     vendor-DLL plugin surfaces as a chip and is NOT accepted off Windows.
 *   - NotWindowsOnly -> false (the caller falls back to manifestRunnableHere).
 *
 * @param m        Parsed manifest (unused today; kept for symmetry + future Wine wiring).
 * @param platform Current platform string: "linux" | "mac" | "windows".
 * @param cls      The classification verdict from classifyWindowsPlugin().
 * @return         @c true if the plugin may run natively on @p platform.
 */
[[nodiscard]] bool
supportsCurrentPlatform(PluginManifest const& m, QString const& platform, WinPluginClass cls);

/**
 * @brief The EMULATED Stream Deck app version advertised to plugins.
 *
 * Single authority for the value previously duplicated as plugin_manager.cpp's
 * kEmulatedSdVersion. A manifest's Software.MinimumVersion refers to the Elgato
 * Stream Deck app (e.g. "4.1", "6.5"), NOT to this app's own version, so gating
 * against QCoreApplication::applicationVersion() (0.1.x) would refuse every real
 * plugin. We emulate the SD v6 plugin API surface; advertise a generous v6
 * version so v4/v5/v6 plugins pass. This is the version EVERY
 * Software.MinimumVersion gate must compare against — spawn-time discovery AND
 * the installedActions picker (they diverged once: the picker used the real app
 * version and hid actions of plugins that were happily running, 2026-06-09).
 *
 * @return The emulated Stream Deck version string (e.g. "6.9").
 */
[[nodiscard]] QString emulatedStreamDeckVersion();

/**
 * @brief Resolve the platform-appropriate CodePath from a manifest.
 *
 * Same resolution PluginManager::spawn() applies (codePathWin on Windows,
 * codePathMac on macOS, generic codePath otherwise/fallback). An EMPTY result
 * means the spawn step can never launch this plugin on the current platform —
 * per the LOCKED Linux OS-accept policy, CodePath existence is "the real gate"
 * after the best-effort OS gate. Exposed here so non-spawn callers (e.g. the
 * installedActions picker) can mirror that gate instead of listing bindable
 * actions for a plugin that can never run.
 *
 * @param manifest Parsed manifest.
 * @return The effective code path for this build's platform; may be empty.
 */
[[nodiscard]] QString resolveEffectiveCodePath(PluginManifest const& manifest);

} // namespace ajazz::app
