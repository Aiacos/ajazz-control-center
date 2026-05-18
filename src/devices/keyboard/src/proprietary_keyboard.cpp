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
#include "ajazz/keyboard/ak980_lighting.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "proprietary_protocol.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

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
 * @brief Build the time-sync preamble packet — control packet for opcode 0x28.
 *
 * Uses the default ReportId=0x04 + CmdSetTime=0x28 at byte 1 + sentinel 0x01 at
 * byte 8. The firmware reads this as "next packet is a CMD_TIME configuration
 * data block, not a CMD_SAVE acknowledgement".
 */
std::array<std::uint8_t, ReportSize> buildSetTimeStart() {
    auto pkt = makeReport(CmdStartTime);
    pkt[8] = 0x01; // configure-mode marker (gohv control_packet pattern)
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildSetTimePreamble() {
    auto pkt = makeReport(CmdSetTime);
    pkt[8] = 0x01;
    return pkt;
}

/**
 * @brief Build the 64-byte time-data packet (ReportId=0x00, magic 0x5A).
 *
 * See proprietary_protocol.hpp for the full byte spec. Year saturates at the
 * 2000 floor so calling with std::chrono::system_clock epoch (year 1970) does
 * not underflow into a uint8 wrap.
 */
std::array<std::uint8_t, ReportSize> buildSetTimeData(std::uint16_t year,
                                                      std::uint8_t month,
                                                      std::uint8_t day,
                                                      std::uint8_t hour,
                                                      std::uint8_t minute,
                                                      std::uint8_t second,
                                                      std::uint8_t dayOfWeek) {
    std::array<std::uint8_t, ReportSize> pkt{};
    pkt[0] = TimeDataReportId; // 0x00 — NOT the default 0x04
    pkt[1] = 0x01;
    pkt[2] = 0x5a;
    pkt[3] = (year >= 2000) ? static_cast<std::uint8_t>(year - 2000) : 0;
    pkt[4] = month;
    pkt[5] = day;
    pkt[6] = hour;
    pkt[7] = minute;
    pkt[8] = second;
    pkt[9] = 0x00;
    // wDayOfWeek (0=Sunday..6=Saturday). The gohv corpus hard-codes 0x04 here;
    // Ghidra decompile of DeviceDriver.exe (2026-05-17, ak980pro_vendor.md
    // §"Time-sync flow" lines 240-244) showed the vendor reads the real
    // day-of-week. Clamp to 0..6 in case the caller passes an out-of-range
    // value (tm_wday is guaranteed 0..6 by the C library but defensive).
    pkt[10] = (dayOfWeek <= 6) ? dayOfWeek : 0;
    // bytes 11..61 stay 0x00 from value-init.
    pkt[ReportSize - 2] = 0xaa;
    pkt[ReportSize - 1] = 0x55;
    return pkt;
}

/**
 * @brief Build the time-sync save packet — control packet for opcode 0x02.
 *
 * Distinct from buildCommitEeprom() (opcode 0x0E for keymap / RGB / macro
 * state). The RTC has its own dedicated save opcode 0x02.
 */
std::array<std::uint8_t, ReportSize> buildSetTimeSave() {
    return makeReport(CmdSaveRtc);
}

/**
 * @brief Build the AK980 PRO settings-batch DATA packet (opcode 0x07 sub 0x10).
 *
 * Byte layout per ak980pro_vendor.md §13.2 (Ghidra decompile of
 * DeviceDriver.exe FUN_0044eed0 callers):
 *
 *   [0]  ReportId 0x04
 *   [1]  CmdSettingsBatch 0x07
 *   [2]  SettingsBatchSub 0x10
 *   [6]  disableWinKey  (0/1)
 *   [7]  disableAltF4   (0/1)
 *   [8]  disableAltTab  (0/1)
 *   [9]  fnLayerSwitch  (0=hold, 1=toggle)
 *   [10] sleepTimerMinutes (vendor enum: 0/1/3/5/10/30)
 *   [12] keyResponseTimeLevel (1..5)
 *   [18] SettingsBatchTrailerHi 0xAA
 *   [19] SettingsBatchTrailerLo 0x55
 *
 * All other bytes are zero-initialised. Issue #57 / P3.x.
 */
std::array<std::uint8_t, ReportSize> buildSettingsBatch(std::uint8_t fnLayerSwitch,
                                                        std::uint8_t sleepTimerMinutes,
                                                        std::uint8_t keyResponseTimeLevel) {
    auto pkt = makeReport(CmdSettingsBatch);
    pkt[2] = SettingsBatchSub;
    pkt[kSettingsByteFnSwitch] = fnLayerSwitch;
    pkt[kSettingsByteSleepTime] = sleepTimerMinutes;
    // Vendor clamps response-time to [1..5]; 0 falls back to the default 3.
    std::uint8_t const responseClamped = std::clamp<std::uint8_t>(
        keyResponseTimeLevel == 0 ? 3 : keyResponseTimeLevel, 1, 5);
    pkt[kSettingsByteKeyResponseTime] = responseClamped;
    pkt[kSettingsByteTrailerHi] = SettingsBatchTrailerHi;
    pkt[kSettingsByteTrailerLo] = SettingsBatchTrailerLo;
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildBatteryQuery() {
    auto pkt = makeReport(CmdBatteryQuery);
    pkt[2] = BatteryQuerySub; // 0x01 — discriminates battery from per-key RGB (sub 0x04)
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildPerKeyRgbWriteHeader(bool isWireless) {
    auto pkt = makeReport(kCmdPerKeyRgbWrite);
    pkt[2] = kPerKeyRgbSub; // 0x04 — discriminates per-key RGB from battery query (sub 0x01)
    pkt[9] = isWireless ? kPerKeyModeWireless : kPerKeyModeWired;
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildPerKeyRgbReadback(bool isWireless) {
    auto pkt = makeReport(kCmdPerKeyRgbReadback);
    pkt[2] = isWireless ? kPerKeyReadbackWirelessSub : kPerKeyReadbackWiredSub;
    return pkt;
}

std::array<std::uint8_t, 3> encodeTftChunkIndex(std::uint32_t chunkIdx) {
    return {
        static_cast<std::uint8_t>(chunkIdx & 0xffu),                   // byte 1: low 8 bits
        static_cast<std::uint8_t>(0x80u | ((chunkIdx >> 16) & 0x7fu)), // byte 2: 0x80 | high 7 bits
        static_cast<std::uint8_t>((chunkIdx >> 8) & 0xffu),            // byte 3: middle 8 bits
    };
}

std::array<std::uint8_t, ReportSize> buildScreenBulkBegin(std::uint8_t lcdSelect,
                                                          std::uint16_t total4kChunks) {
    auto pkt = makeReport(CmdScreenBulkBegin);
    pkt[2] = 0x00;
    pkt[3] = static_cast<std::uint8_t>(lcdSelect + 1u); // LCD-select index + 1
    pkt[4] = static_cast<std::uint8_t>(total4kChunks & 0xffu);
    pkt[5] = static_cast<std::uint8_t>((total4kChunks >> 8) & 0xffu);
    return pkt;
}

std::array<std::uint8_t, ReportSize> buildSetRgbModeData(std::uint8_t modeId,
                                                         std::uint8_t r,
                                                         std::uint8_t g,
                                                         std::uint8_t b,
                                                         std::uint8_t rainbow,
                                                         std::uint8_t brightness,
                                                         std::uint8_t speed,
                                                         std::uint8_t direction) {
    std::array<std::uint8_t, ReportSize> pkt{};
    pkt[0] = ReportId;
    // mode_id lives at byte 1 per ak980pro_vendor.md §3.4 (NOT at byte 2 like
    // the battery query sub-cmd — the 0x13 opcode is the MODE_BEGIN packet
    // that immediately precedes this DATA packet; the data itself uses byte 1
    // for the actual mode value).
    pkt[1] = modeId;
    pkt[2] = r;
    pkt[3] = g;
    pkt[4] = b;
    pkt[8] = (rainbow != 0) ? 0x01 : 0x00;
    pkt[9] = std::min<std::uint8_t>(brightness, 5);
    pkt[10] = std::min<std::uint8_t>(speed, 5);
    pkt[11] = std::min<std::uint8_t>(direction, 3);
    pkt[14] = 0x55;
    pkt[15] = 0xaa;
    return pkt;
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
                                  public IFirmwareCapable,
                                  public IClockCapable,
                                  public IBatteryCapable,
                                  public IFirmwareLightingCapable,
                                  public ISettingsCapable {
public:
    /** Production constructor — creates a real HID transport. */
    ProprietaryKeyboard(DeviceDescriptor descriptor, DeviceId id)
        : ProprietaryKeyboard(std::move(descriptor),
                              id,
                              makeHidTransport(id.vendorId, id.productId, id.serial)) {}

    /** Test constructor — accepts an injected transport (DI for unit tests). */
    ProprietaryKeyboard(DeviceDescriptor descriptor, DeviceId id, TransportPtr transport)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(std::move(transport)) {}

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

    // ---- IBatteryCapable ----------------------------------------------------
    //
    // AK980 PRO charge level via opcode 0x20 sub 0x01 (request) + feature read
    // (response). Wire format reverse-engineered from DeviceDriver.exe
    // FUN_004358c0 (Ghidra, 2026-05-17) — see docs/protocols/keyboard/
    // ak980pro_vendor.md §3 (opcode table row 0x20 0x01) + roadmap §11.2.
    //
    // Response byte 3 carries the charge percent (0..100). Byte 0 of the
    // response should echo 0x20 — we use that as a sanity check to differentiate
    // a real charge reply from a stray "no battery" feature read (0x00 echo +
    // 0x00 charge means "wired, no battery"; we surface std::nullopt rather
    // than a misleading 0%).
    //
    // Polling cadence is the caller's responsibility — vendor app polls 15 s
    // when wireless. Our QML BatteryIndicator will subscribe to a
    // BatteryService that owns the QTimer.
    [[nodiscard]] std::optional<std::uint8_t> batteryPercent() override {
        try {
            (void)m_transport->writeFeature(buildBatteryQuery());
            std::array<std::uint8_t, ReportSize> resp{};
            resp[0] = ReportId; // hidapi convention: pre-fill report id for feature read
            auto const n = m_transport->readFeature(resp);
            if (n < 4 || resp[0] != CmdBatteryQuery) {
                return std::nullopt; // no reply / wrong report / wired-no-battery
            }
            auto const pct = resp[3];
            if (pct == 0) {
                return std::nullopt; // "no battery" sentinel; do not surface as 0%
            }
            return std::min<std::uint8_t>(pct, 100); // clamp out-of-range readings
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN(
                "keyboard.ak980", "batteryPercent: HID feature I/O failed: {}", e.what());
            return std::nullopt;
        }
    }

    // ---- IClockCapable ------------------------------------------------------
    //
    // ARCH-05.1 amendment (2026-05-17): AK980 PRO (Sonix SN32F299 family,
    // VID:PID 0x0c45:0x8009) has a firmware RTC reachable via opcode 0x28.
    // Wire format from two independent reverse-engineering corpora:
    //   - github.com/gohv/EPOMAKER-Ajazz-AK820-Pro (src/protocol.rs + usb.rs)
    //   - github.com/KyleBoyer/TFTTimeSync-node    (src/packets.ts + device.ts)
    //
    // 4-packet envelope, each 64 bytes, all sent via HID SET_FEATURE (Control
    // transfer with hid_send_feature_report), NOT HID Output Reports:
    //   1. START    — ReportId=0x04 CMD_START=0x18 byte[8]=0x01
    //   2. PREAMBLE — ReportId=0x04 CMD_TIME=0x28  byte[8]=0x01
    //   3. DATA     — ReportId=0x00 magic 0x5A + year-2000/mm/dd/hh/mm/ss + 0xAA 0x55
    //   4. SAVE     — ReportId=0x04 CMD_SAVE=0x02
    // Followed by a 100ms sleep so the firmware has time to commit before any
    // subsequent HID write reaches it (gohv usb.rs:set_time pattern).
    //
    // CRITICAL: this MUST use writeFeature() (hid_send_feature_report ⇒
    // USB SET_REPORT on the control endpoint), not write() (hid_write ⇒
    // interrupt OUT endpoint). Agent B disassembly of vendor DeviceDriver.exe
    // confirmed it imports HidD_SetFeature for this code path. Earlier draft
    // used write() and was a silent no-op against firmware.
    //
    // VIA-protocol keyboards (ViaKeyboard) still do NOT inherit IClockCapable
    // per D-03 — they are QMK-style with no vendor clock surface, untouched.
    //
    // Vendor app sends LOCAL time (KyleBoyer + gohv both pass through local
    // Date components without UTC normalisation), so we convert from
    // std::chrono::system_clock::time_point via localtime_s/localtime_r.
    [[nodiscard]] TimeSyncResult setTime(std::chrono::system_clock::time_point tp) override {
        auto const tt = std::chrono::system_clock::to_time_t(tp);
        std::tm local{};
#ifdef _WIN32
        if (::localtime_s(&local, &tt) != 0) {
            AJAZZ_LOG_WARN("keyboard.ak980", "setTime: localtime_s failed");
            return TimeSyncResult::IoError;
        }
#else
        if (::localtime_r(&tt, &local) == nullptr) {
            AJAZZ_LOG_WARN("keyboard.ak980", "setTime: localtime_r failed");
            return TimeSyncResult::IoError;
        }
#endif
        auto const year = static_cast<std::uint16_t>(local.tm_year + 1900);
        auto const month = static_cast<std::uint8_t>(local.tm_mon + 1);
        auto const day = static_cast<std::uint8_t>(local.tm_mday);
        auto const hour = static_cast<std::uint8_t>(local.tm_hour);
        auto const minute = static_cast<std::uint8_t>(local.tm_min);
        auto const second = static_cast<std::uint8_t>(local.tm_sec);
        // tm_wday is 0..6 (Sunday..Saturday) per POSIX/Win32.
        auto const dayOfWeek = static_cast<std::uint8_t>(local.tm_wday);

        try {
            (void)m_transport->writeFeature(buildSetTimeStart());
            (void)m_transport->writeFeature(buildSetTimePreamble());
            (void)m_transport->writeFeature(
                buildSetTimeData(year, month, day, hour, minute, second, dayOfWeek));
            (void)m_transport->writeFeature(buildSetTimeSave());
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("keyboard.ak980", "setTime: HID writeFeature failed: {}", e.what());
            return TimeSyncResult::IoError;
        }
        // Settle window — firmware commits RTC to NV-RAM, racing a subsequent
        // HID write here can drop the save (gohv usb.rs pattern).
        std::this_thread::sleep_for(std::chrono::milliseconds{100});

        AJAZZ_LOG_INFO("keyboard.ak980",
                       "setTime → device clock set to {:04}-{:02}-{:02} {:02}:{:02}:{:02} (local)",
                       year,
                       month,
                       day,
                       hour,
                       minute,
                       second);
        return TimeSyncResult::Ok;
    }

    // ---- IFirmwareLightingCapable (20 built-in modes via opcode 0x13) -----
    //
    // 4-packet envelope per ak980pro_vendor.md §3.4: CMD_START (0x18) ->
    // CMD_MODE_BEGIN (0x13) -> DATA (mode_id, RGB, brightness, speed) ->
    // CMD_SAVE (0x02). The 5th packet CMD_FINISH (0xF0) is part of the
    // standard vendor envelope but our project does not yet ship it
    // (Phase 3 P3.6 pending) - hardware-testing has shown the 4-packet
    // variant works in practice; FINISH is needed only for some other
    // config flows.
    [[nodiscard]] std::vector<FirmwareLightingMode>
    availableFirmwareModes() const override {
        // Names match the vendor 1033.lan English strings (cross-referenced
        // with the Chinese originals documented in ak980_lighting.hpp).
        return {
            {static_cast<std::uint8_t>(AK980LightingMode::Static),     "Static"},
            {static_cast<std::uint8_t>(AK980LightingMode::SingleOn),   "Single light on"},
            {static_cast<std::uint8_t>(AK980LightingMode::SingleOff),  "Single light off"},
            {static_cast<std::uint8_t>(AK980LightingMode::Glittering), "Glittering stars"},
            {static_cast<std::uint8_t>(AK980LightingMode::Falling),    "Falling pixels"},
            {static_cast<std::uint8_t>(AK980LightingMode::Colourful),  "Rainbow blanket"},
            {static_cast<std::uint8_t>(AK980LightingMode::Breath),     "Dynamic breath"},
            {static_cast<std::uint8_t>(AK980LightingMode::Spectrum),   "Spectrum rings"},
            {static_cast<std::uint8_t>(AK980LightingMode::Outward),    "Outward wave"},
            {static_cast<std::uint8_t>(AK980LightingMode::Scrolling),  "Horizontal scroll"},
            {static_cast<std::uint8_t>(AK980LightingMode::Rolling),    "Rolling glow"},
            {static_cast<std::uint8_t>(AK980LightingMode::Rotating),   "Rotating accents"},
            {static_cast<std::uint8_t>(AK980LightingMode::Explode),    "Press burst"},
            {static_cast<std::uint8_t>(AK980LightingMode::Launch),     "Launch trail"},
            {static_cast<std::uint8_t>(AK980LightingMode::Ripples),    "Ripples"},
            {static_cast<std::uint8_t>(AK980LightingMode::Flowing),    "Continuous flow"},
            {static_cast<std::uint8_t>(AK980LightingMode::Pulsating),  "Layered pulse"},
            {static_cast<std::uint8_t>(AK980LightingMode::Tilt),       "Diagonal sweep"},
            {static_cast<std::uint8_t>(AK980LightingMode::Shuttle),    "Shuttle"},
            {static_cast<std::uint8_t>(AK980LightingMode::LedOff),     "LEDs off"},
        };
    }

    bool setFirmwareLightingMode(std::uint8_t modeId, std::uint8_t brightness,
                                 std::uint8_t speed) override {
        try {
            // P1: START (opcode 0x18, marker 0x01)
            (void)m_transport->writeFeature(buildSetTimeStart());
            // P2: MODE_BEGIN (opcode 0x13)
            auto modeBegin = makeReport(CmdSetRgbMode);
            (void)m_transport->writeFeature(modeBegin);
            // P3: DATA - colour 0x00ffffff (white tint) by default; future
            // QML enhancement will plumb an RGB picker through.
            auto data = buildSetRgbModeData(modeId,
                                            /*r*/ 0xff, /*g*/ 0xff, /*b*/ 0xff,
                                            /*rainbow*/ 0, brightness, speed, /*direction*/ 0);
            // Re-write byte 0 to the data ReportId variant; vendor uses
            // the default 0x04 for this packet so makeReport already
            // covers us here (matches buildSetRgbModeData internal layout).
            data[0] = ReportId;
            (void)m_transport->writeFeature(data);
            // P4: SAVE (opcode 0x02 - shared with CmdSaveRtc).
            (void)m_transport->writeFeature(buildSetTimeSave());
            // P5: FINISH (opcode 0xF0) - end-of-envelope sentinel per
            // ak980pro_vendor.md §13.7. Vendor's standard config-commit
            // envelope is 5 packets; the 4-packet RTC variant works
            // because firmware accepts an early stop on SAVE, but for
            // non-RTC commits (lighting mode, settings batches) FINISH
            // is load-bearing on some firmware revisions. Match vendor
            // exactly to stay safe (issue #58 / P3.6).
            (void)m_transport->writeFeature(makeReport(CmdFinish));
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("keyboard.ak980",
                           "setFirmwareLightingMode: HID writeFeature failed: {}", e.what());
            return false;
        }
        AJAZZ_LOG_INFO("keyboard.ak980",
                       "firmware lighting mode {} set (brightness={}, speed={})",
                       static_cast<unsigned>(modeId),
                       static_cast<unsigned>(brightness),
                       static_cast<unsigned>(speed));
        return true;
    }

    [[nodiscard]] std::uint8_t brightnessMax() const noexcept override {
        return kAK980LightingBrightnessMax;
    }
    [[nodiscard]] std::uint8_t speedMax() const noexcept override {
        return kAK980LightingSpeedMax;
    }

    // ---- ISettingsCapable (settings batch opcode 0x07 sub 0x10) -----------
    // Vendor's "Settings" tab commits fn-switch + sleep-timer +
    // key-response-time in one 33-byte short report. Same 5-packet
    // envelope as setFirmwareLightingMode (START / DATA / SAVE / FINISH);
    // the wire format is documented in ak980pro_vendor.md §13.2 and the
    // byte map lives in proprietary_protocol.hpp Settings constants
    // (issue #57 / P3.x).
    bool setKeyboardSettings(core::KeyboardSettings const& settings) override {
        try {
            // P1: START
            (void)m_transport->writeFeature(buildSetTimeStart());
            // P2: SETTINGS-DATA (opcode 0x07 sub 0x10)
            auto data = buildSettingsBatch(settings.fnLayerSwitch,
                                           settings.sleepTimerMinutes,
                                           settings.keyResponseTimeLevel);
            (void)m_transport->writeFeature(data);
            // P3: SAVE (opcode 0x02)
            (void)m_transport->writeFeature(buildSetTimeSave());
            // P4: FINISH (opcode 0xF0) - end-of-envelope sentinel per
            // ak980pro_vendor.md §13.7 (same rule that issue #58 / P3.6
            // applied to setFirmwareLightingMode).
            (void)m_transport->writeFeature(makeReport(CmdFinish));
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("keyboard.ak980",
                           "setKeyboardSettings: HID writeFeature failed: {}", e.what());
            return false;
        }
        m_settingsCache = settings;
        // Normalise the cached entry so subsequent reads see the clamped
        // values the device actually persisted (response-time in [1..5];
        // 0 → vendor default 3 per buildSettingsBatch).
        m_settingsCache.keyResponseTimeLevel =
            settings.keyResponseTimeLevel == 0
                ? static_cast<std::uint8_t>(3)
                : std::clamp<std::uint8_t>(settings.keyResponseTimeLevel, 1, 5);
        AJAZZ_LOG_INFO("keyboard.ak980",
                       "settings batch sent: fn={}, sleep={}min, response={}",
                       static_cast<unsigned>(settings.fnLayerSwitch),
                       static_cast<unsigned>(settings.sleepTimerMinutes),
                       static_cast<unsigned>(m_settingsCache.keyResponseTimeLevel));
        return true;
    }

    [[nodiscard]] core::KeyboardSettings keyboardSettings() const override {
        return m_settingsCache;
    }

private:
    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    EventCallback m_callback;
    std::mutex m_mutex;
    /// Last-known settings cache. Initialised to vendor defaults so
    /// keyboardSettings() returns sensible values before any push.
    core::KeyboardSettings m_settingsCache{};
};

} // namespace

core::DevicePtr makeProprietaryKeyboard(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_shared<ProprietaryKeyboard>(d, std::move(id));
}

core::DevicePtr makeProprietaryKeyboardWithTransport(core::DeviceDescriptor const& d,
                                                     core::DeviceId id,
                                                     core::TransportPtr transport) {
    return std::make_shared<ProprietaryKeyboard>(d, std::move(id), std::move(transport));
}

} // namespace ajazz::keyboard
