// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file capabilities.hpp
 * @brief Capability mix-in interfaces advertised by device backends.
 *
 * A backend implements IDevice plus any subset of the capability mix-ins
 * declared here. The UI and Python SDK query capabilities via `dynamic_cast`
 * or the `capabilities()` bitset returned by IDevice to expose only what the
 * physical device supports.
 *
 * Inspired by the Elgato Stream Deck SDK, OpenDeck's plugin API and the VIA
 * protocol, but reimplemented in a clean-room fashion.
 *
 * @see IDevice, DeviceRegistry
 */
#pragma once

#include "ajazz/core/device.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ajazz::core {

/// Bit-flags describing coarse-grained device features.
enum class Capability : std::uint32_t {
    None = 0,
    PerKeyDisplay = 1u << 0,   ///< LCD/OLED behind individual keys (stream deck family).
    MainDisplay = 1u << 1,     ///< Primary touch strip or dial screen.
    PerKeyRgb = 1u << 2,       ///< Individually addressable RGB per key.
    GlobalRgb = 1u << 3,       ///< Single monolithic RGB zone.
    RgbZones = 1u << 4,        ///< Multiple named RGB zones.
    Encoders = 1u << 5,        ///< Endless rotary encoders (AKP05).
    EncoderPress = 1u << 6,    ///< Encoders can also be pressed.
    TouchStrip = 1u << 7,      ///< Horizontal touch strip (Stream Dock Plus).
    Macros = 1u << 8,          ///< On-device macro playback.
    KeyRemap = 1u << 9,        ///< Keyboards with remappable keymap (VIA-compatible).
    DpiSwitch = 1u << 10,      ///< Mice with runtime DPI presets.
    PollRate = 1u << 11,       ///< Configurable USB poll rate.
    Battery = 1u << 12,        ///< Wireless device; battery level is available.
    Firmware = 1u << 13,       ///< Firmware version query and update supported.
    PerAppProfiles = 1u << 14, ///< On-device per-application profile switching.
    Clock = 1u << 15, ///< Host-settable RTC / clock surface (scaffolded — see IClockCapable).
};

// A-01 (Pitfall 13 lock): Capability::Clock bit value is load-bearing — the
// design doc, devices.yaml, and the static_assert here all pin it to 1u << 15.
// If a future contributor renumbers an earlier bit and shifts Clock to a
// different position, this static_assert turns the silent ABI break into a
// compile error. Capability bits MUST only be appended; never renumber.
static_assert(static_cast<unsigned>(Capability::Clock) == (1u << 15),
              "Capability::Clock must remain 1u << 15 — Pitfall 13: never renumber capability "
              "bits; only append.");

/**
 * @brief Bitwise OR of two Capability flags.
 *
 * @param a Left-hand capability flag.
 * @param b Right-hand capability flag.
 * @return Unsigned 32-bit bitmask with both flags set.
 */
[[nodiscard]] constexpr std::uint32_t operator|(Capability a, Capability b) noexcept {
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}
/**
 * @brief Bitwise AND of two Capability flags.
 *
 * @param a Left-hand capability flag.
 * @param b Right-hand capability flag.
 * @return Unsigned 32-bit bitmask; non-zero iff both flags share a set bit.
 */
[[nodiscard]] constexpr std::uint32_t operator&(Capability a, Capability b) noexcept {
    return static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b);
}

/**
 * @brief 24-bit sRGB color with no alpha channel.
 *
 * Used throughout the capability interfaces to describe key backlight colors
 * and DPI indicator LEDs. Channel order matches the on-wire layout expected
 * by most AJAZZ devices.
 */
struct Rgb {
    std::uint8_t r{0}; ///< Red channel, 0..255.
    std::uint8_t g{0}; ///< Green channel, 0..255.
    std::uint8_t b{0}; ///< Blue channel, 0..255.

    [[nodiscard]] constexpr bool operator==(Rgb const&) const noexcept = default;
};

// -----------------------------------------------------------------------------
// Display capability (per-key LCDs and main screens)
// -----------------------------------------------------------------------------
/**
 * @brief Geometry and encoding details for a device's display surface.
 *
 * Returned by IDisplayCapable::displayInfo(). Callers must pre-scale images
 * to `widthPx` × `heightPx` and encode them as JPEG (when `jpegEncoded`) or
 * PNG before handing them to setKeyImage().
 */
struct DisplayInfo {
    std::uint16_t widthPx{0};  ///< Key image width in pixels.
    std::uint16_t heightPx{0}; ///< Key image height in pixels.
    std::uint8_t keyRows{0};   ///< Number of key rows in the physical grid.
    std::uint8_t keyCols{0};   ///< Number of key columns in the physical grid.
    bool jpegEncoded{false};   ///< true on AKP153-family (JPEG), false on PNG-based decks.
};

/**
 * @brief Mix-in for devices that expose per-key LCDs or a main display.
 *
 * Implementations are responsible for any resizing, color-space conversion,
 * and codec encoding (JPEG or PNG) required by their underlying USB protocol.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 * @see IRgbCapable, IEncoderCapable
 */
class IDisplayCapable {
public:
    virtual ~IDisplayCapable() = default;

    /**
     * @brief Return the display geometry and image-format requirements.
     * @return Immutable DisplayInfo struct for this device model.
     */
    [[nodiscard]] virtual DisplayInfo displayInfo() const noexcept = 0;

    /**
     * @brief Push an RGBA8 image to a key slot.
     *
     * Implementations resize to the device's native key dimensions and encode
     * to the appropriate codec before transmitting. Callers may pass any
     * resolution; the backend normalises it.
     *
     * @param keyIndex Zero-based key index within the display grid.
     * @param rgba     Tightly packed RGBA8 pixels, length == width * height * 4.
     * @param width    Source image width in pixels.
     * @param height   Source image height in pixels.
     *
     * @throws std::system_error if the underlying transport fails.
     * @pre rgba.size() == width * height * 4u
     */
    virtual void setKeyImage(std::uint8_t keyIndex,
                             std::span<std::uint8_t const> rgba,
                             std::uint16_t width,
                             std::uint16_t height) = 0;

    /**
     * @brief Fill a key slot with a solid color.
     *
     * Useful for placeholders and tests. Backends may internally synthesise
     * a minimal JPEG/PNG of the requested color.
     *
     * @param keyIndex Zero-based key index.
     * @param color    Desired solid RGB fill color.
     */
    virtual void setKeyColor(std::uint8_t keyIndex, Rgb color) = 0;

    /**
     * @brief Clear a single key or all keys.
     *
     * @param keyIndex Zero-based key index; pass 0xFF to clear all keys.
     */
    virtual void clearKey(std::uint8_t keyIndex) = 0;

    /**
     * @brief Push an RGBA8 image to the main or touch-strip display.
     *
     * A no-op on devices that lack a main display (e.g. AKP153, AKP03).
     *
     * @param rgba   Tightly packed RGBA8 pixels.
     * @param width  Source image width in pixels.
     * @param height Source image height in pixels.
     */
    virtual void
    setMainImage(std::span<std::uint8_t const> rgba, std::uint16_t width, std::uint16_t height) = 0;

    /**
     * @brief Set global display brightness.
     * @param percent Brightness level, clamped to 0..100.
     */
    virtual void setBrightness(std::uint8_t percent) = 0;

    /**
     * @brief Flush buffered display commands to the hardware.
     *
     * On AKP-family devices this sends the "STP" command word that commits
     * all pending image transfers.
     */
    virtual void flush() = 0;
};

// -----------------------------------------------------------------------------
// RGB capability (keyboards, mice, deck backlights)
// -----------------------------------------------------------------------------
/// Supported predefined RGB lighting animations.
enum class RgbEffect : std::uint8_t {
    Static = 0,     ///< Solid color; no animation.
    Breathing,      ///< Smooth fade between two intensities.
    Wave,           ///< Sweeping color wave across the zone.
    ReactiveRipple, ///< Brief flash on each key-press.
    ColorCycle,     ///< Full-spectrum hue rotation.
    Custom,         ///< Per-LED colors set via setRgbBuffer().
};

/**
 * @brief Descriptor for a single addressable RGB zone.
 *
 * Zones are named by the hardware vendor (e.g. "underglow", "logo").
 * `ledCount` drives the expected buffer length in IRgbCapable::setRgbBuffer().
 */
struct RgbZone {
    std::string name;          ///< Vendor-assigned zone identifier.
    std::uint16_t ledCount{0}; ///< Number of individually addressable LEDs in the zone.
};

/**
 * @brief Mix-in for devices with addressable RGB lighting.
 *
 * Covers per-key RGB keyboards, single-zone mice, and any AKP deck that
 * exposes a backlight zone. Zones are enumerated at runtime via rgbZones().
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 * @see IDisplayCapable
 */
class IRgbCapable {
public:
    virtual ~IRgbCapable() = default;

    /**
     * @brief Enumerate the addressable RGB zones on this device.
     * @return Ordered list of zone descriptors; empty if no zones are present.
     */
    [[nodiscard]] virtual std::vector<RgbZone> rgbZones() const = 0;

    /**
     * @brief Set a zone to a static solid color.
     * @param zone  Zone identifier matching one returned by rgbZones().
     * @param color Desired sRGB color.
     */
    virtual void setRgbStatic(std::string_view zone, Rgb color) = 0;

    /**
     * @brief Activate a predefined animation on a zone.
     * @param zone   Zone identifier.
     * @param effect Desired animation type.
     * @param speed  Animation speed, 0 (slowest) .. 255 (fastest).
     */
    virtual void setRgbEffect(std::string_view zone, RgbEffect effect, std::uint8_t speed) = 0;

    /**
     * @brief Push a per-LED color buffer to a zone.
     *
     * @param zone   Zone identifier.
     * @param colors One Rgb entry per LED; size must equal the zone's `ledCount`.
     *
     * @pre colors.size() == zone.ledCount
     */
    virtual void setRgbBuffer(std::string_view zone, std::span<Rgb const> colors) = 0;

    /**
     * @brief Set global RGB brightness.
     * @param percent Brightness level, clamped to 0..100.
     */
    virtual void setRgbBrightness(std::uint8_t percent) = 0;
};

// -----------------------------------------------------------------------------
// Firmware-built-in lighting mode (AK980 PRO 20-effect picker, opcode 0x13)
// -----------------------------------------------------------------------------

/**
 * @struct FirmwareLightingMode
 * @brief Single entry in the firmware-built-in lighting picker.
 *
 * Distinct from the generic @ref RgbEffect enum: those are *categories*
 * (Static/Breathing/Wave/Cycle/...) that any RGB-capable device can map
 * to its own effects. `FirmwareLightingMode` enumerates the *exact*
 * vendor-defined firmware modes the device knows by id — e.g. the
 * AK980 PRO ships 20 of them addressable via opcode 0x13.
 */
struct FirmwareLightingMode {
    std::uint8_t id{0};   ///< Wire byte 1 of the DATA packet (vendor mode id).
    std::string name;     ///< Human label (caller-localised; defaults to vendor's English).
};

/**
 * @class IFirmwareLightingCapable
 * @brief Optional capability exposing a device's firmware-built-in
 *        lighting modes (as opposed to host-rendered effects).
 *
 * AK980 PRO implements this with its 20-mode 0x13 envelope; future
 * keyboards / mice that ship a fixed-effect catalogue should implement
 * it the same way so the QML lighting picker can list them without
 * special-casing per codename.
 *
 * The mode id semantics are device-defined; callers walk
 * @ref availableFirmwareModes() to discover the legal range.
 */
class IFirmwareLightingCapable {
public:
    virtual ~IFirmwareLightingCapable() = default;

    /// @return Ordered catalogue of firmware modes this device ships.
    [[nodiscard]] virtual std::vector<FirmwareLightingMode> availableFirmwareModes() const = 0;

    /**
     * @brief Activate a firmware-built-in lighting mode.
     *
     * @param modeId     Mode id from @ref availableFirmwareModes(); out-of-range
     *                   values are clamped to the LedOff sentinel by the backend.
     * @param brightness 0..max (device-defined; clamped to @c brightnessMax()).
     * @param speed      0..max (device-defined; clamped to @c speedMax()).
     * @return true if the wire packet went out; false on transport error.
     */
    virtual bool setFirmwareLightingMode(std::uint8_t modeId,
                                         std::uint8_t brightness,
                                         std::uint8_t speed) = 0;

    /// @return Maximum brightness level this firmware accepts (typically 5).
    [[nodiscard]] virtual std::uint8_t brightnessMax() const noexcept = 0;
    /// @return Maximum animation speed level (typically 5).
    [[nodiscard]] virtual std::uint8_t speedMax() const noexcept = 0;
};

// -----------------------------------------------------------------------------
// Polling-rate capability (mice / wireless dongles with multi-rate firmware)
// -----------------------------------------------------------------------------
/**
 * @class IPollingRateCapable
 * @brief Optional capability exposing a device's USB polling-rate picker.
 *
 * Distinct from the legacy @ref IMouseCapable::setPollRateHz convenience: this
 * is the canonical surface that the QML poll-rate picker queries, and it is
 * implementable by any device family with a multi-rate firmware menu (gaming
 * mice, wireless dongles, future game-pads, ...). Backends advertise the
 * device-supported Hz values rather than a fixed 125/500/1000 ladder; this
 * matters for AJ-series mice that ship a 125 / 250 / 500 / 1000 / 2000 /
 * 4000 / 8000 Hz menu where the 2/4/8 KHz entries use a high-bit-set wire
 * encoding (`0x80`-MSB) per
 * `docs/protocols/mouse/aj_series_opcode_table.md` §3.4.
 *
 * Firmware does not advertise a synchronous read-back path for the active
 * rate; backends keep a host-side cache of the last pushed value so callers
 * can display it without a round-trip.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class IPollingRateCapable {
public:
    virtual ~IPollingRateCapable() = default;

    /**
     * @brief Hz values this firmware accepts on the wire.
     *
     * Ordered ascending. Callers should treat the list as authoritative —
     * passing a value outside this set to @ref setPollingRateHz clamps to
     * the nearest supported entry rather than emitting a malformed packet.
     */
    [[nodiscard]] virtual std::vector<std::uint16_t> supportedPollingRatesHz() const = 0;

    /**
     * @brief Push a new polling rate to the device.
     *
     * @param hz Desired rate; values not in @ref supportedPollingRatesHz are
     *           clamped to the nearest supported entry.
     * @return true when the wire packet went out; false on transport error.
     */
    virtual bool setPollingRateHz(std::uint16_t hz) = 0;

    /**
     * @brief Last-known polling rate (no synchronous device read).
     *
     * Firmware does not advertise an authoritative read-back path; backends
     * cache the last pushed value. Returns the vendor default before any
     * push lands (typically 1000 Hz).
     */
    [[nodiscard]] virtual std::uint16_t pollingRateHz() const = 0;
};

// -----------------------------------------------------------------------------
// Onboard profile-select capability (devices with N firmware-resident slots)
// -----------------------------------------------------------------------------
/**
 * @class IProfileSelectCapable
 * @brief Optional capability exposing a device's onboard profile picker.
 *
 * Many AJAZZ peripherals store N independent configuration slots (DPI table,
 * key bindings, lighting) in flash and persist the active slot across power
 * cycles. This capability is the canonical surface for switching between
 * those slots — distinct from the host-side @c Profile JSON file the app
 * uses for its own preset library.
 *
 * AJ-series mice expose 8 onboard slots (`0..7`) selected via opcode
 * `FEA_CMD_SET_PROFILE` 0x05 per
 * `docs/protocols/mouse/aj_series_opcode_table.md` §3.3. Other future
 * families (keyboards with onboard layers, dock-mode encoders, ...) can
 * implement the same interface so the QML profile picker stays uniform.
 *
 * Firmware does not advertise a synchronous read-back path for the active
 * profile; backends keep a host-side cache of the last pushed value.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class IProfileSelectCapable {
public:
    virtual ~IProfileSelectCapable() = default;

    /// @return Number of onboard profile slots the firmware persists (typical 8).
    [[nodiscard]] virtual std::uint8_t onboardProfileCount() const noexcept = 0;

    /**
     * @brief Switch the active onboard profile slot.
     *
     * The new slot is persisted across power-cycle by the firmware.
     *
     * @param index Slot index in `[0, onboardProfileCount())`; out-of-range
     *              values are clamped to the last slot.
     * @return true when the wire packet went out; false on transport error.
     */
    virtual bool setActiveOnboardProfile(std::uint8_t index) = 0;

    /**
     * @brief Last-known active profile (no synchronous device read).
     *
     * Firmware does not advertise an authoritative read-back path; backends
     * cache the last pushed value. Returns 0 before any push lands (vendor
     * default: profile slot 0).
     */
    [[nodiscard]] virtual std::uint8_t activeOnboardProfile() const noexcept = 0;
};

/**
 * @struct KeyboardSettings
 * @brief Single-shot configuration carried by the AK-series settings
 *        batch envelope (opcode 0x07 sub 0x10).
 *
 * Three fields, all device-defined enums. Out-of-range values are clamped
 * by the backend; callers can pass the raw vendor scale directly without
 * range-checking themselves.
 *
 *   - @c fnLayerSwitch — Fn-layer behaviour (0 = hold-only, 1 = toggle).
 *   - @c sleepTimerMinutes — minutes of idle before the firmware halts
 *     the backlight controller. Vendor's UI exposes 0/1/3/5/10/30; 0 = never.
 *   - @c keyResponseTimeLevel — debounce / scan-rate level in [1..5];
 *     higher = faster (snappier) but more battery on wireless.
 */
struct KeyboardSettings {
    std::uint8_t fnLayerSwitch{0};       ///< 0=hold, 1=toggle (vendor enum).
    std::uint8_t sleepTimerMinutes{0};   ///< Minutes; 0 = never sleep.
    std::uint8_t keyResponseTimeLevel{3}; ///< 1..5; vendor default is 3.
};

/**
 * @class ISettingsCapable
 * @brief Optional capability exposing the AK-series "settings batch"
 *        wire-format (opcode 0x07 sub 0x10) — a single-shot commit of
 *        Fn-layer / sleep-timer / key-response-time.
 *
 * Mirrors the vendor utility's "Settings" tab. Three fields land in one
 * 33-byte short report with a 0xAA 0x55 trailer; the firmware persists
 * them to EEPROM so they survive power-cycle.
 *
 * Currently implemented by ProprietaryKeyboard (AK980 PRO and siblings
 * sharing the SN32F299 wire format, issue #57). Stream Deck / mouse
 * backends do not implement it — their settings live elsewhere.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class ISettingsCapable {
public:
    virtual ~ISettingsCapable() = default;

    /**
     * @brief Push a new @ref KeyboardSettings batch to the device.
     *
     * Builds the 33-byte settings packet (opcode 0x07 sub 0x10) and
     * sends it through the standard 5-packet envelope used by other
     * non-RTC commits (START 0x18 / DATA / SAVE 0x02 / FINISH 0xF0).
     *
     * @param settings Field values; out-of-range entries are clamped.
     * @return true when the wire packets all went out; false on
     *         transport error or unsupported field combination.
     */
    virtual bool setKeyboardSettings(KeyboardSettings const& settings) = 0;

    /**
     * @brief Last-known settings cache (no synchronous device read).
     *
     * Firmware does not advertise an authoritative read-back path for
     * the batch fields; backends keep a host-side cache of the last
     * pushed values so UI can display them without a round-trip.
     * Returns the vendor defaults (0 / 0 / 3) before any push lands.
     */
    [[nodiscard]] virtual KeyboardSettings keyboardSettings() const = 0;
};

// -----------------------------------------------------------------------------
// Boot-logo capability (firmware splash / boot-time image upload)
// -----------------------------------------------------------------------------
/**
 * @class IBootLogoCapable
 * @brief Optional capability exposing a device's "custom boot logo" upload op.
 *
 * Pushes an RGBA8 image that the firmware persists to flash and displays at
 * power-on (vendor "Custom boot logo / screensaver" feature). Distinct from
 * @ref IDisplayCapable::setMainImage which targets the live LCD strip and is
 * volatile across power-cycle. Currently implemented by @c Akp05Device via
 * the AKP05/Mirabox N4 "LOG" opcode per
 * @c docs/protocols/streamdeck/akp05_vendor.md §2 row 188.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class IBootLogoCapable {
public:
    virtual ~IBootLogoCapable() = default;

    /**
     * @brief Push an RGBA8 image to the device's boot-logo flash slot.
     *
     * Backends resize to the device's native boot-logo dimensions and encode
     * to the appropriate codec before transmitting. Callers may pass any
     * resolution; the backend normalises.
     *
     * @param rgba   Tightly packed RGBA8 pixels, length == width * height * 4.
     * @param width  Source image width in pixels (> 0).
     * @param height Source image height in pixels (> 0).
     *
     * @pre rgba.size() == width * height * 4u
     */
    virtual void setBootLogo(std::span<std::uint8_t const> rgba,
                             std::uint16_t width,
                             std::uint16_t height) = 0;
};

// -----------------------------------------------------------------------------
// Factory-reset capability (destructive — user-initiated only)
// -----------------------------------------------------------------------------
/**
 * @class IFactoryResettable
 * @brief Optional capability exposing a device's "Restore defaults" wire op.
 *
 * Destructive: the firmware reverts every persisted user setting (key bindings,
 * macros, DPI table, lighting, sleep timers, …) to the factory defaults.
 * Backends implementing this MUST only invoke the wire packet from a deliberate
 * user-initiated control surface (e.g. a confirmation-gated "Reset to factory
 * defaults" button in QML); never auto-call on open / close / hot-plug.
 *
 * Currently implemented by @c AjSeriesMouse via opcode @c FEA_CMD_SET_RESERT
 * 0x02 per @c docs/protocols/mouse/aj_series_opcode_table.md §3.2. The wire
 * packet carries no payload — opcode + BIT7 checksum only.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class IFactoryResettable {
public:
    virtual ~IFactoryResettable() = default;

    /**
     * @brief Push the factory-reset wire packet to the device.
     *
     * Destructive — every persisted setting reverts to vendor defaults on the
     * device. Caller is responsible for any UI confirmation prompt; backends
     * do NOT prompt internally. Backends log the action at WARN level so the
     * destructive emission is traceable in the journal even if the calling
     * UI path is unclear.
     *
     * @return true when the wire packet went out; false on transport error.
     */
    virtual bool factoryReset() = 0;
};

/**
 * @enum LiftOffDistance
 * @brief Vendor 3-step lift-off-distance enum carried by byte 52 of the
 *        AJ-series mouse omnibus settings packet (opcode 0x53).
 *
 * Maps to the wire byte exactly per
 * @c docs/protocols/mouse/aj_series_opcode_table.md §3.9 (byte 52 = "LOD:
 * 0 = 1 mm, 1 = 2 mm, 2 = 3 mm"). The doc lists three levels; there is no
 * "OFF" sentinel on this wire format — disabling LOD requires a separate
 * firmware command not exposed here.
 */
enum class LiftOffDistance : std::uint8_t {
    Mm1 = 0, ///< 1 mm — most aggressive (vendor default).
    Mm2 = 1, ///< 2 mm — middle setting.
    Mm3 = 2, ///< 3 mm — least aggressive.
};

/**
 * @struct MouseSettings
 * @brief Single-shot configuration carried by the AJ-series mouse omnibus
 *        settings envelope (opcode 0x53 — @c FEA_CMD_MOUSE_SET_OPTIONPARAM0).
 *
 * This is the canonical "everything in one packet" command per
 * @c docs/protocols/mouse/aj_series_opcode_table.md §3.9 — the vendor app's
 * single "save" button assembles all of these fields into one 64-byte HID
 * output report. The firmware persists the values to flash so they survive
 * power-cycle.
 *
 * Fields below are listed in vendor-byte order so the struct doubles as a
 * cheat-sheet for the wire format. Out-of-range inputs are clamped by the
 * builder to bytes the firmware accepts; the host-side cache is normalised
 * post-clamp so subsequent reads see what landed on the wire.
 *
 *   - @c debounceMs — debounce time in ms (0..10 typical; byte 10).
 *   - @c lightOff / @c wheelLightOff / @c motionSmoothing /
 *     @c batteryLedSelect / @c powerSaveMode — five flag bits packed into
 *     bytes 12..13 as uint16-LE per §3.9 (only the 5 named bits decoded).
 *   - @c sleepBtIdleSec / @c sleepBtDeepSec — BT idle and deep-sleep
 *     timeouts in seconds (bytes 40..43, both uint16-LE).
 *   - @c sleep24gIdleSec / @c sleep24gDeepSec — 2.4 GHz idle and deep
 *     timeouts (bytes 44..47).
 *   - @c xSensitivity / @c ySensitivity — per-axis sensitivity 0..100%
 *     (bytes 50..51; clamped to 100).
 *   - @c liftOffDistance — 3-step LOD enum (byte 52).
 *   - @c angleSnap — 0/1 flag (byte 53).
 *   - @c batteryLedHigh / @c batteryLedLow — RGB indicator colours for
 *     high-charge and low-charge states (bytes 54..56 and 57..59).
 *   - @c chargingSwitch — true keeps the indicator LED on while charging
 *     (byte 60; default true).
 *
 * Fields the §3.9 byte map lists but whose semantics are NOT decoded
 * (@c buttonChange byte 14, @c wheelToButton byte 15, @c buttonToWheel
 * byte 16) are emitted at vendor-default values inside the builder and are
 * NOT exposed on this struct — the prompt's "no guess" guidance applies.
 */
struct MouseSettings {
    std::uint8_t debounceMs{1};      ///< Byte 10 — debounce time (0..10 typical).

    bool lightOff{false};          ///< Bit 0 of bytes 12..13 — master LED off.
    bool wheelLightOff{false};     ///< Bit 1 — scroll-wheel LED off.
    bool motionSmoothing{false};   ///< Bit 2 — sensor motion smoothing.
    bool batteryLedSelect{false};  ///< Bit 3 — enable battery-state RGB indicator.
    bool powerSaveMode{false};     ///< Bit 4 — aggressive power saving.

    std::uint16_t sleepBtIdleSec{0};  ///< Bytes 40..41 — BT idle seconds.
    std::uint16_t sleepBtDeepSec{0};  ///< Bytes 42..43 — BT deep-sleep seconds.
    std::uint16_t sleep24gIdleSec{0}; ///< Bytes 44..45 — 2.4 GHz idle seconds.
    std::uint16_t sleep24gDeepSec{0}; ///< Bytes 46..47 — 2.4 GHz deep-sleep seconds.

    std::uint8_t xSensitivity{100}; ///< Byte 50 — X-axis sensitivity (clamped 0..100%).
    std::uint8_t ySensitivity{100}; ///< Byte 51 — Y-axis sensitivity (clamped 0..100%).

    LiftOffDistance liftOffDistance{LiftOffDistance::Mm1}; ///< Byte 52 — LOD enum.
    bool angleSnap{false};                                 ///< Byte 53 — angle-snap on/off.

    Rgb batteryLedHigh{0, 0xff, 0}; ///< Bytes 54..56 — high-charge indicator RGB (vendor default green).
    Rgb batteryLedLow{0xff, 0, 0};  ///< Bytes 57..59 — low-charge indicator RGB (vendor default red).

    bool chargingSwitch{true}; ///< Byte 60 — keep LED lit while charging.
};

/**
 * @class IMouseSettingsCapable
 * @brief Optional capability exposing the AJ-series mouse omnibus settings
 *        envelope (opcode 0x53 — @c FEA_CMD_MOUSE_SET_OPTIONPARAM0).
 *
 * Mirrors the vendor utility's single "save" button: lift-off distance,
 * sleep timeouts (BT idle/deep + 2.4 GHz idle/deep), per-axis sensitivity,
 * the five documented sensor / LED flags, the battery-LED RGB indicator
 * colours, and the charging-LED master switch all commit in one 64-byte
 * HID output report. Firmware persists the values to flash.
 *
 * Currently implemented by @c AjSeriesMouse (AJ159 APEX and siblings
 * sharing the AJ-series wire format). Stream Deck / keyboard backends
 * do not implement it — their settings live elsewhere
 * (e.g. @ref ISettingsCapable for AK980 PRO).
 *
 * Firmware does not advertise a synchronous read-back path for these
 * fields; the vendor app caches the host side too, so backends mirror
 * that pattern via @ref mouseSettings() returning the last pushed values.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 * @see docs/protocols/mouse/aj_series_opcode_table.md §3.9
 */
class IMouseSettingsCapable {
public:
    virtual ~IMouseSettingsCapable() = default;

    /**
     * @brief Push a new @ref MouseSettings omnibus packet to the device.
     *
     * Builds the 65-byte HID output report carrying opcode 0x53 with all
     * fields packed into bytes 8..60 per §3.9, stamped with the BIT7
     * checksum at byte 64. Out-of-range fields are clamped to bytes the
     * firmware accepts; the host-side cache is updated post-clamp so the
     * @ref mouseSettings() getter mirrors what landed on the wire.
     *
     * @param settings Field values; out-of-range entries are clamped.
     * @return true when the wire packet went out; false on transport error.
     */
    virtual bool setMouseSettings(MouseSettings const& settings) = 0;

    /**
     * @brief Last-known settings cache (no synchronous device read).
     *
     * Firmware does not advertise an authoritative read-back path for the
     * omnibus fields; backends keep a host-side cache of the last pushed
     * (and clamp-normalised) values. Returns the vendor defaults before
     * any push lands.
     */
    [[nodiscard]] virtual MouseSettings mouseSettings() const = 0;
};

// -----------------------------------------------------------------------------
// Encoder / dial capability (stream deck plus / AKP05)
// -----------------------------------------------------------------------------
/**
 * @brief Descriptor for a device's rotary encoder bank.
 *
 * Returned by IEncoderCapable::encoderInfo(). Used to decide which
 * EventBus events to subscribe to and whether encoder LCD slots exist.
 */
struct EncoderInfo {
    std::uint8_t count{0};               ///< Number of physical rotary encoders.
    bool pressable{false};               ///< Encoders can be depressed like buttons.
    bool hasScreens{false};              ///< Tiny LCDs above each encoder (AKP05).
    std::uint16_t stepsPerRevolution{0}; ///< Detent count per full turn; 0 = endless.
};

/**
 * @brief Mix-in for devices with rotary encoders.
 *
 * The AKP05 / AKP05E exposes four endless encoders each with a 100×100
 * JPEG LCD above them. Encoder events arrive via the standard EventBus
 * as DeviceEvent::Kind::EncoderTurned and EncoderPressed.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 * @see IDisplayCapable
 */
class IEncoderCapable {
public:
    virtual ~IEncoderCapable() = default;

    /**
     * @brief Return the encoder bank geometry and feature flags.
     * @return Immutable EncoderInfo for this device model.
     */
    [[nodiscard]] virtual EncoderInfo encoderInfo() const noexcept = 0;

    /**
     * @brief Push an RGBA8 image to an encoder's LCD screen.
     *
     * A no-op when EncoderInfo::hasScreens is false (e.g. AKP03).
     *
     * @param index  Zero-based encoder index, 0 .. count-1.
     * @param rgba   Tightly packed RGBA8 pixels.
     * @param width  Source image width in pixels.
     * @param height Source image height in pixels.
     */
    virtual void setEncoderImage(std::uint8_t index,
                                 std::span<std::uint8_t const> rgba,
                                 std::uint16_t width,
                                 std::uint16_t height) = 0;
};

// -----------------------------------------------------------------------------
// Keyboard remapping / macros (VIA-style devices)
// -----------------------------------------------------------------------------
/**
 * @brief Physical key matrix dimensions and layer count for a remappable keyboard.
 *
 * Returned by IKeyRemappable::layout(). The total number of programmable
 * slots is rows × cols × layers.
 */
struct KeyboardLayout {
    std::uint8_t rows{0};   ///< Number of rows in the key matrix.
    std::uint8_t cols{0};   ///< Number of columns in the key matrix.
    std::uint8_t layers{0}; ///< Number of independent keymap layers.
};

/**
 * @brief Mix-in for keyboards that support VIA-style runtime key remapping.
 *
 * Callers write HID usage codes into (layer, row, col) slots and call
 * commit() to persist the keymap to the device's non-volatile storage.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class IKeyRemappable {
public:
    virtual ~IKeyRemappable() = default;

    /**
     * @brief Return the key matrix geometry.
     * @return Immutable KeyboardLayout for this device model.
     */
    [[nodiscard]] virtual KeyboardLayout layout() const noexcept = 0;

    /**
     * @brief Assign an HID usage code to a (layer, row, col) position.
     *
     * @param layer   Zero-based layer index, 0 .. layout().layers-1.
     * @param row     Zero-based row index.
     * @param col     Zero-based column index.
     * @param keycode USB HID usage code to assign.
     */
    virtual void
    setKeycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col, std::uint16_t keycode) = 0;

    /**
     * @brief Read the current HID usage code at a (layer, row, col) position.
     *
     * @param layer Zero-based layer index.
     * @param row   Zero-based row index.
     * @param col   Zero-based column index.
     * @return USB HID usage code currently assigned to that slot.
     */
    [[nodiscard]] virtual std::uint16_t
    keycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col) const = 0;

    /**
     * @brief Upload a macro sequence to an on-device slot.
     *
     * @param slot  Zero-based macro slot index.
     * @param bytes Raw macro byte sequence in device-native encoding.
     */
    virtual void setMacro(std::uint8_t slot, std::span<std::uint8_t const> bytes) = 0;

    /**
     * @brief Persist the current keymap to device non-volatile memory.
     *
     * Must be called after one or more setKeycode() calls for changes to
     * survive a power cycle.
     */
    virtual void commit() = 0;
};

// -----------------------------------------------------------------------------
// Mouse capability (DPI, polling, buttons)
// -----------------------------------------------------------------------------
/**
 * @brief One entry in a multi-stage DPI preset table.
 *
 * The `indicator` color is applied to the DPI LED when this stage is active,
 * giving the user visual feedback without requiring a host-side lookup.
 */
struct DpiStage {
    std::uint16_t dpi{0}; ///< Sensor resolution in counts-per-inch.
    Rgb indicator{};      ///< LED color shown while this stage is active.
};

/**
 * @brief Mix-in for mice with configurable DPI, poll rate, and button bindings.
 *
 * DPI stages are indexed 0 .. dpiStageCount()-1 and map directly to the
 * on-device preset table. The active stage is reported back via the hardware
 * DPI button; hosts can also set it programmatically with setActiveDpiStage().
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class IMouseCapable {
public:
    virtual ~IMouseCapable() = default;

    /// Return the number of DPI presets supported by this device.
    [[nodiscard]] virtual std::uint8_t dpiStageCount() const noexcept = 0;

    /**
     * @brief Upload the full DPI preset table.
     * @param stages Ordered DPI entries; size must equal dpiStageCount().
     * @pre stages.size() == dpiStageCount()
     */
    virtual void setDpiStages(std::span<DpiStage const> stages) = 0;

    /**
     * @brief Update a single DPI stage in the host-side cache and re-upload.
     *
     * Used by the UI when the user edits one row of the DPI table without
     * touching the others. Implementations should treat this as the
     * canonical, granular variant of @ref setDpiStages.
     *
     * @param index Zero-based stage index, 0 .. dpiStageCount()-1.
     * @param stage New DPI value + indicator color for this stage.
     */
    virtual void setDpiStage(std::uint8_t index, DpiStage stage) {
        // Default fallback: if the back-end has not overridden this method,
        // surface a single-element vector through setDpiStages so existing
        // implementations remain functional. Sub-classes are encouraged to
        // override for efficient single-stage uploads.
        std::array<DpiStage, 1> single{stage};
        (void)index;
        setDpiStages(single);
    }

    /**
     * @brief Return the host-side cached DPI preset table.
     *
     * The cache is populated by @ref setDpiStages / @ref setDpiStage.
     * Hardware does not currently echo the preset table back, so this is
     * the authoritative source for the UI's DPI editor.
     */
    [[nodiscard]] virtual std::vector<DpiStage> getDpiStages() const = 0;

    /**
     * @brief Switch the active DPI stage immediately.
     * @param index Zero-based stage index, 0 .. dpiStageCount()-1.
     */
    virtual void setActiveDpiStage(std::uint8_t index) = 0;

    /**
     * @brief Set the USB poll rate.
     * @param hz Desired poll rate in Hertz (typical values: 125, 500, 1000).
     */
    virtual void setPollRateHz(std::uint16_t hz) = 0;

    /// Return the current configured poll rate in Hertz.
    [[nodiscard]] virtual std::uint16_t pollRateHz() const noexcept = 0;

    /**
     * @brief Configure the lift-off distance threshold.
     * @param mm Distance in millimetres at which tracking ceases when lifted.
     */
    virtual void setLiftOffDistanceMm(float mm) = 0;

    /**
     * @brief Assign an HID usage code or macro id to a physical button.
     *
     * @param button Zero-based physical button index.
     * @param action USB HID usage code or a device-specific macro identifier.
     */
    virtual void setButtonBinding(std::uint8_t button, std::uint32_t action) = 0;

    /**
     * @brief Return the wireless battery level, if known.
     * @return Battery percentage (0..100), or std::nullopt for wired devices.
     */
    [[nodiscard]] virtual std::optional<std::uint8_t> batteryPercent() const = 0;
};

// -----------------------------------------------------------------------------
// DPI table per-profile capability (AJ-series mouse opcode 0x54)
// -----------------------------------------------------------------------------
/**
 * @struct DpiTable
 * @brief Per-profile 8-stage DPI preset table for AJ-series mice.
 *
 * Mirrors the §3.10 wire envelope of @c FEA_CMD_MOUSE_SET_OPTIONPARAM1 (opcode
 * 0x54) per @c docs/protocols/mouse/aj_series_opcode_table.md §3.10. The
 * vendor firmware stores one such table per onboard profile slot; switching
 * profile (@ref IProfileSelectCapable) brings up a different DPI ladder.
 *
 * Layout (host-side, byte map per §3.10 lines 442..452):
 *   - @c profile      — onboard profile slot the table targets (vendor byte 1).
 *   - @c activeStage  — currently-active stage index 0..7 (vendor byte 2).
 *   - @c stageCount   — number of enabled stages 1..8 (vendor byte 3).
 *   - @c stages       — 8 DPI presets; each carries a uint16 DPI value (sent
 *                       at vendor bytes 8..23 as uint16-LE) and an RGB indicator
 *                       colour (sent at vendor bytes 40..63 as 8 × {R,G,B}).
 *
 * @note Per §3.10 line 452, vendor byte 63 (the BIT7 checksum slot) collides
 *       with the 8th stage's B-channel — UI editors must surface this
 *       limitation (8th stage colour swatch greyed out) or the firmware will
 *       see a checksum byte instead of the user's blue value.
 *
 * Reuses the @ref DpiStage struct already declared above (dpi + indicator)
 * rather than introducing a sibling type so the host-side cache stays
 * coherent with @ref IMouseCapable::getDpiStages().
 */
struct DpiTable {
    std::uint8_t profile{0};     ///< Target onboard profile slot (0..7).
    std::uint8_t activeStage{0}; ///< Currently-active stage index (0..7).
    std::uint8_t stageCount{8};  ///< Number of enabled stages (1..8).
    std::array<DpiStage, 8> stages{}; ///< 8 DPI presets (value + indicator RGB).
};

/**
 * @class IDpiTableCapable
 * @brief Optional capability exposing per-profile 8-stage DPI tables.
 *
 * Distinct from the legacy @ref IMouseCapable DPI surface (which targets
 * whichever profile is currently active and silently uses profile 0 as the
 * implicit slot): this capability lets callers atomically upload the full
 * @ref DpiTable to an arbitrary profile slot, mirroring the vendor utility's
 * per-profile DPI editor.
 *
 * Currently implemented by @c AjSeriesMouse via @c
 * ajazz::mouse::aj_series::buildDpiTable, which emits opcode 0x54 with the
 * profile byte at vendor byte 1, active stage at vendor byte 2, stage count
 * at vendor byte 3, 8 × uint16-LE DPI values at vendor bytes 8..23, and 8 ×
 * {R,G,B} indicator colours at vendor bytes 40..63.
 *
 * Firmware does not advertise a synchronous read-back path; backends keep a
 * host-side cache of the last pushed table per profile.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 * @see docs/protocols/mouse/aj_series_opcode_table.md §3.10
 */
class IDpiTableCapable {
public:
    virtual ~IDpiTableCapable() = default;

    /**
     * @brief Push a full DPI table to the device for the supplied profile slot.
     *
     * @param table Field values; @c activeStage clamped to 0..7,
     *              @c stageCount clamped to 1..8, @c profile clamped to 0..7.
     *              DPI values outside the firmware's accepted 50..42000 range
     *              are passed through unclamped (the wire format is uint16-LE,
     *              so callers are responsible for sensor-specific bounds).
     * @return true when the wire packet went out; false on transport error.
     */
    virtual bool setDpiTable(DpiTable const& table) = 0;

    /**
     * @brief Last-known DPI table cache (no synchronous device read).
     *
     * Firmware does not advertise an authoritative read-back path for the
     * DPI table; backends keep a host-side cache of the last pushed value
     * normalised post-clamp.
     */
    [[nodiscard]] virtual DpiTable dpiTable() const = 0;
};

// -----------------------------------------------------------------------------
// Mouse Fn-layer rebind capability (AJ-series opcode 0x51)
// -----------------------------------------------------------------------------
/**
 * @class IMouseFnRemappable
 * @brief Optional capability exposing the AJ-series mouse Fn-layer key
 *        rebinder (opcode 0x51 — @c FEA_CMD_MOUSE_SET_FNMATRIX).
 *
 * Distinct from @ref IMouseCapable::setButtonBinding (opcode 0x50) which
 * targets the primary key matrix: this surface writes into the Fn-layer
 * key matrix, addressed by a layer index (vendor byte 1) and a button index
 * (vendor byte 2). The 4-byte action descriptor lands at vendor bytes 8..11
 * with identical semantics to the primary layer per §3.6 (`type`/`subtype`/
 * `keyA`/`keyB`).
 *
 * Mirrors the keyboard's @ref IKeyRemappable layered-key-matrix model but
 * stays mouse-specific because the address shape differs: keyboards use
 * (layer, row, col, keycode); mouse Fn-layer uses (layer, button, action).
 *
 * Firmware does not advertise a synchronous read-back path for the Fn-layer
 * key matrix; the host should treat the binding the same way the primary
 * matrix is treated (write-only, callers track desired state themselves).
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 * @see docs/protocols/mouse/aj_series_opcode_table.md §3.8
 */
class IMouseFnRemappable {
public:
    virtual ~IMouseFnRemappable() = default;

    /// @return Number of Fn-layer slots the firmware supports (typical 1..3).
    [[nodiscard]] virtual std::uint8_t fnLayerCount() const noexcept = 0;

    /// @return Number of physical mouse buttons rebindable on each Fn-layer.
    [[nodiscard]] virtual std::uint8_t fnButtonCount() const noexcept = 0;

    /**
     * @brief Rebind a button on the Fn-layer to a 4-byte action descriptor.
     *
     * @param fnLayer    Fn-layer index; clamped to 0..@ref fnLayerCount()-1.
     * @param buttonIndex Physical button index; clamped to 0..@ref fnButtonCount()-1.
     * @param action     4-byte action descriptor (big-endian on the wire) per
     *                   §3.6 — byte 0 = action type, bytes 1..3 = type-specific.
     * @return true when the wire packet went out; false on transport error.
     */
    virtual bool setFnLayerBinding(std::uint8_t fnLayer,
                                   std::uint8_t buttonIndex,
                                   std::uint32_t action) = 0;
};

// -----------------------------------------------------------------------------
// Firmware
// -----------------------------------------------------------------------------
/**
 * @brief Firmware identification returned by IFirmwareCapable::firmwareInfo().
 */
struct FirmwareInfo {
    std::string version;             ///< Human-readable version string (e.g. "1.2.3").
    std::string buildDate;           ///< ISO-8601 build date or empty if unavailable.
    bool bootloaderAvailable{false}; ///< True if the device exposes a DFU bootloader.
};

/**
 * @brief Mix-in for devices that support firmware version query and update.
 *
 * The update flow is: call beginFirmwareUpdate() with a verified firmware
 * image blob, then poll firmwareUpdateProgress() until it returns 100.
 * The device typically reboots into its application firmware afterwards.
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 */
class IFirmwareCapable {
public:
    virtual ~IFirmwareCapable() = default;

    /**
     * @brief Query the current firmware version and build metadata.
     * @return FirmwareInfo populated from the device response.
     */
    [[nodiscard]] virtual FirmwareInfo firmwareInfo() const = 0;

    /**
     * @brief Initiate a firmware update with a verified image blob.
     *
     * The device must already be open. The image must be a valid signed
     * firmware binary for this device family.
     *
     * @param image Raw firmware image bytes.
     * @return Opaque progress token; pass to firmwareUpdateProgress().
     *
     * @throws std::runtime_error if the device refuses the update or the
     *         transport fails.
     */
    virtual std::uint32_t beginFirmwareUpdate(std::span<std::uint8_t const> image) = 0;

    /**
     * @brief Poll the progress of a firmware update.
     *
     * @param token Progress token returned by beginFirmwareUpdate().
     * @return Completion percentage, 0..100. Returns 0 when the token is
     *         invalid or the update has not started.
     */
    [[nodiscard]] virtual std::uint8_t firmwareUpdateProgress(std::uint32_t token) const = 0;
};

// -----------------------------------------------------------------------------
// Battery capability (wireless devices — host queries charge level)
// -----------------------------------------------------------------------------
/**
 * @brief Mix-in for wireless devices that expose a host-readable battery level.
 *
 * Distinct from @ref IMouseCapable::batteryPercent() which is a legacy
 * convenience on the mouse mix-in; this interface is the canonical surface
 * for any device family that has a host-pollable charge level (keyboards
 * with 2.4 GHz / BT, wireless mice via dongle, etc.).
 *
 * Polling cadence is the caller's responsibility — typical UI services
 * poll every 15 s (matches the AK980 PRO vendor app cadence per its RE
 * @c FUN_00435250 200 ms loop sampled at 15 s tick boundaries).
 *
 * @note Thread-affine: must be called from the device's I/O thread.
 * @see Capability::Battery
 */
class IBatteryCapable {
public:
    virtual ~IBatteryCapable() = default;

    /**
     * @brief Query the current battery charge level.
     *
     * @return Percentage 0..100, or @c std::nullopt if the device is wired /
     *         the battery sub-system is unavailable / the read timed out.
     *
     * @throws std::system_error if the underlying HID transport fails (distinct
     *         from a clean "no battery" reply which returns @c std::nullopt).
     */
    [[nodiscard]] virtual std::optional<std::uint8_t> batteryPercent() = 0;
};

// -----------------------------------------------------------------------------
// Time-sync capability (host → device clock push)
// -----------------------------------------------------------------------------
/**
 * @brief Outcome of a single @ref IClockCapable::setTime call.
 *
 * Tri-state because backend support is staged: as of 2026-05-13 no AJAZZ
 * device firmware exposes a host-settable RTC over HID (vendor recon
 * commit `d5616ef` confirmed the Stream Dock SDK has no time events; the
 * AKB980 keyboard driver is locally archived as a Delphi installer that
 * still requires extraction). Backends therefore return @c NotImplemented
 * today and the application surface stays uniform across families. When a
 * real wire format lands, only the backend body changes.
 */
enum class TimeSyncResult : std::uint8_t {
    Ok = 0,             ///< Device acknowledged the new time.
    NotImplemented = 1, ///< Backend has no wire format for setTime yet.
    IoError = 2,        ///< HID write failed or the device timed out on the ack.
};

/**
 * @brief Mix-in for devices that expose a host-settable RTC / clock.
 *
 * Pushes an absolute UTC moment-in-time to the device. Each implementation
 * is responsible for converting to whatever encoding the firmware expects
 * (BCD, Unix epoch, vendor-specific frame, …) — the interface stays
 * representation-agnostic.
 *
 * @note Thread-affine: must be called from the device's I/O thread (the
 *       same thread that owns the @c hidapi handle).
 *
 * @see TimeSyncResult, Capability::Clock
 */
class IClockCapable {
public:
    virtual ~IClockCapable() = default;

    /**
     * @brief Set the device clock to the supplied UTC time-point.
     * @param tp Absolute UTC time-point. Backends translate to local /
     *           BCD / firmware-specific representation as needed.
     * @return @c TimeSyncResult::Ok on confirmed acknowledgement,
     *         @c NotImplemented while no wire format is wired up,
     *         @c IoError on HID write failure or ack timeout.
     */
    [[nodiscard]] virtual TimeSyncResult setTime(std::chrono::system_clock::time_point tp) = 0;
};

} // namespace ajazz::core
