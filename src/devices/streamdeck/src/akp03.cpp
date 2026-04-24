// SPDX-License-Identifier: GPL-3.0-or-later
//
// AJAZZ AKP03 / Mirabox N3 backend. 6 keys + 1 rotary encoder.
//
// The wire protocol is a close cousin of the AKP153 (same `CRT` prefix, same
// 512-byte packet size) with a different image codec (PNG 72×72) and an
// additional encoder. Everything here is reconstructed in a clean-room
// fashion from docs/protocols/streamdeck/akp03.md; no third-party code is
// incorporated. See `akp03_protocol.hpp` for the wire format.
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp03_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace ajazz::streamdeck {

namespace akp03 {

// -----------------------------------------------------------------------------
// Protocol helpers (pure functions, unit-tested without a real device).
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

std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                      std::uint16_t pngSize) {
    auto pkt = buildCmdHeader(CmdImagePng);
    // Big-endian 16-bit payload size at offsets 10..11, key index at 12.
    pkt[10] = static_cast<std::uint8_t>((pngSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(pngSize & 0xffu);
    pkt[12] = keyIndex;
    return pkt;
}

std::optional<InputEvent> parseInputReport(std::span<std::uint8_t const> frame) {
    if (frame.size() < 16) {
        return std::nullopt;
    }

    // ACK frames start with the ASCII word "ACK".
    if (frame[0] == 0x41 && frame[1] == 0x43 && frame[2] == 0x4b) {
        return std::nullopt;
    }

    auto const tag = frame[9];

    // Key events: tag is the 1-based key index (1..KeyCount). A separate
    // byte at offset 10 encodes the press/release edge (0x01 = press,
    // 0x00 = release). This mirrors the AKP153 behaviour documented in
    // docs/protocols/streamdeck/akp03.md.
    if (tag >= 1 && tag <= KeyCount) {
        bool const pressed = frame[10] != 0x00;
        InputEvent ev{};
        ev.kind = pressed ? InputEvent::Kind::KeyPressed : InputEvent::Kind::KeyReleased;
        ev.index = tag;
        return ev;
    }

    // Encoder events: tag in [0x20..0x2f]. Bit 0..3 is the encoder index
    // (always 0 on the AKP03, which has a single knob). Rotation is encoded
    // as a signed delta in byte 10 (+1 CW, 0xff CCW, 0 for press/release),
    // and byte 11 signals the button edge (0x01 = down, 0x00 = up).
    if ((tag & 0xf0u) == 0x20u) {
        auto const encIndex = static_cast<std::uint8_t>(tag & 0x0fu);
        auto const rot = static_cast<std::int8_t>(frame[10]);
        auto const btn = frame[11];

        InputEvent ev{};
        ev.index = encIndex;
        if (rot != 0) {
            ev.kind = InputEvent::Kind::EncoderTurned;
            ev.delta = rot;
            return ev;
        }
        ev.kind =
            btn != 0x00 ? InputEvent::Kind::EncoderPressed : InputEvent::Kind::EncoderReleased;
        return ev;
    }

    return std::nullopt;
}

} // namespace akp03

namespace {

using namespace ajazz::core;

// -----------------------------------------------------------------------------
// Akp03Device: wires protocol helpers to the transport and capability mix-ins.
// -----------------------------------------------------------------------------
class Akp03Device final : public IDevice, public IDisplayCapable, public IEncoderCapable {
public:
    Akp03Device(DeviceDescriptor descriptor, DeviceId id)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(makeHidTransport(m_id.vendorId, m_id.productId, m_id.serial)) {}

    // ---- IDevice ------------------------------------------------------------
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
        AJAZZ_LOG_INFO("akp03", "device opened: {}", m_descriptor.model);
    }

    void close() override {
        if (!m_transport->isOpen()) {
            return;
        }
        try {
            auto const stop = akp03::buildCmdHeader(akp03::CmdStop);
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
        std::array<std::uint8_t, akp03::PacketSize> buf{};
        std::size_t emitted = 0;
        for (int i = 0; i < 8; ++i) {
            auto const n = m_transport->read(buf, std::chrono::milliseconds{0});
            if (n == 0) {
                break;
            }
            auto ev = akp03::parseInputReport({buf.data(), n});
            if (!ev) {
                continue;
            }

            DeviceEvent devEv{};
            switch (ev->kind) {
            case akp03::InputEvent::Kind::KeyPressed:
                devEv.kind = DeviceEvent::Kind::KeyPressed;
                devEv.index = ev->index;
                break;
            case akp03::InputEvent::Kind::KeyReleased:
                devEv.kind = DeviceEvent::Kind::KeyReleased;
                devEv.index = ev->index;
                break;
            case akp03::InputEvent::Kind::EncoderTurned:
                devEv.kind = DeviceEvent::Kind::EncoderTurned;
                devEv.index = ev->index;
                devEv.value = ev->delta;
                break;
            case akp03::InputEvent::Kind::EncoderPressed:
                devEv.kind = DeviceEvent::Kind::EncoderPressed;
                devEv.index = ev->index;
                devEv.value = 1;
                break;
            case akp03::InputEvent::Kind::EncoderReleased:
                devEv.kind = DeviceEvent::Kind::EncoderPressed;
                devEv.index = ev->index;
                devEv.value = 0;
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
            .widthPx = akp03::KeyWidthPx,
            .heightPx = akp03::KeyHeightPx,
            .keyRows = 2,
            .keyCols = 3,
            .jpegEncoded = false, // AKP03 expects PNG blobs
        };
    }

    void setKeyImage(std::uint8_t keyIndex,
                     std::span<std::uint8_t const> rgba,
                     std::uint16_t width,
                     std::uint16_t height) override {
        // The caller is expected to pass already-PNG-encoded bytes for now.
        // When the image pipeline lands (phase 2) this method will resize to
        // 72×72 and encode to PNG itself.
        (void)width;
        (void)height;
        sendImage(keyIndex, rgba);
    }

    void setKeyColor(std::uint8_t keyIndex, Rgb color) override {
        // Without the image pipeline we fall back to clearing the key; the
        // full implementation will synthesise a tiny PNG of `color`.
        (void)color;
        clearKey(keyIndex);
    }

    void clearKey(std::uint8_t keyIndex) override {
        auto const pkt =
            (keyIndex == 0xff) ? akp03::buildClearAll() : akp03::buildClearKey(keyIndex);
        (void)m_transport->write(pkt);
    }

    void setMainImage(std::span<std::uint8_t const>, std::uint16_t, std::uint16_t) override {
        // AKP03 has no main display.
    }

    void setBrightness(std::uint8_t percent) override {
        auto const pkt = akp03::buildSetBrightness(percent);
        (void)m_transport->write(pkt);
    }

    void flush() override {
        auto const pkt = akp03::buildCmdHeader(akp03::CmdStop);
        (void)m_transport->write(pkt);
    }

    // ---- IEncoderCapable ----------------------------------------------------
    [[nodiscard]] EncoderInfo encoderInfo() const noexcept override {
        return EncoderInfo{
            .count = akp03::EncoderCount,
            .pressable = true,
            .hasScreens = false,
            .stepsPerRevolution = 0, // endless
        };
    }

    void setEncoderImage(std::uint8_t,
                         std::span<std::uint8_t const>,
                         std::uint16_t,
                         std::uint16_t) override {
        // No screen above the knob on AKP03.
    }

private:
    void sendImage(std::uint8_t keyIndex, std::span<std::uint8_t const> png) {
        auto const header = akp03::buildImageHeader(
            keyIndex, static_cast<std::uint16_t>(std::min<std::size_t>(png.size(), 0xffff)));
        (void)m_transport->write(header);

        std::size_t offset = 0;
        while (offset < png.size()) {
            std::array<std::uint8_t, akp03::PacketSize> chunk{};
            auto const take = std::min<std::size_t>(akp03::PacketSize, png.size() - offset);
            std::memcpy(chunk.data(), png.data() + offset, take);
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

core::DevicePtr makeAkp03(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_unique<Akp03Device>(d, std::move(id));
}

} // namespace ajazz::streamdeck
