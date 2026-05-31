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

    // --- Optional fields ---
    QString url;           ///< URL (author homepage)
    QString apiVersion;    ///< APIVersion (AJAZZ extension, author hint)
    QString puuid;         ///< PUUID (AJAZZ extension, plugin UUID alias)
    QString nodejsVersion; ///< Nodejs.Version (empty if no Nodejs object)

    // --- AJAZZ extension flags ---
    bool runAsAdministrator{false}; ///< RunAsAdministrator
    bool isK1Pro{false};            ///< IsK1Pro (top-level; per-action IsK1Pro is in PluginAction)

    // --- Actions ---
    std::vector<PluginAction> actions; ///< Actions array

    // --- Runtime-populated (NOT from JSON) ---
    /// Absolute path of the `.sdPlugin` directory this manifest was loaded
    /// from. Set by PluginManager::discover() after parsing; empty when the
    /// manifest was parsed from a byte buffer (unit tests). spawn() uses it as
    /// the child process working directory so a relative CodePath resolves and
    /// the plugin's own relative resource paths work.
    QString sourceDir;
};

/// Bitmask of drop-target affordances derived from a Controllers QStringList.
/// "Knob" and "Encoder" both map to Dial; absent/[] defaults to Key only.
/// "Information" is ignored (not a physical drop surface).
enum class Affordance : int { Key = 1, Dial = 2, TouchZone = 4 };

/// Compute the bitmask of drop-target affordances from a Controllers QStringList.
/// Key=1, Dial=2, TouchZone=4. Empty or absent Controllers list defaults to Key only.
/// ["Information"]-only returns 0 (non-draggable — no physical drop surface).
[[nodiscard]] int affordanceMask(QStringList const& controllers) noexcept;

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
 * @brief Return the compile-time platform string for the current build target.
 *
 * Maps Q_OS_MACOS -> "mac", Q_OS_WIN -> "windows", anything else -> "linux".
 * This is the value callers should pass to manifestRunnableHere() for the real host.
 */
[[nodiscard]] QString currentPlatformString();

} // namespace ajazz::app
