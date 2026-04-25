// SPDX-License-Identifier: GPL-3.0-or-later
/** @file akp153.cpp
 *  @brief AJAZZ AKP153 / AKP153E "Stream Dock"-class device backend.
 *
 *  Implements the full wire protocol for the AKP153 family: 15 keys arranged
 *  in a 3×5 grid, each with an 85×85 JPEG LCD.  Two USB Product IDs exist:
 *  0x1001 (international / Mirabox HSV293S) and 0x1002 (China / AKP153E).
 *  Both share the identical protocol.
 *
 *  The protocol is a clean-room reconstruction from the notes in
 *  docs/protocols/streamdeck/akp153.md; no third-party source is incorporated.
 */
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

/** @brief Allocate a zero-initialised 512-byte packet.
 *
 *  Convenience factory used by @ref buildCmdHeader() so that all protocol
 *  builder functions start from a clean slate without repeating the
 *  zero-fill logic.
 *
 *  @return Zero-filled 512-byte packet array.
 */
std::array<std::uint8_t, PacketSize> emptyPacket() noexcept {
    std::array<std::uint8_t, PacketSize> pkt{};
    return pkt;
}

/** @brief Construct a 512-byte command packet with the AKP prefix and a
 *         three-byte ASCII command word.
 *
 *  Byte layout produced:
 *  - Bytes 0–2:  "CRT" prefix (0x43 0x52 0x54)
 *  - Bytes 3–4:  0x00 0x00
 *  - Bytes 5–7:  @p cmd (three ASCII command bytes)
 *  - Bytes 8–511: zero-padded payload area (filled by callers)
 *
 *  @param cmd  Three-byte ASCII command identifier (e.g. CmdLight, CmdBat).
 *  @return     Fully initialised command packet.
 */
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

/** @brief Build a backlight brightness command packet.
 *
 *  Controls the global brightness of all 15 key LCDs.
 *  Byte 10 carries the clamped brightness value (0–100).
 *
 *  @param percent  Desired brightness in the range 0–100; values above 100
 *                  are silently clamped to 100.
 *  @return         Ready-to-send 512-byte packet.
 */
std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent) {
    auto pkt = buildCmdHeader(CmdLight);
    pkt[10] = std::min<std::uint8_t>(percent, 100);
    return pkt;
}

/** @brief Build a "clear all keys" command packet.
 *
 *  Instructs the firmware to black-out every key LCD simultaneously.
 *  Byte 10 = 0x00, byte 11 = 0xFF (broadcast sentinel).
 *
 *  @return Ready-to-send 512-byte packet.
 */
std::array<std::uint8_t, PacketSize> buildClearAll() {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x00;
    pkt[11] = 0xff;
    return pkt;
}

/** @brief Build a "clear single key" command packet.
 *
 *  Instructs the firmware to black-out one specific key LCD.
 *  Byte 10 = 0x00, byte 11 = @p keyIndex.
 *
 *  @param keyIndex  1-based key index (1..KeyCount).
 *  @return          Ready-to-send 512-byte packet.
 */
std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex) {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x00;
    pkt[11] = keyIndex;
    return pkt;
}

/** @brief Build a "show device logo" command packet.
 *
 *  Triggers the firmware to display the built-in AJAZZ logo on all key
 *  LCDs.  The magic byte pair 0x44/0x43 at offsets 10–11 is a firmware
 *  constant ("DC") observed during protocol capture.
 *
 *  @return Ready-to-send 512-byte packet.
 */
std::array<std::uint8_t, PacketSize> buildShowLogo() {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x44;
    pkt[11] = 0x43;
    return pkt;
}

/** @brief Build the header packet for a key-image transfer.
 *
 *  The firmware expects one header packet followed immediately by one or more
 *  512-byte raw JPEG data packets.  The header encodes:
 *  - Bytes 10–11: big-endian total JPEG byte count
 *  - Byte 12:     1-based key index
 *
 *  @param keyIndex  Destination key index (1..KeyCount).
 *  @param jpegSize  Total byte length of the JPEG payload (≤ 0xFFFF).
 *  @return          Ready-to-send 512-byte header packet.
 */
std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                      std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdBat);
    // Big-endian 16-bit size at offsets 10..11.
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    pkt[12] = keyIndex;
    return pkt;
}

/** @brief Decode one raw HID input report into a structured @ref KeyEvent.
 *
 *  The AKP153 encodes key events in a 512-byte HID report.  The tag byte at
 *  offset 9 carries the 1-based key index (1..KeyCount); values of 0 or above
 *  KeyCount are rejected.  ACK frames (bytes 0–2 == "ACK" = 0x41 0x43 0x4B)
 *  are silently discarded.
 *
 *  @note  The device emits a single "released" style report per transition;
 *         press/release state is maintained by the higher-level polling loop.
 *         This function always returns @c pressed = @c true for valid reports.
 *
 *  @param frame  Raw HID report bytes (must be at least 16 bytes).
 *  @return       Decoded key event, or @c std::nullopt for ACK / invalid frames.
 */
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

/** @brief Concrete IDevice implementation for the AJAZZ AKP153 / AKP153E.
 *
 *  @class Akp153Device
 *
 *  Glues the stateless @c akp153:: protocol helpers to the HID transport layer
 *  and exposes the @ref IDisplayCapable and @ref IFirmwareCapable mix-in
 *  interfaces.  The AKP153 does not have a main display or rotary encoders.
 *
 *  Hardware capabilities:
 *  - 15 keys arranged in a 3×5 grid, each with an 85×85 JPEG LCD
 *  - Two USB PIDs: 0x1001 (international) and 0x1002 (China / AKP153E)
 *  - No encoder, no touch strip, no main LCD
 *
 *  @note  Firmware update support is defined in the interface but not yet
 *         implemented; @c beginFirmwareUpdate() throws unconditionally.
 */
class Akp153Device final : public IDevice, public IDisplayCapable, public IFirmwareCapable {
public:
    /// Production constructor: opens a real libhidapi transport for this device.
    Akp153Device(DeviceDescriptor descriptor, DeviceId id)
        : Akp153Device(std::move(descriptor),
                       id,
                       makeHidTransport(id.vendorId, id.productId, id.serial)) {}

    /// Test constructor: accepts an injected transport (e.g. a fake) so unit
    /// tests don't need libhidapi or a real device on the bus (COD-026).
    Akp153Device(DeviceDescriptor descriptor, DeviceId id, TransportPtr transport)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(std::move(transport)) {}

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
    /** @brief Transmit a JPEG image to a single key LCD.
     *
     *  Sends the pre-built image header packet (carrying the key index and
     *  big-endian JPEG size) and then streams the JPEG payload in 512-byte
     *  chunks.  Partial trailing chunks are zero-padded automatically by the
     *  zero-initialised @c chunk array.
     *
     *  @param keyIndex  1-based destination key index (1..KeyCount).
     *  @param jpeg      Raw JPEG bytes to transmit (≤ 65535 bytes).
     */
    void sendImage(std::uint8_t keyIndex, std::span<std::uint8_t const> jpeg) {
        // SEC-008 / COD-013 / CWE-190: header length field is 16 bits.
        // Refuse oversize payloads instead of truncating the header while
        // still streaming the full body, which would desync the firmware.
        if (jpeg.size() > 0xFFFFu) {
            AJAZZ_LOG_WARN("akp153",
                           "sendImage: payload {} bytes exceeds 65535-byte protocol max; "
                           "refusing",
                           jpeg.size());
            return;
        }
        auto const header =
            akp153::buildImageHeader(keyIndex, static_cast<std::uint16_t>(jpeg.size()));
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

    DeviceDescriptor m_descriptor; ///< Static hardware description supplied at construction.
    DeviceId m_id;                 ///< HID bus identity (VID, PID, serial string).
    TransportPtr m_transport;      ///< Underlying HID I/O channel.
    FirmwareInfo m_firmware{.version = "unknown", .buildDate = {}, .bootloaderAvailable = false};
    ///< Cached firmware metadata; version remains "unknown" until queried.
    EventCallback m_callback; ///< Registered input-event sink (may be null).
    std::mutex m_mutex;       ///< Guards m_callback for thread-safe registration.
};

} // namespace

/** @brief Factory function: construct an AKP153 device object.
 *
 *  Instantiates an @c Akp153Device with the provided descriptor and HID
 *  identity.  Suitable for both the 0x1001 (international) and 0x1002
 *  (China / AKP153E) product IDs — the protocol is identical.
 *
 *  @param d    Static device descriptor (model name, family, codename).
 *  @param id   HID bus identity used to open the underlying transport.
 *  @return     Owning pointer to the new device instance.
 */
core::DevicePtr makeAkp153(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_unique<Akp153Device>(d, std::move(id));
}

} // namespace ajazz::streamdeck
