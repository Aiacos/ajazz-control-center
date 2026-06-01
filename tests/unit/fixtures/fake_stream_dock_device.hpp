// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file fake_stream_dock_device.hpp
 * @brief In-process Stream Dock device fixture (no transport, no process).
 *
 * experiment/mirajazz Slice B. Replaces makeAkp05WithTransport as the unit-test
 * fixture for app-behavior assertions once the C++ AKP05 wire backend is
 * removed. Unlike the real backends (and the QProcess sidecar) this records the
 * *capability calls* it receives — setKeyImage / setBrightness / clearKey /
 * setEncoderImage / flush — rather than producing wire bytes, so tests assert
 * behavior ("profileChanged repainted all bound keys") instead of byte
 * sequences (that wire coverage moves to the Rust sidecar's cargo tests).
 *
 * Input is injectable: tests call injectEvent() to push a DeviceEvent through
 * the registered EventCallback, exercising StreamDockInputService end to end
 * without any hardware or decode layer.
 */
#pragma once

#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace ajazz::tests {

/// Records every capability call; drives input via injectEvent().
class FakeStreamDockDevice final : public core::IDevice,
                                   public core::IDisplayCapable,
                                   public core::IEncoderCapable,
                                   public core::ITouchStripDisplayCapable {
public:
    // ---- Recorded calls (public for direct assertion) -------------------
    struct ImageCall {
        std::uint8_t index{0};
        std::uint16_t width{0};
        std::uint16_t height{0};
        std::size_t byteCount{0};
    };
    std::vector<ImageCall> keyImages;                          ///< setKeyImage calls.
    std::vector<ImageCall> encoderImages;                      ///< setEncoderImage calls.
    std::vector<std::pair<std::uint8_t, core::Rgb>> keyColors; ///< setKeyColor calls.
    std::vector<std::uint8_t> clearedKeys;                     ///< clearKey args (0xFF = all).
    std::vector<std::uint8_t> brightnessCalls;                 ///< setBrightness args.
    int flushCount{0};
    int keepAliveCount{0};
    int openCount{0};
    int closeCount{0};

    FakeStreamDockDevice(core::DeviceDescriptor descriptor, core::DeviceId id)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)) {}

    /// Set the firmware string returned by firmwareVersion().
    void setFirmwareVersion(std::string fw) { m_firmware = std::move(fw); }

    /// Push a synthetic input event through the registered callback.
    void injectEvent(core::DeviceEvent const& ev) const {
        if (m_callback) {
            m_callback(ev);
        }
    }

    // ---- IDevice --------------------------------------------------------
    [[nodiscard]] core::DeviceDescriptor const& descriptor() const noexcept override {
        return m_descriptor;
    }
    [[nodiscard]] core::DeviceId id() const noexcept override { return m_id; }
    [[nodiscard]] std::string firmwareVersion() const override { return m_firmware; }
    void open() override {
        ++openCount;
        m_open = true;
    }
    void close() override {
        ++closeCount;
        m_open = false;
    }
    [[nodiscard]] bool isOpen() const noexcept override { return m_open; }
    void onEvent(core::EventCallback cb) override { m_callback = std::move(cb); }
    std::size_t poll() override { return 0; }

    // ---- IDisplayCapable ------------------------------------------------
    [[nodiscard]] core::DisplayInfo displayInfo() const noexcept override {
        core::DisplayInfo info{};
        info.widthPx = 112;
        info.heightPx = 112;
        info.keyRows = m_descriptor.keyRows;
        info.keyCols = static_cast<std::uint8_t>(m_descriptor.gridColumns);
        info.jpegEncoded = true;
        return info;
    }
    void setKeyImage(std::uint8_t keyIndex,
                     std::span<std::uint8_t const> rgba,
                     std::uint16_t width,
                     std::uint16_t height) override {
        keyImages.push_back({keyIndex, width, height, rgba.size()});
    }
    void setKeyColor(std::uint8_t keyIndex, core::Rgb color) override {
        keyColors.emplace_back(keyIndex, color);
    }
    void clearKey(std::uint8_t keyIndex) override { clearedKeys.push_back(keyIndex); }
    void setMainImage(std::span<std::uint8_t const> rgba,
                      std::uint16_t width,
                      std::uint16_t height) override {
        mainImages.push_back({0, width, height, rgba.size()});
    }
    void setBrightness(std::uint8_t percent) override { brightnessCalls.push_back(percent); }
    void flush() override { ++flushCount; }
    void keepAlive() override { ++keepAliveCount; }

    // ---- IEncoderCapable ------------------------------------------------
    [[nodiscard]] core::EncoderInfo encoderInfo() const noexcept override {
        core::EncoderInfo info{};
        info.count = static_cast<std::uint8_t>(m_descriptor.encoderCount);
        info.pressable = true;
        info.hasScreens = true;
        info.stepsPerRevolution = 0;
        return info;
    }
    void setEncoderImage(std::uint8_t index,
                         std::span<std::uint8_t const> rgba,
                         std::uint16_t width,
                         std::uint16_t height) override {
        // Honour the backend contract: out-of-range encoder index is a no-op.
        if (index >= m_descriptor.encoderCount) {
            return;
        }
        encoderImages.push_back({index, width, height, rgba.size()});
    }

    // ---- ITouchStripDisplayCapable --------------------------------------
    std::vector<ImageCall> touchStripImages; ///< setTouchStripImage calls (index = zone/location).
    int clearTouchStripCount{0};

    [[nodiscard]] core::TouchStripInfo touchStripInfo() const noexcept override {
        core::TouchStripInfo info{};
        info.widthPx = 800;
        info.heightPx = 480;
        info.zoneCount = m_descriptor.touchZoneCount;
        return info;
    }
    bool setTouchStripImage(std::span<std::uint8_t const> rgba,
                            std::uint16_t srcWidth,
                            std::uint16_t srcHeight,
                            std::uint8_t location,
                            std::uint16_t /*x*/,
                            std::uint16_t /*y*/,
                            std::uint16_t /*rectWidth*/,
                            std::uint16_t /*rectHeight*/) override {
        if (location >= m_descriptor.touchZoneCount) {
            return false;
        }
        touchStripImages.push_back({location, srcWidth, srcHeight, rgba.size()});
        return true;
    }
    bool clearTouchStrip() override {
        ++clearTouchStripCount;
        return true;
    }

    std::vector<ImageCall> mainImages; ///< setMainImage calls (no-op surfaces still recorded).

private:
    core::DeviceDescriptor m_descriptor;
    core::DeviceId m_id;
    std::string m_firmware{"V3.FAKE.00.000"};
    core::EventCallback m_callback;
    bool m_open{false};
};

} // namespace ajazz::tests
