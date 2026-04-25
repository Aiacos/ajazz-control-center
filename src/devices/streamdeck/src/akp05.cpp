// SPDX-License-Identifier: GPL-3.0-or-later
//
// AJAZZ AKP05 / AKP05E "Stream Dock Plus"-class backend.
//
// 15 keys (85×85 JPEG, reusing the AKP153 image format), 4 endless rotary
// encoders with tiny LCDs above them, one touch strip and one main LCD.
//
// The wire protocol below is a clean-room reconstruction from the notes in
// docs/protocols/streamdeck/akp05.md; no third-party source is incorporated.
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp05_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace ajazz::streamdeck {

namespace akp05 {

// -----------------------------------------------------------------------------
// Protocol helpers — pure functions, covered by unit tests.
// -----------------------------------------------------------------------------
std::array<std::uint8_t, PacketSize> buildCmdHeader(std::array<std::uint8_t, 3> const& cmd) {
    std::array<std::uint8_t, PacketSize> pkt{};
    pkt[0] = CmdPrefix[0];
    pkt[1] = CmdPrefix[1];
    pkt[2] = CmdPrefix[2];
    pkt[5] = cmd[0];
    pkt[6] = cmd[1];
    pkt[7] = cmd[2];
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent) {
    auto pkt = buildCmdHeader(CmdLight);
    pkt[10] = std::min<std::uint8_t>(percent, 100);
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildClearAll() {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x00;
    pkt[11] = 0xff;
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex) {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x00;
    pkt[11] = keyIndex;
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildKeyImageHeader(std::uint8_t keyIndex,
                                                         std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdKeyImage);
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    pkt[12] = keyIndex;
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildEncoderImageHeader(std::uint8_t encoderIndex,
                                                             std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdEncImage);
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    pkt[12] = encoderIndex;
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildMainImageHeader(std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdMainImage);
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    return pkt;
}

std::optional<InputEvent> parseInputReport(std::span<std::uint8_t const> frame) {
    if (frame.size() < 16) {
        return std::nullopt;
    }

    // ACK frames.
    if (frame[0] == 0x41 && frame[1] == 0x43 && frame[2] == 0x4b) {
        return std::nullopt;
    }

    auto const tag = frame[9];

    // Key events: 1-based key index 1..KeyCount. Byte 10 = press/release edge.
    if (tag >= 1 && tag <= KeyCount) {
        bool const pressed = frame[10] != 0x00;
        InputEvent ev{};
        ev.kind = pressed ? InputEvent::Kind::KeyPressed : InputEvent::Kind::KeyReleased;
        ev.index = tag;
        return ev;
    }

    // Encoder events: tag in [0x20..0x2f]. Bits 0..3 = encoder index. Byte 10 =
    // signed rotation delta (+1 CW, 0xff CCW, 0 for press/release). Byte 11 =
    // button edge (0x01 down, 0x00 up).
    if ((tag & 0xf0u) == 0x20u) {
        auto const encIndex = static_cast<std::uint8_t>(tag & 0x0fu);
        if (encIndex >= EncoderCount) {
            return std::nullopt;
        }
        auto const rot = static_cast<std::int8_t>(frame[10]);
        auto const btn = frame[11];

        InputEvent ev{};
        ev.index = encIndex;
        if (rot != 0) {
            ev.kind = InputEvent::Kind::EncoderTurned;
            ev.value = rot;
            return ev;
        }
        ev.kind =
            btn != 0x00 ? InputEvent::Kind::EncoderPressed : InputEvent::Kind::EncoderReleased;
        return ev;
    }

    // Touch-strip events: tag in [0x30..0x3f]. Low nibble is the gesture:
    //   0x0 = tap, 0x1 = swipe-left, 0x2 = swipe-right, 0x3 = long-press.
    // Bytes 10..11 (big-endian) carry the absolute X coordinate (0..639).
    if ((tag & 0xf0u) == 0x30u) {
        auto const gesture = static_cast<std::uint8_t>(tag & 0x0fu);
        auto const x = static_cast<std::uint16_t>((static_cast<std::uint32_t>(frame[10]) << 8U) |
                                                  static_cast<std::uint32_t>(frame[11]));

        InputEvent ev{};
        ev.value = static_cast<std::int16_t>(x);
        switch (gesture) {
        case 0x0:
            ev.kind = InputEvent::Kind::TouchTap;
            break;
        case 0x1:
            ev.kind = InputEvent::Kind::TouchSwipeLeft;
            break;
        case 0x2:
            ev.kind = InputEvent::Kind::TouchSwipeRight;
            break;
        case 0x3:
            ev.kind = InputEvent::Kind::TouchLongPress;
            break;
        default:
            return std::nullopt;
        }
        return ev;
    }

    return std::nullopt;
}

} // namespace akp05

namespace {

using namespace ajazz::core;

// -----------------------------------------------------------------------------
// Akp05Device: glues the protocol to ITransport and the capability mix-ins.
// -----------------------------------------------------------------------------
class Akp05Device final : public IDevice, public IDisplayCapable, public IEncoderCapable {
public:
    Akp05Device(DeviceDescriptor descriptor, DeviceId id)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(makeHidTransport(m_id.vendorId, m_id.productId, m_id.serial)) {}

    [[nodiscard]] DeviceDescriptor const& descriptor() const noexcept override {
        return m_descriptor;
    }
    [[nodiscard]] DeviceId id() const noexcept override { return m_id; }
    [[nodiscard]] std::string firmwareVersion() const override { return "unknown"; }

    void open() override {
        if (m_transport->isOpen()) {
            return;
        }
        m_transport->open();
        AJAZZ_LOG_INFO("akp05", "device opened: {}", m_descriptor.model);
    }

    void close() override {
        if (!m_transport->isOpen()) {
            return;
        }
        try {
            auto const stop = akp05::buildCmdHeader(akp05::CmdStop);
            (void)m_transport->write(stop);
        } catch (...) { /* best-effort */
        }
        m_transport->close();
    }

    [[nodiscard]] bool isOpen() const noexcept override { return m_transport->isOpen(); }

    void onEvent(EventCallback cb) override {
        std::lock_guard const lock(m_mutex);
        m_callback = std::move(cb);
    }

    std::size_t poll() override {
        std::array<std::uint8_t, akp05::PacketSize> buf{};
        std::size_t emitted = 0;
        for (int i = 0; i < 8; ++i) {
            auto const n = m_transport->read(buf, std::chrono::milliseconds{0});
            if (n == 0) {
                break;
            }
            auto ev = akp05::parseInputReport({buf.data(), n});
            if (!ev) {
                continue;
            }
            DeviceEvent devEv{};
            devEv.index = ev->index;
            devEv.value = ev->value;
            switch (ev->kind) {
            case akp05::InputEvent::Kind::KeyPressed:
                devEv.kind = DeviceEvent::Kind::KeyPressed;
                break;
            case akp05::InputEvent::Kind::KeyReleased:
                devEv.kind = DeviceEvent::Kind::KeyReleased;
                break;
            case akp05::InputEvent::Kind::EncoderTurned:
                devEv.kind = DeviceEvent::Kind::EncoderTurned;
                break;
            case akp05::InputEvent::Kind::EncoderPressed:
                devEv.kind = DeviceEvent::Kind::EncoderPressed;
                devEv.value = 1;
                break;
            case akp05::InputEvent::Kind::EncoderReleased:
                devEv.kind = DeviceEvent::Kind::EncoderPressed;
                devEv.value = 0;
                break;
            case akp05::InputEvent::Kind::TouchTap:
            case akp05::InputEvent::Kind::TouchSwipeLeft:
            case akp05::InputEvent::Kind::TouchSwipeRight:
            case akp05::InputEvent::Kind::TouchLongPress:
                devEv.kind = DeviceEvent::Kind::TouchStrip;
                // Encode gesture type in the upper 16 bits of value; raw X
                // coordinate in the lower 16 bits so downstream consumers
                // can recover both without widening the event struct.
                {
                    std::uint32_t const gesture =
                        static_cast<std::uint32_t>(ev->kind) -
                        static_cast<std::uint32_t>(akp05::InputEvent::Kind::TouchTap);
                    std::uint32_t const x = static_cast<std::uint16_t>(ev->value);
                    devEv.value = static_cast<std::int32_t>((gesture << 16) | x);
                }
                break;
            }

            EventCallback cb;
            {
                std::lock_guard const lock(m_mutex);
                cb = m_callback;
            }
            if (cb) {
                cb(devEv);
            }
            ++emitted;
        }
        return emitted;
    }

    // ---- IDisplayCapable ----------------------------------------------------
    [[nodiscard]] DisplayInfo displayInfo() const noexcept override {
        return DisplayInfo{
            .widthPx = akp05::KeyWidthPx,
            .heightPx = akp05::KeyHeightPx,
            .keyRows = 3,
            .keyCols = 5,
            .jpegEncoded = true,
        };
    }

    void setKeyImage(std::uint8_t keyIndex,
                     std::span<std::uint8_t const> rgba,
                     std::uint16_t width,
                     std::uint16_t height) override {
        (void)width;
        (void)height;
        sendImage(
            akp05::buildKeyImageHeader(
                keyIndex, static_cast<std::uint16_t>(std::min<std::size_t>(rgba.size(), 0xffff))),
            rgba);
    }

    void setKeyColor(std::uint8_t keyIndex, Rgb color) override {
        (void)color;
        clearKey(keyIndex);
    }

    void clearKey(std::uint8_t keyIndex) override {
        auto const pkt =
            (keyIndex == 0xff) ? akp05::buildClearAll() : akp05::buildClearKey(keyIndex);
        (void)m_transport->write(pkt);
    }

    void setMainImage(std::span<std::uint8_t const> jpeg,
                      std::uint16_t width,
                      std::uint16_t height) override {
        (void)width;
        (void)height;
        sendImage(akp05::buildMainImageHeader(
                      static_cast<std::uint16_t>(std::min<std::size_t>(jpeg.size(), 0xffff))),
                  jpeg);
    }

    void setBrightness(std::uint8_t percent) override {
        auto const pkt = akp05::buildSetBrightness(percent);
        (void)m_transport->write(pkt);
    }

    void flush() override {
        auto const pkt = akp05::buildCmdHeader(akp05::CmdStop);
        (void)m_transport->write(pkt);
    }

    // ---- IEncoderCapable ----------------------------------------------------
    [[nodiscard]] EncoderInfo encoderInfo() const noexcept override {
        return EncoderInfo{
            .count = akp05::EncoderCount,
            .pressable = true,
            .hasScreens = true,
            .stepsPerRevolution = 0, // endless
        };
    }

    void setEncoderImage(std::uint8_t encoderIndex,
                         std::span<std::uint8_t const> rgba,
                         std::uint16_t width,
                         std::uint16_t height) override {
        (void)width;
        (void)height;
        sendImage(akp05::buildEncoderImageHeader(
                      encoderIndex,
                      static_cast<std::uint16_t>(std::min<std::size_t>(rgba.size(), 0xffff))),
                  rgba);
    }

private:
    void sendImage(std::array<std::uint8_t, akp05::PacketSize> const& header,
                   std::span<std::uint8_t const> payload) {
        (void)m_transport->write(header);
        std::size_t offset = 0;
        while (offset < payload.size()) {
            std::array<std::uint8_t, akp05::PacketSize> chunk{};
            auto const take = std::min<std::size_t>(akp05::PacketSize, payload.size() - offset);
            std::memcpy(chunk.data(), payload.data() + offset, take);
            (void)m_transport->write(chunk);
            offset += take;
        }
    }

    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    EventCallback m_callback;
    std::mutex m_mutex;
};

} // namespace

core::DevicePtr makeAkp05(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_unique<Akp05Device>(d, std::move(id));
}

} // namespace ajazz::streamdeck
