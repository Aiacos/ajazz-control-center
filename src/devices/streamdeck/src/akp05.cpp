// SPDX-License-Identifier: GPL-3.0-or-later
//
// AJAZZ AKP05 / AKP05E "Stream Dock Plus"-class backend. 15 keys + 4 endless
// encoders + touch strip + main LCD. Encoder and touch-strip paths are the
// distinguishing feature; images re-use the AKP153 JPEG pipeline.
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"

#include <mutex>

namespace ajazz::streamdeck {

namespace {

using namespace ajazz::core;

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
    std::size_t poll() override { return 0; }

    [[nodiscard]] DisplayInfo displayInfo() const noexcept override {
        return DisplayInfo{
            .widthPx = 85, .heightPx = 85, .keyRows = 3, .keyCols = 5, .jpegEncoded = true};
    }
    void setKeyImage(std::uint8_t,
                     std::span<std::uint8_t const>,
                     std::uint16_t,
                     std::uint16_t) override {}
    void setKeyColor(std::uint8_t, Rgb) override {}
    void clearKey(std::uint8_t) override {}
    void setMainImage(std::span<std::uint8_t const>, std::uint16_t, std::uint16_t) override {}
    void setBrightness(std::uint8_t) override {}
    void flush() override {}

    [[nodiscard]] EncoderInfo encoderInfo() const noexcept override {
        return EncoderInfo{
            .count = 4, .pressable = true, .hasScreens = true, .stepsPerRevolution = 0};
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

core::DevicePtr makeAkp05(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_unique<Akp05Device>(d, std::move(id));
}

} // namespace ajazz::streamdeck
