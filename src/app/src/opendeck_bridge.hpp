// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file opendeck_bridge.hpp
 * @brief Maps our backend singletons to the OpenDeck IPC command/event contract
 *        (docs/opendeck-ui/01-contract.md), for the embedded OpenDeck web UI.
 *
 * Phase 1 (webui seam) — this object is the C++ side of the bridge. The
 * QWebChannel surface (`invoke` slot + `invokeResponse`/`event` signals) is
 * fixed now so the JS Tauri shim can target it; the synchronous `handle()` seam
 * computes the JSON result and is what the unit tests and the debug-control
 * `opendeck.invoke` method exercise headlessly.
 *
 * Not a QML singleton: it is registered on a QWebChannel (Phase 1 part 2) and is
 * owned by Application. The data-shaping is factored into the free functions in
 * @ref ajazz::app::opendeck_detail so they can be unit-tested without
 * constructing the heavy backend QObjects.
 */
#pragma once

#include "ajazz/core/profile.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;

namespace ajazz::app {

class DeviceModel;
class ProfileController;
class PluginCatalogModel;
class StreamDockControlService;
class StreamDockInputService;

/// Pure, side-effect-free JSON shaping helpers (our types -> OpenDeck shapes).
/// Free functions so they are unit-testable without DeviceModel/ProfileController.
namespace opendeck_detail {

/// Serialise any QJsonValue (object/array/scalar) to a compact JSON string.
[[nodiscard]] QString jsonToString(QJsonValue const& value);

/// Map a DeviceModel::capabilitiesFor() map (+ codename) to an OpenDeck
/// DeviceInfo object {id,name,rows,columns,encoders,touchpoints,type}.
[[nodiscard]] QJsonObject deviceInfoJson(QString const& codename, QVariantMap const& caps);

/// Build one OpenDeck Action object with the full required shape.
[[nodiscard]] QJsonObject makeActionJson(QString const& name,
                                         QString const& uuid,
                                         QString const& plugin,
                                         QString const& tooltip,
                                         QString const& icon,
                                         QString const& propertyInspector,
                                         QStringList const& controllers);

/// Map one PluginCatalogModel::installedActions() entry to an OpenDeck Action.
[[nodiscard]] QJsonObject actionFromCatalogEntry(QVariantMap const& entry);

/// Group installed actions by plugin name and prepend the built-in "OpenDeck"
/// category. Returns {category: Action[]}.
[[nodiscard]] QJsonObject categoriesJson(QVariantList const& installedActions);

/// Map a core KeyState to an OpenDeck ActionState object (full shape).
[[nodiscard]] QJsonObject actionStateJson(core::KeyState const& state);

/// Map a bound key Binding to an OpenDeck ActionInstance object, or a JSON null
/// value when the slot is effectively empty.
[[nodiscard]] QJsonValue keyInstanceJson(core::Binding const& binding, QString const& context);

/// Map a bound EncoderBinding to an OpenDeck ActionInstance, or null.
[[nodiscard]] QJsonValue encoderInstanceJson(core::EncoderBinding const& binding,
                                             QString const& context);

/// Map a bound touch-strip zone (TouchZoneBinding) to an OpenDeck
/// ActionInstance (controller "Keypad"), or null.
[[nodiscard]] QJsonValue touchInstanceJson(core::TouchZoneBinding const& binding,
                                           QString const& context);

/// Assemble the full OpenDeck Profile {device,id,keys[],sliders[]}.
/// `keys[]` length is `keyCount + touchCount`: the first `keyCount` slots are
/// the keypad (controller "Keypad", positions 0..keyCount-1) and the trailing
/// `touchCount` slots are the touch-strip zones (also controller "Keypad",
/// positions keyCount..keyCount+touchCount-1) — matching how OpenDeck's
/// DeviceView reads `profile.keys[(rows*cols)+i]` for touch points. `sliders[]`
/// is the `encoderCount` encoders (controller "Encoder").
[[nodiscard]] QJsonObject
profileJson(core::Profile const& profile, int keyCount, int encoderCount, int touchCount);

/// Overlay persisted OpenDeck UI settings on the {language,rotation,brightness}
/// defaults (stored values win). Pure so the merge is unit-tested without the
/// QSettings I/O that get_settings/set_settings wrap around it.
[[nodiscard]] QJsonObject settingsWithDefaults(QJsonObject const& stored);

/// Override the CURRENT state's image/text of an ActionInstance object in place
/// with a live plugin visual (setImage data URI / setTitle text) — the payload
/// half of the "update_state" event the web UI's Key component consumes. An
/// empty @p imageDataUri leaves the image untouched; @p titleChanged=true
/// applies @p title even when empty (title cleared). Returns false (instance
/// untouched) when current_state is out of range. Pure so the override contract
/// is unit-tested without the bridge QObjects.
[[nodiscard]] bool overrideStateVisual(QJsonObject& instance,
                                       QString const& imageDataUri,
                                       QString const& title,
                                       bool titleChanged);

} // namespace opendeck_detail

/**
 * @class OpenDeckBridge
 * @brief Command/event bridge between the OpenDeck web UI and our backend.
 */
class OpenDeckBridge : public QObject {
    Q_OBJECT

public:
    OpenDeckBridge(DeviceModel* devices,
                   ProfileController* profiles,
                   PluginCatalogModel* catalog,
                   QObject* parent = nullptr);
    ~OpenDeckBridge() override;

    /// Synchronous testable seam: run @p command with JSON @p argsJson and return
    /// the JSON result as a string (a JSON value: object/array/string/number/null).
    /// Unknown/unimplemented commands return JSON null and log a warning.
    [[nodiscard]] Q_INVOKABLE QString handle(QString const& command, QString const& argsJson);

    /// Async QWebChannel entry point: computes handle() and emits invokeResponse.
    Q_INVOKABLE void
    invoke(QString const& requestId, QString const& command, QString const& argsJson);

    /// Inject the live device-image path (update_image -> set_image). Set after
    /// construction because StreamDockControlService is declared after the
    /// bridge in Application's member list (init-order). May be null in headless
    /// builds without the control service.
    void setStreamDockControl(StreamDockControlService* control) { m_control = control; }

    /// Inject the live input service so trigger_virtual_press can drive a
    /// synthetic key/encoder press through the SAME dispatch path real hardware
    /// uses (built-in actions + plugin host). Set after construction (init-order,
    /// like setStreamDockControl). May be null in headless builds.
    void setInputService(StreamDockInputService* input) { m_input = input; }

    /// Emit the OpenDeck events the web UI subscribes to. Wired to backend
    /// signals in Application; safe to call when no web UI is attached.
    void notifyProfileChanged(); ///< -> "switch_profile" + "rerender_images"
    void notifyDevicesChanged(); ///< -> "devices" (the get_devices map)

    /// Mirror a live plugin visual (setImage/setTitle on a Keypad key) into the
    /// web UI: composes the bound ActionInstance for the slot with its state
    /// image/text overridden by the live values and pushes the OpenDeck
    /// "update_state" event the SPA's Key component subscribes to. Wired to
    /// PluginDeviceBridge::liveKeyVisual in Application. No-op when the slot is
    /// unbound or the active profile belongs to another device.
    void notifyLiveKeyVisual(QString const& deviceId,
                             int position,
                             QString const& imageDataUri,
                             QString const& title,
                             bool titleChanged);

    // Bring QObject::event(QEvent*) into scope so the QWebChannel `event` signal
    // below does not "hide" the inherited virtual — AppleClang's
    // -Werror=overloaded-virtual (and MSVC /W4) otherwise fail the build, while
    // GCC/Linux-Clang stay silent. The signal MUST keep the name `event`: the
    // web-UI Tauri shim subscribes to it by that exact name.
    using QObject::event;

signals:
    /// Resolves the JS-side Promise for @p requestId. @p errorJson is empty on success.
    void
    invokeResponse(QString const& requestId, QString const& resultJson, QString const& errorJson);

    /// Pushes one of the 10 OpenDeck events to the web UI (payload is a JSON string).
    void event(QString const& name, QString const& payloadJson);

private:
    /// Download an http(s) plugin archive (install_plugin {url}) and install it
    /// off disk via PluginCatalogModel; resolves the JS Promise for @p requestId.
    void installPluginFromUrl(QString const& requestId, QString const& url);

    /// Annotate a get_selected_profile result in place: any bound instance whose
    /// action uuid is neither a builtin nor an installed action is "orphaned"
    /// (its plugin was uninstalled / never had a Linux code path). Its name is
    /// suffixed so the SPA's action label / property inspector show it is a stale
    /// binding the user can remove, instead of a silent broken-image tile.
    void markOrphanedInstances(QJsonObject& profile) const;

    /// Re-attach catalog metadata (plugin / icon / property_inspector /
    /// controllers) onto a single bound instance's `action` object, looked up by
    /// its UUID. Used for the instance returned by create_instance / move_instance
    /// so a freshly-dropped action immediately exposes its Property Inspector.
    /// Returns the (possibly enriched) value unchanged when it is not an object.
    [[nodiscard]] QJsonValue enrichInstance(QJsonValue const& instance) const;

    DeviceModel* m_devices;
    ProfileController* m_profiles;
    PluginCatalogModel* m_catalog;
    StreamDockControlService* m_control = nullptr;
    StreamDockInputService* m_input = nullptr;
    QNetworkAccessManager* m_pluginDownloader = nullptr;
};

} // namespace ajazz::app
