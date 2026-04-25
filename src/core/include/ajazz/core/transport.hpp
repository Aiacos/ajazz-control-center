// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file transport.hpp
 * @brief Abstract byte-stream transport interface for USB device communication.
 *
 * The default concrete implementation is HidTransport (libhidapi backend).
 * Alternative transports — libusb raw, WinUSB, or a virtual capture-replay
 * stub for unit tests — can be substituted behind this interface without
 * touching device backend code.
 *
 * @see makeHidTransport, IDevice
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace ajazz::core {

/**
 * @brief Cumulative I/O counters for a transport session.
 *
 * All counters are monotonically increasing; they are never reset between
 * open/close cycles. Useful for diagnostics and rate-limiting heuristics.
 */
struct TransportStats {
    std::uint64_t bytesSent{0};     ///< Total bytes written via write() and writeFeature().
    std::uint64_t bytesReceived{0}; ///< Total bytes read via read() and readFeature().
    std::uint64_t errors{0};        ///< Total transport-level errors (write or read failures).
};

/**
 * @brief Abstract USB transport; devices communicate exclusively through this.
 *
 * HID output reports go through write(), feature reports go through
 * writeFeature(). Input reports arrive via the blocking read() with a
 * timeout of zero (non-blocking) or a positive duration.
 *
 * @note ITransport instances are neither copyable nor movable.
 * @see makeHidTransport
 */
class ITransport {
public:
    virtual ~ITransport() = default;

    ITransport(ITransport const&) = delete;
    ITransport& operator=(ITransport const&) = delete;
    ITransport(ITransport&&) = delete;
    ITransport& operator=(ITransport&&) = delete;

    /**
     * @brief Open the underlying transport handle.
     * @throws std::runtime_error if the device cannot be opened.
     * @post isOpen() == true on success.
     */
    virtual void open() = 0;

    /**
     * @brief Close the transport and release the device handle.
     * @post isOpen() == false.
     */
    virtual void close() = 0;

    /// Return true if the transport is open and ready for I/O.
    [[nodiscard]] virtual bool isOpen() const noexcept = 0;

    /**
     * @brief Send a raw HID output report.
     *
     * For HID output reports the first byte of `data` must be the report id
     * (0x00 for single-report devices). Use writeFeature() for HID feature
     * reports.
     *
     * @param data Raw report bytes including the report-id byte.
     * @return Number of bytes actually sent.
     * @throws std::runtime_error on transport failure.
     */
    virtual std::size_t write(std::span<std::uint8_t const> data) = 0;

    /**
     * @brief Attempt to read one HID input report within a timeout.
     *
     * @param out     Buffer to receive the report bytes.
     * @param timeout Maximum time to wait; pass zero for non-blocking.
     * @return Number of bytes read, or 0 on timeout.
     * @throws std::runtime_error on device error (distinct from timeout).
     */
    virtual std::size_t read(std::span<std::uint8_t> out, std::chrono::milliseconds timeout) = 0;

    /**
     * @brief Send a HID feature report (hid_send_feature_report).
     * @param data Raw feature report bytes including the report-id byte.
     * @return Number of bytes actually sent.
     * @throws std::runtime_error on transport failure.
     */
    virtual std::size_t writeFeature(std::span<std::uint8_t const> data) = 0;

    /**
     * @brief Read a HID feature report (hid_get_feature_report).
     * @param out Buffer to receive the feature report.
     * @return Number of bytes read.
     * @throws std::runtime_error on transport failure.
     */
    virtual std::size_t readFeature(std::span<std::uint8_t> out) = 0;

    /**
     * @brief Return cumulative I/O counters.
     * @return Snapshot of TransportStats at the time of the call.
     */
    [[nodiscard]] virtual TransportStats stats() const noexcept = 0;

protected:
    ITransport() = default;
};

/// Owning smart-pointer to an ITransport implementation.
using TransportPtr = std::unique_ptr<ITransport>;

} // namespace ajazz::core
