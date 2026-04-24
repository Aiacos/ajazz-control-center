// SPDX-License-Identifier: GPL-3.0-or-later
//
// AJAZZ AKP03 / Mirabox N3 backend.
//
// The AKP03 exposes a 6-key layout plus a knob. The wire protocol is a
// close cousin of the AKP153 (same `CRT` prefix, same packet size) with a
// smaller key count and a different image codec (PNG 72×72 instead of JPEG
// 85×85). Full protocol details live in docs/protocols/streamdeck/akp03.md.
//
// Phase 1 provides the device scaffold + identity; the image and encoder
// paths are stubbed out pending a hardware capture.
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <mutex>

namespace ajazz::streamdeck {

namespace {

using namespace ajazz::core;

class Akp03Device final : public IDevice, public IDisplayCapable, public IEncoderCapable {
public:
    Akp03Device(DeviceDescriptor descriptor, DeviceId id)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(makeHidTransport(m_id.vendorId, m_id.productId, m_id.serial)) {}

    [[nodiscard]] DeviceDescriptor const& descriptor() const noexcept override {
        return m_descriptor;
    }
    [[nodiscard]] DeviceId id() const noexcept override { return m_id; }
    [[nodiscard]] std::string firmwareVersion() const override { return "unknown"; }

    void open() override {
        if (!m_transport->isOpen()) {
            m_transport->open();
        }
    }
    void close() override {
        if (m_transport->isOpen()) {
            m_transport->close();
        }
    }
    [[nodiscard]] bool isOpen() const noexcept override { return m_transport->isOpen(); }

    void onEvent(EventCallback cb) override {
        std::lock_guard const lock(m_mutex);
        m_callback = std::move(cb);
    }
    std::size_t poll() override { return 0; } // TODO: input report parsing

    // IDisplayCapable
    [[nodiscard]] DisplayInfo displayInfo() const noexcept override {
        return DisplayInfo{
            .widthPx = 72, .heightPx = 72, .keyRows = 2, .keyCols = 3, .jpegEncoded = false};
    }
    void setKeyImage(std::uint8_t,
                     std::span<std::uint8_t const>,
                     std::uint16_t,
                     std::uint16_t) override {
        AJAZZ_LOG_WARN("akp03", "setKeyImage: not yet implemented");
    }
    void setKeyColor(std::uint8_t, Rgb) override {}
    void clearKey(std::uint8_t) override {}
    void setMainImage(std::span<std::uint8_t const>, std::uint16_t, std::uint16_t) override {}
    void setBrightness(std::uint8_t) override {}
    void flush() override {}

    // IEncoderCapable
    [[nodiscard]] EncoderInfo encoderInfo() const noexcept override {
        return EncoderInfo{
            .count = 1, .pressable = true, .hasScreens = false, .stepsPerRevolution = 0};
    }
    void setEncoderImage(std::uint8_t,
                         std::span<std::uint8_t const>,
                         std::uint16_t,
                         std::uint16_t) override {}

private:
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
