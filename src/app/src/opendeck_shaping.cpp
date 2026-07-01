// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file opendeck_shaping.cpp
 * @brief Pure JSON shaping for the OpenDeck bridge (our types -> OpenDeck
 *        shapes). Split out from opendeck_bridge.cpp so it links against only
 *        Qt Core/JSON + the header-only core structs and is unit-testable
 *        without the heavy backend QObjects.
 */
#include "ajazz/core/action_instance.hpp"
#include "ajazz/core/capabilities.hpp"
#include "opendeck_bridge.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace ajazz::app::opendeck_detail {

namespace {

QString rgbHex(core::Rgb const& c) {
    return QString::asprintf("#%02x%02x%02x", c.r, c.g, c.b);
}

/// Best-effort: parse an escaped-JSON settings string into a value; falls back
/// to an empty object when empty or not valid JSON.
QJsonValue parseSettings(std::string const& settings) {
    if (settings.empty()) {
        return QJsonObject{};
    }
    QJsonParseError err{};
    QJsonDocument const doc = QJsonDocument::fromJson(QByteArray::fromStdString(settings), &err);
    if (err.error != QJsonParseError::NoError) {
        return QJsonObject{};
    }
    if (doc.isObject()) {
        return doc.object();
    }
    if (doc.isArray()) {
        return doc.array();
    }
    return QJsonObject{};
}

/// Shared shaping for a bound control: builds the OpenDeck ActionInstance from
/// the instance (preferred) or the legacy chain + visual state.
QJsonValue instanceJson(std::optional<core::ActionInstance> const& instance,
                        std::vector<core::Action> const& primaryChain,
                        core::KeyState const& legacyState,
                        QString const& context,
                        QStringList const& controllers) {
    bool const hasInstance = instance.has_value() && !instance->id.empty();
    bool const hasChain = !primaryChain.empty();
    bool const hasVisual = legacyState.text.has_value() || legacyState.imagePath.has_value();
    if (!hasInstance && !hasChain && !hasVisual) {
        return QJsonValue(QJsonValue::Null);
    }

    QString id;
    QString settingsStr;
    QString label;
    QJsonArray states;
    int currentState = 0;

    if (hasInstance) {
        id = QString::fromStdString(instance->id);
        settingsStr = QString::fromStdString(instance->settings);
        currentState = static_cast<int>(instance->currentState);
        for (core::ActionState const& st : instance->states) {
            states.append(actionStateJson(st.visual));
        }
    }
    if (hasChain) {
        core::Action const& first = primaryChain.front();
        if (id.isEmpty() && first.kind == core::ActionKind::Plugin) {
            id = QString::fromStdString(first.id);
        }
        if (settingsStr.isEmpty()) {
            settingsStr = QString::fromStdString(first.settingsJson);
        }
        label = QString::fromStdString(first.label);
    }
    if (states.isEmpty()) {
        states.append(actionStateJson(legacyState));
    }

    QString const actionName = label.isEmpty() ? id : label;
    QJsonObject inst;
    inst[QStringLiteral("action")] =
        makeActionJson(actionName, id, QString{}, QString{}, QString{}, QString{}, controllers);
    inst[QStringLiteral("context")] = context;
    inst[QStringLiteral("states")] = states;
    inst[QStringLiteral("current_state")] = currentState;
    inst[QStringLiteral("settings")] = parseSettings(settingsStr.toStdString());
    inst[QStringLiteral("children")] = QJsonValue(QJsonValue::Null);
    return inst;
}

} // namespace

QString jsonToString(QJsonValue const& value) {
    if (value.isObject()) {
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    }
    if (value.isArray()) {
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    }
    QJsonArray wrap;
    wrap.append(value);
    QByteArray bytes = QJsonDocument(wrap).toJson(QJsonDocument::Compact);
    if (bytes.size() >= 2) {
        bytes = bytes.mid(1, bytes.size() - 2);
    }
    return QString::fromUtf8(bytes);
}

QJsonObject deviceInfoJson(QString const& codename, QVariantMap const& caps) {
    int const keyCount = caps.value(QStringLiteral("keyCount")).toInt();
    int const encoders = caps.value(QStringLiteral("encoderCount")).toInt();
    // Elgato device "type": 7 = Stream Deck +, 1 = Mini, 2 = XL, 0 = standard.
    // Only used by the UI for layout hints; rows/columns drive the grid.
    int type = 0;
    if (encoders > 0) {
        type = 7;
    } else if (keyCount > 0 && keyCount <= 6) {
        type = 1;
    } else if (keyCount >= 32) {
        type = 2;
    }
    return QJsonObject{
        {QStringLiteral("id"), codename},
        {QStringLiteral("name"), caps.value(QStringLiteral("model")).toString()},
        {QStringLiteral("rows"), caps.value(QStringLiteral("keyRows")).toInt()},
        {QStringLiteral("columns"), caps.value(QStringLiteral("gridColumns")).toInt()},
        {QStringLiteral("encoders"), encoders},
        {QStringLiteral("touchpoints"), caps.value(QStringLiteral("touchZoneCount")).toInt()},
        {QStringLiteral("type"), type},
    };
}

QJsonObject actionStateJson(core::KeyState const& state) {
    return QJsonObject{
        {QStringLiteral("image"),
         state.imagePath ? QString::fromStdString(*state.imagePath) : QString{}},
        {QStringLiteral("image_scale"), 100},
        {QStringLiteral("background_colour"),
         state.background ? rgbHex(*state.background) : QStringLiteral("#000000")},
        {QStringLiteral("name"), QString{}},
        {QStringLiteral("text"), state.text ? QString::fromStdString(*state.text) : QString{}},
        {QStringLiteral("show"), true},
        {QStringLiteral("colour"),
         state.foreground ? rgbHex(*state.foreground) : QStringLiteral("#f2f2f2")},
        {QStringLiteral("stroke_colour"), QStringLiteral("#000000")},
        {QStringLiteral("alignment"), QStringLiteral("middle")},
        {QStringLiteral("family"), QStringLiteral("Liberation Sans")},
        {QStringLiteral("style"), QStringLiteral("Regular")},
        {QStringLiteral("size"), static_cast<int>(state.fontSize)},
        {QStringLiteral("stroke_size"), 0},
        {QStringLiteral("underline"), false},
    };
}

QJsonObject makeActionJson(QString const& name,
                           QString const& uuid,
                           QString const& plugin,
                           QString const& tooltip,
                           QString const& icon,
                           QString const& propertyInspector,
                           QStringList const& controllers) {
    QJsonArray controllersArr;
    for (QString const& c : controllers) {
        controllersArr.append(c);
    }
    QJsonArray states;
    states.append(actionStateJson(core::KeyState{}));
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("uuid"), uuid},
        {QStringLiteral("plugin"), plugin},
        {QStringLiteral("tooltip"), tooltip},
        {QStringLiteral("icon"), icon},
        {QStringLiteral("visible_in_action_list"), true},
        {QStringLiteral("supported_in_multi_actions"), true},
        {QStringLiteral("property_inspector"), propertyInspector},
        {QStringLiteral("controllers"), controllersArr},
        {QStringLiteral("states"), states},
    };
}

QJsonObject actionFromCatalogEntry(QVariantMap const& entry) {
    QStringList controllers;
    for (QVariant const& c : entry.value(QStringLiteral("controllers")).toList()) {
        controllers.append(c.toString());
    }
    if (controllers.isEmpty()) {
        controllers.append(QStringLiteral("Keypad"));
    }
    QJsonObject action =
        makeActionJson(entry.value(QStringLiteral("actionName")).toString(),
                       entry.value(QStringLiteral("actionId")).toString(),
                       entry.value(QStringLiteral("pluginName")).toString(),
                       QString{},
                       entry.value(QStringLiteral("icon")).toString(),
                       entry.value(QStringLiteral("propertyInspectorPath")).toString(),
                       controllers);
    action[QStringLiteral("visible_in_action_list")] =
        entry.value(QStringLiteral("visibleInActionsList"), true).toBool();
    return action;
}

QJsonObject categoriesJson(QVariantList const& installedActions) {
    QJsonObject categories;

    // When the real OpenDeck "starterpack" plugin is installed it provides the
    // genuine Run Command / Open URL / Switch Profile / Device Brightness /
    // Simulate Input actions (with working backends + property inspectors) under
    // its own category. In that case we must NOT also synthesize the placeholder
    // built-ins below, or the action list shows duplicates. The synthesized stubs
    // remain only as a fallback for installs without the starterpack.
    bool starterpackInstalled = false;
    for (QVariant const& v : installedActions) {
        if (v.toMap()
                .value(QStringLiteral("pluginUuid"))
                .toString()
                .startsWith(QStringLiteral("com.amansprojects.starterpack"))) {
            starterpackInstalled = true;
            break;
        }
    }

    QJsonArray builtins;
    // Icon strings use the `opendeck/<path>` form: OpenDeck's getImage()/ActionList
    // strip the `opendeck` prefix to a root-relative `/<path>` that resolves against
    // the opendeck://app/ origin and is served by OpenDeckSchemeHandler from the
    // bundled SPA qrc (:/opendeck/<path>). Multi/Toggle Action use OpenDeck's own
    // bundled multi-action.png / toggle-action.png (src-tauri shared.rs CATEGORIES).
    // The remaining four are OpenDeck "starterpack" plugin actions upstream; their
    // dedicated icons ship in the bundled starterpack plugin's asset dir. CMake
    // copies those PNGs into :/opendeck alongside the SPA (see AJAZZ_BUILD_WEBUI),
    // so the same `opendeck/<file>.png` form resolves them via OpenDeckSchemeHandler.
    auto addBuiltin = [&](QString const& name,
                          QString const& uuid,
                          QString const& tooltip,
                          QString const& icon,
                          QStringList const& controllers) {
        builtins.append(makeActionJson(
            name, uuid, QStringLiteral("opendeck"), tooltip, icon, QString{}, controllers));
    };
    addBuiltin(QStringLiteral("Multi Action"),
               QStringLiteral("opendeck.multiaction"),
               QStringLiteral("Run several actions in sequence"),
               QStringLiteral("opendeck/multi-action.png"),
               {QStringLiteral("Keypad")});
    addBuiltin(QStringLiteral("Toggle Action"),
               QStringLiteral("opendeck.toggleaction"),
               QStringLiteral("Toggle between actions"),
               QStringLiteral("opendeck/toggle-action.png"),
               {QStringLiteral("Keypad")});
    if (!starterpackInstalled) {
        addBuiltin(QStringLiteral("Run Command"),
                   QStringLiteral("opendeck.runcommand"),
                   QStringLiteral("Run a shell command"),
                   QStringLiteral("opendeck/runCommand.png"),
                   {QStringLiteral("Keypad"), QStringLiteral("Encoder")});
        addBuiltin(QStringLiteral("Open URL"),
                   QStringLiteral("opendeck.openurl"),
                   QStringLiteral("Open a URL in the browser"),
                   QStringLiteral("opendeck/openUrl.png"),
                   {QStringLiteral("Keypad"), QStringLiteral("Encoder")});
        addBuiltin(QStringLiteral("Switch Profile"),
                   QStringLiteral("opendeck.switchprofile"),
                   QStringLiteral("Switch the active profile"),
                   QStringLiteral("opendeck/switchProfile.png"),
                   {QStringLiteral("Keypad"), QStringLiteral("Encoder")});
        addBuiltin(QStringLiteral("Device Brightness"),
                   QStringLiteral("opendeck.brightness"),
                   QStringLiteral("Set the device brightness"),
                   QStringLiteral("opendeck/deviceBrightness.png"),
                   {QStringLiteral("Keypad"), QStringLiteral("Encoder")});
    }
    // OpenDeck's ActionList expects each category VALUE to be an object
    // `{ icon?, actions: Action[] }` (it destructures `{ actions }` and reads
    // `actions.length`), NOT a bare Action[]. Wrap every group accordingly.
    categories[QStringLiteral("OpenDeck")] = QJsonObject{{QStringLiteral("actions"), builtins}};

    for (QVariant const& v : installedActions) {
        QVariantMap const entry = v.toMap();
        QString group = entry.value(QStringLiteral("pluginName")).toString();
        if (group.isEmpty()) {
            group = QStringLiteral("Plugins");
        }
        QJsonArray arr =
            categories.value(group).toObject().value(QStringLiteral("actions")).toArray();
        arr.append(actionFromCatalogEntry(entry));
        categories[group] = QJsonObject{{QStringLiteral("actions"), arr}};
    }
    return categories;
}

QJsonValue keyInstanceJson(core::Binding const& binding, QString const& context) {
    return instanceJson(
        binding.instance, binding.onPress, binding.state, context, {QStringLiteral("Keypad")});
}

QJsonValue encoderInstanceJson(core::EncoderBinding const& binding, QString const& context) {
    std::vector<core::Action> const& primary =
        !binding.onPress.empty() ? binding.onPress : binding.onCw;
    return instanceJson(
        binding.instance, primary, binding.state, context, {QStringLiteral("Encoder")});
}

/// Touch-strip zone -> OpenDeck ActionInstance (controller "Keypad"; touch
/// zones have no encoder/instance, only an onTap chain + visual state).
QJsonValue touchInstanceJson(core::TouchZoneBinding const& binding, QString const& context) {
    return instanceJson(
        std::nullopt, binding.onTap, binding.state, context, {QStringLiteral("Keypad")});
}

QJsonObject
profileJson(core::Profile const& profile, int keyCount, int encoderCount, int touchCount) {
    QString const device = QString::fromStdString(profile.deviceCodename);
    // The web UI uses the profile NAME as the dropdown id (get_profiles returns
    // names), so present the name here too. The real uuid (profile.id) is the
    // bridge's internal handle and is reconciled in the write-path phase.
    QString const id = QString::fromStdString(profile.name);

    QJsonArray keys;
    for (int i = 0; i < keyCount; ++i) {
        auto const it = profile.keys.find(static_cast<std::uint16_t>(i));
        QString const ctx =
            device + QStringLiteral(".") + id + QStringLiteral(".Keypad.") + QString::number(i);
        keys.append(it != profile.keys.end() ? keyInstanceJson(it->second, ctx)
                                             : QJsonValue(QJsonValue::Null));
    }
    // Touch-strip zones are appended AFTER the keypad in keys[], at positions
    // keyCount..keyCount+touchCount-1, controller "Keypad" — exactly how
    // OpenDeck's DeviceView reads them (`profile.keys[(rows*cols)+i]`).
    for (int i = 0; i < touchCount; ++i) {
        auto const it = profile.touchZones.find(static_cast<std::uint8_t>(i));
        int const pos = keyCount + i;
        QString const ctx =
            device + QStringLiteral(".") + id + QStringLiteral(".Keypad.") + QString::number(pos);
        keys.append(it != profile.touchZones.end() ? touchInstanceJson(it->second, ctx)
                                                   : QJsonValue(QJsonValue::Null));
    }
    QJsonArray sliders;
    for (int i = 0; i < encoderCount; ++i) {
        auto const it = profile.encoders.find(static_cast<std::uint16_t>(i));
        QString const ctx =
            device + QStringLiteral(".") + id + QStringLiteral(".Encoder.") + QString::number(i);
        sliders.append(it != profile.encoders.end() ? encoderInstanceJson(it->second, ctx)
                                                    : QJsonValue(QJsonValue::Null));
    }
    return QJsonObject{
        {QStringLiteral("device"), device},
        {QStringLiteral("id"), id},
        {QStringLiteral("keys"), keys},
        {QStringLiteral("sliders"), sliders},
    };
}

bool overrideStateVisual(QJsonObject& instance,
                         QString const& imageDataUri,
                         QString const& title,
                         bool titleChanged) {
    int const cs = instance.value(QStringLiteral("current_state")).toInt(0);
    QJsonArray states = instance.value(QStringLiteral("states")).toArray();
    if (cs < 0 || cs >= states.size()) {
        return false;
    }
    QJsonObject state = states.at(cs).toObject();
    if (!imageDataUri.isEmpty()) {
        state.insert(QStringLiteral("image"), imageDataUri);
    }
    if (titleChanged) {
        state.insert(QStringLiteral("text"), title);
    }
    states.replace(cs, state);
    instance.insert(QStringLiteral("states"), states);
    return true;
}

QJsonObject settingsWithDefaults(QJsonObject const& stored) {
    QJsonObject out{
        {QStringLiteral("language"), QStringLiteral("en")},
        {QStringLiteral("rotation"), 0},
        {QStringLiteral("brightness"), 50},
    };
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        out.insert(it.key(), it.value());
    }
    return out;
}

} // namespace ajazz::app::opendeck_detail
