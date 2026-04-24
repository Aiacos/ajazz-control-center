// SPDX-License-Identifier: GPL-3.0-or-later
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp153_protocol.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace ajazz::streamdeck {

namespace {

using namespace ajazz::core;
using namespace ajazz::streamdeck::akp153;

// -----------------------------------------------------------------------------
// Protocol helpers
// -----------------------------------------------------------------------------
std::array<std::uint8_t, PacketSize> emptyPacket() noexcept {
    std::array<std::uint8_t, PacketSize> pkt{};
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildCmdHeader(std::array<std::uint8_t, 3> const& cmd) {
    auto pkt = emptyPacket();
    pkt[0] = CmdPrefix[0];
    pkt[1] = CmdPrefix[1];
    pkt[2] = CmdPrefix[2];
    pkt[3] = 0x00;
    pkt[4] = 0x00;
    pkt[5] = cmd[0];
    pkt[6] = cmd[1];
    pkt[7] = cmd[2];
    return pkt;
}

} // namespace

namespace akp153 {

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

std::array<std::uint8_t, PacketSize> buildShowLogo() {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x44;
    pkt[11] = 0x43;
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                      std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdBat);
    // Big-endian 16-bit size at offsets 10..11.
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    pkt[12] = keyIndex;
    return pkt;
}

std::optional<KeyEvent> parseInputReport(std::span<std::uint8_t const> frame) {
    if (frame.size() < 16) {
        return std::nullopt;
    }

    // ACK frames start with "ACK"
    if (frame[0] == 0x41 && frame[1] == 0x43 && frame[2] == 0x4b) {
        return std::nullopt;
    }

    // Key release has its payload at byte 9.
    auto const keyIndex = frame[9];
    if (keyIndex == 0 || keyIndex > KeyCount) {
        return std::nullopt;
    }

    // The AKP153 emits a "released" report on each press/release transition;
    // a higher layer maintains the press/release state machine by diffing
    // successive reports. For now we expose "pressed==true" as a single shot.
    return KeyEvent{.keyIndex = keyIndex, .pressed = true};
}

} // namespace akp153

namespace {

// -----------------------------------------------------------------------------
// Akp153Device: glues the protocol helpers to ITransport + capability mix-ins.
// -----------------------------------------------------------------------------
class Akp153Device final : public IDevice, public IDisplayCapable, public IFirmwareCapable {
public:
    Akp153Device(DeviceDescriptor descriptor, DeviceId id)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(makeHidTransport(m_id.vendorId, m_id.productId, m_id.serial)) {}

    // ---- IDevice ------------------------------------------------------------
    [[nodiscard]] DeviceDescriptor const& descriptor() const noexcept override {
        return m_descriptor;
    }
    [[nodiscard]] DeviceId id() const noexcept override { return m_id; }

    [[nodiscard]] std::string firmwareVersion() const override { return m_firmware.version; }

    void open() override {
        if (m_transport->isOpen()) {
            return;
        }
        m_transport->open();
        AJAZZ_LOG_INFO("akp153", "device opened: {}", m_descriptor.model);
    }

    void close() override {
        if (!m_transport->isOpen()) {
            return;
        }
        auto const stop = buildCmdHeader(akp153::CmdStop);
        try {
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
        std::array<std::uint8_t, akp153::PacketSize> buf{};
        std::size_t emitted = 0;
        for (int i = 0; i < 8; ++i) {
            auto const n = m_transport->read(buf, std::chrono::milliseconds{0});
            if (n == 0) {
                break;
            }
            if (auto ev = akp153::parseInputReport({buf.data(), n})) {
                DeviceEvent devEv{};
                devEv.kind =
                    ev->pressed ? DeviceEvent::Kind::KeyPressed : DeviceEvent::Kind::KeyReleased;
                devEv.index = ev->keyIndex;
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
        }
        return emitted;
    }

    // ---- IDisplayCapable ----------------------------------------------------
    [[nodiscard]] DisplayInfo displayInfo() const noexcept override {
        return DisplayInfo{
            .widthPx = akp153::KeyWidthPx,
            .heightPx = akp153::KeyHeightPx,
            .keyRows = 3,
            .keyCols = 5,
            .jpegEncoded = true,
        };
    }

    void setKeyImage(std::uint8_t keyIndex,
                     std::span<std::uint8_t const> rgba,
                     std::uint16_t width,
                     std::uint16_t height) override {
        // The real implementation resizes `rgba` to KeyWidthPx × KeyHeightPx
        // and re-encodes as JPEG (quality ≈ 90). That pipeline lives in
        // src/devices/streamdeck/src/image_pipeline.cpp (phase 2). Here we
        // accept any payload and send it verbatim as JPEG.
        (void)width;
        (void)height;
        sendImage(keyIndex, rgba);
    }

    void setKeyColor(std::uint8_t keyIndex, Rgb color) override {
        // Encode a 1×1 JPEG of the requested color; good enough for
        // placeholders. Full implementation in phase 2.
        (void)keyIndex;
        (void)color;
        clearKey(keyIndex);
    }

    void clearKey(std::uint8_t keyIndex) override {
        auto const pkt =
            (keyIndex == 0xff) ? akp153::buildClearAll() : akp153::buildClearKey(keyIndex);
        (void)m_transport->write(pkt);
    }

    void setMainImage(std::span<std::uint8_t const>, std::uint16_t, std::uint16_t) override {
        // AKP153 has no main display.
    }

    void setBrightness(std::uint8_t percent) override {
        auto const pkt = akp153::buildSetBrightness(percent);
        (void)m_transport->write(pkt);
    }

    void flush() override {
        auto const pkt = buildCmdHeader(akp153::CmdStop);
        (void)m_transport->write(pkt);
    }

    // ---- IFirmwareCapable ---------------------------------------------------
    [[nodiscard]] FirmwareInfo firmwareInfo() const override { return m_firmware; }

    std::uint32_t beginFirmwareUpdate(std::span<std::uint8_t const>) override {
        throw std::runtime_error("AKP153 firmware update not yet supported");
    }

    [[nodiscard]] std::uint8_t firmwareUpdateProgress(std::uint32_t) const override { return 0; }

private:
    void sendImage(std::uint8_t keyIndex, std::span<std::uint8_t const> jpeg) {
        auto const header = akp153::buildImageHeader(
            keyIndex, static_cast<std::uint16_t>(std::min<std::size_t>(jpeg.size(), 0xffff)));
        (void)m_transport->write(header);

        std::size_t offset = 0;
        while (offset < jpeg.size()) {
            std::array<std::uint8_t, akp153::PacketSize> chunk{};
            auto const take = std::min<std::size_t>(akp153::PacketSize, jpeg.size() - offset);
            std::memcpy(chunk.data(), jpeg.data() + offset, take);
            (void)m_transport->write(chunk);
            offset += take;
        }
    }

    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    FirmwareInfo m_firmware{.version = "unknown"};
    EventCallback m_callback;
    std::mutex m_mutex;
};

} // namespace

core::DevicePtr makeAkp153(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_unique<Akp153Device>(d, std::move(id));
}

} // namespace ajazz::streamdeck
