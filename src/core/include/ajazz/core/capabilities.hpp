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
};

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

} // namespace ajazz::core
