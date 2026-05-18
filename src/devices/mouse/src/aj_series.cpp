// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file aj_series.cpp
 * @brief IDevice backend for AJAZZ AJ-series gaming mice (P3.12.2 migrated).
 *
 * Setters now dispatch through the vendor-correct `aj_series_protocol`
 * primitives (P3.12.1 commit 656fb1c) using the AJ159 APEX wire format
 * documented in `docs/protocols/mouse/aj_series_opcode_table.md`. The
 * prior all-wrong CommandId enum + makeEnvelope helper are removed.
 *
 * **Transport correction** (per AJ159 deep RE): mouse uses HID OUTPUT
 * REPORTS (`m_transport->write()`, interrupt-OUT) — NOT feature reports
 * (`writeFeature()`, SET_REPORT control transfer). Confirmed by Ghidra
 * audit of vendor iot_driver.exe (FUN_00702e40 = hid_write wrapper,
 * called via gRPC sendMsg, not sendRawFeature).
 *
 * The wire format ships byte-precise per the vendor spec but is UNTESTED
 * against real AJ-series hardware. Hardware witness required per
 * `docs/research/phase3-patch-sequence.md` §P3.12 before promoting any
 * AJ-series device's `devices.yaml` row from `scaffolded` to `partial`.
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
#include "aj_series_protocol.hpp"
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/mouse/mouse.hpp"

#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>

namespace ajazz::mouse {

namespace {

using namespace ajazz::core;
using namespace ajazz::mouse::aj_series; // FeaCmd, build*, kReportId, kReportSize

// Default profile used for setters that don't explicitly take a profile slot.
// Mouse vendor app stores configuration per-profile (8 slots); our backend
// targets profile 0 until per-profile UX lands (P1 roadmap item).
constexpr std::uint8_t kDefaultProfile = 0;

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
    [[nodiscard]] std::uint8_t dpiStageCount() const noexcept override {
        // P3.12.2: vendor supports 8 DPI stages per aj_series_opcode_table.md
        // §3.10. Prior 6 was an arbitrary cap from the early scaffold.
        return 8;
    }

    void setDpiStages(std::span<DpiStage const> stages) override {
        // Refresh the host-side cache so getDpiStages() reflects what is now
        // on the device. The cache is sized once at construction time.
        m_dpiStages.assign(dpiStageCount(), DpiStage{});
        for (std::size_t i = 0; i < stages.size() && i < dpiStageCount(); ++i) {
            m_dpiStages[i] = stages[i];
        }
        uploadDpiTableAtomic();
    }

    void setDpiStage(std::uint8_t index, DpiStage stage) override {
        if (index >= dpiStageCount()) {
            throw std::out_of_range("aj_series: setDpiStage index out of range");
        }
        if (m_dpiStages.size() != dpiStageCount()) {
            m_dpiStages.assign(dpiStageCount(), DpiStage{});
        }
        m_dpiStages[index] = stage;
        // Vendor pattern: write the FULL 8-stage table atomically via opcode
        // 0x54 on every change (not per-stage); no separate commit step.
        uploadDpiTableAtomic();
    }

    [[nodiscard]] std::vector<DpiStage> getDpiStages() const override { return m_dpiStages; }

    void setActiveDpiStage(std::uint8_t index) override {
        m_activeDpiStage = std::min<std::uint8_t>(index, 7);
        uploadDpiTableAtomic(); // re-upload with new active index
    }

    void setPollRateHz(std::uint16_t hz) override {
        // Opcode 0x04 (FEA_CMD_SET_REPORT), single-byte from _RateToNum lookup
        // at pkt[3]. Replaces our prior uint16-BE encoding which was wrong.
        auto const pkt = buildSetReportRate(kDefaultProfile, hz);
        (void)m_transport->write(pkt);
        m_pollRate = hz;
    }

    [[nodiscard]] std::uint16_t pollRateHz() const noexcept override { return m_pollRate; }

    void setLiftOffDistanceMm(float mm) override {
        // Opcode 0x53 omnibus packet (FEA_CMD_MOUSE_SET_OPTIONPARAM0). LOD
        // lives at byte 52 of this single transaction; we send the full
        // packet with all other fields at their cached defaults.
        m_options.liftCutOff = mmToLiftCutOffCode(mm);
        auto const pkt = buildMouseSetOption0(m_options);
        (void)m_transport->write(pkt);
    }

    void setButtonBinding(std::uint8_t button, std::uint32_t action) override {
        // Opcode 0x50 (FEA_CMD_MOUSE_SET_KEYMATRIX). Action at bytes 8..11
        // (our prior buggy offset was bytes 4..7).
        auto const pkt = buildMouseSetKeyMatrix(kDefaultProfile, button, action);
        (void)m_transport->write(pkt);
    }

    [[nodiscard]] std::optional<std::uint8_t> batteryPercent() const override {
        // Vendor has NO standalone battery query opcode on the mouse path —
        // battery is push-streamed from the dongle via the gRPC watchDevList
        // stream (`aj_series_opcode_table.md` §4). Our prior 0x40 query was
        // nonexistent and may have been silently NAK'd by firmware.
        // Returning nullopt advertises "battery state unknown" honestly.
        return std::nullopt;
    }

    // IRgbCapable
    [[nodiscard]] std::vector<RgbZone> rgbZones() const override {
        // Mouse RGB is a SINGLE 8-byte LED packet (opcode 0x07) covering both
        // logo and scroll zones together — no per-zone addressing in this
        // wire format. Surface a single virtual "all" zone to match.
        return {RgbZone{.name = "all", .ledCount = 1}};
    }

    void setRgbStatic(std::string_view, Rgb color) override {
        // Effect 1 (AlwaysOn), default speed 0, max brightness 5, no dazzle.
        m_lastLed.effect = 1;
        m_lastLed.speed = 0;
        m_lastLed.brightness = 5;
        m_lastLed.modeBits = 0x07; // NORMAL mode, no dazzle bit
        m_lastLed.r = color.r;
        m_lastLed.g = color.g;
        m_lastLed.b = color.b;
        emitLedPacket();
    }

    void setRgbEffect(std::string_view, RgbEffect effect, std::uint8_t speed) override {
        // Map our generic RgbEffect enum to vendor's effect IDs (1..10).
        // Conservative mapping — extend when QML UX surfaces specific vendor
        // effects via the new AJ-series-specific picker.
        m_lastLed.effect = mapEffectToVendor(effect);
        m_lastLed.speed = speed;
        m_lastLed.modeBits = 0x07;
        emitLedPacket();
    }

    void setRgbBuffer(std::string_view, std::span<Rgb const>) override {
        // Vendor does not expose per-LED RGB on the mouse — single 8-byte
        // packet only. No-op honestly.
    }

    void setRgbBrightness(std::uint8_t percent) override {
        // Brightness rides byte 3 of the 0x07 LED packet (no standalone
        // opcode per vendor RE). Clamp 0..100% → vendor scale 0..5.
        m_lastLed.brightness = static_cast<std::uint8_t>((percent * 5u) / 100u);
        emitLedPacket();
    }

private:
    /// Re-upload the full 8-stage DPI table atomically via opcode 0x54.
    void uploadDpiTableAtomic() {
        std::array<std::uint16_t, 8> dpis{};
        std::array<std::array<std::uint8_t, 3>, 8> colours{};
        std::uint8_t stageCount = 0;
        for (std::size_t i = 0; i < 8 && i < m_dpiStages.size(); ++i) {
            dpis[i] = m_dpiStages[i].dpi;
            colours[i] = {
                m_dpiStages[i].indicator.r, m_dpiStages[i].indicator.g, m_dpiStages[i].indicator.b};
            if (m_dpiStages[i].dpi != 0) {
                ++stageCount;
            }
        }
        auto const pkt = buildMouseSetOption1(m_activeDpiStage, stageCount, dpis, colours);
        (void)m_transport->write(pkt);
    }

    /// Emit the cached LED packet via opcode 0x07.
    void emitLedPacket() {
        auto const pkt = buildSetLedParam(m_lastLed.effect,
                                          m_lastLed.speed,
                                          m_lastLed.brightness,
                                          m_lastLed.modeBits,
                                          m_lastLed.r,
                                          m_lastLed.g,
                                          m_lastLed.b);
        (void)m_transport->write(pkt);
    }

    /// Translate float mm → vendor's 3-step lift-cut-off enum (0=1mm, 1=2mm, 2=3mm).
    static std::uint8_t mmToLiftCutOffCode(float mm) noexcept {
        if (mm < 1.5f)
            return 0;
        if (mm < 2.5f)
            return 1;
        return 2;
    }

    /// Map our generic RgbEffect to vendor's 1..10 effect ID enum.
    static std::uint8_t mapEffectToVendor(RgbEffect effect) noexcept {
        switch (effect) {
        case RgbEffect::Static:
            return 1; // AlwaysOn
        case RgbEffect::Breathing:
            return 2; // Breath
        case RgbEffect::Wave:
            return 4; // Wave
        case RgbEffect::ColorCycle:
            return 3; // Neon (rainbow cycle)
        case RgbEffect::ReactiveRipple:
            return 5; // Dazzle (closest equivalent)
        case RgbEffect::Custom:
            return 1; // fall back to AlwaysOn
        }
        return 1;
    }

    /// Cached LED packet state — every setter that mutates brightness / effect /
    /// colour updates this struct, then `emitLedPacket()` writes the full 8-byte
    /// 0x07 packet to keep all fields in sync with vendor semantics.
    struct CachedLedState {
        std::uint8_t effect{1}; // AlwaysOn
        std::uint8_t speed{0};
        std::uint8_t brightness{5};
        std::uint8_t modeBits{0x07}; // NORMAL, no dazzle
        std::uint8_t r{0xff};
        std::uint8_t g{0xff};
        std::uint8_t b{0xff};
    };
    CachedLedState m_lastLed{};

    /// Cached omnibus options for setLiftOffDistance / future setSensitivity /
    /// setSleepTimer / etc. — every setOption0-class setter mutates this struct
    /// then re-emits the full 0x53 packet (vendor's canonical pattern).
    aj_series::OptionPacket0 m_options{};

    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    EventCallback m_callback;
    std::mutex m_mutex;
    std::uint16_t m_pollRate{1000};
    std::uint8_t m_activeDpiStage{0};
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
