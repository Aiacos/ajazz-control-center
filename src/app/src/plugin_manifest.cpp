// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_manifest.cpp
 * @brief PluginManifest parser + manifestRunnableHere() implementation.
 *
 * JSON field names are read exactly as akp_plugin_sdk.md §2 / §2.1 documents them.
 * Never rename a reader to match a C++ field name (CLAUDE.md schema-doc-is-source-of-truth).
 *
 * Qt-only JSON parsing (COD-031 boundary): QJsonDocument is used throughout. The
 * third-party JSON library is NOT used; it is PRIVATE-linked only in ajazz_plugins
 * and must never appear in ajazz_core or any installed public header.
 *
 * Manifest validation gate for OS / Software.MinimumVersion:
 *   - Assume A1 (LOCKED): manifests with no "linux" entry in OS are accepted on Linux.
 *     Vendor never emits "linux"; a strict match would block every real plugin on our
 *     primary OS. The SPAWN step (18-04) is the real runnability fence (CodePath check).
 *   - QVersionNumber::fromString handles "2.9" vs "2.10" correctly; do NOT use string compare.
 *
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-01 (PLUGIN-06)
 */
#include "plugin_manifest.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>

namespace ajazz::app {

namespace {

/// Parse a single States[] element from the action's states array.
/// Prefers the Elgato-standard key (FontSize / FontFamily); falls back to the
/// Mirabox synonym (FSize / FFamily) only when the standard key is absent.
/// See akp_plugin_sdk.md §2 States[i].FSize / FFamily.
PluginActionState parseState(QJsonObject const& obj) {
    PluginActionState s;
    s.image = obj.value(QStringLiteral("Image")).toString();

    // FontSize: prefer Elgato standard; fall back to Mirabox FSize synonym (§2.1).
    QString const fontSize = obj.value(QStringLiteral("FontSize")).toString();
    s.fontSize = fontSize.isEmpty() ? obj.value(QStringLiteral("FSize")).toString() : fontSize;

    // FontFamily: prefer Elgato standard; fall back to Mirabox FFamily synonym (§2.1).
    QString const fontFamily = obj.value(QStringLiteral("FontFamily")).toString();
    s.fontFamily =
        fontFamily.isEmpty() ? obj.value(QStringLiteral("FFamily")).toString() : fontFamily;

    s.fontStyle = obj.value(QStringLiteral("FontStyle")).toString();
    s.titleColor = obj.value(QStringLiteral("TitleColor")).toString();
    s.titleAlignment = obj.value(QStringLiteral("TitleAlignment")).toString();

    // Phase 28 extensions: Name, Title, ShowTitle (per schema §2 States[i]).
    s.name = obj.value(QStringLiteral("Name")).toString();
    s.title = obj.value(QStringLiteral("Title")).toString();
    s.showTitle = obj.value(QStringLiteral("ShowTitle")).toBool(true);

    return s;
}

/// Parse a single Actions[] element.
PluginAction parseAction(QJsonObject const& obj) {
    PluginAction a;
    a.uuid = obj.value(QStringLiteral("UUID")).toString();
    a.name = obj.value(QStringLiteral("Name")).toString();
    a.icon = obj.value(QStringLiteral("Icon")).toString();
    a.tooltip = obj.value(QStringLiteral("Tooltip")).toString();
    a.propertyInspectorPath = obj.value(QStringLiteral("PropertyInspectorPath")).toString();
    a.isK1Pro = obj.value(QStringLiteral("IsK1Pro")).toBool(false);

    // Controllers: accepted values are Keypad, Knob, Information, SecondaryScreen (§2).
    // No rejection on any string value — just store what's there.
    QJsonArray const controllers = obj.value(QStringLiteral("Controllers")).toArray();
    for (QJsonValue const& cv : controllers)
        a.controllers.append(cv.toString());

    // States[]: an empty or missing array is valid (e.g. [{}]).
    QJsonArray const states = obj.value(QStringLiteral("States")).toArray();
    a.states.reserve(static_cast<std::size_t>(states.size()));
    for (QJsonValue const& sv : states)
        a.states.push_back(parseState(sv.toObject()));

    // Phase 28 extensions (T-28-01 mitigations: guard every nested extraction).

    // VisibleInActionsList: default true (absent = visible per Elgato SDK spec).
    a.visibleInActionsList = obj.value(QStringLiteral("VisibleInActionsList")).toBool(true);

    // DisableAutomaticStates: default false.
    a.disableAutomaticStates = obj.value(QStringLiteral("DisableAutomaticStates")).toBool(false);

    // Settings: store as compact JSON string; guard on isObject() (T-28-01).
    QJsonValue const settingsVal = obj.value(QStringLiteral("Settings"));
    if (settingsVal.isObject()) {
        a.defaultSettings =
            QJsonDocument(settingsVal.toObject()).toJson(QJsonDocument::Compact).toStdString();
    }

    // Encoder block: guard on isObject() — absent/non-object yields empty PluginEncoderBlock.
    QJsonValue const encVal = obj.value(QStringLiteral("Encoder"));
    if (encVal.isObject()) {
        QJsonObject const encObj = encVal.toObject();
        a.encoderBlock.icon = encObj.value(QStringLiteral("Icon")).toString();
        a.encoderBlock.layout = encObj.value(QStringLiteral("layout")).toString();
        // TriggerDescription sub-object (optional within Encoder).
        QJsonValue const tdVal = encObj.value(QStringLiteral("TriggerDescription"));
        if (tdVal.isObject()) {
            QJsonObject const td = tdVal.toObject();
            a.encoderBlock.triggerDescriptionRotate = td.value(QStringLiteral("Rotate")).toString();
            a.encoderBlock.triggerDescriptionPush = td.value(QStringLiteral("Push")).toString();
            a.encoderBlock.triggerDescriptionTouch = td.value(QStringLiteral("Touch")).toString();
            a.encoderBlock.triggerDescriptionLongTouch =
                td.value(QStringLiteral("LongTouch")).toString();
        }
    }

    return a;
}

/// Parse an OS[] element (Platform + MinimumVersion).
PluginOsRequirement parseOsEntry(QJsonObject const& obj) {
    PluginOsRequirement req;
    req.platform = obj.value(QStringLiteral("Platform")).toString();
    req.minimumVersion = obj.value(QStringLiteral("MinimumVersion")).toString();
    return req;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// affordanceMask
// ---------------------------------------------------------------------------

int affordanceMask(QStringList const& controllers) noexcept {
    // T-28-03 mitigation: unknown tokens contribute NO affordance bit (fail-safe).
    // Only explicit Keypad/Knob/Encoder/SecondaryScreen tokens set bits.
    // "Information" is intentionally ignored — no physical drop surface.
    if (controllers.isEmpty())
        return static_cast<int>(Affordance::Key); // Pitfall 2: empty defaults to Key

    int mask = 0;
    for (QString const& token : controllers) {
        if (token == QStringLiteral("Keypad")) {
            mask |= static_cast<int>(Affordance::Key);
        } else if (token == QStringLiteral("Knob") || token == QStringLiteral("Encoder")) {
            mask |= static_cast<int>(Affordance::Dial);
        } else if (token == QStringLiteral("SecondaryScreen")) {
            mask |= static_cast<int>(Affordance::TouchZone);
        }
        // "Information" and all other unknown tokens: no bit set (T-28-03).
    }
    // If only Information tokens were present, mask stays 0 — non-draggable (Pitfall 3).
    return mask;
}

// ---------------------------------------------------------------------------
// parsePluginManifest
// ---------------------------------------------------------------------------

std::optional<PluginManifest> parsePluginManifest(QByteArray const& json) {
    // QJsonDocument::fromJson is bounds-safe and returns a null doc on malformed input
    // (T-18-MANIFEST). No exceptions possible.
    QJsonParseError err;
    QJsonDocument const doc = QJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject())
        return std::nullopt;

    QJsonObject const root = doc.object();

    // --- Required field presence checks (§2 Required column) ---
    // If any of these is absent, parsing fails hard.
    if (!root.contains(QStringLiteral("Name")))
        return std::nullopt;
    if (!root.contains(QStringLiteral("Author")))
        return std::nullopt;
    if (!root.contains(QStringLiteral("Version")))
        return std::nullopt;
    if (!root.contains(QStringLiteral("SDKVersion")))
        return std::nullopt;
    if (!root.contains(QStringLiteral("OS")))
        return std::nullopt;
    if (!root.contains(QStringLiteral("Actions")))
        return std::nullopt;

    // CodePath is required OR at least one of CodePathWin / CodePathMac must be present.
    bool const hasCodePath = root.contains(QStringLiteral("CodePath")) ||
                             root.contains(QStringLiteral("CodePathWin")) ||
                             root.contains(QStringLiteral("CodePathMac"));
    if (!hasCodePath)
        return std::nullopt;

    // --- Populate the struct ---
    PluginManifest m;

    // Required string fields
    m.name = root.value(QStringLiteral("Name")).toString();
    m.author = root.value(QStringLiteral("Author")).toString();
    m.version = root.value(QStringLiteral("Version")).toString();
    m.sdkVersion = root.value(QStringLiteral("SDKVersion")).toInt(0);

    // Optional string fields that may also be required per vendor practice
    m.icon = root.value(QStringLiteral("Icon")).toString();
    m.category = root.value(QStringLiteral("Category")).toString();
    m.categoryIcon = root.value(QStringLiteral("CategoryIcon")).toString();
    m.description = root.value(QStringLiteral("Description")).toString();
    m.url = root.value(QStringLiteral("URL")).toString();
    m.apiVersion = root.value(QStringLiteral("APIVersion")).toString();
    m.puuid = root.value(QStringLiteral("PUUID")).toString();

    // Code paths
    m.codePath = root.value(QStringLiteral("CodePath")).toString();
    m.codePathWin = root.value(QStringLiteral("CodePathWin")).toString();
    m.codePathMac = root.value(QStringLiteral("CodePathMac")).toString();

    // AJAZZ extension flags (§2.1)
    m.runAsAdministrator = root.value(QStringLiteral("RunAsAdministrator")).toBool(false);
    m.isK1Pro = root.value(QStringLiteral("IsK1Pro")).toBool(false);

    // Nodejs.Version — §2 Nodejs object (optional)
    QJsonValue const nodejsVal = root.value(QStringLiteral("Nodejs"));
    if (nodejsVal.isObject()) {
        m.nodejsVersion = nodejsVal.toObject().value(QStringLiteral("Version")).toString();
    }

    // Software.MinimumVersion — §2 Software object (optional)
    QJsonValue const softwareVal = root.value(QStringLiteral("Software"));
    if (softwareVal.isObject()) {
        m.softwareMinimumVersion =
            softwareVal.toObject().value(QStringLiteral("MinimumVersion")).toString();
    }

    // OS array — §2 OS (required by earlier check, but could be empty array)
    QJsonArray const osArray = root.value(QStringLiteral("OS")).toArray();
    m.os.reserve(static_cast<std::size_t>(osArray.size()));
    for (QJsonValue const& osVal : osArray)
        m.os.push_back(parseOsEntry(osVal.toObject()));

    // Actions array — §2 Actions (required by earlier check)
    QJsonArray const actionsArray = root.value(QStringLiteral("Actions")).toArray();
    m.actions.reserve(static_cast<std::size_t>(actionsArray.size()));
    for (QJsonValue const& actVal : actionsArray)
        m.actions.push_back(parseAction(actVal.toObject()));

    return m;
}

// ---------------------------------------------------------------------------
// manifestRunnableHere
// ---------------------------------------------------------------------------

bool manifestRunnableHere(PluginManifest const& manifest,
                          QString const& platform,
                          QString const& appVer) {
    // --- OS gate ---
    // If the OS array is non-empty, at least one entry must match the requested platform.
    // Exception: LOCKED Linux OS-accept policy (Assumption A1 from 18-RESEARCH.md /
    // Open Question 1): vendor manifests never list "linux", so a strict match would
    // reject every real plugin on our primary OS. When platform == "linux" and no "linux"
    // entry exists anywhere in the array, we accept the plugin (best-effort; the SPAWN
    // step checks CodePath existence as the real gate).
    if (!manifest.os.empty()) {
        bool osMatch = false;
        bool hasLinuxEntry = false;

        for (auto const& osReq : manifest.os) {
            if (osReq.platform == platform) {
                osMatch = true;
                break;
            }
            if (osReq.platform == QStringLiteral("linux")) {
                hasLinuxEntry = true;
            }
        }

        if (!osMatch) {
            // Apply the Linux OS-accept rule: if we're on Linux AND there is no "linux"
            // entry in the array (vendor packages don't list it), treat as runnable.
            // LOCKED ACCEPT — documented in 18-RESEARCH.md Assumption A1 / Open Question 1.
            if (platform == QStringLiteral("linux") && !hasLinuxEntry) {
                // Fall through to the MinimumVersion check below (accept so far).
            } else {
                return false; // strict reject for non-Linux or when a linux entry exists
            }
        }
    }

    // --- Software.MinimumVersion gate ---
    // Uses QVersionNumber so "2.10" > "2.9" is handled correctly (string compare fails).
    if (!manifest.softwareMinimumVersion.isEmpty()) {
        QVersionNumber const required = QVersionNumber::fromString(manifest.softwareMinimumVersion);
        QVersionNumber const running = QVersionNumber::fromString(appVer);
        if (running < required)
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// currentPlatformString
// ---------------------------------------------------------------------------

QString currentPlatformString() {
#if defined(Q_OS_MACOS)
    return QStringLiteral("mac");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

} // namespace ajazz::app
