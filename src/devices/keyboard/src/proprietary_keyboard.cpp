// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file proprietary_keyboard.cpp
 * @brief IDevice backend for AJAZZ proprietary-protocol keyboards.
 *
 * Covers the AK680, AK510, and similar "gaming" keyboards that ship with the
 * closed-source Windows configuration tool.  The wire protocol is a clean-room
 * reconstruction from USB captures; see
 * docs/protocols/keyboard/proprietary.md for the authoritative byte-level
 * reference.  No vendor firmware, driver, or SDK code is reused.
 *
 * The file is split into two parts:
 * -# Pure protocol helpers (namespace proprietary) — stateless, unit-testable.
 * -# ProprietaryKeyboard class — the IDevice implementation that owns the
 *    HID transport and routes I/O to those helpers.
 */
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "proprietary_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ajazz::keyboard {

namespace proprietary {

// -----------------------------------------------------------------------------
// Pure protocol helpers — every command below is verifiable in isolation.
// Implementations live here; declarations and contracts in proprietary_protocol.hpp.
// -----------------------------------------------------------------------------

/**
 * @brief Build the firmware-version query report (CmdGetFirmwareVersion).
 * @return 64-byte report with command 0x01 and all payload bytes zeroed.
 */
std::array<std::uint8_t, ReportSize> buildGetFirmwareVersion() {
    return makeReport(CmdGetFirmwareVersion);
}

/**
 * @brief Build a set-keycode report (CmdSetKeycode, 0x05).
 *
 * Payload layout: layer(2) row(3) col(4) keycode-hi(5) keycode-lo(6).
 * The keycode is encoded big-endian.
 *
 * @param layer    Layer index (0-based).
 * @param row      Key-matrix row.
 * @param col      Key-matrix column.
 * @param keycode  HID usage code.
 */
std::array<std::uint8_t, ReportSize>
buildSetKeycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col, std::uint16_t keycode) {
    auto pkt = makeReport(CmdSetKeycode);
    pkt[2] = layer;
    pkt[3] = row;
    pkt[4] = col;
    pkt[5] = static_cast<std::uint8_t>((keycode >> 8) & 0xffu);
    pkt[6] = static_cast<std::uint8_t>(keycode & 0xffu);
    return pkt;
}

/**
 * @brief Build a static RGB report (CmdSetRgbStatic, 0x08).
 *
 * Payload layout: zone(2) R(3) G(4) B(5).
 *
 * @param zone  Zone id (ZoneKeys / ZoneSides / ZoneLogo).
 * @param r     Red component 0–255.
 * @param g     Green component 0–255.
 * @param b     Blue component 0–255.
 */
std::array<std::uint8_t, ReportSize>
buildSetRgbStatic(std::uint8_t zone, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    auto pkt = makeReport(CmdSetRgbStatic);
    pkt[2] = zone;
    pkt[3] = r;
    pkt[4] = g;
    pkt[5] = b;
    return pkt;
}

/**
 * @brief Build an RGB animation report (CmdSetRgbEffect, 0x09).
 *
 * Payload layout: zone(2) effect-id(3) speed(4).
 *
 * @param zone      Zone id.
 * @param effectId  Firmware animation preset index.
 * @param speed     Animation speed 0–255.
 */
std::array<std::uint8_t, ReportSize>
buildSetRgbEffect(std::uint8_t zone, std::uint8_t effectId, std::uint8_t speed) {
    auto pkt = makeReport(CmdSetRgbEffect);
    pkt[2] = zone;
    pkt[3] = effectId;
    pkt[4] = speed;
    return pkt;
}

/**
 * @brief Build a brightness report (CmdSetRgbBrightness, 0x0b).
 *
 * @param percent  Global brightness 0–100; clamped before encoding.
 */
std::array<std::uint8_t, ReportSize> buildSetRgbBrightness(std::uint8_t percent) {
    auto pkt = makeReport(CmdSetRgbBrightness);
    pkt[2] = std::min<std::uint8_t>(percent, 100);
    return pkt;
}

/**
 * @brief Build a layer-switch report (CmdSetLayer, 0x0c).
 *
 * @param layer  Target layer index; clamped to MaxLayers–1 (3) if out of range.
 */
std::array<std::uint8_t, ReportSize> buildSetLayer(std::uint8_t layer) {
    auto pkt = makeReport(CmdSetLayer);
    pkt[2] = std::min<std::uint8_t>(layer, static_cast<std::uint8_t>(MaxLayers - 1));
    return pkt;
}

/**
 * @brief Build an EEPROM commit report (CmdCommitEeprom, 0x0e).
 *
 * Instructs the firmware to persist any staged configuration changes.
 * Must be issued after all key-remap or macro upload reports.
 */
std::array<std::uint8_t, ReportSize> buildCommitEeprom() {
    return makeReport(CmdCommitEeprom);
}

/**
 * @brief Return the LED count for a given zone id.
 *
 * @param zone  Zone id constant (ZoneKeys, ZoneSides, or ZoneLogo).
 * @return      LED count, or 0 for an unrecognised zone.
 */
std::uint16_t ledCountForZone(std::uint8_t zone) {
    switch (zone) {
    case ZoneKeys:
        return LedCountKeys;
    case ZoneSides:
        return LedCountSides;
    case ZoneLogo:
        return LedCountLogo;
    default:
        return 0;
    }
}

/**
 * @brief Translate a zone name string to its numeric id.
 *
 * @param name  One of @c "keys", @c "sides", or @c "logo" (case-sensitive).
 * @return      Numeric zone id, or @c 0xFF if the name is not recognised.
 */
std::uint8_t zoneIdFromName(std::string_view name) {
    if (name == "keys") {
        return ZoneKeys;
    }
    if (name == "sides") {
        return ZoneSides;
    }
    if (name == "logo") {
        return ZoneLogo;
    }
    return 0xff;
}

} // namespace proprietary

namespace {

using namespace ajazz::core;
using namespace ajazz::keyboard::proprietary;

/**
 * @brief IDevice backend for proprietary-protocol AJAZZ keyboards.
 *
 * Implements IDevice, IKeyRemappable, IRgbCapable, and IFirmwareCapable
 * using the reverse-engineered HID command set documented in
 * docs/protocols/keyboard/proprietary.md.
 *
 * @note Thread-safe for concurrent onEvent() / poll() calls; a single
 *       mutex guards the event callback.
 * @see makeProprietaryKeyboard()
 */
class ProprietaryKeyboard final : public IDevice,
                                  public IKeyRemappable,
                                  public IRgbCapable,
                                  public IFirmwareCapable {
public:
    ProprietaryKeyboard(DeviceDescriptor descriptor, DeviceId id)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(makeHidTransport(m_id.vendorId, m_id.productId, m_id.serial)) {}

    // ---- IDevice ------------------------------------------------------------
    [[nodiscard]] DeviceDescriptor const& descriptor() const noexcept override {
        return m_descriptor;
    }
    [[nodiscard]] DeviceId id() const noexcept override { return m_id; }

    [[nodiscard]] std::string firmwareVersion() const override {
        auto const pkt = buildGetFirmwareVersion();
        try {
            (void)m_transport->write(pkt);
            std::array<std::uint8_t, ReportSize> resp{};
            auto const n = m_transport->read(resp, std::chrono::milliseconds{100});
            if (n >= 5) {
                char buf[16]{};
                (void)std::snprintf(buf, sizeof(buf), "%u.%u.%u", resp[2], resp[3], resp[4]);
                return std::string{buf};
            }
        } catch (...) { /* fall through to unknown */
        }
        return "unknown";
    }

    void open() override {
        if (!m_transport->isOpen()) {
            m_transport->open();
            AJAZZ_LOG_INFO("kbd/proprietary", "device opened: {}", m_descriptor.model);
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

    std::size_t poll() override {
        std::array<std::uint8_t, ReportSize> buf{};
        std::size_t emitted = 0;
        for (int i = 0; i < 4; ++i) {
            auto const n = m_transport->read(buf, std::chrono::milliseconds{0});
            if (n == 0) {
                break;
            }
            // Input reports currently surface only the active layer change;
            // per-key HID events travel on the keyboard's standard
            // boot-protocol interface and are consumed by the OS.
            if (n >= 3 && buf[0] == ReportId && buf[1] == CmdSetLayer) {
                DeviceEvent devEv{};
                devEv.kind = DeviceEvent::Kind::KeyPressed;
                devEv.index = 0;
                devEv.value = buf[2];
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

    // ---- IKeyRemappable -----------------------------------------------------
    [[nodiscard]] KeyboardLayout layout() const noexcept override {
        // Conservative TKL layout that fits every supported model; the real
        // numbers come from resources/device-db/keyboards.json once the
        // device database ships.
        return KeyboardLayout{.rows = 6, .cols = 17, .layers = MaxLayers};
    }

    void setKeycode(std::uint8_t layer,
                    std::uint8_t row,
                    std::uint8_t col,
                    std::uint16_t keycode) override {
        auto const pkt = buildSetKeycode(layer, row, col, keycode);
        (void)m_transport->write(pkt);
    }

    [[nodiscard]] std::uint16_t
    keycode(std::uint8_t /*layer*/, std::uint8_t /*row*/, std::uint8_t /*col*/) const override {
        // Read-back uses command 0x04 but its payload format is not yet
        // confirmed across all models; defer until a capture lands.
        throw std::runtime_error("proprietary keyboard keycode read-back not yet implemented");
    }

    void setMacro(std::uint8_t slot, std::span<std::uint8_t const> bytes) override {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            auto pkt = makeReport(CmdUploadMacro);
            pkt[2] = slot;
            pkt[3] = static_cast<std::uint8_t>((offset >> 8) & 0xffu);
            pkt[4] = static_cast<std::uint8_t>(offset & 0xffu);
            auto const take = std::min<std::size_t>(MacroChunk, bytes.size() - offset);
            pkt[5] = static_cast<std::uint8_t>(take);
            // Bytes [6..7] reserved; payload starts at byte 8.
            std::memcpy(pkt.data() + 8, bytes.data() + offset, take);
            (void)m_transport->write(pkt);
            offset += take;
        }
    }

    void commit() override {
        auto const pkt = buildCommitEeprom();
        (void)m_transport->write(pkt);
    }

    // ---- IRgbCapable --------------------------------------------------------
    [[nodiscard]] std::vector<RgbZone> rgbZones() const override {
        return {RgbZone{.name = "keys", .ledCount = LedCountKeys},
                RgbZone{.name = "sides", .ledCount = LedCountSides},
                RgbZone{.name = "logo", .ledCount = LedCountLogo}};
    }

    void setRgbStatic(std::string_view zone, Rgb color) override {
        auto const zoneId = zoneIdFromName(zone);
        if (zoneId == 0xff) {
            throw std::invalid_argument("unknown RGB zone");
        }
        auto const pkt = buildSetRgbStatic(zoneId, color.r, color.g, color.b);
        (void)m_transport->write(pkt);
    }

    void setRgbEffect(std::string_view zone, RgbEffect effect, std::uint8_t speed) override {
        auto const zoneId = zoneIdFromName(zone);
        if (zoneId == 0xff) {
            throw std::invalid_argument("unknown RGB zone");
        }
        auto const pkt = buildSetRgbEffect(zoneId, static_cast<std::uint8_t>(effect), speed);
        (void)m_transport->write(pkt);
    }

    void setRgbBuffer(std::string_view zone, std::span<Rgb const> colors) override {
        auto const zoneId = zoneIdFromName(zone);
        if (zoneId == 0xff) {
            throw std::invalid_argument("unknown RGB zone");
        }
        auto const expected = ledCountForZone(zoneId);
        if (colors.size() != expected) {
            throw std::invalid_argument("RGB buffer size mismatch for zone");
        }

        // Flatten to a contiguous RGB8 byte buffer, then upload in 60-byte
        // chunks (20 LEDs per report).
        std::vector<std::uint8_t> flat;
        flat.reserve(colors.size() * 3);
        for (auto const& c : colors) {
            flat.push_back(c.r);
            flat.push_back(c.g);
            flat.push_back(c.b);
        }

        std::size_t offset = 0;
        while (offset < flat.size()) {
            auto pkt = makeReport(CmdSetRgbBuffer);
            pkt[2] = zoneId;
            pkt[3] = static_cast<std::uint8_t>((offset >> 8) & 0xffu);
            pkt[4] = static_cast<std::uint8_t>(offset & 0xffu);
            auto const take = std::min<std::size_t>(RgbBufferChunk, flat.size() - offset);
            // Byte 5 reports the length of this chunk.
            pkt[5] = static_cast<std::uint8_t>(take);
            std::memcpy(
                pkt.data() + 6, flat.data() + offset, std::min<std::size_t>(take, ReportSize - 6));
            (void)m_transport->write(pkt);
            offset += take;
        }
    }

    void setRgbBrightness(std::uint8_t percent) override {
        auto const pkt = buildSetRgbBrightness(percent);
        (void)m_transport->write(pkt);
    }

    // ---- IFirmwareCapable ---------------------------------------------------
    [[nodiscard]] FirmwareInfo firmwareInfo() const override {
        return FirmwareInfo{
            .version = firmwareVersion(), .buildDate = {}, .bootloaderAvailable = false};
    }

    std::uint32_t beginFirmwareUpdate(std::span<std::uint8_t const>) override {
        throw std::runtime_error(
            "proprietary keyboard firmware update not yet supported (bootloader unknown)");
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

core::DevicePtr makeProprietaryKeyboard(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_unique<ProprietaryKeyboard>(d, std::move(id));
}

} // namespace ajazz::keyboard
