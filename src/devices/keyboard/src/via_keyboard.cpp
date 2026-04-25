// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file via_keyboard.cpp
 * @brief IDevice backend for VIA-compatible AJAZZ keyboards (AK820 Pro, AK870, …).
 *
 * Implements the open VIA raw-HID protocol.  Command byte summary (full
 * reference in docs/protocols/keyboard/via.md):
 *
 * | Command | Meaning                          |
 * |---------|----------------------------------|
 * | 0x01    | id_get_protocol_version          |
 * | 0x02    | id_get_keyboard_value            |
 * | 0x03    | id_set_keyboard_value            |
 * | 0x04    | id_dynamic_keymap_get_keycode    |
 * | 0x05    | id_dynamic_keymap_set_keycode    |
 * | 0x07    | id_dynamic_keymap_reset          |
 * | 0x08    | id_custom_set_value (RGB)        |
 * | 0x09    | id_custom_get_value              |
 * | 0x0A    | id_custom_save                   |
 * | 0x0D    | id_dynamic_keymap_macro_set_buffer|
 */
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/keyboard/keyboard.hpp"

#include <array>
#include <mutex>
#include <stdexcept>

namespace ajazz::keyboard {

namespace {

using namespace ajazz::core;

constexpr std::size_t kReportSize = 32;

/**
 * @brief IDevice backend for VIA-compatible AJAZZ keyboards.
 *
 * Implements IDevice, IKeyRemappable, IRgbCapable, and IFirmwareCapable
 * on top of the public VIA raw-HID protocol.  Report size is 32 bytes
 * (VIA standard).
 *
 * @note Firmware update is not supported over the VIA raw-HID path; the
 *       bootloader requires a separate DFU interface not yet implemented.
 * @see makeViaKeyboard()
 */
class ViaKeyboard final : public IDevice,
                          public IKeyRemappable,
                          public IRgbCapable,
                          public IFirmwareCapable {
public:
    ViaKeyboard(DeviceDescriptor descriptor, DeviceId id)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(makeHidTransport(m_id.vendorId, m_id.productId, m_id.serial)) {}

    // IDevice
    [[nodiscard]] DeviceDescriptor const& descriptor() const noexcept override {
        return m_descriptor;
    }
    [[nodiscard]] DeviceId id() const noexcept override { return m_id; }
    [[nodiscard]] std::string firmwareVersion() const override {
        std::array<std::uint8_t, kReportSize> pkt{};
        pkt[0] = 0x00; // report id
        pkt[1] = 0x01; // id_get_protocol_version
        (void)m_transport->write(pkt);
        std::array<std::uint8_t, kReportSize> resp{};
        (void)m_transport->read(resp, std::chrono::milliseconds{100});
        char buf[8] = {};
        (void)std::snprintf(buf, sizeof(buf), "%u", (resp[2] << 8) | resp[3]);
        return std::string{buf};
    }

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

    // IKeyRemappable
    [[nodiscard]] KeyboardLayout layout() const noexcept override {
        return KeyboardLayout{.rows = 6, .cols = 16, .layers = 4};
    }

    void setKeycode(std::uint8_t layer,
                    std::uint8_t row,
                    std::uint8_t col,
                    std::uint16_t keycode) override {
        std::array<std::uint8_t, kReportSize> pkt{};
        pkt[1] = 0x05; // id_dynamic_keymap_set_keycode
        pkt[2] = layer;
        pkt[3] = row;
        pkt[4] = col;
        pkt[5] = static_cast<std::uint8_t>(keycode >> 8);
        pkt[6] = static_cast<std::uint8_t>(keycode & 0xff);
        (void)m_transport->write(pkt);
    }

    [[nodiscard]] std::uint16_t
    keycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col) const override {
        std::array<std::uint8_t, kReportSize> pkt{};
        pkt[1] = 0x04;
        pkt[2] = layer;
        pkt[3] = row;
        pkt[4] = col;
        (void)m_transport->write(pkt);
        std::array<std::uint8_t, kReportSize> resp{};
        (void)m_transport->read(resp, std::chrono::milliseconds{100});
        return static_cast<std::uint16_t>((resp[5] << 8) | resp[6]);
    }

    void setMacro(std::uint8_t /*slot*/, std::span<std::uint8_t const> bytes) override {
        // Macro buffer upload uses command 0x0D with chunked transfer.
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            std::array<std::uint8_t, kReportSize> pkt{};
            pkt[1] = 0x0D;
            pkt[2] = static_cast<std::uint8_t>(offset >> 8);
            pkt[3] = static_cast<std::uint8_t>(offset & 0xff);
            auto const take = std::min<std::size_t>(28, bytes.size() - offset);
            pkt[4] = static_cast<std::uint8_t>(take);
            for (std::size_t i = 0; i < take; ++i) {
                pkt[5 + i] = bytes[offset + i];
            }
            (void)m_transport->write(pkt);
            offset += take;
        }
    }

    void commit() override {
        std::array<std::uint8_t, kReportSize> pkt{};
        pkt[1] = 0x0A; // id_custom_save
        (void)m_transport->write(pkt);
    }

    // IRgbCapable
    [[nodiscard]] std::vector<RgbZone> rgbZones() const override {
        return {RgbZone{.name = "keys", .ledCount = 96}};
    }

    void setRgbStatic(std::string_view /*zone*/, Rgb color) override {
        std::array<std::uint8_t, kReportSize> pkt{};
        pkt[1] = 0x08; // id_custom_set_value
        pkt[2] = 0x01; // channel: qmk_rgblight
        pkt[3] = 0x04; // sub: color
        pkt[4] = color.r;
        pkt[5] = color.g;
        pkt[6] = color.b;
        (void)m_transport->write(pkt);
    }

    void setRgbEffect(std::string_view, RgbEffect effect, std::uint8_t speed) override {
        std::array<std::uint8_t, kReportSize> pkt{};
        pkt[1] = 0x08;
        pkt[2] = 0x01;
        pkt[3] = 0x03; // sub: effect
        pkt[4] = static_cast<std::uint8_t>(effect);
        pkt[5] = speed;
        (void)m_transport->write(pkt);
    }

    void setRgbBuffer(std::string_view, std::span<Rgb const> /*colors*/) override {
        throw std::runtime_error("per-LED RGB buffer: TODO (requires QMK_RGB_MATRIX path)");
    }

    void setRgbBrightness(std::uint8_t percent) override {
        std::array<std::uint8_t, kReportSize> pkt{};
        pkt[1] = 0x08;
        pkt[2] = 0x01;
        pkt[3] = 0x02;
        pkt[4] = static_cast<std::uint8_t>((percent * 255u) / 100u);
        (void)m_transport->write(pkt);
    }

    // IFirmwareCapable
    [[nodiscard]] FirmwareInfo firmwareInfo() const override {
        return FirmwareInfo{
            .version = firmwareVersion(), .buildDate = {}, .bootloaderAvailable = false};
    }
    std::uint32_t beginFirmwareUpdate(std::span<std::uint8_t const>) override {
        throw std::runtime_error("VIA fw update over raw HID not supported");
    }
    [[nodiscard]] std::uint8_t firmwareUpdateProgress(std::uint32_t) const override { return 0; }

private:
    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    EventCallback m_callback;
    std::mutex m_mutex;
};

} // namespace

core::DevicePtr makeViaKeyboard(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_unique<ViaKeyboard>(d, std::move(id));
}

} // namespace ajazz::keyboard
