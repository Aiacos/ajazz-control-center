// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_device_bridge.hpp
 * @brief Bridge connecting SdPluginServer <-> StreamDockControlService and
 *        StreamDockInputService (Phase 19 / PLUGIN-10).
 *
 * The PluginDeviceBridge QObject is the convergence point of the v1.3 milestone.
 * It composes four already-shipped seams — it does NOT reimplement any of them:
 *
 *   - SdPluginServer::actionReceived  (Phase 17)  — inbound plugin->host actions
 *   - SdPluginServer::sendEvent       (Phase 17)  — outbound host->plugin events
 *   - StreamDockControlService::assignKeyImage (Phase 14) — key paint path
 *   - StreamDockInputService          (Phase 15)  — DeviceEvent source
 *
 * The bridge owns the **context registry** (the only genuinely new data structure):
 * an opaque `context` id ↔ ActionContext (device, page, coordinates, actionUUID,
 * pluginUuid). Contexts are minted as encoded tuples so re-activating the same
 * page yields a stable id that plugins may cache.
 *
 * This header also declares four **pure helper functions** that are unit-testable
 * without constructing the QObject:
 *   - coordsForKeyIndex  / keyIndexForCoords  — 1-based device ↔ 0-based Elgato
 *   - decodeDataUriImage                      — data: URI → QImage (crash-free)
 *   - ownerForActionUuid                      — longest-prefix plugin UUID match
 *
 * COD-031: app-layer QJson/QImage only; never nlohmann in any core/public header.
 *
 * Security:
 *   T-19-img   — decodeDataUriImage returns {ok:false} on any failure (no crash).
 *   T-19-coord — coordsForKeyIndex/keyIndexForCoords are the single named converters
 *                sourced from displayInfo().keyCols; round-trip tested (Pitfall 2).
 *   T-19-owner — ownerForActionUuid: longest-prefix match; unowned action → no-op.
 */
#pragma once

#include "ajazz/core/device.hpp"
#include "ajazz/core/profile.hpp"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>

// AJAZZ_HAVE_WEBSOCKETS gate: the whole PluginDeviceBridge surface compiles away when the
// WebSockets module is absent (it composes SdPluginServer). Mirrors obs_client.hpp /
// sd_plugin_server.hpp; every includer (application.hpp, test_plugin_device_bridge.cpp) is
// already AJAZZ_HAVE_WEBSOCKETS-gated, so AUTOMOC on this header is a no-op on minimal builds.
#if defined(AJAZZ_HAVE_WEBSOCKETS)

// Forward declarations for seam pointers wired in 19-02.
namespace ajazz::app {
class SdPluginServer;           // Phase 17
class StreamDockControlService; // Phase 14
class StreamDockInputService;   // Phase 15
} // namespace ajazz::app

namespace ajazz::app {

// ---------------------------------------------------------------------------
// ActionContext  — one registered plugin action instance on a visible page
// ---------------------------------------------------------------------------

/**
 * @brief Represents a single visible plugin action instance on an active page.
 *
 * row / column are 0-based Elgato coordinates per spec §4.4.
 * controller is "Keypad" or "Encoder".
 */
struct ActionContext {
    QString deviceId;     ///< Active device codename, e.g. "akp05e".
    QString pageId;       ///< Profile page id; "root" for the root page.
    int row{0};           ///< 0-based Elgato row coordinate.
    int column{0};        ///< 0-based Elgato column coordinate.
    QString controller;   ///< "Keypad" | "Encoder"
    QString actionUUID;   ///< Dotted action id, e.g. com.vendor.plugin.action.
    QString pluginUuid;   ///< Owning plugin uuid, e.g. com.vendor.plugin.
    int stateIndex{0};    ///< Current 0-based action state (Elgato setState; §4.4 `state`).
    QString settingsJson; ///< Per-instance settings JSON (Elgato payload.settings); "" => {}.
};

// ---------------------------------------------------------------------------
// ContextRegistry — opaque context-string ↔ ActionContext
// ---------------------------------------------------------------------------

/**
 * @brief In-memory registry that maps opaque `context` strings to ActionContext
 *        structs and supports both inbound (by context) and outbound
 *        (by controller + row + column) lookups.
 *
 * **Context-id scheme (LOCKED):** The context id is an encoded tuple
 * `deviceId#pageId#controller#row#column`. This makes the id stable across
 * re-registration of the same page so plugins may cache it without receiving
 * spurious `willAppear`/`willDisappear` pairs on every page navigation to the
 * same page. The trade-off is that the id is human-readable in logs (acceptable
 * for a loopback-only IPC channel) rather than opaque random UUID, but the spec
 * only requires uniqueness and stability, not opacity.
 *
 * Methods are NOT thread-safe — all calls must occur on the Qt GUI thread
 * (same constraint as the services this bridge composes).
 */
class ContextRegistry {
public:
    ContextRegistry() = default;

    /**
     * @brief Mint (or re-use) a context id for the given ActionContext and
     *        store the registration.
     *
     * Uses the encoded-tuple scheme: `deviceId#pageId#controller#row#column`.
     * If the derived id already exists in the registry the entry is updated
     * (idempotent re-registration on page re-activation).
     *
     * @param ctx  The action context to register.
     * @return     The opaque context string the plugin will use to address this
     *             action instance.
     */
    [[nodiscard]] QString registerContext(ActionContext const& ctx);

    /**
     * @brief Look up an ActionContext by its opaque context string.
     *
     * @param context  The opaque context id (from registerPlugin or willAppear).
     * @return         The ActionContext if found; std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<ActionContext> byContext(QString const& context) const;

    /**
     * @brief Look up the opaque context string for a given device + controller + coords.
     *
     * Used when a DeviceEvent arrives and the bridge needs to find the plugin
     * that owns the pressed key/encoder (outbound direction).
     *
     * deviceId is the first component of the coord key so that two simultaneously-
     * connected devices sharing the same controller/row/col do not collide in
     * m_byCoord (CR-02 / WR-04 multi-device isolation).
     *
     * @param deviceId    Device codename, e.g. "akp05e".
     * @param controller  "Keypad" or "Encoder".
     * @param row         0-based row (Keypad) or 0 (Encoder, see note below).
     * @param column      0-based column (Keypad) or encoder index (Encoder).
     * @return            The matching ActionContext if one is registered;
     *                    std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<ActionContext>
    byCoord(QString const& deviceId, QString const& controller, int row, int column) const;

    /**
     * @brief Remove a single registration by context string.
     *
     * @param context  The opaque context id to retire.
     */
    void retire(QString const& context);

    /**
     * @brief Remove all registrations for the given device.
     *
     * Called on deviceDidDisconnect.
     *
     * @param deviceId  Device codename, e.g. "akp05e".
     */
    void retireDevice(QString const& deviceId);

    /**
     * @brief Remove all registrations for the given device + page pair.
     *
     * Called on willDisappear for a whole page (e.g. page navigation).
     *
     * @param deviceId  Device codename.
     * @param pageId    Profile page id.
     */
    void retirePage(QString const& deviceId, QString const& pageId);

    /// Remove all registrations.
    void clear();

    /**
     * @brief Update the current state index of a registered context (setState).
     *
     * The state index is NOT part of the context-id tuple, so this mutates the
     * stored entry in place without changing the opaque context string the
     * plugin holds. Negative indices clamp to 0 (Elgato states are 0-based).
     *
     * @param context     The opaque context id (from setState's `context`).
     * @param stateIndex  The new 0-based action state.
     * @return            true if the context was found and updated; false if no
     *                    such context is registered (stale/unknown).
     */
    bool setState(QString const& context, int stateIndex);

    /// Update the per-instance settings JSON for a context (Elgato setSettings),
    /// so subsequent keyDown/willAppear events report the live settings.
    /// @return true if the context was found and updated; false if stale/unknown.
    bool updateSettings(QString const& context, QString const& settingsJson);

    /// @return Number of currently registered contexts.
    [[nodiscard]] int size() const noexcept;

    /**
     * @brief Return a snapshot of all registered ActionContexts.
     *
     * Used by PluginDeviceBridge to enumerate contexts before sending
     * willDisappear and then retiring them. The snapshot is a copy so the
     * caller may safely call retire() while iterating it.
     *
     * @return A list of {contextId, ActionContext} pairs for all registered
     *         contexts at the moment of the call.
     */
    [[nodiscard]] QList<std::pair<QString, ActionContext>> snapshot() const;

    /// Derive the stable encoded-tuple context id from an ActionContext
    /// (`deviceId#pageId#controller#row#column`). Public so outbound emitters can
    /// reconstruct the `context` id for the top-level Elgato envelope field from a
    /// byCoord-resolved ActionContext without a second registry lookup.
    [[nodiscard]] static QString deriveContextId(ActionContext const& ctx);

private:
    /// Derive the coord-index key used in m_byCoord.
    /// deviceId is the FIRST component so two devices sharing the same controller/row/col
    /// do not collide (CR-02 multi-device isolation).
    static QString
    coordKey(QString const& deviceId, QString const& controller, int row, int column);

    /// context-string -> ActionContext (inbound resolve from setImage's `context`).
    QHash<QString, ActionContext> m_byContext;

    /// deviceId+"#"+controller+"#"+row+"#"+col -> context-string (outbound: DeviceEvent ->
    /// context).
    QHash<QString, QString> m_byCoord;
};

// ---------------------------------------------------------------------------
// Pure helper types and free functions (unit-testable without the QObject)
// ---------------------------------------------------------------------------

/**
 * @brief 0-based {row, column} Elgato grid coordinate pair.
 *
 * Row 0 is the top row; column 0 is the leftmost column. This is the §4.4
 * `coordinates` shape the plugin protocol expects.
 */
struct GridCoord {
    int row{0};
    int column{0};
};

/**
 * @brief Convert a 1-based device keyIndex to 0-based Elgato {row, column}.
 *
 * Formula: row = (keyIndex-1) / keyCols, column = (keyIndex-1) % keyCols.
 * Source keyCols from displayInfo().keyCols at call sites (not a literal) so
 * the helper is pure and testable with any grid shape.
 *
 * @param oneBasedKeyIndex  1-based device key index (1 .. keyRows*keyCols).
 * @param keyCols           Number of key columns in the physical grid (from
 *                          IDisplayCapable::displayInfo().keyCols).
 * @return                  0-based {row, column} Elgato coordinate.
 *
 * @note Pitfall 2: the device backend uses 1-based indices; the plugin
 *       protocol uses 0-based Elgato coordinates. Convert ONLY here.
 */
[[nodiscard]] GridCoord coordsForKeyIndex(std::uint8_t oneBasedKeyIndex,
                                          std::uint8_t keyCols) noexcept;

/**
 * @brief Convert 0-based Elgato {row, column} back to a 1-based device keyIndex.
 *
 * Formula: row * keyCols + column + 1.
 * Source keyCols from displayInfo().keyCols at call sites.
 *
 * @param row     0-based row.
 * @param column  0-based column.
 * @param keyCols Number of key columns in the physical grid.
 * @return        1-based device key index (1 .. keyRows*keyCols).
 */
[[nodiscard]] std::uint8_t keyIndexForCoords(int row, int column, std::uint8_t keyCols) noexcept;

/**
 * @brief Result of decodeDataUriImage.
 *
 * ok==true  => image is a valid non-null QImage (any format).
 * ok==false => image is null; the CALLER paints a placeholder (spec §5 — no
 *              failure event is sent back to the plugin).
 */
struct DecodedImage {
    bool ok{false}; ///< true on successful decode.
    QImage image;   ///< Decoded QImage; null when ok==false.
};

/**
 * @brief Decode a `data:image/...;base64,...` URI into a QImage.
 *
 * Uses Qt's validated `QByteArray::fromBase64` + `QImage::loadFromData` —
 * no hand-rolled base64 or image decoder (Don't Hand-Roll, Pitfall 6).
 *
 * Behaviour:
 *   - If a comma is present, everything after it is treated as the base64 body.
 *   - If no comma is present, the whole string is treated as the base64 body
 *     (tolerates a raw base64 body without the `data:` prefix).
 *   - An empty body, malformed base64, or data that `QImage::loadFromData`
 *     cannot parse all return `{ok:false, {}}` — never crash, never throw.
 *   - A successful decode returns `{ok:true, img}` with img.isNull()==false.
 *
 * Security: T-19-img — `fromBase64` + `loadFromData` bool-checked; all
 * failure paths return {ok:false} without propagating the error to the plugin
 * (spec §5: no failure event).
 *
 * @param dataUri  The plugin-supplied `image` field value (verbatim from the
 *                 §4.3 setImage payload). Untrusted.
 * @return         DecodedImage with ok==true on success.
 */
[[nodiscard]] DecodedImage decodeDataUriImage(QString const& dataUri);

/**
 * @brief Resolve a dotted action UUID to the owning registered plugin UUID.
 *
 * Uses longest-prefix match on dotted-component boundaries (Pitfall 4 — must
 * not naively trim the last dotted segment, as action UUIDs and plugin UUIDs
 * are both reverse-DNS and a plugin may declare multi-segment action ids).
 *
 * Example: `ownerForActionUuid("com.x.plugin.action1", {"com.x.plugin",
 * "com.y.other"})` returns `"com.x.plugin"`. An action with no registered
 * owner returns an empty QString.
 *
 * Security: T-19-owner — prevents routing events to the wrong plugin;
 * foundation for cross-plugin access control enforced in 19-02/19-03.
 *
 * @param dottedActionUuid    Dotted action id, e.g. `com.vendor.plugin.action`.
 * @param registeredPluginUuids  Set of currently registered plugin UUIDs.
 * @return  The longest matching registered plugin UUID, or an empty QString.
 */
[[nodiscard]] QString ownerForActionUuid(QString const& dottedActionUuid,
                                         QSet<QString> const& registeredPluginUuids);

// ---------------------------------------------------------------------------
// PluginDeviceBridge QObject shell (wired in 19-02 and 19-03)
// ---------------------------------------------------------------------------

/**
 * @class PluginDeviceBridge
 * @brief App-layer bridge composing Phase 14/15/17 seams for the PLUGIN-10
 *        setImage end-to-end and inbound event routing.
 *
 * Phase 19-01 delivers the shell + ContextRegistry + pure helpers.
 * Phase 19-02 wires inbound actions (setImage/setTitle/setState/…) → device.
 * Phase 19-03 wires device input events → sendEvent to the bound plugin.
 *
 * **Inbound action routing (Phase 19-02):**
 *   `SdPluginServer::actionReceived(pluginUuid, action)` → `onAction` dispatches the
 *   visual action family (setImage/setTitle/setState/setBG/setFeedback/setText) via
 *   the context registry with ownership enforcement (T-19-xplugin). All other action
 *   names are no-ops (handled in later phases).
 *
 * **Security (Phase 19-02):**
 *   T-19-img    — decodeDataUriImage + ok:false → paintPlaceholder (solid fill via
 *                 assignKeyImage); no null QImage to device; no failure event back.
 *   T-19-xplugin — context ownership: ctx.pluginUuid == sending pluginUuid required.
 *   T-19-stale  — unknown context → no-op.
 *   T-19-input  — all JSON reads are defensive (toString/toInt(default)).
 *
 * COD-031: app-layer QJson/QImage only; never nlohmann.
 */
class PluginDeviceBridge : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct the bridge (Phase 19-02 — seams wired and connected).
     *
     * @param server   Phase-17 SdPluginServer (non-owning observing pointer).
     * @param control  Phase-14 StreamDockControlService (non-owning).
     * @param input    Phase-15 StreamDockInputService (non-owning, unused until 19-03).
     * @param parent   QObject parent for lifetime management.
     */
    explicit PluginDeviceBridge(SdPluginServer* server,
                                StreamDockControlService* control,
                                StreamDockInputService* input,
                                QObject* parent = nullptr);

    ~PluginDeviceBridge() override;

    /// Access the context registry (for testing and Phase 19-02/03 use).
    [[nodiscard]] ContextRegistry& registry() noexcept;
    [[nodiscard]] ContextRegistry const& registry() const noexcept;

public slots:
    /**
     * @brief Route an inbound plugin action to the device.
     *
     * Connected to SdPluginServer::actionReceived in Application (19-02).
     * Dispatches the six visual actions (setImage/setTitle/setState/setBG/
     * setFeedback/setText) via the context registry with ownership enforcement.
     * All other event names are no-ops.
     *
     * Security: T-19-xplugin (ownership check), T-19-stale (unknown context),
     * T-19-input (defensive JSON reads), T-19-img (placeholder on decode failure).
     *
     * @param pluginUuid Sending plugin UUID (from SdPluginServer::actionReceived).
     * @param action     Verbatim action JSON object from the WebSocket frame.
     */
    void onAction(QString const& pluginUuid, QJsonObject const& action);

    // ---- Phase 19-03: outbound device->plugin event routing ----------------

    /**
     * @brief Map a raw DeviceEvent to a §4.4 plugin event and deliver it.
     *
     * Connected to StreamDockInputService::deviceEvent (Phase 19 seam).
     * Routes the event to the owning plugin via ContextRegistry::byCoord +
     * SdPluginServer::sendEvent. An unbound coordinate is silently dropped
     * (T-19-leak: no cross-plugin leakage, no crash).
     *
     * Event mapping:
     *   KeyPressed/KeyReleased -> keyDown/keyUp with 0-based coordinates.
     *   EncoderTurned          -> dialRotate with signed ticks + controller "Encoder".
     *   EncoderPressed         -> dialDown + legacy keyDownCord alias.
     *   EncoderReleased        -> dialUp + legacy keyUpCord alias.
     *   TouchUp                -> touchTap {x, y:0, hold:false} (completed touch).
     *   TouchDown/TouchMove    -> dropped (intermediate edges, not plugin events).
     *   Connected/Disconnected -> handled by onDeviceConnected/Disconnected.
     *
     * @param deviceId  Device codename from StreamDockInputService::deviceEvent.
     * @param ev        The raw DeviceEvent from the input service.
     */
    void onDeviceEvent(QString const& deviceId, ajazz::core::DeviceEvent const& ev);

    /**
     * @brief Handle plugin registration: populate contexts + willAppear for
     *        the newly-connected plugin's actions on the root page.
     *
     * @param pluginUuid  The registered plugin UUID (from SdPluginServer::pluginRegistered).
     */
    void onPluginRegistered(QString const& pluginUuid);

    /**
     * @brief Handle plugin disconnect: retire its contexts (willDisappear sent
     *        for each retired context).
     *
     * @param pluginUuid  The disconnected plugin UUID (from SdPluginServer::pluginDisconnected).
     */
    void onPluginDisconnected(QString const& pluginUuid);

    /**
     * @brief Handle device connect: populate contexts + send deviceDidConnect
     *        to all registered plugins.
     *
     * @param deviceId  Device codename of the newly-connected device.
     */
    void onDeviceConnected(QString const& deviceId);

    /**
     * @brief Handle device disconnect: retire contexts + send deviceDidDisconnect
     *        to all registered plugins.
     *
     * @param deviceId  Device codename of the removed device.
     */
    void onDeviceDisconnected(QString const& deviceId);

    /**
     * @brief Handle page change: retire old-page contexts (willDisappear) then
     *        populate new-page contexts (willAppear).
     *
     * @param deviceId  Device whose profile page changed.
     * @param pageId    New active page id.
     */
    void onActivePageChanged(QString const& deviceId, QString const& pageId);

    /**
     * @brief Deliver a Property-Inspector-originated settings change to the live
     *        plugin process as a @c didReceiveSettings event.
     *
     * Wired (in application.cpp, per fresh PIBridge) from
     * @c PIBridge::contextSettingsChanged. Resolves @p contextId via the registry
     * to the full ActionContext, refreshes its stored settings, and sends the
     * Elgato @c didReceiveSettings envelope to the owning plugin — the PI->plugin
     * half of the settings round-trip (mirrors OpenDeck set_settings notifying the
     * plugin when the edit came from the PI). No-op if the context is not
     * registered (no live instance) or the server is absent.
     *
     * @param pluginUuid  Owning plugin uuid (relay target).
     * @param contextId   Wire context id (device#page#controller#row#column).
     * @param json        New per-context settings JSON.
     */
    void onPropertyInspectorSettings(QString const& pluginUuid,
                                     QString const& contextId,
                                     QString const& json);

    /**
     * @brief Register contexts + send willAppear for every bound ActionKind::Plugin
     *        action on the root page of the active profile that belongs to the
     *        given plugin (or all registered plugins if pluginUuid is empty).
     *
     * Scoped to the root page (A6 simplification: multi-page navigation authority
     * lives in Phase 16; the bridge populates only what is currently visible).
     *
     * Also public so that application.cpp can trigger it on profileChanged
     * without a dedicated slot (PLUGIN-19 wiring).
     *
     * @param deviceId    Device codename, e.g. "akp05e".
     * @param pluginUuid  If non-empty, only emit willAppear for this plugin's actions.
     *                    If empty, emit for all registered plugins.
     */
    void populateContextsForActivePage(QString const& deviceId, QString const& pluginUuid = {});

    /**
     * @brief Return the most-recently-connected device codename.
     *
     * Empty when no device is connected. Exposed so application.cpp can guard
     * the profileChanged -> populateContextsForActivePage lambda without needing
     * a separate slot (PLUGIN-19 / T-28-09).
     *
     * Follows the inline-accessor style from branding_service.hpp:74.
     */
    [[nodiscard]] QString activeDeviceId() const noexcept { return m_activeDeviceId; }

signals:
    /// Emitted when a plugin sends sendToPropertyInspector — the host relays it to
    /// the open Property Inspector for that context. Application wires this to the
    /// active PIBridge (the bridge itself does not own the PI surface).
    void relayToPropertyInspector(QString const& pluginUuid,
                                  QString const& contextId,
                                  QJsonObject const& payload);

private:
    /// Handle the inbound settings + PI-relay family (setSettings / getSettings /
    /// setGlobalSettings / getGlobalSettings / sendToPropertyInspector) that the
    /// plugin sends over its WebSocket. Per-context settings persist to the shared
    /// plugin_settings_store keyed by the WIRE context id and are echoed back via
    /// didReceiveSettings; global settings persist plugin-wide. Returns true if the
    /// event was consumed (so onAction skips the visual path).
    [[nodiscard]] bool handleSettingsAction(QString const& pluginUuid,
                                            QString const& event,
                                            QJsonObject const& action);

    /// Dispatch setImage: decode data-URI, check ownership, call assignKeyImage
    /// (or paintPlaceholder on decode failure). No failure event sent back (§5).
    void onSetImage(QString const& pluginUuid,
                    QJsonObject const& action,
                    ActionContext const& ctx,
                    std::uint8_t keyCols);

    /// Dispatch setTitle: render the title text to a QImage and call assignKeyImage.
    /// Phase 19 renders the title as a text overlay on a solid background.
    void onSetTitle(QString const& pluginUuid,
                    QJsonObject const& action,
                    ActionContext const& ctx,
                    std::uint8_t keyCols);

    /// Dispatch setBG: render a solid background color and call assignKeyImage.
    void onSetBG(QString const& pluginUuid,
                 QJsonObject const& action,
                 ActionContext const& ctx,
                 std::uint8_t keyCols);

    /// Paint a crash-free placeholder on the key when decodeDataUriImage fails (§5).
    /// Uses assignKeyImage with a neutral solid-fill QImage — never a null image.
    /// Sends NO failure event back to the plugin (spec §5).
    void paintPlaceholder(ActionContext const& ctx, std::uint8_t keyCols);

    /// Re-apply the key's tracked title (if any) over its current BASE image.
    /// Called after a base-changing paint (setImage / setBG / setState image)
    /// so a previously-set title survives a later setImage — Elgato keeps the
    /// title as an independent layer over the action icon. No-op if the key has
    /// no tracked title.
    void reapplyTitle(std::uint8_t keyIndex);

    // ---- Phase 19-03 private helpers ---------------------------------------

    /**
     * @brief Send willDisappear for every context belonging to the given plugin
     *        on the given device+page, then retire them from the registry.
     *
     * @param deviceId   Device codename.
     * @param pageId     Page id to retire.
     * @param pluginUuid If non-empty, only retire contexts owned by this plugin.
     */
    void retirePageContexts(QString const& deviceId,
                            QString const& pageId,
                            QString const& pluginUuid = {});

    SdPluginServer* m_server{nullptr};            // Phase 17 seam
    StreamDockControlService* m_control{nullptr}; // Phase 14 seam
    StreamDockInputService* m_input{nullptr};     // Phase 15 seam (used in 19-03)

    ContextRegistry m_registry;

    /// Set of currently registered plugin UUIDs (from pluginRegistered signal).
    /// Used for owner resolution and lifecycle gating (T-19-owner).
    QSet<QString> m_registeredPlugins;

    /// Most-recently-connected device codename. Updated by onDeviceConnected /
    /// onDeviceDisconnected. Used by onPluginRegistered / onPluginDisconnected as
    /// the active device context for lifecycle operations (WR-03: eliminates the
    /// hardcoded "akp05e" fallback). Empty when no device is connected.
    QString m_activeDeviceId;

    /// Accessor returning the currently active profile by const-ref.
    /// Injected from Application after construction via setProfileAccessor().
    /// Used by populateContextsForActivePage to enumerate bound plugin actions.
    std::function<ajazz::core::Profile const&()> m_profileAccessor;

    /// Resolver: (actionUuid, stateIndex) -> absolute manifest state-image path
    /// ("" if none). Injected from Application (PluginManager::stateImagePath).
    /// Used by the setState handler to auto-render the declared state image.
    std::function<QString(QString const&, int)> m_stateImageResolver;

    /// Resolver: actionUuid -> owning plugin UUID ("" if unknown). Injected from
    /// Application (PluginManager::ownerForAction). This is the stored-owner map
    /// (OpenDeck model); resolveOwner() consults it before falling back to the
    /// legacy dotted-prefix match. Fixes the silent-no-willAppear trap where an
    /// action UUID is not a dotted prefix of its plugin UUID.
    std::function<QString(QString const&)> m_actionOwnerResolver;

    /// Resolve the owning plugin UUID for an action: stored-owner map first
    /// (m_actionOwnerResolver), then the dotted-prefix fallback over the set of
    /// currently-registered plugins. Returns "" when no owner can be determined.
    [[nodiscard]] QString resolveOwner(QString const& actionUuid) const;

    /// Current title overlay per 1-based key index (active device). Set by
    /// setTitle, re-applied by reapplyTitle() after a base-changing paint so the
    /// title persists across a later setImage. Cleared on device (re)connect.
    std::map<std::uint8_t, QString> m_titleByKey;

public:
    /**
     * @brief Inject a profile accessor so the bridge can enumerate bound plugin
     *        actions for willAppear population.
     *
     * Called by Application after constructing the bridge. If not set,
     * populateContextsForActivePage is a no-op (graceful degradation).
     *
     * @param accessor  Lambda returning `Profile const&` for the active profile.
     */
    void setProfileAccessor(std::function<ajazz::core::Profile const&()> accessor);

    /**
     * @brief Inject the manifest state-image resolver (PluginManager::stateImagePath).
     *
     * When set, a `setState` for a Keypad action auto-renders the manifest's
     * declared `States[index].Image` onto the key (so a multi-state action
     * changes its key image without pushing its own `setImage`). If not set, or
     * the resolver returns "" (no declared image), setState only updates the
     * tracked state — graceful degradation.
     *
     * @param resolver  (actionUuid, stateIndex) -> absolute image path or "".
     */
    void setStateImageResolver(std::function<QString(QString const&, int)> resolver);

    /**
     * @brief Inject the stored action-owner resolver (PluginManager::ownerForAction).
     *
     * When set, resolveOwner() uses it to map an action UUID to its owning plugin
     * UUID via the discovered manifests (the OpenDeck stored-owner model), instead
     * of requiring the action UUID to be a dotted prefix of the plugin UUID. Falls
     * back to the legacy prefix match when unset or when it returns "".
     *
     * @param resolver  actionUuid -> owning plugin UUID, or "" if unknown.
     */
    void setActionOwnerResolver(std::function<QString(QString const&)> resolver);
};

} // namespace ajazz::app

#endif // defined(AJAZZ_HAVE_WEBSOCKETS)
