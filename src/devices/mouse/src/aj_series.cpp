// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file aj_series.cpp
 * @brief IDevice backend for AJAZZ AJ-series gaming mice.
 *
 * Wire format: vendor-correct AJ159 APEX byte layout per
 * `docs/protocols/mouse/aj_series_opcode_table.md` and
 * `docs/protocols/mouse/aj_series_vendor.md`. Setters dispatch through
 * the `aj_series_protocol` builder library (`buildSetReportRate`,
 * `buildMouseSetOption0/1`, `buildMouseSetKeyMatrix`, `buildSetLedParam`,
 * `buildSetTftLcdData`); this file only owns lifecycle + caching.
 *
 * Transport: HID OUTPUT REPORTS (`m_transport->write()`, interrupt-OUT).
 * NOT feature reports — confirmed by Ghidra audit of `iot_driver.exe`
 * (vendor's Rust gRPC daemon dispatches via the `sendMsg` path, which
 * wraps `hid_write`, not `sendRawFeature`).
 *
 * Capabilities implemented:
 *   - @ref IMouseCapable      — DPI stages, poll rate, LOD, button bind
 *   - @ref IRgbCapable        — 8-byte 0x07 LED packet (single virtual zone)
 *   - @ref IClockCapable      — clock + DPI face on the dock TFT (opcode
 *                               0x25 chunked RGB565 per
 *                               `aj_series_tft_pipeline`)
 *   - @ref IMouseMacroCapable — 256-byte macro upload (opcode 0x16 chunked,
 *                               20 slots) per `aj_series_opcode_table.md` §3.11
 *
 * Battery is intentionally NOT exposed via @ref IBatteryCapable: the
 * vendor RE confirmed there is no standalone battery-query opcode on the
 * mouse HID path. Vendor pulls battery from the dongle's gRPC
 * `watchDevList` stream (out of scope for our in-process design).
 *
 * Maturity: `scaffolded` -> `partial` for SKUs with a TFT basetta
 * (ajazz_24g_8k, aj199_family, aj199_family_dongle, aj159_apex_*) per
 * `devices.yaml`. Hardware round-trip witness is still pending for the
 * setter envelopes (see HANDOFF "Hardware witnesses").
 */
#include "aj_series_protocol.hpp"
#include "aj_series_tft_pipeline.hpp"
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/core/hid_transport.hpp"
#include "ajazz/core/logger.hpp"
#include "ajazz/mouse/mouse.hpp"

#include <QSize>

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
 * Implements IDevice, IMouseCapable, IRgbCapable, IClockCapable using
 * 65-byte HID OUTPUT reports (interrupt-OUT endpoint) on the
 * configuration interface. Every setter dispatches through the
 * `aj_series_protocol` builder library — this class only owns the
 * lifecycle, the host-side caches for the omnibus 0x53/0x54 packets,
 * and the IClockCapable TFT face renderer pipeline.
 *
 * @note The mutex guards only @c m_callback; configuration setters are not
 *       internally serialised and should be called from a single thread.
 * @see makeAjSeries(), aj_series_protocol.hpp, aj_series_tft_pipeline.hpp
 */
class AjSeriesMouse final : public IDevice,
                            public IMouseCapable,
                            public IRgbCapable,
                            public IClockCapable,
                            public IPollingRateCapable,
                            public IProfileSelectCapable,
                            public IMouseSettingsCapable,
                            public IFactoryResettable,
                            public IDpiTableCapable,
                            public IMouseFnRemappable,
                            public IMouseMacroCapable {
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
        // Legacy IMouseCapable convenience — delegates to the canonical
        // IPollingRateCapable path so the cache and wire byte stay in sync
        // and out-of-table values get clamped instead of silently falling
        // back to 1 KHz via pollRateToWireCode.
        (void)setPollingRateHz(hz);
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
        // (our prior buggy offset was bytes 4..7). Binds against the
        // currently-active onboard profile so the QML profile picker
        // (IProfileSelectCapable) and per-button rebinds stay coherent.
        auto const pkt = buildMouseSetKeyMatrix(m_activeProfile, button, action);
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

    // ---- IClockCapable (TFT basetta clock + DPI widget) -------------------
    //
    // The AJ-series wireless dock / mouse has a small TFT LCD. The vendor
    // app DOES NOT push the time as a structured opcode; it RENDERS a
    // bitmap of the current time + active DPI value locally and uploads
    // it via opcode 0x25 SETTFTLCDDATA chunked at 54 bytes RGB565 per
    // packet (`aj_series_opcode_table.md` §3.12; vendor doc line 326).
    //
    // The widget choice (clock / weather / CPU info / custom) lives in
    // the vendor's sled `screen` table (`aj_series_ui_action_map.md`
    // line 107). We default to the clock + DPI face; future UX can
    // expose a picker.
    //
    // Devices without a TFT LCD MCU will NAK opcode 0x25; firmware
    // silently drops the chunks. Behaviour gracefully degrades to a
    // no-op on those SKUs, which is honest for now (`devices.yaml`
    // currently lists 7 mouse codenames; only the 8K/AJ159/AJ199
    // families ship the panel).
    [[nodiscard]] TimeSyncResult setTime(std::chrono::system_clock::time_point tp) override {
        // Panel size: USB capture pending. 128x128 is the default — small
        // enough that mice with a smaller panel still get a usable face,
        // large enough that mice with a 240x240 panel get the upper-left
        // quadrant (firmware should letterbox). Override via
        // setTftPanelSize() once we have device introspection.
        QSize const panel = m_tftPanelSize;
        std::uint32_t activeDpi = 0;
        if (m_activeDpiStage < m_dpiStages.size()) {
            activeDpi = m_dpiStages[m_activeDpiStage].dpi;
        }
        QImage const face = renderClockDpiFace(panel, tp, activeDpi);
        if (face.isNull()) {
            AJAZZ_LOG_WARN("mouse.aj_series", "setTime: renderClockDpiFace returned null");
            return TimeSyncResult::IoError;
        }
        auto const chunks = encodeRgb565Chunks(face);
        if (chunks.empty()) {
            AJAZZ_LOG_WARN("mouse.aj_series", "setTime: encodeRgb565Chunks empty");
            return TimeSyncResult::IoError;
        }
        try {
            for (std::size_t i = 0; i < chunks.size(); ++i) {
                auto const pkt = aj_series::buildSetTftLcdData(
                    /*frame*/ 0,
                    /*frameCount*/ 1,
                    /*frameDelayMs*/ 0,
                    static_cast<std::uint16_t>(i),
                    chunks[i]);
                (void)m_transport->write(pkt);
            }
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("mouse.aj_series", "setTime: HID write failed: {}", e.what());
            return TimeSyncResult::IoError;
        }
        AJAZZ_LOG_INFO("mouse.aj_series",
                       "TFT face uploaded ({} chunks, panel {}x{}, dpi={})",
                       chunks.size(), panel.width(), panel.height(),
                       static_cast<unsigned>(activeDpi));
        return TimeSyncResult::Ok;
    }

    // ---- IPollingRateCapable -----------------------------------------------
    //
    // Vendor `_RateToNum` table per `aj_series_opcode_table.md` §3.4:
    //   125 -> 0x08, 250 -> 0x04, 500 -> 0x02, 1000 -> 0x01,
    //   2000 -> 0x84, 4000 -> 0x82, 8000 -> 0x81.
    // The high-bit-set encoding for >=2000 Hz means a naive sum checksum
    // would set bit 7; BIT7 masking (§5) clears it before transmission.
    //
    // No synchronous read-back from firmware — host caches the last pushed
    // value in @c m_pollRate (shared with the legacy IMouseCapable getter).
    [[nodiscard]] std::vector<std::uint16_t> supportedPollingRatesHz() const override {
        return {125, 250, 500, 1000, 2000, 4000, 8000};
    }

    bool setPollingRateHz(std::uint16_t hz) override {
        auto const supported = supportedPollingRatesHz();
        // Clamp to nearest supported rate; falls back to 1000 Hz default for
        // pathological inputs (matches pollRateToWireCode()'s unknown-input
        // behaviour so wire byte and cache stay in sync).
        std::uint16_t const clamped = clampToSupportedRate(hz, supported);
        auto const pkt = buildSetReportRate(m_activeProfile, clamped);
        try {
            (void)m_transport->write(pkt);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("mouse.aj_series",
                           "setPollingRateHz: HID write failed: {}", e.what());
            return false;
        }
        m_pollRate = clamped;
        return true;
    }

    [[nodiscard]] std::uint16_t pollingRateHz() const override { return m_pollRate; }

    // ---- IProfileSelectCapable ---------------------------------------------
    //
    // 8 onboard slots per `aj_series_opcode_table.md` §3.3 (FEA_CMD_SET_PROFILE
    // 0x05). Firmware persists the active slot across power-cycle. The wire
    // byte at pkt[2] is the profile index (0..7); the builder clamps.
    [[nodiscard]] std::uint8_t onboardProfileCount() const noexcept override { return 8; }

    bool setActiveOnboardProfile(std::uint8_t index) override {
        std::uint8_t const clamped = std::min<std::uint8_t>(
            index, static_cast<std::uint8_t>(onboardProfileCount() - 1));
        auto const pkt = buildSetProfile(clamped);
        try {
            (void)m_transport->write(pkt);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("mouse.aj_series",
                           "setActiveOnboardProfile: HID write failed: {}", e.what());
            return false;
        }
        m_activeProfile = clamped;
        return true;
    }

    [[nodiscard]] std::uint8_t activeOnboardProfile() const noexcept override {
        return m_activeProfile;
    }

    // ---- IMouseSettingsCapable (omnibus 0x53 — §3.9) ----------------------
    //
    // Vendor's "single save" omnibus packet. Maps the user-facing
    // MouseSettings struct (LOD enum, sleep timeouts, sensor flags,
    // battery LED RGB, charging-LED switch, per-axis sensitivity) onto the
    // §3.9 byte layout via aj_series::buildMouseSettings, clamping each
    // field to its documented valid range so the firmware always sees an
    // acceptable wire byte.
    //
    // The host-side cache (m_mouseSettings) is normalised post-clamp so
    // subsequent reads via mouseSettings() reflect what landed on the wire,
    // not the raw caller input. m_options is also refreshed so the next
    // setLiftOffDistanceMm / future per-field setter doesn't undo this push
    // by overwriting on its own cached snapshot.
    bool setMouseSettings(core::MouseSettings const& settings) override {
        auto const pkt = buildMouseSettings(m_activeProfile, m_pollRate, settings);
        try {
            (void)m_transport->write(pkt);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("mouse.aj_series",
                           "setMouseSettings: HID write failed: {}", e.what());
            return false;
        }
        // Normalise the cache post-clamp so mouseSettings() mirrors the
        // wire bytes the firmware actually saw.
        core::MouseSettings normalised = settings;
        constexpr std::uint8_t kDebounceMaxMs = 10;
        constexpr std::uint8_t kSensitivityMaxPercent = 100;
        normalised.debounceMs =
            std::min<std::uint8_t>(normalised.debounceMs, kDebounceMaxMs);
        normalised.xSensitivity =
            std::min<std::uint8_t>(normalised.xSensitivity, kSensitivityMaxPercent);
        normalised.ySensitivity =
            std::min<std::uint8_t>(normalised.ySensitivity, kSensitivityMaxPercent);
        std::uint8_t const lodRaw = static_cast<std::uint8_t>(normalised.liftOffDistance);
        normalised.liftOffDistance =
            static_cast<core::LiftOffDistance>(std::min<std::uint8_t>(lodRaw, 2));
        m_mouseSettings = normalised;

        // Mirror into m_options so granular per-field setters (e.g. the
        // legacy setLiftOffDistanceMm) stay consistent with the omnibus
        // push and don't clobber unrelated fields on their next re-emit.
        m_options.debounceMs = normalised.debounceMs;
        std::uint16_t flagsBits = 0;
        if (normalised.lightOff)         flagsBits |= 1u << 0;
        if (normalised.wheelLightOff)    flagsBits |= 1u << 1;
        if (normalised.motionSmoothing)  flagsBits |= 1u << 2;
        if (normalised.batteryLedSelect) flagsBits |= 1u << 3;
        if (normalised.powerSaveMode)    flagsBits |= 1u << 4;
        m_options.flags = flagsBits;
        m_options.sleepBtIdleSec = normalised.sleepBtIdleSec;
        m_options.sleepBtDeepSec = normalised.sleepBtDeepSec;
        m_options.sleep24gIdleSec = normalised.sleep24gIdleSec;
        m_options.sleep24gDeepSec = normalised.sleep24gDeepSec;
        m_options.xSensitivity = normalised.xSensitivity;
        m_options.ySensitivity = normalised.ySensitivity;
        m_options.liftCutOff = static_cast<std::uint8_t>(normalised.liftOffDistance);
        m_options.angleSnap = normalised.angleSnap ? 1 : 0;
        m_options.batteryColorHigh = {
            normalised.batteryLedHigh.r, normalised.batteryLedHigh.g, normalised.batteryLedHigh.b};
        m_options.batteryColorLow = {
            normalised.batteryLedLow.r, normalised.batteryLedLow.g, normalised.batteryLedLow.b};
        m_options.chargingSwitch = normalised.chargingSwitch ? 1 : 0;

        AJAZZ_LOG_INFO("mouse.aj_series",
                       "settings omnibus sent: lod={}, sleep_bt={}s, sleep_24g={}s, sens={}/{}",
                       static_cast<unsigned>(normalised.liftOffDistance),
                       static_cast<unsigned>(normalised.sleepBtIdleSec),
                       static_cast<unsigned>(normalised.sleep24gIdleSec),
                       static_cast<unsigned>(normalised.xSensitivity),
                       static_cast<unsigned>(normalised.ySensitivity));
        return true;
    }

    [[nodiscard]] core::MouseSettings mouseSettings() const override {
        return m_mouseSettings;
    }

    // ---- IFactoryResettable (§3.2 — FEA_CMD_SET_RESERT 0x02) --------------
    //
    // Destructive: firmware reverts every persisted setting (key bindings,
    // macros, DPI table, lighting, sleep timers, battery LED colours, …) to
    // vendor defaults. WARN-log every emission so the destructive action is
    // traceable in the journal — this method is NEVER auto-called by the
    // backend, only from a user-confirmed UI surface.
    //
    // Wire packet shape per aj_series_opcode_table.md §3.2:
    //   pkt[0] = 0x05 (HID Report ID)
    //   pkt[1] = 0x02 (FeaCmd::SetReset)
    //   pkt[2..63] = 0
    //   pkt[64] = BIT7 checksum (sum(pkt[1..63]) & 0x7F)
    bool factoryReset() override {
        AJAZZ_LOG_WARN("mouse.aj_series",
                       "factoryReset: emitting destructive opcode 0x02 — every "
                       "persisted setting will revert to vendor defaults");
        auto const pkt = buildFactoryReset();
        try {
            (void)m_transport->write(pkt);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("mouse.aj_series",
                           "factoryReset: HID write failed: {}", e.what());
            return false;
        }
        return true;
    }

    // ---- IDpiTableCapable (§3.10 — FEA_CMD_MOUSE_SET_OPTIONPARAM1 0x54) ----
    //
    // Per-profile DPI table push. Distinct from the legacy IMouseCapable
    // setDpiStages path, which implicitly targets profile 0 and re-emits
    // via buildMouseSetOption1 (no profile byte). This surface honours
    // the profile slot at vendor byte 1 via the new buildDpiTable builder.
    //
    // Cache normalisation: the host-side m_dpiTableCache is updated
    // post-clamp so dpiTable() reflects what landed on the wire (profile
    // 0..7, activeStage 0..7, stageCount 0..8). DPI values pass through
    // unclamped — the wire format is uint16-LE and callers know the
    // sensor's accepted bound (50..42000 on AJ159 APEX).
    bool setDpiTable(core::DpiTable const& table) override {
        auto const pkt = buildDpiTable(table);
        try {
            (void)m_transport->write(pkt);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("mouse.aj_series",
                           "setDpiTable: HID write failed: {}", e.what());
            return false;
        }
        // Normalise the cache post-clamp so dpiTable() mirrors the wire bytes.
        core::DpiTable normalised = table;
        normalised.profile = std::min<std::uint8_t>(normalised.profile, 7);
        normalised.activeStage = std::min<std::uint8_t>(normalised.activeStage, 7);
        normalised.stageCount = std::min<std::uint8_t>(normalised.stageCount, 8);
        m_dpiTableCache = normalised;
        AJAZZ_LOG_INFO("mouse.aj_series",
                       "DPI table pushed: profile={}, active={}, stages={}",
                       static_cast<unsigned>(normalised.profile),
                       static_cast<unsigned>(normalised.activeStage),
                       static_cast<unsigned>(normalised.stageCount));
        return true;
    }

    [[nodiscard]] core::DpiTable dpiTable() const override { return m_dpiTableCache; }

    // ---- IMouseFnRemappable (§3.8 — FEA_CMD_MOUSE_SET_FNMATRIX 0x51) -------
    //
    // Fn-layer key rebind. Same envelope shape as opcode 0x50 (primary
    // matrix) but the vendor byte 1 carries the Fn-layer index instead
    // of the profile slot, and addressing is (fnLayer, buttonIndex) per
    // §3.8. Mirrors IKeyRemappable's layered-keymap model but kept
    // mouse-specific because the address shape differs (no rows/cols).
    //
    // Firmware does not advertise a read-back path for the Fn-layer
    // matrix — write-only surface, no host-side cache beyond what
    // callers track themselves.
    [[nodiscard]] std::uint8_t fnLayerCount() const noexcept override {
        // Vendor AJ159 APEX firmware exposes a single Fn-layer (vendor byte
        // 1 is technically 0..7, but the UI only surfaces one Fn key). The
        // 0..7 range stays available via the clamp in buildFnLayerRemap for
        // future SKUs that may extend it.
        return 1;
    }

    [[nodiscard]] std::uint8_t fnButtonCount() const noexcept override {
        // 16-button max per §3.7 (full key-matrix read returns 16 × 4-byte
        // action records). AJ159 APEX exposes 7 physical buttons; the wider
        // 16-slot range stays accessible for future SKUs.
        return 16;
    }

    bool setFnLayerBinding(std::uint8_t fnLayer,
                           std::uint8_t buttonIndex,
                           std::uint32_t action) override {
        // Per-spec clamp: fnLayer at vendor byte 1 (same slot semantics as
        // profile on opcode 0x50), buttonIndex at vendor byte 2, action
        // big-endian at vendor bytes 8..11.
        auto const pkt = buildFnLayerRemap(fnLayer, buttonIndex, action);
        try {
            (void)m_transport->write(pkt);
        } catch (std::exception const& e) {
            AJAZZ_LOG_WARN("mouse.aj_series",
                           "setFnLayerBinding: HID write failed: {}", e.what());
            return false;
        }
        return true;
    }

    // ---- IMouseMacroCapable (§3.11 — FEA_CMD_SET_MACRO_SIMPLE 0x16) --------
    //
    // Chunked macro upload. Encodes the @ref MouseMacroEvent sequence to
    // the §3.11 256-byte payload format via aj_series::encodeMouseMacro,
    // then splits into 56-byte chunks per vendor pattern (line 487 — "For
    // each chunk c in 0..(chunkCount-1)") and emits one opcode 0x16 packet
    // per chunk through the transport. Slot clamps to 0..19.
    //
    // The lastNonZeroPos header byte is computed once over the FULL
    // encoded payload and stamped identically on every chunk so the
    // firmware sees a coherent "where the data truly ends" marker
    // regardless of chunk ordering on the wire. The isFinal flag is set
    // ONLY on the trailing chunk.
    //
    // Empty events still emit one header-only chunk with isFinal=true and
    // lastNonZeroPos=1 (covering the repeatCount uint16-LE bytes 0..1 the
    // encoder always emits). This advertises "macro cleared" intent to the
    // firmware rather than no-op'ing silently — important so a user-
    // confirmed "Clear macro" UI button actually lands a wire packet.
    [[nodiscard]] std::uint8_t macroSlotCount() const noexcept override {
        return aj_series::kMacroSlotCount;
    }

    bool uploadMacro(std::uint8_t slot,
                     std::vector<core::MouseMacroEvent> const& events) override {
        auto const encoded = aj_series::encodeMouseMacro(std::span<core::MouseMacroEvent const>(events));
        // Clamp the encoded payload to the §3.11 ceiling so over-budget
        // sequences don't run off the end of the 5-chunk wire envelope.
        std::size_t const payloadBytes =
            std::min(encoded.size(), aj_series::kMacroPayloadBytes);
        std::uint8_t const clampedSlot = std::min<std::uint8_t>(
            slot, static_cast<std::uint8_t>(aj_series::kMacroSlotCount - 1));

        // §3.11 line 491: lastNonZeroPos = 56*(u-1) + s where u = highest
        // non-empty chunk index (1-based) and s = position of last non-zero
        // byte within that chunk. We compute over the full encoded payload
        // (1-based byte position of the last non-zero byte). For an empty
        // payload the encoder still wrote the 2-byte repeatCount header
        // (0x01, 0x00), so payloadBytes >= 2 in practice and the last
        // non-zero byte is at position 1 (the 0x01 repeatCount low byte).
        std::uint8_t lastNonZeroPos = 0;
        for (std::size_t i = 0; i < payloadBytes; ++i) {
            if (encoded[i] != 0) {
                lastNonZeroPos = static_cast<std::uint8_t>(i);
            }
        }

        // Always emit at least one chunk so empty events still send a
        // header (firmware interprets as "macro cleared").
        std::size_t const chunkCount =
            std::max<std::size_t>(1, (payloadBytes + aj_series::kMacroChunkPayloadBytes - 1) /
                                          aj_series::kMacroChunkPayloadBytes);

        for (std::size_t chunkIdx = 0; chunkIdx < chunkCount; ++chunkIdx) {
            std::size_t const offset = chunkIdx * aj_series::kMacroChunkPayloadBytes;
            std::size_t const remaining = (offset < payloadBytes) ? (payloadBytes - offset) : 0;
            std::size_t const n = std::min(aj_series::kMacroChunkPayloadBytes, remaining);
            std::span<std::uint8_t const> const chunkPayload(encoded.data() + offset, n);
            bool const isFinal = (chunkIdx + 1 == chunkCount);
            auto const pkt = (chunkIdx == 0)
                ? aj_series::buildMacroHeader(clampedSlot, lastNonZeroPos, isFinal, chunkPayload)
                : aj_series::buildMacroChunk(clampedSlot,
                                              static_cast<std::uint8_t>(chunkIdx),
                                              lastNonZeroPos,
                                              isFinal,
                                              chunkPayload);
            try {
                (void)m_transport->write(pkt);
            } catch (std::exception const& e) {
                AJAZZ_LOG_WARN("mouse.aj_series",
                               "uploadMacro: HID write failed on chunk {}: {}",
                               chunkIdx, e.what());
                return false;
            }
        }
        AJAZZ_LOG_INFO("mouse.aj_series",
                       "macro uploaded: slot={}, chunks={}, payload_bytes={}",
                       static_cast<unsigned>(clampedSlot),
                       chunkCount,
                       payloadBytes);
        return true;
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

    /// Clamp an arbitrary Hz value to the nearest entry in @p supported.
    /// Used by setPollingRateHz so out-of-table values don't silently fall
    /// back to 1 KHz via pollRateToWireCode (which would desync the host
    /// cache from the wire byte).
    static std::uint16_t
    clampToSupportedRate(std::uint16_t hz, std::vector<std::uint16_t> const& supported) noexcept {
        if (supported.empty()) {
            return hz;
        }
        std::uint16_t best = supported.front();
        std::uint32_t bestDelta = (hz > best) ? (hz - best) : (best - hz);
        for (auto const candidate : supported) {
            std::uint32_t const delta = (hz > candidate) ? (hz - candidate) : (candidate - hz);
            if (delta < bestDelta) {
                best = candidate;
                bestDelta = delta;
            }
        }
        return best;
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

    /// Host-side cache of the last @ref core::MouseSettings push, normalised
    /// post-clamp. Initialised to the vendor defaults baked into the struct's
    /// in-class member initialisers (LOD=1mm, sleeps=0, flags off, sensitivity
    /// 100%, battery LED green/red, chargingSwitch=true) so the cache reader
    /// returns sensible values before any push lands. See §3.9.
    core::MouseSettings m_mouseSettings{};

    /// Host-side cache of the last @ref core::DpiTable push, normalised
    /// post-clamp. Default-constructed entries (profile=0, activeStage=0,
    /// stageCount=8, all stages zero) are returned before any push lands so
    /// the @ref dpiTable() reader stays well-defined. See §3.10.
    core::DpiTable m_dpiTableCache{};

    DeviceDescriptor m_descriptor;
    DeviceId m_id;
    TransportPtr m_transport;
    EventCallback m_callback;
    std::mutex m_mutex;
    std::uint16_t m_pollRate{1000};
    std::uint8_t m_activeDpiStage{0};
    /// Last-known active onboard profile (cached host-side; firmware does not
    /// expose a synchronous read-back path). 0..7 per
    /// `aj_series_opcode_table.md` §3.3.
    std::uint8_t m_activeProfile{kDefaultProfile};
    std::vector<DpiStage> m_dpiStages{dpiStageCount(),
                                      DpiStage{}}; ///< Host-side cache of the on-device DPI table.
    /// TFT basetta pixel resolution. Default 128x128 — small enough that
    /// mice with a smaller dock panel still get a usable face, and
    /// firmware on larger panels (e.g. 240x240 AJ159) typically
    /// letterboxes the upload to the panel centre. Setter follow-up will
    /// expose a per-device descriptor override once USB capture confirms
    /// each SKU's actual native resolution.
    QSize m_tftPanelSize{128, 128};
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
