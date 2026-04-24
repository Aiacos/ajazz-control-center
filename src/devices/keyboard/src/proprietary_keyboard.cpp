// SPDX-License-Identifier: GPL-3.0-or-later
//
// AJAZZ keyboards with closed-source Windows software (e.g. AK680, AK510).
// Protocol work in progress — the backend is a stub that declares the
// capability set so the UI can show the device greyed-out with a pointer
// to the contribution guide.
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/keyboard/keyboard.hpp"

#include <mutex>

namespace ajazz::keyboard {

namespace {

using namespace ajazz::core;

class ProprietaryKeyboard final : public IDevice, public IRgbCapable {
public:
    ProprietaryKeyboard(DeviceDescriptor descriptor, DeviceId id)
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

    [[nodiscard]] std::vector<RgbZone> rgbZones() const override {
        return {RgbZone{.name = "keys", .ledCount = 104}, RgbZone{.name = "sides", .ledCount = 18}};
    }
    void setRgbStatic(std::string_view, Rgb) override {}
    void setRgbEffect(std::string_view, RgbEffect, std::uint8_t) override {}
    void setRgbBuffer(std::string_view, std::span<Rgb const>) override {}
    void setRgbBrightness(std::uint8_t) override {}

private:
    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    EventCallback m_callback;
    std::mutex m_mutex;
};

} // namespace

core::DevicePtr makeProprietaryKeyboard(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_unique<ProprietaryKeyboard>(d, std::move(id));
}

} // namespace ajazz::keyboard
