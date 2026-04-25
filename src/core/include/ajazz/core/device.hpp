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
 * device-specific QML layouts and icon assets.
 */
struct DeviceDescriptor {
    std::uint16_t vendorId{0};                  ///< USB Vendor ID.
    std::uint16_t productId{0};                 ///< USB Product ID.
    DeviceFamily family{DeviceFamily::Unknown}; ///< Product category.
    std::string model;                          ///< Human-readable model name, e.g. "AJAZZ AKP153".
    std::string codename;                       ///< Short machine identifier, e.g. "akp153".
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
        KeyPressed,     ///< A key was depressed; `index` = 1-based key number.
        KeyReleased,    ///< A key was released; `index` = 1-based key number.
        EncoderTurned,  ///< Encoder rotated; `index` = encoder number, `value` = signed delta.
        EncoderPressed, ///< Encoder knob depressed; `value` = 1 (down) or 0 (up).
        TouchStrip,     ///< Touch-strip gesture; `value` encodes gesture + X coordinate.
        Connected,      ///< Device became available on the bus.
        Disconnected,   ///< Device was removed or lost.
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

/// Owning smart-pointer to a heap-allocated device backend.
using DevicePtr = std::unique_ptr<IDevice>;

/**
 * @brief Factory function signature used by DeviceRegistry.
 *
 * Receives the static descriptor and runtime id of the device to construct.
 * Returns a fully initialised (but not yet open) DevicePtr.
 */
using DeviceFactory = std::function<DevicePtr(DeviceDescriptor const&, DeviceId)>;

} // namespace ajazz::core
