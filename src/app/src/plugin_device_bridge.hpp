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
#include <utility>

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
// DeviceGeometry — physical key/encoder layout of one device codename
// ---------------------------------------------------------------------------

/**
 * @brief Resolved physical geometry for a device, used to convert between
 *        Elgato 0-based {row,column} coordinates and 1-based key indices and to
 *        advertise device size in `deviceDidConnect` / the `-info` payload.
 *
 * The defaults describe the AKP05E (the value the bridge hardcoded before the
 * geometry resolver existed), so an un-injected resolver preserves the prior
 * single-device behaviour and every pre-existing test fixture keeps passing.
 * The real per-device values come from `core::DeviceDescriptor`
 * (`gridColumns`, `keyRows`, `keyCount`, `encoderCount`) via the resolver wired
 * in Application. See docs/protocols/streamdeck/elgato_plugin_protocol.md §6.3.
 */
struct DeviceGeometry {
    std::uint8_t keyCols{5};                       ///< Key grid columns (AKP05E = 5, AKP03 = 3).
    std::uint8_t keyRows{2};                       ///< Key grid rows (AKP05E = 2, AKP153 = 3).
    std::uint16_t keyCount{10};                    ///< Total LCD keys.
    std::uint16_t encoderCount{4};                 ///< Rotary encoders (Stream Deck + dials).
    int elgatoType{7};                             ///< Elgato DeviceType for deviceInfo (SD+ = 7).
    QString model{QStringLiteral("AJAZZ AKP05E")}; ///< Human-readable name.
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
 * @brief Decode a `data:image/...` URI into a QImage.
 *
 * Uses Qt's validated `QByteArray::fromBase64` / `fromPercentEncoding` +
 * `QImage::loadFromData` — no hand-rolled decoding (Don't Hand-Roll, Pitfall 6).
 *
 * Behaviour:
 *   - If a comma is present, everything after it is the body. A header
 *     containing `;base64` (or an absent header) marks a base64 body; any
 *     other header (e.g. `data:image/svg+xml;charset=utf8` — the Elgato
 *     setImage SVG form MiraBox plugins emit) carries the body as plain or
 *     percent-encoded text, handed to loadFromData verbatim after decoding.
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
 * @brief Re-encode a plugin-supplied data URI so a web view can load it.
 *
 * Non-base64 bodies (the Elgato setImage SVG form,
 * `data:image/svg+xml;charset=utf8,<svg …>`) may contain raw `#` (colour
 * literals), which URI parsers treat as the fragment delimiter — Chromium
 * truncates the image there and the OpenDeck SPA canvas paints its alert
 * placeholder. This helper percent-encodes such bodies (idempotent: an
 * already-encoded body is decoded first); base64 bodies pass through verbatim.
 * Used on the update_state mirror path only — the device path decodes the raw
 * URI directly via decodeDataUriImage().
 *
 * @param dataUri  The plugin-supplied `image` field value. Untrusted.
 * @return         A URI safe for `<img src>` / CanvasImage consumption.
 */
[[nodiscard]] QString mirrorSafeDataUri(QString const& dataUri);

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
     * @brief Resolve a context string to a registered ActionContext, accepting
     *        BOTH the bridge wire id and the SPA "device.profile.controller.position"
     *        form. Tries an exact ContextRegistry::byContext first, then falls back
     *        to coordinate translation (resolvePropertyInspectorContext). This is
     *        the canonical entry point for any context that originates in the
     *        embedded OpenDeck SPA (PI registration owner lookup, PI lifecycle,
     *        PI setSettings) so the dot-vs-hash context formats no longer diverge.
     */
    [[nodiscard]] std::optional<ActionContext> lookupContext(QString const& contextId) const;

    /// Canonical wire id for @p contextId (accepts both wire and SPA forms via
    /// lookupContext); returns the input unchanged when unresolved. Wired into
    /// SdPluginServer::setContextCanonicalizer so the PI<->plugin relays speak
    /// one context namespace (audit 2.4).
    [[nodiscard]] QString canonicalContextId(QString const& contextId) const;

    /**
     * @brief Resolve a Property Inspector context to a registered ActionContext.
     *
     * The embedded OpenDeck SPA addresses an instance by its own context string
     * "device.profile.controller.position" (dot-separated, position-based), which
     * does NOT match the bridge's wire context id
     * "device#page#controller#row#column" (hash-separated, coordinate-based, page
     * "root"). A direct ContextRegistry::byContext therefore misses, so PI
     * setSettings silently never reached the plugin (the action stayed inert).
     * This translates the SPA form: parse device/controller/position (from the
     * right, tolerating dotted profile names), convert position->{row,column} via
     * the device geometry, and resolve through ContextRegistry::byCoord (which is
     * keyed by coordinates and so is page-agnostic). Returns nullopt if the string
     * is malformed or no instance is mounted at those coordinates.
     */
    [[nodiscard]] std::optional<ActionContext>
    resolvePropertyInspectorContext(QString const& contextId) const;

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

    /**
     * @brief The set of currently-registered plugin UUIDs.
     *
     * Phase 34-04 (APROF-04 / EVENT-03): host-level outbound events
     * (applicationDidLaunch/Terminate, systemDidWakeUp) are delivered ONLY to
     * registered plugins (V4 access control / T-34-04-02 information-disclosure
     * mitigation — never broadcast). Application owns m_pluginServer and performs
     * the sendEvent fan-out, but the registered-plugin authority lives in the
     * bridge (it tracks register/disconnect). Exposing the set here keeps the
     * fan-out at the Application composition seam without the bridge gaining a new
     * responsibility. The same m_registeredPlugins set drives deviceDidConnect/
     * Disconnect fan-out (onDeviceConnected/onDeviceDisconnected).
     */
    [[nodiscard]] QSet<QString> registeredPlugins() const { return m_registeredPlugins; }

    /**
     * @brief Render a Toggle Action's current state + emit a state-change willAppear.
     *
     * BIND-07 render hook. Wired (in application.cpp) into
     * StreamDockInputService::setToggleRenderHook so a built-in Toggle press, after
     * cycling its `currentState` in the profile, repaints the control with
     * `instance.states[currentState].visual` (imagePath/title) and notifies any
     * plugin that owns the context with a fresh willAppear carrying the new state.
     *
     * Render path reuse: paints via StreamDockControlService::assignKeyImage
     * (Keypad) / assignEncoderImage (Encoder) -- the SAME repaint mechanism the
     * plugin-driven setState handler uses (plugin_device_bridge.cpp setState path);
     * no new render mechanism is introduced.
     *
     * willAppear: if a context is registered at the control's coordinates AND a
     * plugin owns it, the registry stateIndex is advanced (ContextRegistry::setState)
     * and a willAppear is re-sent for that one context with the new state. The
     * cross-plugin ownership guard is preserved (a built-in toggle never drives a
     * context owned by a different plugin). For a PURE built-in toggle (no owning
     * plugin) there is no plugin to notify, so willAppear emission is a clean no-op
     * and only the repaint runs.
     *
     * @param controller "Keypad" or "Encoder" (touch zones register under "Encoder").
     * @param index      0-based control index (key index or encoder/zone index).
     * @param instance   The ALREADY-CYCLED ActionInstance (currentState is the new
     *                   index; states[currentState] is the visual to render).
     */
    void renderToggleState(QString const& controller,
                           int index,
                           ajazz::core::ActionInstance const& instance);

signals:
    /// Emitted when a plugin sends sendToPropertyInspector — the host relays it to
    /// the open Property Inspector for that context. Application wires this to the
    /// active PIBridge (the bridge itself does not own the PI surface).
    void relayToPropertyInspector(QString const& pluginUuid,
                                  QString const& contextId,
                                  QJsonObject const& payload);

    /// Emitted after a plugin-driven visual lands on a control (setImage /
    /// setTitle), so the OpenDeck web UI can mirror the live surface via its
    /// "update_state" event (Application wires this to
    /// OpenDeckBridge::notifyLiveInstanceVisual). Without it the SPA canvas
    /// shows only the static bind-time icon while the hardware streams live
    /// frames. @p controller is "Keypad" or "Encoder"; @p position is the
    /// 0-based SPA slot (keys: row * keyCols + column; encoders: dial index).
    /// @p imageDataUri is empty when only the title changed; @p titleChanged
    /// distinguishes "title cleared" (true, empty title) from "no title change".
    void liveInstanceVisual(QString const& deviceId,
                            QString const& controller,
                            int position,
                            QString const& imageDataUri,
                            QString const& title,
                            bool titleChanged);

    /// Emitted when a plugin sends setTriggerDescription (SD+ dial interaction
    /// hints: rotate/push/touch/longTouch). Non-visual — the host records the
    /// hints for the encoder UI surface; the bridge does not repaint a key.
    /// @p descriptions is the command payload object. (T025/D3)
    void triggerDescriptionChanged(QString const& deviceId,
                                   QString const& contextId,
                                   QJsonObject const& descriptions);

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

    /// Drop the cached visual layers a retiring context owns (title overlay,
    /// encoder feedback bag, layout override) so they cannot composite onto the
    /// NEXT action bound to the same control (audit 6.1). Called at both
    /// retire sites (reconcile pass + retirePageContexts).
    void purgeContextVisualCaches(ActionContext const& ctx, QString const& ctxId);

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

    /// Resolver: actionUuid -> manifest-declared default Settings (compact JSON,
    /// "" if none). Injected from Application (PluginManager::defaultSettingsForAction).
    /// Last fallback when composing willAppear settings: MiraBox SDVueSDK draw
    /// code dereferences default-settings keys unguarded (timeClock:
    /// `settings.checkboxGroup.includes` -> TypeError on {}), so a first-run
    /// instance must appear with the manifest defaults, as the vendor host does.
    std::function<QString(QString const&)> m_defaultSettingsResolver;

    /// Resolver: device codename -> physical DeviceGeometry. Injected from
    /// Application (over core::DeviceRegistry / streamDockSidecarDescriptors).
    /// Drives every 0-based{row,column} <-> 1-based keyIndex conversion and the
    /// deviceDidConnect / -info device size, replacing the AKP05E-hardcoded
    /// keyCols=5 (PLUGIN-GAP-ANALYSIS F2). Unset or unknown codename => the
    /// DeviceGeometry default (AKP05E), preserving prior single-device behaviour.
    std::function<DeviceGeometry(QString const&)> m_deviceGeometryResolver;

    /// Resolve the physical geometry for a device codename via
    /// m_deviceGeometryResolver, or the AKP05E default when unset/unknown.
    [[nodiscard]] DeviceGeometry geometryForDevice(QString const& deviceId) const;

    /// Per-encoder-context feedback state for the built-in dial layouts:
    /// ctxId -> merged setFeedback item bag, and ctxId -> active layout id
    /// (setFeedbackLayout overrides the manifest Encoder.layout). Cleared when
    /// the context retires.
    std::map<QString, QJsonObject> m_encoderFeedback;
    std::map<QString, QString> m_encoderLayoutOverride;

    /// Resolver: actionUuid -> {manifest Encoder.layout id, resolved absolute
    /// Encoder.Icon path}. Injected from Application
    /// (PluginManager::encoderLayoutInfo). Unset/empty -> $X1 with no icon.
    std::function<std::pair<QString, QString>(QString const&)> m_encoderLayoutResolver;

    /// Compose and paint the feedback surface for an Encoder context: active
    /// layout (override > manifest > $X1) rendered with the merged feedback
    /// bag (defaults: title = nothing, icon = manifest Encoder.Icon) onto the
    /// strip zone via assignEncoderImage(column). No-op without m_control.
    void renderEncoderFeedback(ActionContext const& ctx);

    /// Resolver: actionUuid -> {state count, DisableAutomaticStates}. Injected
    /// from Application (PluginManager::actionStateMeta). Drives the host-side
    /// automatic state cycle on keyUp (Elgato/OpenDeck: a 2-state action
    /// advances state on keyUp unless DisableAutomaticStates). {0,false} or an
    /// unset resolver disables cycling — graceful degradation.
    std::function<std::pair<int, bool>(QString const&)> m_actionStateMetaResolver;

    /// Paint the manifest-declared image for @p ctx's CURRENT state onto its
    /// key (Keypad contexts on the active device only) and re-apply the title
    /// overlay. No-op when the resolver is unset, resolves "", the image fails
    /// to load, or the controller is not Keypad. Shared by the mount-time
    /// default render (willAppear) and the keyUp automatic state cycle.
    void paintDeclaredStateImage(ActionContext const& ctx, std::uint8_t keyCols);

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

    /**
     * @brief Inject the manifest default-Settings resolver
     *        (PluginManager::defaultSettingsForAction).
     *
     * When set, willAppear settings composition gains a LAST fallback: persisted
     * store record -> binding default -> manifest action `Settings` block. This
     * mirrors the vendor StreamDock host, which seeds a brand-new instance with
     * the manifest defaults; MiraBox SDVueSDK plugins rely on it (their draw
     * path dereferences default keys unguarded and throws on empty settings,
     * so the 1 Hz repaint never starts). Unset => prior behaviour.
     *
     * @param resolver  actionUuid -> compact Settings JSON, or "" if none.
     */
    void setDefaultSettingsResolver(std::function<QString(QString const&)> resolver);

    /**
     * @brief Inject the device-geometry resolver (codename -> DeviceGeometry).
     *
     * When set, every coordinate<->keyIndex conversion and the
     * deviceDidConnect / -info device size source columns/rows/type from the
     * device's own core::DeviceDescriptor instead of the AKP05E-hardcoded
     * keyCols=5 (PLUGIN-GAP-ANALYSIS F2). Unset (or a codename the resolver
     * does not know) falls back to the AKP05E DeviceGeometry default, so
     * existing single-device fixtures and tests are unaffected.
     *
     * @param resolver  device codename -> DeviceGeometry.
     */
    void setDeviceGeometryResolver(std::function<DeviceGeometry(QString const&)> resolver);

    /**
     * @brief Build the Elgato `deviceInfo` JSON for @p deviceId.
     *
     * The canonical `{name,type,size:{columns,rows},columns,rows,encoders}`
     * object sent in `deviceDidConnect` (§4.4) and reused for the vendor
     * `passHello.deviceInfo` (B7) so a plugin reading geometry at hello gets the
     * real per-device grid instead of an empty `{}`. Sourced from the injected
     * geometry resolver; an unknown/empty codename yields the AKP05E default.
     *
     * @param deviceId  device codename, e.g. "akp05e".
     * @return the deviceInfo object (never empty for a resolvable device).
     */
    [[nodiscard]] QJsonObject deviceInfoFor(QString const& deviceId) const;

    /**
     * @brief Inject the action state-metadata resolver (PluginManager::actionStateMeta).
     *
     * When set, keyUp on a Keypad context whose action declares EXACTLY two
     * states (and not DisableAutomaticStates) advances the tracked state
     * (Elgato/OpenDeck automatic state cycle), auto-renders the new state's
     * declared image, and the keyUp envelope + a follow-up
     * titleParametersDidChange carry the NEW state. When unset, no cycling.
     *
     * @param resolver  actionUuid -> {stateCount, disableAutomaticStates}.
     */
    void setActionStateMetaResolver(std::function<std::pair<int, bool>(QString const&)> resolver);

    /**
     * @brief Inject the encoder layout/icon resolver (PluginManager::encoderLayoutInfo).
     *
     * Enables the built-in dial layouts: a dial action's manifest
     * `Encoder.layout` selects the initial layout ($X1 default) and
     * `Encoder.Icon` seeds the icon item; `setFeedbackLayout` / `setFeedback`
     * then drive the surface at runtime. When unset, dial actions render $X1
     * with whatever the plugin pushes.
     *
     * @param resolver  actionUuid -> {layout id, absolute icon path} ("" allowed).
     */
    void
    setEncoderLayoutResolver(std::function<std::pair<QString, QString>(QString const&)> resolver);
};

} // namespace ajazz::app

#endif // defined(AJAZZ_HAVE_WEBSOCKETS)
