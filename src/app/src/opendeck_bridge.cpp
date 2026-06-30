// SPDX-License-Identifier: GPL-3.0-or-later
#include "opendeck_bridge.hpp"

#include "ajazz/core/profile.hpp"
#include "device_model.hpp"
#include "plugin_catalog_model.hpp"
#include "profile_controller.hpp"
#include "stream_dock_control_service.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QStringList>
#include <QTemporaryFile>
#include <QUrl>

namespace ajazz::app {

namespace {
Q_LOGGING_CATEGORY(lcBridge, "ajazz.opendeck.bridge")

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
                byPlugin.insert(
                    id,
                    QJsonObject{
                        {QStringLiteral("id"), id},
                        {QStringLiteral("name"), a.value(QStringLiteral("pluginName")).toString()},
                        {QStringLiteral("icon"), a.value(QStringLiteral("icon")).toString()}});
            }
        }
        QJsonArray out;
        for (QJsonObject const& p : byPlugin) {
            out.append(p);
        }
        return str(out);
    }
    if (command == QLatin1String("remove_plugin")) {
        QString const id = args.value(QStringLiteral("id")).toString();
        if (m_catalog != nullptr && !id.isEmpty()) {
            m_catalog->uninstall(id);
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
        return str(profileJson(profile, keyCount, encoderCount, touchCount));
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
        QString const actionId = args.value(QStringLiteral("action")).toString();
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
            return str(it != p.encoders.end() ? encoderInstanceJson(it->second, ctx)
                                              : QJsonValue(QJsonValue::Null));
        }
        if (c.position >= keyCount) {
            auto const it = p.touchZones.find(static_cast<std::uint8_t>(c.position - keyCount));
            return str(it != p.touchZones.end() ? touchInstanceJson(it->second, ctx)
                                                : QJsonValue(QJsonValue::Null));
        }
        auto const it = p.keys.find(static_cast<std::uint16_t>(c.position));
        return str(it != p.keys.end() ? keyInstanceJson(it->second, ctx)
                                      : QJsonValue(QJsonValue::Null));
    }

    if (command == QLatin1String("remove_instance")) {
        if (m_profiles == nullptr) {
            return str(QJsonValue(QJsonValue::Null));
        }
        Ctx const c = parseCtxValue(args.value(QStringLiteral("context")));
        if (c.valid && c.controller == QLatin1String("Keypad") &&
            c.position < keyCountOf(c.device)) {
            m_profiles->removeKeyActionAt(c.position, 0);
        }
        // TODO(phase2b-followup): encoder/touch-zone removal verbs.
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
                    m_profiles->loadProfileById(m.value(QStringLiteral("id")).toString());
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
            return str(it != p.encoders.end() ? encoderInstanceJson(it->second, ctx)
                                              : QJsonValue(QJsonValue::Null));
        }
        if (dstTouch) {
            auto const it = p.touchZones.find(static_cast<std::uint8_t>(dst.position - keyCount));
            return str(it != p.touchZones.end() ? touchInstanceJson(it->second, ctx)
                                                : QJsonValue(QJsonValue::Null));
        }
        auto const it = p.keys.find(static_cast<std::uint16_t>(dst.position));
        return str(it != p.keys.end() ? keyInstanceJson(it->second, ctx)
                                      : QJsonValue(QJsonValue::Null));
    }

    // TODO(phase2b-followup): set_state, trigger_virtual_press,
    // switch_property_inspector — return null (no-op) for now so the SPA does
    // not reject; tracked in docs/opendeck-ui/03-dev-plan.md.
    if (command == QLatin1String("set_state") ||
        command == QLatin1String("trigger_virtual_press") ||
        command == QLatin1String("switch_property_inspector")) {
        return str(QJsonValue(QJsonValue::Null));
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

void OpenDeckBridge::invoke(QString const& requestId,
                            QString const& command,
                            QString const& argsJson) {
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
