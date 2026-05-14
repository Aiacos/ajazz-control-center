// SPDX-License-Identifier: GPL-3.0-or-later
/** @file akp815.cpp
 *  @brief AJAZZ AKP815 device backend — 15-key v1-API Stream Dock with
 *         100×100 LCD keys and an 800×480 LCD strip.
 *
 *  The wire protocol is identical to the AKP153 (`[ajazz-sdk]/info.rs`
 *  reports both as `is_v1_api()`); only the image geometry differs:
 *
 *  | Property         | AKP153                 | AKP815                |
 *  |------------------|------------------------|-----------------------|
 *  | LCD-key grid     | 3 rows × 5 cols        | 5 rows × 3 cols       |
 *  | Per-key JPEG     | 85×85, Rot90 + mirror  | 100×100, Rot180       |
 *  | LCD strip        | 854×480, Rot0          | 800×480, Rot0         |
 *  | USB VID:PID      | 0x5548:0x6674          | 0x5548:0x6672         |
 *
 *  The backend delegates the wire-level state machine (open / close /
 *  key event parsing / brightness / clear) to the AKP153 implementation
 *  by reusing `akp153::*` builders and the same `parseInputReport`. The
 *  only behavioural difference is the `DisplayInfo` returned to callers,
 *  which determines how the image-pipeline phase will resize and rotate
 *  the bitmap before pushing it through `BAT` chunks.
 *
 *  Clean-room reconstruction from `[ajazz-sdk]` plus the AKP815 entry in
 *  `docs/protocols/streamdeck/akp815.md`. No vendor source is included.
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/streamdeck/streamdeck.hpp"
#include "akp153_protocol.hpp"
#include "akp815_protocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

namespace ajazz::streamdeck {

namespace {

using namespace ajazz::core;

// File-static once_flag (Pitfall 14): WARN emits at most once per process
// lifetime for this backend. Distinct flag — not shared across backends.
std::once_flag s_warned_akp815;

/** @brief Concrete IDevice implementation for the AJAZZ AKP815.
 *
 *  @class Akp815Device
 *
 *  Wraps the same wire-level state machine as @ref Akp153Device but with
 *  a different display geometry (5×3 100×100 keys plus an 800×480 strip).
 *  All write paths (`setKeyImage`, `setBrightness`, `clearKey`, `flush`)
 *  reuse the AKP153 `akp153::build*` helpers because the opcode bytes are
 *  identical at the v1 API. The differences kick in inside the image
 *  pipeline (phase 2) when the source bitmap is resized and rotated for
 *  the AKP815's 100×100 / `Rot180` slots.
 *
 *  Hardware capabilities:
 *  - 15 keys in a 5×3 grid with 100×100 JPEG LCDs
 *  - One 800×480 LCD strip used for boot logo / wallpaper
 *  - No encoder, no touch strip
 *
 *  @note Firmware update support is not yet implemented;
 *        @c beginFirmwareUpdate() throws unconditionally.
 */
class Akp815Device final : public IDevice,
                           public IDisplayCapable,
                           public IFirmwareCapable,
                           public IClockCapable {
public:
    /// Production constructor: opens a real libhidapi transport for this device.
    Akp815Device(DeviceDescriptor descriptor, DeviceId id)
        : Akp815Device(std::move(descriptor),
                       id,
                       makeHidTransport(id.vendorId, id.productId, id.serial)) {}

    /// Test constructor accepting an injected transport (COD-026).
    Akp815Device(DeviceDescriptor descriptor, DeviceId id, TransportPtr transport)
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
        AJAZZ_LOG_INFO("akp815", "device opened: {}", m_descriptor.model);
    }

    void close() override {
        if (!m_transport->isOpen()) {
            return;
        }
        try {
            auto stop = std::array<std::uint8_t, akp815::PacketSize>{};
            stop[0] = akp153::CmdPrefix[0];
            stop[1] = akp153::CmdPrefix[1];
            stop[2] = akp153::CmdPrefix[2];
            stop[5] = akp153::CmdStop[0];
            stop[6] = akp153::CmdStop[1];
            stop[7] = akp153::CmdStop[2];
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
        std::array<std::uint8_t, akp815::PacketSize> buf{};
        std::size_t emitted = 0;
        for (int i = 0; i < 8; ++i) {
            auto const n = m_transport->read(buf, std::chrono::milliseconds{0});
            if (n == 0) {
                break;
            }
            // The input-report layout is identical to AKP153 (key index at
            // byte 9, no press/release polarity), so we reuse the AKP153
            // parser to keep a single source of truth for the v1-API
            // input pipeline.
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
        // The AKP815-specific geometry is the *only* place where this
        // backend diverges from AKP153 functionally — the rest is just
        // delegating to the shared v1-API state machine.
        return DisplayInfo{
            .widthPx = akp815::KeyWidthPx,
            .heightPx = akp815::KeyHeightPx,
            .keyRows = akp815::KeyRows,
            .keyCols = akp815::KeyCols,
            .jpegEncoded = true,
        };
    }

    void setKeyImage(std::uint8_t keyIndex,
                     std::span<std::uint8_t const> rgba,
                     std::uint16_t width,
                     std::uint16_t height) override {
        // The image pipeline that resizes to 100×100 and rotates 180° is
        // tracked in `TODO.md` → "AKP815 image pipeline". Here we accept
        // pre-encoded JPEG payloads verbatim, mirroring the AKP153 path.
        (void)width;
        (void)height;
        sendImage(keyIndex, rgba);
    }

    void setKeyColor(std::uint8_t keyIndex, Rgb color) override {
        // No JPEG synthesiser yet — fall back to clearing the key.
        (void)color;
        clearKey(keyIndex);
    }

    void clearKey(std::uint8_t keyIndex) override {
        auto const pkt =
            (keyIndex == 0xff) ? akp153::buildClearAll() : akp153::buildClearKey(keyIndex);
        (void)m_transport->write(pkt);
    }

    void setMainImage(std::span<std::uint8_t const>, std::uint16_t, std::uint16_t) override {
        // The 800×480 LCD strip is reachable through a dedicated upload path
        // (`LOG` opcode + payload) that the image pipeline will exercise
        // once it ships. Today the AKP815 strip is left to display the
        // factory boot logo.
    }

    void setBrightness(std::uint8_t percent) override {
        auto const pkt = akp153::buildSetBrightness(percent);
        (void)m_transport->write(pkt);
    }

    void flush() override {
        // Shares the AKP153 `STP` opcode.
        auto stop = std::array<std::uint8_t, akp815::PacketSize>{};
        stop[0] = akp153::CmdPrefix[0];
        stop[1] = akp153::CmdPrefix[1];
        stop[2] = akp153::CmdPrefix[2];
        stop[5] = akp153::CmdStop[0];
        stop[6] = akp153::CmdStop[1];
        stop[7] = akp153::CmdStop[2];
        (void)m_transport->write(stop);
    }

    // ---- IFirmwareCapable ---------------------------------------------------
    [[nodiscard]] FirmwareInfo firmwareInfo() const override { return m_firmware; }

    std::uint32_t beginFirmwareUpdate(std::span<std::uint8_t const>) override {
        throw std::runtime_error("AKP815 firmware update not yet supported");
    }

    [[nodiscard]] std::uint8_t firmwareUpdateProgress(std::uint32_t) const override { return 0; }

    // ---- IClockCapable ------------------------------------------------------
    //
    // Scaffolded stub (A-02 — Phase 5 D-01 amendment 1): AKP815 backend landed
    // post-upstream (commit 62da68c). Symmetric with akp153/03/05 — WARN-once
    // via s_warned_akp815 (Pitfall 14), return NotImplemented.
    [[nodiscard]] TimeSyncResult
    setTime([[maybe_unused]] std::chrono::system_clock::time_point tp) override {
        std::call_once(s_warned_akp815, [] {
            AJAZZ_LOG_WARN("streamdeck.akp815", "setTime() not yet implemented for akp815");
        });
        return TimeSyncResult::NotImplemented;
    }

private:
    /** @brief Transmit a JPEG image to a single AKP815 key LCD.
     *
     *  Shares the AKP153 image-header framing because the v1 API is
     *  identical at byte level. The only thing that should differ is the
     *  pre-upload resize + rotate transform, which lives in the image
     *  pipeline.
     */
    void sendImage(std::uint8_t keyIndex, std::span<std::uint8_t const> jpeg) {
        if (jpeg.size() > 0xFFFFu) {
            AJAZZ_LOG_WARN("akp815",
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
            std::array<std::uint8_t, akp815::PacketSize> chunk{};
            auto const take = std::min<std::size_t>(akp815::PacketSize, jpeg.size() - offset);
            std::memcpy(chunk.data(), jpeg.data() + offset, take);
            (void)m_transport->write(chunk);
            offset += take;
        }
    }

    DeviceDescriptor m_descriptor; ///< Static hardware description.
    DeviceId m_id;                 ///< HID bus identity.
    TransportPtr m_transport;      ///< Underlying HID I/O channel.
    FirmwareInfo m_firmware{.version = "unknown", .buildDate = {}, .bootloaderAvailable = false};
    EventCallback m_callback; ///< Registered input-event sink (may be null).
    std::mutex m_mutex;       ///< Guards m_callback for thread-safe registration.
};

} // namespace

core::DevicePtr makeAkp815(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_shared<Akp815Device>(d, std::move(id));
}

} // namespace ajazz::streamdeck
