// SPDX-License-Identifier: GPL-3.0-or-later
#include "opendeck_bridge.hpp"

#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"
#include "device_model.hpp"
#include "plugin_catalog_model.hpp"
#include "profile_controller.hpp"
#include "stream_dock_control_service.hpp"
#include "stream_dock_input_service.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QTemporaryFile>
#include <QUrl>

namespace ajazz::app {

namespace {
Q_LOGGING_CATEGORY(lcBridge, "ajazz.opendeck.bridge")

/// Copy an installed-action catalog entry's metadata onto a bound instance's
/// `action` object. A committed binding stores only the action UUID, so the
/// reshaped instance (instanceJson) carries an action with empty plugin / icon /
/// property_inspector / controllers. Without these the SPA renders no icon and —
/// critically — never builds the Property Inspector iframe (PropertyInspectorView
/// gates on `instance.action.property_inspector`). Re-attach them from the
/// catalog so a dropped action shows its real settings.
void applyActionMeta(QJsonObject& action, QVariantMap const& entry) {
    QString const icon = entry.value(QStringLiteral("icon")).toString();
    action[QStringLiteral("plugin")] = entry.value(QStringLiteral("pluginUuid")).toString();
    action[QStringLiteral("icon")] = icon;
    action[QStringLiteral("property_inspector")] =
        entry.value(QStringLiteral("propertyInspectorPath")).toString();
    if (action.value(QStringLiteral("name")).toString().isEmpty()) {
        action[QStringLiteral("name")] = entry.value(QStringLiteral("actionName")).toString();
    }
    QJsonArray controllers;
    for (QVariant const& c : entry.value(QStringLiteral("controllers")).toList()) {
        controllers.append(c.toString());
    }
    if (!controllers.isEmpty()) {
        action[QStringLiteral("controllers")] = controllers;
    }
    // Default the action's per-state fallback image to the icon. The SPA renders a
    // bound key as `getImage(instanceState.image, action.states[s].image ?? action.icon)`;
    // our reshaped action carries states with an EMPTY-STRING image, and JS `?? `
    // treats "" as a set value, so the `?? action.icon` fallback never fires and the
    // key paints the /alert.png placeholder. Seed each empty state image with the
    // icon so a freshly-dropped action shows its real icon on the key/dial.
    if (!icon.isEmpty()) {
        QJsonArray states = action.value(QStringLiteral("states")).toArray();
        if (states.isEmpty()) {
            states.append(QJsonObject{});
        }
        for (qsizetype i = 0; i < states.size(); ++i) {
            QJsonObject state = states.at(i).toObject();
            if (state.value(QStringLiteral("image")).toString().isEmpty()) {
                state[QStringLiteral("image")] = icon;
            }
            states[i] = state;
        }
        action[QStringLiteral("states")] = states;
    }
}

/// Parsed OpenDeck context (`device.profile.controller.position`).
struct Ctx {
    QString device;
    QString profile;
    QString controller;
    int position = -1;
    bool valid = false;
};

/// Parse a context STRING. Parsed from the right (position last, controller
/// second-last, device first) so a profile name containing '.' is tolerated.
Ctx parseCtxString(QString const& s) {
    Ctx c;
    QStringList parts = s.split(QLatin1Char('.'));
    if (parts.size() < 4) {
        return c;
    }
    bool ok = false;
    c.position = parts.takeLast().toInt(&ok);
    c.controller = parts.takeLast();
    c.device = parts.takeFirst();
    c.profile = parts.join(QLatin1Char('.'));
    c.valid = ok && c.position >= 0 && !c.controller.isEmpty();
    return c;
}

/// Parse a context VALUE: a Context object {device,profile,controller,position}
/// (create_instance/move_instance) or the dotted string form (everything else).
Ctx parseCtxValue(QJsonValue const& v) {
    if (v.isObject()) {
        QJsonObject const o = v.toObject();
        Ctx c;
        c.device = o.value(QStringLiteral("device")).toString();
        c.profile = o.value(QStringLiteral("profile")).toString();
        c.controller = o.value(QStringLiteral("controller")).toString();
        c.position = o.value(QStringLiteral("position")).toInt(-1);
        c.valid = c.position >= 0 && !c.controller.isEmpty();
        return c;
    }
    return parseCtxString(v.toString());
}
} // namespace

OpenDeckBridge::OpenDeckBridge(DeviceModel* devices,
                               ProfileController* profiles,
                               PluginCatalogModel* catalog,
                               QObject* parent)
    : QObject(parent), m_devices(devices), m_profiles(profiles), m_catalog(catalog) {}

OpenDeckBridge::~OpenDeckBridge() = default;

QString OpenDeckBridge::handle(QString const& command, QString const& argsJson) {
    using namespace opendeck_detail;
    QJsonObject const args = QJsonDocument::fromJson(argsJson.toUtf8()).object();
    auto const str = [](QJsonValue const& v) { return jsonToString(v); };

    // --- bootstrap / static -------------------------------------------------
    if (command == QLatin1String("get_build_info")) {
        return str(QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(),
                                               QCoreApplication::applicationVersion()));
    }
    if (command == QLatin1String("get_port_base")) {
        return str(57116); // TODO(phase): real plugin server base port once wired
    }
    if (command == QLatin1String("get_fonts")) {
        QJsonArray fonts;
        for (QString const& f : {QStringLiteral("Liberation Sans"),
                                 QStringLiteral("Liberation Serif"),
                                 QStringLiteral("Archivo Black"),
                                 QStringLiteral("Anton"),
                                 QStringLiteral("Comic Neue"),
                                 QStringLiteral("Courier Prime"),
                                 QStringLiteral("Tinos"),
                                 QStringLiteral("Open Sans"),
                                 QStringLiteral("Fira Sans")}) {
            fonts.append(f);
        }
        return str(fonts);
    }
    if (command == QLatin1String("get_settings")) {
        QSettings settings;
        QJsonObject const stored =
            QJsonDocument::fromJson(
                settings.value(QStringLiteral("opendeck/settings")).toString().toUtf8())
                .object();
        return str(settingsWithDefaults(stored));
    }
    if (command == QLatin1String("set_settings")) {
        // OpenDeck sends {settings:{...}} (fall back to the raw object defensively).
        QJsonValue const v = args.value(QStringLiteral("settings"));
        QJsonObject const incoming = v.isObject() ? v.toObject() : args;
        QSettings settings;
        settings.setValue(
            QStringLiteral("opendeck/settings"),
            QString::fromUtf8(QJsonDocument(incoming).toJson(QJsonDocument::Compact)));
        return str(QJsonValue(QJsonValue::Null));
    }
    if (command == QLatin1String("get_localisations") || command == QLatin1String("make_info") ||
        command == QLatin1String("get_application_profiles")) {
        return str(QJsonObject{});
    }
    if (command == QLatin1String("set_application_profiles")) {
        // TODO(opendeck-ui): no per-app profile backing yet — accept + ignore so
        // the SPA's app-profile writes don't trip the unhandled-command warning.
        // Pairs with the get_application_profiles {} stub above.
        return str(QJsonValue(QJsonValue::Null));
    }
    if (command == QLatin1String("get_applications")) {
        return str(QJsonArray{});
    }
    if (command == QLatin1String("list_plugins")) {
        // Group our installed actions by plugin -> OpenDeck's installed-plugin list
        // (PluginManager + ActionList read id/name/icon).
        QMap<QString, QJsonObject> byPlugin;
        if (m_catalog != nullptr) {
            for (QVariant const& v : m_catalog->installedActions()) {
                QVariantMap const a = v.toMap();
                QString const id = a.value(QStringLiteral("pluginUuid")).toString();
                if (id.isEmpty() || byPlugin.contains(id)) {
                    continue;
                }
                // Plugin-level icon (manifest top-level Icon/CategoryIcon),
                // inlined as a data: URI. The per-action `icon` is a file://
                // URL that does not load cross-origin in the SPA's
                // `opendeck://app/` webview, and a plugin frequently has no
                // first-action icon at all (e.g. Battery -> icon:"") — so
                // resolve the plugin's own icon to the SPA-loadable data: form.
                QString icon = m_catalog->pluginIconDataUri(id);
                if (icon.isEmpty()) {
                    icon = a.value(QStringLiteral("icon")).toString();
                }
                // `version` + `registered` feed OpenDeck's PluginManager
                // subtitle + connected-state dot; without them the SPA renders
                // "undefined" and a permanent disconnected warning. Everything
                // list_plugins reports comes from installedActions(), which only
                // lists plugins with a runnable action on this OS -> registered.
                byPlugin.insert(id,
                                QJsonObject{{QStringLiteral("id"), id},
                                            {QStringLiteral("name"),
                                             a.value(QStringLiteral("pluginName")).toString()},
                                            {QStringLiteral("version"),
                                             a.value(QStringLiteral("pluginVersion")).toString()},
                                            {QStringLiteral("registered"), true},
                                            {QStringLiteral("icon"), icon}});
            }
        }
        QJsonArray out;
        for (QJsonObject const& p : byPlugin) {
            out.append(p);
        }
        return str(out);
    }
    if (command == QLatin1String("remove_plugin")) {
        // `id` is the install-dir name that list_plugins emits (the disk-backed
        // `pluginUuid`), so route to removeInstalledPlugin (which deletes the
        // dir + clears bindings), NOT uninstall() (which keys off the catalogue
        // uuid and leaves the dir on disk -> the disk-backed list never updates).
        QString const id = args.value(QStringLiteral("id")).toString();
        if (m_catalog != nullptr && !id.isEmpty()) {
            m_catalog->removeInstalledPlugin(id);
            emit event(QStringLiteral("plugin_reloaded"), QStringLiteral("{}"));
        }
        return str(QJsonValue(QJsonValue::Null));
    }
    if (command == QLatin1String("install_plugin")) {
        // A local file / file:// path installs synchronously here; an http(s) URL
        // is downloaded asynchronously in invoke() (handled before this seam).
        QString const file = args.value(QStringLiteral("file")).toString();
        QString const url = args.value(QStringLiteral("url")).toString();
        QString const path = !file.isEmpty()                          ? file
                             : url.startsWith(QLatin1String("file:")) ? url
                                                                      : QString{};
        if (m_catalog != nullptr && !path.isEmpty()) {
            m_catalog->installFromFile(path, /*userConfirmedUnsigned=*/true);
            emit event(QStringLiteral("plugin_reloaded"), QStringLiteral("{}"));
        }
        return str(QJsonValue(QJsonValue::Null));
    }

    // --- live backend -------------------------------------------------------
    if (command == QLatin1String("get_devices")) {
        // OpenDeck's frontend keys `devices` by id (`Object.keys(devices)`,
        // `devices[id]`), so this is a MAP {id: DeviceInfo}, not an array.
        QJsonObject devices;
        if (m_devices != nullptr) {
            for (QString const& codename : m_devices->connectedCodenames()) {
                QVariantMap const caps = m_devices->capabilitiesFor(codename);
                // Only stream controllers are editable in this UI. Skip devices
                // with no editable surface (mice/keyboards: 0 keys/encoders/
                // zones) so the SPA's first-device auto-select can't land on a
                // non-grid device and render an empty canvas.
                bool const editable = caps.value(QStringLiteral("keyCount")).toInt() > 0 ||
                                      caps.value(QStringLiteral("encoderCount")).toInt() > 0 ||
                                      caps.value(QStringLiteral("touchZoneCount")).toInt() > 0;
                if (!editable) {
                    continue;
                }
                devices.insert(codename, deviceInfoJson(codename, caps));
            }
        }
        return str(devices);
    }
    if (command == QLatin1String("get_categories")) {
        QVariantList const installed =
            m_catalog != nullptr ? m_catalog->installedActions() : QVariantList{};
        return str(categoriesJson(installed));
    }
    if (command == QLatin1String("get_profiles")) {
        QJsonArray names;
        if (m_profiles != nullptr) {
            QString const device = args.value(QStringLiteral("device")).toString();
            for (QVariant const& v : m_profiles->profilesForDevice(device)) {
                names.append(v.toMap().value(QStringLiteral("name")).toString());
            }
        }
        return str(names);
    }
    if (command == QLatin1String("get_selected_profile")) {
        if (m_profiles == nullptr) {
            return str(QJsonValue(QJsonValue::Null));
        }
        core::Profile const& profile = m_profiles->activeProfile();
        QString device = args.value(QStringLiteral("device")).toString();
        if (device.isEmpty()) {
            device = QString::fromStdString(profile.deviceCodename);
        }
        QVariantMap const caps =
            m_devices != nullptr ? m_devices->capabilitiesFor(device) : QVariantMap{};
        int const keyCount = caps.value(QStringLiteral("keyCount")).toInt();
        int const encoderCount = caps.value(QStringLiteral("encoderCount")).toInt();
        int const touchCount = caps.value(QStringLiteral("touchZoneCount")).toInt();
        QJsonObject prof = profileJson(profile, keyCount, encoderCount, touchCount);
        markOrphanedInstances(prof);
        return str(prof);
    }

    // --- write path ---------------------------------------------------------
    auto keyCountOf = [this](QString const& device) {
        return m_devices != nullptr
                   ? m_devices->capabilitiesFor(device).value(QStringLiteral("keyCount")).toInt()
                   : 0;
    };

    if (command == QLatin1String("create_instance")) {
        if (m_profiles == nullptr) {
            return str(QJsonValue(QJsonValue::Null));
        }
        Ctx const c = parseCtxValue(args.value(QStringLiteral("context")));
        // The SPA sends `action` as the full Action OBJECT (DeviceView.handleDrop:
        // JSON.parse(dataTransfer.getData("action"))), so read its `uuid` — a bare
        // .toString() on an object yields "" and silently dropped every bind
        // (drag-drop onto a key "had no effect"). Tolerate a plain-string id too.
        QJsonValue const actionVal = args.value(QStringLiteral("action"));
        QString const actionId = actionVal.isObject()
                                     ? actionVal.toObject().value(QStringLiteral("uuid")).toString()
                                     : actionVal.toString();
        if (!c.valid || actionId.isEmpty()) {
            return str(QJsonValue(QJsonValue::Null));
        }
        int const keyCount = keyCountOf(c.device);
        // ActionKind::Plugin == 0; library actions are committed as plugin steps.
        if (c.controller == QLatin1String("Encoder")) {
            m_profiles->commitEncoderBinding(c.position, {}, {}, 0, QStringLiteral("{}"), actionId);
        } else if (c.position >= keyCount) {
            m_profiles->commitTouchZoneBinding(
                c.position - keyCount, {}, {}, 0, QStringLiteral("{}"), actionId);
        } else {
            m_profiles->commitKeyBinding(c.position, {}, {}, 0, QStringLiteral("{}"), actionId);
        }
        // Shape the freshly-committed binding back as the ActionInstance the SPA
        // assigns into its slot.
        core::Profile const& p = m_profiles->activeProfile();
        QString const ctx = c.device + QStringLiteral(".") + c.profile + QStringLiteral(".") +
                            c.controller + QStringLiteral(".") + QString::number(c.position);
        if (c.controller == QLatin1String("Encoder")) {
            auto const it = p.encoders.find(static_cast<std::uint16_t>(c.position));
            return str(it != p.encoders.end() ? enrichInstance(encoderInstanceJson(it->second, ctx))
                                              : QJsonValue(QJsonValue::Null));
        }
        if (c.position >= keyCount) {
            auto const it = p.touchZones.find(static_cast<std::uint8_t>(c.position - keyCount));
            return str(it != p.touchZones.end() ? enrichInstance(touchInstanceJson(it->second, ctx))
                                                : QJsonValue(QJsonValue::Null));
        }
        auto const it = p.keys.find(static_cast<std::uint16_t>(c.position));
        return str(it != p.keys.end() ? enrichInstance(keyInstanceJson(it->second, ctx))
                                      : QJsonValue(QJsonValue::Null));
    }

    if (command == QLatin1String("remove_instance")) {
        if (m_profiles == nullptr) {
            return str(QJsonValue(QJsonValue::Null));
        }
        Ctx const c = parseCtxValue(args.value(QStringLiteral("context")));
        if (c.valid) {
            // Mirror create_instance's surface routing: Encoder -> encoder slot;
            // a Keypad context past the key count addresses a touch-strip zone;
            // otherwise a real keypad key.
            int const keyCount = keyCountOf(c.device);
            if (c.controller == QLatin1String("Encoder")) {
                m_profiles->removeEncoderActionAt(c.position, 0);
            } else if (c.position >= keyCount) {
                m_profiles->removeTouchZoneActionAt(c.position - keyCount, 0);
            } else {
                m_profiles->removeKeyActionAt(c.position, 0);
            }
        }
        return str(QJsonValue(QJsonValue::Null));
    }

    if (command == QLatin1String("update_image")) {
        // The SPA renders each key client-side and streams the frame here; we
        // decode the data-URL and push it to the live device surface.
        Ctx const c = parseCtxValue(args.value(QStringLiteral("context")));
        if (m_control == nullptr || !c.valid) {
            return str(QJsonValue(QJsonValue::Null));
        }
        int const keyCount = keyCountOf(c.device);
        QJsonValue const imgVal = args.value(QStringLiteral("image"));
        if (imgVal.isNull() || imgVal.toString().isEmpty()) {
            if (c.controller == QLatin1String("Keypad") && c.position < keyCount) {
                m_control->clearKeyImage(static_cast<std::uint8_t>(c.position + 1));
            }
            return str(QJsonValue(QJsonValue::Null));
        }
        QString const dataUrl = imgVal.toString();
        qsizetype const comma = dataUrl.indexOf(QLatin1Char(','));
        QByteArray const raw =
            QByteArray::fromBase64((comma >= 0 ? dataUrl.mid(comma + 1) : dataUrl).toUtf8());
        QImage img;
        if (!img.loadFromData(raw)) {
            return str(QJsonValue(QJsonValue::Null));
        }
        if (c.controller == QLatin1String("Encoder")) {
            m_control->assignEncoderImage(static_cast<std::uint8_t>(c.position), img);
        } else if (c.position >= keyCount) {
            m_control->assignTouchStripZone(static_cast<std::uint8_t>(c.position - keyCount), img);
        } else {
            // assignKeyImage uses a 1-based key index.
            m_control->assignKeyImage(static_cast<std::uint8_t>(c.position + 1), img);
        }
        return str(QJsonValue(QJsonValue::Null));
    }

    if (command == QLatin1String("set_selected_profile")) {
        if (m_profiles != nullptr) {
            QString const device = args.value(QStringLiteral("device")).toString();
            QString const name = args.value(QStringLiteral("id")).toString();
            for (QVariant const& v : m_profiles->profilesForDevice(device)) {
                QVariantMap const m = v.toMap();
                if (m.value(QStringLiteral("name")).toString() == name) {
                    // Idempotence guard: selecting the already-active profile is a
                    // no-op. Without it this command re-emits profileChanged, which
                    // notifyProfileChanged() echoes back to the SPA as
                    // "switch_profile", whose DeviceSelector listener calls
                    // set_selected_profile again — a feedback loop that storms
                    // willAppear/setImage at millisecond cadence (observed 480
                    // set_selected_profile calls in 5 s once shim event dispatch
                    // was fixed). Mirrors the idempotent-switch guard in the
                    // per-app auto-switch path (resolved == activeProfileId -> skip).
                    QString const id = m.value(QStringLiteral("id")).toString();
                    if (id != m_profiles->activeProfileId()) {
                        m_profiles->loadProfileById(id);
                    }
                    break;
                }
            }
        }
        return str(QJsonValue(QJsonValue::Null));
    }

    if (command == QLatin1String("rename_profile")) {
        if (m_profiles != nullptr) {
            m_profiles->renameActiveProfile(args.value(QStringLiteral("newId")).toString());
        }
        return str(QJsonValue(QJsonValue::Null));
    }
    if (command == QLatin1String("delete_profile")) {
        if (m_profiles != nullptr) {
            // delete_profile passes the profile NAME (our presented id); resolve.
            QString const device = args.value(QStringLiteral("device")).toString();
            QString const name = args.value(QStringLiteral("profile")).toString();
            for (QVariant const& v : m_profiles->profilesForDevice(device)) {
                QVariantMap const m = v.toMap();
                if (m.value(QStringLiteral("name")).toString() == name) {
                    m_profiles->deleteProfile(m.value(QStringLiteral("id")).toString());
                    break;
                }
            }
        }
        return str(QJsonValue(QJsonValue::Null));
    }

    if (command == QLatin1String("move_instance")) {
        if (m_profiles == nullptr) {
            return str(QJsonValue(QJsonValue::Null));
        }
        Ctx const src = parseCtxValue(args.value(QStringLiteral("source")));
        Ctx const dst = parseCtxValue(args.value(QStringLiteral("destination")));
        bool const retain = args.value(QStringLiteral("retain")).toBool();
        // The SPA only issues a move onto an EMPTY destination (it early-returns
        // on an occupied slot), so a swap with the empty dst is a full-fidelity
        // move (src binding -> dst, src cleared). retain=true (copy/paste) has no
        // faithful primitive yet -> deferred (return null; the SPA no-ops on null).
        if (!src.valid || !dst.valid || retain || src.controller != dst.controller) {
            return str(QJsonValue(QJsonValue::Null));
        }
        int const keyCount = keyCountOf(dst.device);
        bool const srcTouch = src.position >= keyCount;
        bool const dstTouch = dst.position >= keyCount;
        if (src.controller == QLatin1String("Encoder")) {
            m_profiles->swapEncoderBindings(src.position, dst.position);
        } else if (srcTouch && dstTouch) {
            m_profiles->swapTouchZoneBindings(src.position - keyCount, dst.position - keyCount);
        } else if (!srcTouch && !dstTouch) {
            m_profiles->swapKeyBindings(src.position, dst.position);
        } else {
            return str(QJsonValue(QJsonValue::Null)); // keypad<->touch moves unsupported
        }
        emit event(QStringLiteral("rerender_images"), QStringLiteral("{}"));
        // Return the ActionInstance now at the destination (the SPA slots it in).
        core::Profile const& p = m_profiles->activeProfile();
        QString const ctx = dst.device + QStringLiteral(".") + dst.profile + QStringLiteral(".") +
                            dst.controller + QStringLiteral(".") + QString::number(dst.position);
        if (dst.controller == QLatin1String("Encoder")) {
            auto const it = p.encoders.find(static_cast<std::uint16_t>(dst.position));
            return str(it != p.encoders.end() ? enrichInstance(encoderInstanceJson(it->second, ctx))
                                              : QJsonValue(QJsonValue::Null));
        }
        if (dstTouch) {
            auto const it = p.touchZones.find(static_cast<std::uint8_t>(dst.position - keyCount));
            return str(it != p.touchZones.end() ? enrichInstance(touchInstanceJson(it->second, ctx))
                                                : QJsonValue(QJsonValue::Null));
        }
        auto const it = p.keys.find(static_cast<std::uint16_t>(dst.position));
        return str(it != p.keys.end() ? enrichInstance(keyInstanceJson(it->second, ctx))
                                      : QJsonValue(QJsonValue::Null));
    }

    // set_state: the InstanceEditor selected which state of a multi-state action
    // is live ({context, index}); make it current + persist + repaint.
    if (command == QLatin1String("set_state")) {
        if (m_profiles != nullptr) {
            Ctx const c = parseCtxValue(args.value(QStringLiteral("context")));
            if (c.valid) {
                m_profiles->setInstanceCurrentState(
                    c.controller, c.position, args.value(QStringLiteral("index")).toInt());
                emit event(QStringLiteral("rerender_images"), QStringLiteral("{}"));
            }
        }
        return str(QJsonValue(QJsonValue::Null));
    }

    // trigger_virtual_press: the SPA's "test this key" affordance. Inject a
    // synthetic press+release through the SAME dispatch path real hardware uses
    // (m_input->injectSyntheticEvent), so built-in actions AND the plugin host
    // fire exactly as on a physical press.
    if (command == QLatin1String("trigger_virtual_press")) {
        Ctx const c = parseCtxValue(args.value(QStringLiteral("context")));
        if (m_input != nullptr && c.valid) {
            bool const isEncoder = c.controller == QLatin1String("Encoder");
            core::DeviceEvent down{};
            core::DeviceEvent up{};
            if (isEncoder) {
                // Encoder index is 0-based on the wire (matches commitEncoderBinding).
                down.kind = core::DeviceEvent::Kind::EncoderPressed;
                up.kind = core::DeviceEvent::Kind::EncoderReleased;
                down.index = up.index = static_cast<std::uint16_t>(c.position);
            } else {
                // DeviceEvent key indices are 1-based; the context position is 0-based.
                down.kind = core::DeviceEvent::Kind::KeyPressed;
                up.kind = core::DeviceEvent::Kind::KeyReleased;
                down.index = up.index = static_cast<std::uint16_t>(c.position + 1);
            }
            m_input->injectSyntheticEvent(down);
            m_input->injectSyntheticEvent(up);
        }
        return str(QJsonValue(QJsonValue::Null));
    }

    // switch_property_inspector: a focus hint about which context's PI is open.
    // Our PI message routing is by-context (SdPluginServer::propertyInspectorSocketForContext),
    // not by-focus, so there is nothing to track here — intentional no-op.
    if (command == QLatin1String("switch_property_inspector")) {
        return str(QJsonValue(QJsonValue::Null));
    }

    // open_url: the SPA's "open external page / download latest release" button
    // (PluginDetails.svelte). Launch it in the user's real browser.
    if (command == QLatin1String("open_url")) {
        QString const url = args.value(QStringLiteral("url")).toString();
        if (!url.isEmpty()) {
            QDesktopServices::openUrl(QUrl(url));
        }
        return str(QJsonValue(QJsonValue::Null));
    }

    // Tauri dialog plugin (@tauri-apps/plugin-dialog). The OpenDeck SPA gates
    // plugin install/remove on ask()/confirm() and reports the outcome with
    // message(); install-from-file uses open(). Without these the confirm
    // resolved to null -> "!await ask(...)" was truthy -> install aborted before
    // ever calling install_plugin (the embedded-Plugins-tab "can't install" bug).
    // We back them with native Qt widgets (QApplication is already in use).
    if (command == QLatin1String("plugin:dialog|ask") ||
        command == QLatin1String("plugin:dialog|confirm")) {
        QString const title = args.value(QStringLiteral("title")).toString();
        QString const heading = title.isEmpty() ? QCoreApplication::applicationName() : title;
        bool const isConfirm = command.endsWith(QLatin1String("confirm"));
        // ask() -> Yes/No, confirm() -> OK/Cancel (Tauri semantics); both return bool.
        QMessageBox::StandardButtons const buttons = isConfirm
                                                         ? (QMessageBox::Ok | QMessageBox::Cancel)
                                                         : (QMessageBox::Yes | QMessageBox::No);
        QMessageBox::StandardButton const def = isConfirm ? QMessageBox::Ok : QMessageBox::Yes;
        QMessageBox::StandardButton const clicked = QMessageBox::question(
            nullptr, heading, args.value(QStringLiteral("message")).toString(), buttons, def);
        bool const accepted = (clicked == QMessageBox::Yes || clicked == QMessageBox::Ok);
        return str(QJsonValue(accepted));
    }
    if (command == QLatin1String("plugin:dialog|message")) {
        QString const title = args.value(QStringLiteral("title")).toString();
        QString const heading = title.isEmpty() ? QCoreApplication::applicationName() : title;
        QString const message = args.value(QStringLiteral("message")).toString();
        QString const kind = args.value(QStringLiteral("kind")).toString();
        // CRITICAL: this Tauri dialog plugin routes ask()/confirm() THROUGH
        // `plugin:dialog|message` with a `buttons` discriminator (verified live:
        // ask() arrives as buttons:"YesNo"), NOT a separate `plugin:dialog|ask`.
        // ask() is `(await message(...)) === 'Yes'` and confirm() is `=== 'Ok'`
        // (see @tauri-apps/plugin-dialog), so the command must return the clicked
        // button's LABEL STRING ("Yes"/"No"/"Ok"/"Cancel"), NOT a bool. Returning
        // a bool (or null) made the comparison always false -> the confirm read as
        // "No" -> every install aborted before calling install_plugin.
        QString const buttons = args.value(QStringLiteral("buttons")).toString();
        if (buttons == QLatin1String("YesNo") || buttons == QLatin1String("OkCancel")) {
            bool const okCancel = (buttons == QLatin1String("OkCancel"));
            QMessageBox::StandardButtons const b = okCancel
                                                       ? (QMessageBox::Ok | QMessageBox::Cancel)
                                                       : (QMessageBox::Yes | QMessageBox::No);
            QMessageBox::StandardButton const def = okCancel ? QMessageBox::Ok : QMessageBox::Yes;
            QMessageBox::StandardButton const clicked =
                QMessageBox::question(nullptr, heading, message, b, def);
            bool const accepted = (clicked == QMessageBox::Yes || clicked == QMessageBox::Ok);
            QString const label = okCancel
                                      ? (accepted ? QStringLiteral("Ok") : QStringLiteral("Cancel"))
                                      : (accepted ? QStringLiteral("Yes") : QStringLiteral("No"));
            return str(QJsonValue(label));
        }
        if (kind == QLatin1String("error")) {
            QMessageBox::critical(nullptr, heading, message);
        } else if (kind == QLatin1String("warning")) {
            QMessageBox::warning(nullptr, heading, message);
        } else {
            QMessageBox::information(nullptr, heading, message);
        }
        return str(QJsonValue(QJsonValue::Null));
    }
    if (command == QLatin1String("plugin:dialog|open")) {
        // Tauri v2 nests the options under "options"; tolerate a flat form too.
        QJsonObject opts = args.value(QStringLiteral("options")).toObject();
        if (opts.isEmpty()) {
            opts = args;
        }
        QString const title = opts.value(QStringLiteral("title")).toString();
        bool const directory = opts.value(QStringLiteral("directory")).toBool();
        bool const multiple = opts.value(QStringLiteral("multiple")).toBool();
        QString path;
        if (directory) {
            path = QFileDialog::getExistingDirectory(
                nullptr, title.isEmpty() ? QStringLiteral("Select a folder") : title);
        } else {
            path = QFileDialog::getOpenFileName(
                nullptr,
                title.isEmpty() ? QStringLiteral("Select a plugin file") : title,
                QString(),
                QStringLiteral(
                    "Stream Deck plugins (*.streamDeckPlugin *.sdPlugin *.zip);;All files (*)"));
        }
        if (path.isEmpty()) {
            return str(QJsonValue(QJsonValue::Null)); // user cancelled
        }
        // open() returns a string for multiple:false, an array for multiple:true.
        return multiple ? str(QJsonArray{path}) : str(QJsonValue(path));
    }

    qCWarning(lcBridge) << "unhandled command:" << command;
    return str(QJsonValue(QJsonValue::Null));
}

void OpenDeckBridge::notifyProfileChanged() {
    if (m_profiles == nullptr) {
        return;
    }
    QString const device = m_profiles->activeProfileId().isEmpty()
                               ? QString{}
                               : QString::fromStdString(m_profiles->activeProfile().deviceCodename);
    QJsonObject const payload{{QStringLiteral("device"), device},
                              {QStringLiteral("profile"), m_profiles->activeProfileName()}};
    emit event(QStringLiteral("switch_profile"), opendeck_detail::jsonToString(payload));
    emit event(QStringLiteral("rerender_images"), QStringLiteral("{}"));
}

void OpenDeckBridge::notifyLiveInstanceVisual(QString const& deviceId,
                                              QString const& controller,
                                              int position,
                                              QString const& imageDataUri,
                                              QString const& title,
                                              bool titleChanged) {
    if (m_profiles == nullptr || position < 0) {
        return;
    }
    core::Profile const& profile = m_profiles->activeProfile();
    if (QString::fromStdString(profile.deviceCodename) != deviceId) {
        return; // live paint for a device whose profile is not the active one
    }
    // Resolve the bound instance for the slot; keys and encoders live in
    // different profile maps but share the ActionInstance JSON shape (the SPA
    // renders sliders with the same Key component, context
    // device.profileName.Encoder.N — same form profileJson emits).
    QJsonValue instVal;
    QString ctx;
    if (controller == QLatin1String("Encoder")) {
        auto const it = profile.encoders.find(static_cast<std::uint16_t>(position));
        if (it == profile.encoders.end()) {
            return; // stale paint for an unbound dial
        }
        ctx = deviceId + QStringLiteral(".") + QString::fromStdString(profile.name) +
              QStringLiteral(".Encoder.") + QString::number(position);
        instVal = enrichInstance(opendeck_detail::encoderInstanceJson(it->second, ctx));
    } else {
        auto const it = profile.keys.find(static_cast<std::uint16_t>(position));
        if (it == profile.keys.end()) {
            return; // stale paint for an unbound slot
        }
        // Same context form the SPA's Key slots carry (profileJson: NAME as id).
        ctx = deviceId + QStringLiteral(".") + QString::fromStdString(profile.name) +
              QStringLiteral(".Keypad.") + QString::number(position);
        instVal = enrichInstance(opendeck_detail::keyInstanceJson(it->second, ctx));
    }
    if (!instVal.isObject()) {
        return;
    }
    QJsonObject contents = instVal.toObject();
    if (!opendeck_detail::overrideStateVisual(contents, imageDataUri, title, titleChanged)) {
        return; // current_state out of range — malformed instance, don't push
    }
    emit event(QStringLiteral("update_state"),
               opendeck_detail::jsonToString(QJsonObject{
                   {QStringLiteral("context"), ctx},
                   {QStringLiteral("contents"), contents},
               }));
}

void OpenDeckBridge::notifyDevicesChanged() {
    QJsonObject devices;
    if (m_devices != nullptr) {
        for (QString const& codename : m_devices->connectedCodenames()) {
            QVariantMap const caps = m_devices->capabilitiesFor(codename);
            bool const editable = caps.value(QStringLiteral("keyCount")).toInt() > 0 ||
                                  caps.value(QStringLiteral("encoderCount")).toInt() > 0 ||
                                  caps.value(QStringLiteral("touchZoneCount")).toInt() > 0;
            if (editable) {
                devices.insert(codename, opendeck_detail::deviceInfoJson(codename, caps));
            }
        }
    }
    emit event(QStringLiteral("devices"), opendeck_detail::jsonToString(devices));
}

void OpenDeckBridge::markOrphanedInstances(QJsonObject& profile) const {
    // Known action uuids = the six OpenDeck builtins + every currently-installed
    // action. Anything else bound in the profile is a stale/orphaned reference.
    QSet<QString> known{QStringLiteral("opendeck.multiaction"),
                        QStringLiteral("opendeck.toggleaction"),
                        QStringLiteral("opendeck.runcommand"),
                        QStringLiteral("opendeck.openurl"),
                        QStringLiteral("opendeck.switchprofile"),
                        QStringLiteral("opendeck.brightness")};
    // uuid -> catalog entry, built from a SINGLE installedActions() scan so the
    // per-slot enrichment below does not re-scan the plugin dir 18×.
    QHash<QString, QVariantMap> meta;
    if (m_catalog != nullptr) {
        for (QVariant const& v : m_catalog->installedActions()) {
            QVariantMap const e = v.toMap();
            QString const id = e.value(QStringLiteral("actionId")).toString();
            known.insert(id);
            meta.insert(id, e);
        }
    }

    auto annotate = [&known, &meta](QJsonArray const& in) {
        QJsonArray out;
        for (QJsonValue const& slotV : in) {
            if (!slotV.isObject()) {
                out.append(slotV); // empty slot (JSON null) — leave as-is.
                continue;
            }
            QJsonObject slot = slotV.toObject();
            QJsonObject action = slot.value(QStringLiteral("action")).toObject();
            QString const uuid = action.value(QStringLiteral("uuid")).toString();
            auto const metaIt = meta.constFind(uuid);
            if (metaIt != meta.constEnd()) {
                // Installed action: re-attach its plugin/icon/PI/controllers so the
                // bound slot renders its icon and opens its Property Inspector.
                applyActionMeta(action, metaIt.value());
                slot[QStringLiteral("action")] = action;
            } else if (!uuid.isEmpty() && !known.contains(uuid)) {
                QString const name = action.value(QStringLiteral("name")).toString();
                action[QStringLiteral("name")] =
                    QStringLiteral("%1 (plugin not installed)").arg(name.isEmpty() ? uuid : name);
                slot[QStringLiteral("action")] = action;
            }
            out.append(slot);
        }
        return out;
    };

    profile[QStringLiteral("keys")] = annotate(profile.value(QStringLiteral("keys")).toArray());
    profile[QStringLiteral("sliders")] =
        annotate(profile.value(QStringLiteral("sliders")).toArray());
}

QJsonValue OpenDeckBridge::enrichInstance(QJsonValue const& instance) const {
    if (!instance.isObject() || m_catalog == nullptr) {
        return instance;
    }
    QJsonObject inst = instance.toObject();
    QJsonObject action = inst.value(QStringLiteral("action")).toObject();
    QString const uuid = action.value(QStringLiteral("uuid")).toString();
    if (uuid.isEmpty()) {
        return instance;
    }
    for (QVariant const& v : m_catalog->installedActions()) {
        QVariantMap const e = v.toMap();
        if (e.value(QStringLiteral("actionId")).toString() != uuid) {
            continue;
        }
        applyActionMeta(action, e);
        inst[QStringLiteral("action")] = action;
        return inst;
    }
    return instance;
}

void OpenDeckBridge::invoke(QString const& requestId,
                            QString const& command,
                            QString const& argsJson) {
    // Trace every command the SPA sends (debug level — off by default, enable
    // with QT_LOGGING_RULES="ajazz.opendeck.bridge.debug=true"). The shim only
    // logs invokes when the SPA URL carries ?oddebug, so this is the reliable
    // way to see the exact command sequence behind a UX action (e.g. install).
    qCDebug(lcBridge) << "invoke" << command << argsJson;
    // install_plugin with an http(s) URL is the one async command: download the
    // archive, then install it off disk. Everything else resolves synchronously.
    if (command == QLatin1String("install_plugin") && m_catalog != nullptr) {
        QJsonObject const args = QJsonDocument::fromJson(argsJson.toUtf8()).object();
        QString const url = args.value(QStringLiteral("url")).toString();
        if (url.startsWith(QLatin1String("http"))) {
            installPluginFromUrl(requestId, url);
            return;
        }
    }
    emit invokeResponse(requestId, handle(command, argsJson), QString{});
}

void OpenDeckBridge::installPluginFromUrl(QString const& requestId, QString const& url) {
    if (m_pluginDownloader == nullptr) {
        m_pluginDownloader = new QNetworkAccessManager(this);
    }
    qCInfo(lcBridge) << "install_plugin: downloading" << url;
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_pluginDownloader->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
        reply->deleteLater();
        auto const fail = [this, requestId](QString const& msg) {
            qCWarning(lcBridge) << "install_plugin:" << msg;
            emit invokeResponse(
                requestId, QStringLiteral("null"), opendeck_detail::jsonToString(msg));
        };
        if (reply->error() != QNetworkReply::NoError) {
            fail(QStringLiteral("download failed: ") + reply->errorString());
            return;
        }
        QByteArray const body = reply->readAll();
        QString const bad = PluginCatalogModel::validateDownloadedArchive(body);
        if (!bad.isEmpty()) {
            fail(bad);
            return;
        }
        QTemporaryFile tmp(QDir::tempPath() +
                           QStringLiteral("/opendeck-plugin-XXXXXX.streamDeckPlugin"));
        if (!tmp.open() || tmp.write(body) != body.size()) {
            fail(QStringLiteral("cannot stage the download"));
            return;
        }
        tmp.flush();
        bool const ok = m_catalog->installFromFile(tmp.fileName(), /*userConfirmedUnsigned=*/true);
        emit event(QStringLiteral("plugin_reloaded"), QStringLiteral("{}"));
        if (ok) {
            emit invokeResponse(requestId, QStringLiteral("null"), QString{});
        } else {
            fail(QStringLiteral("install refused (signature or format)"));
        }
    });
}

} // namespace ajazz::app
