// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file aj_series.cpp
 * @brief IDevice backend for AJAZZ AJ-series gaming mice.
 *
 * **⚠ KNOWN WIRE-FORMAT MISMATCH ⚠**
 * The AJ159 APEX vendor RE landed 2026-05-17 found that the opcode table
 * baked into this file is **wrong against the actual AJ159 firmware** —
 * see docs/protocols/mouse/aj_series_vendor.md and the prioritised gap
 * list in docs/research/clean-reimplementation-roadmap.md §1 (P0 safety
 * fixes) and §11.5 (full wire-format rewrite scheduled as the largest
 * single commit in the next milestone window).
 *
 * Specifically, the vendor app:
 *   - uses opcode 0x54 (not 0x21) for the omnibus DPI-table write
 *   - uses opcode 0x04 (not 0x22) for poll-rate via _RateToNum lookup
 *   - rides LOD inside byte 52 of the 0x53 omnibus packet (not standalone 0x23)
 *   - uses opcode 0x50 (not 0x24) for button binding, payload at byte 8
 *   - uses opcode 0x07 (not 0x30) for an 8-byte light packet (no standalone
 *     brightness opcode — brightness is byte 3 of that packet)
 *   - pulls battery from the dongle's gRPC watchDevList stream — there is
 *     NO standalone 0x40 query opcode
 *   - computes checksum as `sum & 0x7F` (BIT7), NOT `sum & 0xFF` (BIT8) we
 *     hard-code at line ~82
 *
 * **DAMAGING bug isolated in the same RE:** our former `commit()` helper
 * wrote opcode `kCmdCommit = 0x50` which is `FEA_CMD_MOUSE_SET_KEYMATRIX`
 * in vendor speak — every commit call silently corrupted button slot 0
 * with a malformed keymap. The `commit()` helper and its two call sites
 * have been **REMOVED** as a P0 safety guard (this file); the other wrong
 * opcodes degrade gracefully to silent no-ops until the §11.5 rewrite
 * lands. Risk currently contained because AJ159 family PIDs
 * (0x3151:0x5008 wired / 0x3151:0x4026 wireless) are NOT registered in
 * register.cpp — only 0x3151:0x5007 (ajazz_24g_8k) is, and per the
 * vendor RE its wire format is likely the same broken set.
 *
 * Reverse-engineered from the official Windows utility (Wireshark + USBPcap
 * captures of @c ajazz-aj199-official-software).  Byte-level vendor reference
 * in docs/protocols/mouse/aj_series_vendor.md (vendor RE doc, authoritative).
 * docs/protocols/mouse/aj_series.md (clean-room doc) is now STALE pending
 * the §11.5 rewrite.
 *
 * Feature-report envelope (64 bytes, our current implementation):
 * @code
 *   byte  0 : report id (0x05)
 *   byte  1 : command id (CommandId enum)         ← WRONG opcodes per §11.5
 *   byte  2 : sub-command
 *   byte  3 : payload length
 *   byte 4…N: payload
 *   byte 63 : checksum = sum(bytes 1..62) & 0xFF  ← WRONG (should be & 0x7F)
 * @endcode
 */
//
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/mouse/mouse.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <numeric>
#include <stdexcept>

namespace ajazz::mouse {

namespace {

using namespace ajazz::core;

constexpr std::size_t kReportSize = 64;

/// Command byte values placed at offset 1 of the 64-byte feature-report envelope.
///
/// **⚠ EVERY VALUE IN THIS ENUM IS WRONG against the AJ159 vendor firmware ⚠**
/// See file-header comment for the corrected opcode table that the §11.5
/// rewrite will introduce. The constants remain here so the file compiles
/// and the wire format stays stable for non-AJ159 PIDs that historically
/// shipped against this code path, but each setter using them currently
/// degrades to a silent no-op on AJ159 family hardware.
///
/// `kCmdCommit = 0x50` in particular collides with
/// `FEA_CMD_MOUSE_SET_KEYMATRIX` — its helper has been REMOVED from this
/// translation unit as a P0 safety guard. The constant is retained for the
/// §11.5 rewrite to replace with the vendor-correct keymap opcode.
enum CommandId : std::uint8_t {
    kCmdDpi = 0x21,      ///< [WRONG; vendor: 0x54] Configure DPI stages and per-stage indicator colour.
    kCmdPollRate = 0x22, ///< [WRONG; vendor: 0x04] Set the USB polling rate (Hz).
    kCmdLod = 0x23,      ///< [WRONG; vendor: byte 52 of 0x53 omnibus] Set lift-off distance.
    kCmdButton = 0x24,   ///< [WRONG; vendor: 0x50 at byte 8] Bind a button to a HID action.
    kCmdRgb = 0x30,      ///< [WRONG; vendor: 0x07 8-byte packet] Control RGB lighting.
    kCmdBattery = 0x40,  ///< [WRONG; vendor: no standalone opcode — dongle pushes via stream] Battery query.
    kCmdCommit = 0x50,   ///< [DAMAGING; vendor: FEA_CMD_MOUSE_SET_KEYMATRIX] DO NOT INVOKE — helper removed.
};

/**
 * @brief Build a 64-byte feature-report envelope.
 *
 * Fills the standard header (report-id, command, sub-command, length),
 * copies up to 59 bytes of payload, then appends the checksum at byte 63.
 * The checksum is the 8-bit sum of bytes 1–62.
 *
 * @param cmd      Command id (CommandId enum value).
 * @param sub      Sub-command discriminator (command-specific meaning).
 * @param payload  Up to 59 bytes of command-specific data.
 * @return         Fully formed 64-byte report ready for ITransport::writeFeature().
 */
/// Maximum payload bytes that fit between the header (4 bytes) and trailing
/// checksum byte. Reports larger than this are rejected to keep the advertised
/// length byte and the bytes actually written in lockstep (SEC-007 / CWE-131).
constexpr std::size_t kMaxPayload = kReportSize - 5;

std::array<std::uint8_t, kReportSize>
makeEnvelope(std::uint8_t cmd, std::uint8_t sub, std::span<std::uint8_t const> payload) {
    if (payload.size() > kMaxPayload) {
        throw std::invalid_argument("aj_series: payload exceeds 59 bytes");
    }
    std::array<std::uint8_t, kReportSize> pkt{};
    pkt[0] = 0x05;
    pkt[1] = cmd;
    pkt[2] = sub;
    // Length byte is the count we actually copied below; never the caller's
    // raw .size() if it could be larger than the body.
    pkt[3] = static_cast<std::uint8_t>(payload.size());
    std::copy(payload.begin(), payload.end(), pkt.begin() + 4);
    pkt[kReportSize - 1] = static_cast<std::uint8_t>(
        std::accumulate(pkt.begin() + 1, pkt.end() - 1, std::uint32_t{0}) & 0xff);
    return pkt;
}

/**
 * @brief IDevice backend for AJAZZ AJ-series gaming mice.
 *
 * Implements IDevice, IMouseCapable, and IRgbCapable using 64-byte HID
 * feature reports on the configuration interface.  All configuration
 * commands use writeFeature(); battery reads use writeFeature + readFeature.
 *
 * @note The mutex guards only @c m_callback; configuration setters are not
 *       internally serialised and should be called from a single thread.
 * @see makeAjSeries()
 */
class AjSeriesMouse final : public IDevice, public IMouseCapable, public IRgbCapable {
public:
    /** Production constructor — creates a real HID transport. */
    AjSeriesMouse(DeviceDescriptor descriptor, DeviceId id)
        : AjSeriesMouse(std::move(descriptor),
                        id,
                        makeHidTransport(id.vendorId, id.productId, id.serial)) {}

    /** Test constructor with injected transport (DI for unit tests). */
    AjSeriesMouse(DeviceDescriptor descriptor, DeviceId id, TransportPtr transport)
        : m_descriptor(std::move(descriptor)), m_id(std::move(id)),
          m_transport(std::move(transport)) {}

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

    // IMouseCapable
    [[nodiscard]] std::uint8_t dpiStageCount() const noexcept override { return 6; }

    void setDpiStages(std::span<DpiStage const> stages) override {
        // Refresh the host-side cache so getDpiStages() reflects what is now
        // on the device. The cache is sized once at construction time.
        m_dpiStages.assign(dpiStageCount(), DpiStage{});
        for (std::size_t i = 0; i < stages.size() && i < dpiStageCount(); ++i) {
            m_dpiStages[i] = stages[i];
            uploadDpiStage(static_cast<std::uint8_t>(i), stages[i]);
        }
        // P0 safety guard (2026-05-17): commit() helper removed because its
        // opcode 0x50 collides with FEA_CMD_MOUSE_SET_KEYMATRIX in the vendor
        // firmware. The §11.5 rewrite will replace this with the vendor's
        // actual commit path (the vendor app has NO separate commit step —
        // writes via opcode 0x54 are immediately persistent).
    }

    void setDpiStage(std::uint8_t index, DpiStage stage) override {
        if (index >= dpiStageCount()) {
            throw std::out_of_range("aj_series: setDpiStage index out of range");
        }
        if (m_dpiStages.size() != dpiStageCount()) {
            m_dpiStages.assign(dpiStageCount(), DpiStage{});
        }
        m_dpiStages[index] = stage;
        uploadDpiStage(index, stage);
        // P0 safety guard (2026-05-17): no commit() call — see setDpiStages().
    }

    [[nodiscard]] std::vector<DpiStage> getDpiStages() const override { return m_dpiStages; }

    void setActiveDpiStage(std::uint8_t index) override {
        std::array<std::uint8_t, 1> p{index};
        auto const pkt = makeEnvelope(kCmdDpi, 0x01, p);
        (void)m_transport->writeFeature(pkt);
    }

    void setPollRateHz(std::uint16_t hz) override {
        std::array<std::uint8_t, 2> p{static_cast<std::uint8_t>(hz >> 8),
                                      static_cast<std::uint8_t>(hz & 0xff)};
        auto const pkt = makeEnvelope(kCmdPollRate, 0x00, p);
        (void)m_transport->writeFeature(pkt);
        m_pollRate = hz;
    }

    [[nodiscard]] std::uint16_t pollRateHz() const noexcept override { return m_pollRate; }

    void setLiftOffDistanceMm(float mm) override {
        std::array<std::uint8_t, 1> p{static_cast<std::uint8_t>(mm * 10.0f)};
        auto const pkt = makeEnvelope(kCmdLod, 0x00, p);
        (void)m_transport->writeFeature(pkt);
    }

    void setButtonBinding(std::uint8_t button, std::uint32_t action) override {
        std::array<std::uint8_t, 5> p{
            button,
            static_cast<std::uint8_t>(action >> 24),
            static_cast<std::uint8_t>(action >> 16),
            static_cast<std::uint8_t>(action >> 8),
            static_cast<std::uint8_t>(action & 0xff),
        };
        auto const pkt = makeEnvelope(kCmdButton, 0x00, p);
        (void)m_transport->writeFeature(pkt);
    }

    [[nodiscard]] std::optional<std::uint8_t> batteryPercent() const override {
        std::array<std::uint8_t, kReportSize> req = makeEnvelope(kCmdBattery, 0x00, {});
        (void)m_transport->writeFeature(req);
        std::array<std::uint8_t, kReportSize> resp{};
        resp[0] = 0x05;
        (void)m_transport->readFeature(resp);
        return resp[4];
    }

    // IRgbCapable
    [[nodiscard]] std::vector<RgbZone> rgbZones() const override {
        return {RgbZone{.name = "logo", .ledCount = 1}, RgbZone{.name = "scroll", .ledCount = 1}};
    }

    void setRgbStatic(std::string_view zone, Rgb color) override {
        std::array<std::uint8_t, 4> p{
            zone == "scroll" ? std::uint8_t{1} : std::uint8_t{0}, color.r, color.g, color.b};
        auto const pkt = makeEnvelope(kCmdRgb, 0x00, p);
        (void)m_transport->writeFeature(pkt);
    }

    void setRgbEffect(std::string_view, RgbEffect effect, std::uint8_t speed) override {
        std::array<std::uint8_t, 2> p{static_cast<std::uint8_t>(effect), speed};
        auto const pkt = makeEnvelope(kCmdRgb, 0x01, p);
        (void)m_transport->writeFeature(pkt);
    }

    void setRgbBuffer(std::string_view, std::span<Rgb const>) override {}
    void setRgbBrightness(std::uint8_t percent) override {
        std::array<std::uint8_t, 1> p{percent};
        auto const pkt = makeEnvelope(kCmdRgb, 0x02, p);
        (void)m_transport->writeFeature(pkt);
    }

private:
    /**
     * @brief Upload a single DPI stage to the mouse without committing.
     *
     * Wrapping the per-stage HID write here keeps setDpiStages() and
     * setDpiStage() in lock-step on the wire format and lets the
     * unit-test transport intercept either path identically.
     */
    void uploadDpiStage(std::uint8_t index, DpiStage const& stage) {
        std::array<std::uint8_t, 6> p{
            index,
            static_cast<std::uint8_t>(stage.dpi >> 8),
            static_cast<std::uint8_t>(stage.dpi & 0xff),
            stage.indicator.r,
            stage.indicator.g,
            stage.indicator.b,
        };
        auto const pkt = makeEnvelope(kCmdDpi, 0x00, p);
        (void)m_transport->writeFeature(pkt);
    }

    // P0 safety guard (2026-05-17): commit() helper REMOVED. Its opcode 0x50
    // collides with FEA_CMD_MOUSE_SET_KEYMATRIX in the AJAZZ vendor firmware
    // (see roadmap §1.1 and docs/protocols/mouse/aj_series_vendor.md). Every
    // call wrote a malformed 4-byte HID action to button slot 0, silently
    // corrupting the user's button mapping. The §11.5 rewrite will implement
    // the vendor-correct path (vendor has NO separate commit; opcode 0x54
    // DPI writes are immediately persistent on hardware).

    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    EventCallback m_callback;
    std::mutex m_mutex;
    std::uint16_t m_pollRate{1000};
    std::vector<DpiStage> m_dpiStages{dpiStageCount(),
                                      DpiStage{}}; ///< Host-side cache of the on-device DPI table.
};

} // namespace

core::DevicePtr makeAjSeries(core::DeviceDescriptor const& d, core::DeviceId id) {
    return std::make_shared<AjSeriesMouse>(d, std::move(id));
}

/**
 * @brief CAPTURE-04 (Plan 09-04) — test-only factory that forwards to the
 *        anonymous-namespace `AjSeriesMouse` COD-026 DI constructor with
 *        an injected `ITransport`.
 *
 * Production code uses `makeAjSeries()` above; this overload exposes the
 * same backend with a substitutable transport so unit tests can assert
 * byte-level wire-format equality via `MockTransport::writes()` without
 * touching real HID hardware.
 */
core::DevicePtr makeAjSeriesWithTransport(core::DeviceDescriptor const& d,
                                          core::DeviceId id,
                                          core::TransportPtr transport) {
    return std::make_shared<AjSeriesMouse>(d, std::move(id), std::move(transport));
}

} // namespace ajazz::mouse
