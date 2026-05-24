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

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>

#include <cstdint>
#include <optional>

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
    QString deviceId;   ///< Active device codename, e.g. "akp05e".
    QString pageId;     ///< Profile page id; "root" for the root page.
    int row{0};         ///< 0-based Elgato row coordinate.
    int column{0};      ///< 0-based Elgato column coordinate.
    QString controller; ///< "Keypad" | "Encoder"
    QString actionUUID; ///< Dotted action id, e.g. com.vendor.plugin.action.
    QString pluginUuid; ///< Owning plugin uuid, e.g. com.vendor.plugin.
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
     * @brief Look up the opaque context string for a given controller + coords.
     *
     * Used when a DeviceEvent arrives and the bridge needs to find the plugin
     * that owns the pressed key/encoder (outbound direction).
     *
     * @param controller  "Keypad" or "Encoder".
     * @param row         0-based row (Keypad) or 0 (Encoder, see note below).
     * @param column      0-based column (Keypad) or encoder index (Encoder).
     * @return            The matching ActionContext if one is registered;
     *                    std::nullopt otherwise.
     */
    [[nodiscard]] std::optional<ActionContext>
    byCoord(QString const& controller, int row, int column) const;

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

    /// @return Number of currently registered contexts.
    [[nodiscard]] int size() const noexcept;

private:
    /// Derive the stable encoded-tuple context id from an ActionContext.
    static QString deriveContextId(ActionContext const& ctx);

    /// Derive the coord-index key used in m_byCoord.
    static QString coordKey(QString const& controller, int row, int column);

    /// context-string -> ActionContext (inbound resolve from setImage's `context`).
    QHash<QString, ActionContext> m_byContext;

    /// controller+"#"+row+"#"+col -> context-string (outbound: DeviceEvent -> context).
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
 * Construction: the service pointers are commented "wired in 19-02"; the
 * constructor body is a stub in this plan so the QObject compiles and the
 * pure helpers + ContextRegistry are already unit-testable.
 *
 * COD-031: app-layer QJson/QImage only; never nlohmann.
 */
class PluginDeviceBridge : public QObject {
    Q_OBJECT

public:
    /**
     * @brief Construct the bridge shell (Phase 19-01 — seam pointers wired in 19-02).
     *
     * @param server   Phase-17 SdPluginServer (wired in 19-02).
     * @param control  Phase-14 StreamDockControlService (wired in 19-02).
     * @param input    Phase-15 StreamDockInputService (wired in 19-02).
     * @param parent   QObject parent for lifetime management.
     */
    explicit PluginDeviceBridge(SdPluginServer* server,            // wired in 19-02
                                StreamDockControlService* control, // wired in 19-02
                                StreamDockInputService* input,     // wired in 19-02
                                QObject* parent = nullptr);

    ~PluginDeviceBridge() override;

    /// Access the context registry (for testing and Phase 19-02/03 use).
    [[nodiscard]] ContextRegistry& registry() noexcept;
    [[nodiscard]] ContextRegistry const& registry() const noexcept;

private:
    SdPluginServer* m_server{nullptr};            // Phase 17 seam (wired in 19-02)
    StreamDockControlService* m_control{nullptr}; // Phase 14 seam (wired in 19-02)
    StreamDockInputService* m_input{nullptr};     // Phase 15 seam (wired in 19-02)

    ContextRegistry m_registry;
};

} // namespace ajazz::app
