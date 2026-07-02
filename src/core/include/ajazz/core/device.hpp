// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file device.hpp
 * @brief Abstract device interface shared by all AJAZZ device backends.
 *
 * Every concrete backend (stream deck, keyboard, mouse) implements IDevice
 * and registers a factory with DeviceRegistry. Capability mix-ins are
 * declared separately in capabilities.hpp and queried via dynamic_cast.
 *
 * @see DeviceRegistry, IDevice
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ajazz::core {

/**
 * @brief Stable runtime identifier for a physical USB device.
 *
 * The combination of (vendorId, productId, serial) uniquely identifies a
 * connected device. Serial may be empty for devices that do not expose one.
 */
struct DeviceId {
    std::uint16_t vendorId{0};  ///< USB Vendor ID.
    std::uint16_t productId{0}; ///< USB Product ID.
    std::string serial;         ///< Device serial number string; may be empty.

    [[nodiscard]] bool operator==(DeviceId const&) const = default;
};

/// Broad product category, used by the UI to select an appropriate layout.
enum class DeviceFamily : std::uint8_t {
    Unknown = 0, ///< Unrecognised or not yet categorised.
    StreamDeck,  ///< AKP-family stream decks with per-key LCD displays.
    Keyboard,    ///< Programmable keyboards (VIA-compatible).
    Mouse,       ///< Gaming mice with DPI and button configuration.
};

/**
 * @brief Static descriptor of a supported device model.
 *
 * Registered with DeviceRegistry at startup. The UI uses codename to load
 * device-specific QML layouts and icon assets, and reads the capability
 * fields below to size grids and toggle tabs without hard-coded constants.
 */
struct DeviceDescriptor {
    std::uint16_t vendorId{0};                  ///< USB Vendor ID.
    std::uint16_t productId{0};                 ///< USB Product ID.
    DeviceFamily family{DeviceFamily::Unknown}; ///< Product category.
    std::string model;                          ///< Human-readable model name, e.g. "AJAZZ AKP153".
    std::string codename;                       ///< Short machine identifier, e.g. "akp153".

    // ---- Capability hints (UI sizing) -----------------------------------
    // These are static layout hints used by the QML UI to avoid magic
    // constants like `model: 15`. They should match the values reported by
    // the runtime IDevice/ICapability interfaces of the same backend.
    //
    // DRIFT WARNING: these fields duplicate runtime capability data with no
    // compile-time or test enforcement that the two agree, so they are a
    // latent drift surface — a backend whose runtime IDevice/ICapability
    // values change without a matching descriptor edit will silently
    // mis-size or mis-gate the UI. Keep this descriptor in lockstep with the
    // backend's actual capabilities (or, longer-term, derive these from the
    // runtime interface instead of restating them here).

    std::uint16_t keyCount{0};      ///< Number of LCD/macro keys (0 if N/A).
    std::uint16_t gridColumns{0};   ///< Preferred grid column count for the keys (0 if N/A).
    std::uint16_t encoderCount{0};  ///< Number of rotary encoders (0 if N/A).
    std::uint16_t dpiStageCount{0}; ///< Number of DPI stages (mice; 0 if N/A).
    bool hasRgb{false};             ///< True if the device exposes RGB lighting.
    bool hasTouchStrip{false};      ///< True if the device exposes a touch strip.
    bool hasClock{false};   ///< True if the device advertises Capability::Clock (scaffolded — see
                            ///< IClockCapable).
    bool hasBattery{false}; ///< True if the device advertises Capability::Battery (wireless devices
                            ///< with a host-readable charge level — see IBatteryCapable).
    bool hasSettings{false}; ///< True if the device advertises ISettingsCapable (AK-series
                             ///< settings batch — issue #57). UI uses this to gate the
                             ///< per-device Settings tab so non-capable devices don't render
                             ///< empty fake-functional rows.

    // ---- OpenDeck-pattern geometry (Phase 26) --------------------------------
    // These fields enable the geometry-driven DeviceView editor introduced in
    // Phase 26 to discriminate the three stacked row types: the key grid,
    // the encoder-dial row, and the touch-strip-zone row. They are additive
    // with zero defaults — existing aggregate-initialiser call sites compile
    // unchanged. AKP815 intentionally stays at keyRows=0 (deferred sentinel
    // per D-13: its 800x480 strip is one wide rect, not discrete zones).

    /// Number of LCD-key rows. When 0, the editor falls back to inferring
    /// rows from keyCount / gridColumns. Required for AKP815 (5x3 portrait)
    /// vs AKP05 (2x5 landscape) discrimination — relying on accident is fragile.
    /// AKP05/N4/AKP05E = 2; AKP153 family = 3; AKP03 family = 2;
    /// AKP815 = 0 (deferred sentinel per D-13).
    std::uint8_t keyRows{0};

    /// Number of discrete touch-strip zones aligned to encoder positions.
    /// 0 = no touch strip, or strip is a single wide rect (use mainScreenWidthPx).
    /// >0 = N discrete touchpoints in Stream Deck Plus style.
    /// AKP05/Mirabox N4/AKP05E = 4 (one zone per encoder);
    /// AKP815 = 0 (its strip is one 800x480 addressable rect, not 4 zones).
    std::uint8_t touchZoneCount{0};

    /// Width in pixels of the main LCD strip for wide-rect-addressable devices
    /// (e.g. AKP815's 800x480 strip). 0 if the device has no single-wide strip.
    /// Mutually exclusive with touchZoneCount in practice: a device has either
    /// per-encoder touch zones OR a single wide strip, not both.
    std::uint16_t mainScreenWidthPx{0};

    /// Height in pixels of the main LCD strip (companion to mainScreenWidthPx).
    /// 0 if absent.
    std::uint16_t mainScreenHeightPx{0};

    /// HID usage page of the vendor control interface, for composite devices
    /// that expose several HID interfaces/collections (0 = open the first
    /// matching interface, the default for single-interface devices). When
    /// non-zero, HidTransport opens the interface whose usage page matches via
    /// hid_enumerate + hid_open_path instead of a bare vid/pid open — which on a
    /// composite device picks the boot keyboard, not the vendor channel where
    /// feature reports (RTC, battery, RGB) live. Verified for the AK980 PRO:
    /// the control channel is usage page 0xFF13 (the boot keyboard is 0x0001).
    std::uint16_t controlUsagePage{0};

    /// HID usage (within @ref controlUsagePage) of the vendor control collection
    /// (0 = match by usage page only). Needed when a device exposes MULTIPLE
    /// collections sharing one usage page — e.g. the AJ-series mouse has two
    /// 0xFFFF collections (usage 2 = control, usage 1 = not), so the usage page
    /// alone is ambiguous and a re-enumeration can pick the wrong one.
    std::uint16_t controlUsage{0};
};

/**
 * @brief Input event emitted by a device backend when user interaction occurs.
 *
 * Delivered to all registered EventCallback handlers, optionally via the
 * EventBus. The semantics of `index` and `value` depend on the event kind.
 */
struct DeviceEvent {
    /// Discriminates the type of input that occurred.
    enum class Kind : std::uint8_t {
        KeyPressed,      ///< A key was depressed; `index` = 1-based key number.
        KeyReleased,     ///< A key was released; `index` = 1-based key number.
        EncoderTurned,   ///< Encoder rotated; `index` = encoder number, `value` = signed delta.
        EncoderPressed,  ///< Encoder knob depressed; `index` = encoder number.
        EncoderReleased, ///< Encoder knob released; `index` = encoder number.
        TouchDown,       ///< Touch-strip press began; `value` = X coordinate (0..255).
        TouchMove,       ///< Touch-strip contact moved; `value` = X coordinate (0..255).
        TouchUp,         ///< Touch-strip press ended;  `value` = X coordinate (0..255).
        Connected,       ///< Device became available on the bus.
        Disconnected,    ///< Device was removed or lost.
    };

    Kind kind{Kind::Connected};
    std::uint16_t index{0}; ///< Key or encoder index; 0 when not applicable.
    std::int32_t value{0};  ///< Encoder delta, touch X position, or press state.
};

/// Callback type for device input events. See IDevice::onEvent().
using EventCallback = std::function<void(DeviceEvent const&)>;

/**
 * @brief Common interface implemented by every AJAZZ device backend.
 *
 * Concrete backends (Akp153Device, Akp03Device, …) inherit IDevice and
 * optionally one or more capability mix-ins (IDisplayCapable, etc.).
 * DeviceRegistry constructs backends via registered DeviceFactory functions.
 *
 * IDevice instances are not copyable or movable; always hold via DevicePtr.
 *
 * @note Thread-safety: open()/close()/poll() are not thread-safe with
 *       respect to each other. onEvent() may be called from any thread
 *       before open(); the callback is invoked from the I/O thread.
 *
 * @note Zombie contract (D-06 / ARCH-03): each IDevice implementation MUST
 *       gate every HID I/O call on an internal alive flag (or equivalent
 *       hidapi handle validity check). After the underlying USB device
 *       disappears, public methods fail safe per the capability's own error
 *       model (a no-op, a `false` / empty-`std::optional` return, or a
 *       `TimeSyncResult` error plus a logged warning) rather than
 *       dereferencing closed handles. This is what
 *       lets the shared_ptr flyweight cache (DeviceRegistry::open) hand the
 *       same backend instance to multiple consumers safely across hot-plug
 *       events. See D-06 in 04-CONTEXT.md and ARCH-03.
 * @see DeviceRegistry, capabilities.hpp
 */
class IDevice {
public:
    virtual ~IDevice() = default;

    IDevice(IDevice const&) = delete;
    IDevice& operator=(IDevice const&) = delete;
    IDevice(IDevice&&) = delete;
    IDevice& operator=(IDevice&&) = delete;

    /// Return the static descriptor for this device model.
    [[nodiscard]] virtual DeviceDescriptor const& descriptor() const noexcept = 0;

    /// Return the runtime identifier (VID/PID/serial) of this instance.
    [[nodiscard]] virtual DeviceId id() const noexcept = 0;

    /// Return the firmware version string, or "unknown" if not yet queried.
    [[nodiscard]] virtual std::string firmwareVersion() const = 0;

    /**
     * @brief Open the underlying HID device.
     * @throws std::runtime_error if the device cannot be opened.
     * @post isOpen() == true on success.
     */
    virtual void open() = 0;

    /**
     * @brief Close the device and release the transport handle.
     *
     * Sends a best-effort "stop" command before closing so the device
     * returns to its idle state.
     */
    virtual void close() = 0;

    /// Return true when the device transport is open and ready.
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    /**
     * @brief Register a callback to receive input events from this device.
     *
     * Replaces any previously registered callback. Pass an empty
     * std::function to deregister.
     *
     * @param cb Handler invoked (from the I/O thread) for each DeviceEvent.
     */
    virtual void onEvent(EventCallback cb) = 0;

    /**
     * @brief Drain pending input reports from the transport.
     *
     * Backends may also run an internal reader thread; in that case poll()
     * is a no-op. Returns the number of events emitted to the callback.
     *
     * @return Count of events dispatched during this call.
     */
    virtual std::size_t poll() = 0;

protected:
    IDevice() = default;
};

/// Shared smart-pointer to a heap-allocated device backend.
///
/// Per ARCH-03 (atomic ownership migration) the registry slot ownership
/// changed from `std::unique_ptr<IDevice>` to `std::shared_ptr<IDevice>` so
/// that multiple consumers (KeyDesigner today, TimeSyncService in Phase 5)
/// can hold a stable handle to the same backend instance across event-loop
/// turns and across hot-plug events. The `DeviceRegistry::open()` flyweight
/// (D-06) caches a `weak_ptr<IDevice>` per (vendorId, productId) so the
/// same `(vid, pid)` always vends the same backend / one HID handle.
using DevicePtr = std::shared_ptr<IDevice>;

/**
 * @brief Factory function signature used by DeviceRegistry.
 *
 * Receives the static descriptor and runtime id of the device to construct.
 * Returns a fully initialised (but not yet open) DevicePtr (shared_ptr).
 */
using DeviceFactory = std::function<DevicePtr(DeviceDescriptor const&, DeviceId)>;

} // namespace ajazz::core
