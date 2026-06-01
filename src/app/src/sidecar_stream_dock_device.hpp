// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file sidecar_stream_dock_device.hpp
 * @brief core::IDevice backed by the out-of-process streamdock-host sidecar.
 *
 * experiment/mirajazz Slice 3b. Implements the AKP05/N4 device by spawning the
 * Rust `streamdock-host` binary (built on mirajazz) and speaking the
 * newline-delimited JSON protocol from sidecar_protocol.hpp over QProcess
 * stdin/stdout. Holds ONE persistent handle for the device's lifetime — the
 * hardware-confirmed fix for the per-interaction open/close churn that wedges
 * the panel under the legacy in-tree C++ backend.
 *
 * Not a QObject: QProcess signals are wired via context-object functor
 * connects, so no MOC pass is needed. Lives in the app tier (Qt-Core only, no
 * nlohmann — COD-031); composed by a core::DeviceFactory the app registers
 * (Slice 4 flips the AKP05 VID/PID from makeAkp05 to this).
 */
#pragma once

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>

class QProcess;

namespace ajazz::app {

/// Injectable resolver for the streamdock-host binary path (tests fake it).
using SidecarBinaryResolver = std::function<QString()>;

/// Default resolution order: $AJAZZ_STREAMDOCK_HOST -> next to the app
/// executable -> PATH -> bare "streamdock-host" (let QProcess fail).
[[nodiscard]] QString defaultSidecarBinary();

/**
 * @brief AKP05/N4 backend proxied to the mirajazz sidecar process.
 *
 * @note Construct/use on the GUI thread: QProcess signals are delivered on the
 *       thread that owns the QProcess, and open() drives a short blocking
 *       handshake via waitForReadyRead.
 */
class SidecarStreamDockDevice final : public core::IDevice,
                                      public core::IDisplayCapable,
                                      public core::IEncoderCapable {
public:
    SidecarStreamDockDevice(core::DeviceDescriptor descriptor,
                            core::DeviceId id,
                            SidecarBinaryResolver resolver = {});
    ~SidecarStreamDockDevice() override;

    // --- IDevice ---------------------------------------------------------
    [[nodiscard]] core::DeviceDescriptor const& descriptor() const noexcept override;
    [[nodiscard]] core::DeviceId id() const noexcept override;
    [[nodiscard]] std::string firmwareVersion() const override;
    void open() override;
    void close() override;
    [[nodiscard]] bool isOpen() const noexcept override;
    void onEvent(core::EventCallback cb) override;
    std::size_t poll() override;

    // --- IDisplayCapable -------------------------------------------------
    [[nodiscard]] core::DisplayInfo displayInfo() const noexcept override;
    void setKeyImage(std::uint8_t keyIndex,
                     std::span<std::uint8_t const> rgba,
                     std::uint16_t width,
                     std::uint16_t height) override;
    void setKeyColor(std::uint8_t keyIndex, core::Rgb color) override;
    void clearKey(std::uint8_t keyIndex) override;
    void setMainImage(std::span<std::uint8_t const> rgba,
                      std::uint16_t width,
                      std::uint16_t height) override;
    void setBrightness(std::uint8_t percent) override;
    void flush() override;
    void keepAlive() override;

    // --- IEncoderCapable -------------------------------------------------
    [[nodiscard]] core::EncoderInfo encoderInfo() const noexcept override;
    void setEncoderImage(std::uint8_t index,
                         std::span<std::uint8_t const> rgba,
                         std::uint16_t width,
                         std::uint16_t height) override;

private:
    [[nodiscard]] QString effectiveSerial() const;
    void writeCommand(QByteArray const& line);
    void drainStdout();
    void handleLine(QByteArray const& line);

    core::DeviceDescriptor m_descriptor;
    core::DeviceId m_id;
    SidecarBinaryResolver m_resolver;
    std::unique_ptr<QProcess> m_process;
    QByteArray m_lineBuffer;
    QString m_sidecarSerial; ///< Serial reported by the sidecar's connected event.

    mutable std::mutex m_mutex; ///< Guards m_firmwareVersion + m_callback.
    std::string m_firmwareVersion{"unknown"};
    core::EventCallback m_callback;
    bool m_ready{false}; ///< Set true once the sidecar emits its "ready" event.
};

/// core::DeviceFactory entry point — see DeviceRegistry::registerDevice.
[[nodiscard]] core::DevicePtr makeSidecarStreamDock(core::DeviceDescriptor const& d,
                                                    core::DeviceId id);

} // namespace ajazz::app
