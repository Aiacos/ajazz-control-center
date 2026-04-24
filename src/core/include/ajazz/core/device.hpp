// SPDX-License-Identifier: GPL-3.0-or-later
//
// Abstract device interface. Every concrete backend (stream deck, keyboard,
// mouse) implements IDevice and registers a factory with DeviceRegistry.
//
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ajazz::core {

/// Stable identifier for a physical device (VID/PID + serial).
struct DeviceId {
    std::uint16_t vendorId{0};
    std::uint16_t productId{0};
    std::string serial;

    [[nodiscard]] bool operator==(DeviceId const&) const = default;
};

enum class DeviceFamily : std::uint8_t {
    Unknown = 0,
    StreamDeck,
    Keyboard,
    Mouse,
};

/// Static descriptor of a supported device model.
struct DeviceDescriptor {
    std::uint16_t vendorId{0};
    std::uint16_t productId{0};
    DeviceFamily family{DeviceFamily::Unknown};
    std::string model;       ///< e.g. "AJAZZ AKP153"
    std::string codename;    ///< e.g. "akp153" (matches backend directory)
};

/// Event emitted by the device when user interaction happens.
struct DeviceEvent {
    enum class Kind : std::uint8_t {
        KeyPressed,
        KeyReleased,
        EncoderTurned,
        EncoderPressed,
        TouchStrip,
        Connected,
        Disconnected,
    };

    Kind kind{Kind::Connected};
    std::uint16_t index{0};  ///< button/encoder index, 0 when not applicable
    std::int32_t value{0};   ///< encoder delta / touch position
};

using EventCallback = std::function<void(DeviceEvent const&)>;

/// Common interface for every AJAZZ device backend.
class IDevice {
public:
    virtual ~IDevice() = default;

    IDevice(IDevice const&) = delete;
    IDevice& operator=(IDevice const&) = delete;
    IDevice(IDevice&&) = delete;
    IDevice& operator=(IDevice&&) = delete;

    [[nodiscard]] virtual DeviceDescriptor const& descriptor() const noexcept = 0;
    [[nodiscard]] virtual DeviceId id() const noexcept = 0;
    [[nodiscard]] virtual std::string firmwareVersion() const = 0;

    virtual void open()  = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    /// Register a callback to receive input events from this device.
    /// Thread-safety: callbacks are invoked from the transport reader thread.
    virtual void onEvent(EventCallback cb) = 0;

    /// Poll the device once; backends may also run their own reader thread.
    /// Returns the number of events emitted since the last poll.
    virtual std::size_t poll() = 0;

protected:
    IDevice() = default;
};

using DevicePtr = std::unique_ptr<IDevice>;

/// Factory used by DeviceRegistry to instantiate a backend for a given match.
using DeviceFactory = std::function<DevicePtr(DeviceDescriptor const&, DeviceId)>;

}  // namespace ajazz::core
