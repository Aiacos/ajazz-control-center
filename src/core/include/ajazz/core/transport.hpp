// SPDX-License-Identifier: GPL-3.0-or-later
//
// Abstract transport layer. The default implementation is `HidTransport`
// built on top of libhidapi. Other transports (libusb, WinUSB, virtual
// capture-replay for tests) can be plugged in behind this interface.
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace ajazz::core {

struct TransportStats {
    std::uint64_t bytesSent{0};
    std::uint64_t bytesReceived{0};
    std::uint64_t errors{0};
};

class ITransport {
public:
    virtual ~ITransport() = default;

    ITransport(ITransport const&) = delete;
    ITransport& operator=(ITransport const&) = delete;
    ITransport(ITransport&&) = delete;
    ITransport& operator=(ITransport&&) = delete;

    virtual void open() = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    /// Send a raw report. For HID output reports the first byte is the report
    /// id; for feature reports callers must use writeFeature() instead.
    virtual std::size_t write(std::span<std::uint8_t const> data) = 0;

    /// Blocking read with timeout. Returns the number of bytes read; 0 on
    /// timeout, or throws on device error.
    virtual std::size_t read(std::span<std::uint8_t> out, std::chrono::milliseconds timeout) = 0;

    virtual std::size_t writeFeature(std::span<std::uint8_t const> data) = 0;
    virtual std::size_t readFeature(std::span<std::uint8_t> out) = 0;

    [[nodiscard]] virtual TransportStats stats() const noexcept = 0;

protected:
    ITransport() = default;
};

using TransportPtr = std::unique_ptr<ITransport>;

} // namespace ajazz::core

// <chrono> is kept out of the public surface by forward-declaring the type
// above; include it here so consumers do not need to.
#include <chrono> // IWYU pragma: keep
