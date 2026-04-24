// SPDX-License-Identifier: GPL-3.0-or-later
//
// Capability interfaces advertised by device backends.
//
// A backend implements IDevice plus any subset of the capability mix-ins
// below. The UI and Python SDK query capabilities via `dynamic_cast` or the
// `capabilities()` bitset to expose only what the device supports.
//
// Inspired by the Elgato Stream Deck SDK, OpenDeck's plugin API and the VIA
// protocol, but reimplemented in a clean-room fashion.
//
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
    PerKeyDisplay = 1u << 0,   ///< LCD/OLED behind keys (stream deck)
    MainDisplay = 1u << 1,     ///< primary touch strip / dial screen
    PerKeyRgb = 1u << 2,       ///< individually addressable RGB per key
    GlobalRgb = 1u << 3,       ///< single RGB zone
    RgbZones = 1u << 4,        ///< multiple named RGB zones
    Encoders = 1u << 5,        ///< endless rotary encoders (AKP05)
    EncoderPress = 1u << 6,    ///< encoders can be pressed
    TouchStrip = 1u << 7,      ///< horizontal touch strip (stream deck plus)
    Macros = 1u << 8,          ///< on-device macro playback
    KeyRemap = 1u << 9,        ///< keyboards with remappable keymap (VIA)
    DpiSwitch = 1u << 10,      ///< mice with runtime DPI presets
    PollRate = 1u << 11,       ///< configurable USB poll rate
    Battery = 1u << 12,        ///< wireless device, battery level available
    Firmware = 1u << 13,       ///< firmware version query + update
    PerAppProfiles = 1u << 14, ///< on-device per-application profiles
};

[[nodiscard]] constexpr std::uint32_t operator|(Capability a, Capability b) noexcept {
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}
[[nodiscard]] constexpr std::uint32_t operator&(Capability a, Capability b) noexcept {
    return static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b);
}

/// 24-bit RGB color (sRGB, no alpha).
struct Rgb {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};

    [[nodiscard]] constexpr bool operator==(Rgb const&) const noexcept = default;
};

// -----------------------------------------------------------------------------
// Display capability (per-key LCDs and main screens)
// -----------------------------------------------------------------------------
struct DisplayInfo {
    std::uint16_t widthPx{0};
    std::uint16_t heightPx{0};
    std::uint8_t keyRows{0};
    std::uint8_t keyCols{0};
    bool jpegEncoded{false}; ///< true on AKP153-family, false on Elgato PNG decks
};

/// Mix-in for devices with on-key or main displays.
class IDisplayCapable {
public:
    virtual ~IDisplayCapable() = default;

    [[nodiscard]] virtual DisplayInfo displayInfo() const noexcept = 0;

    /// Push an RGBA8 image to a key slot. Implementations are responsible
    /// for any resizing, color-space and encoding conversions required by
    /// the underlying protocol.
    virtual void setKeyImage(std::uint8_t keyIndex,
                             std::span<std::uint8_t const> rgba,
                             std::uint16_t width,
                             std::uint16_t height) = 0;

    /// Fill a key slot with a solid color. Useful for placeholders and tests.
    virtual void setKeyColor(std::uint8_t keyIndex, Rgb color) = 0;

    /// Clear a single key; 0xFF means "clear all".
    virtual void clearKey(std::uint8_t keyIndex) = 0;

    /// Push an image to the main/touch-strip display, if present.
    virtual void
    setMainImage(std::span<std::uint8_t const> rgba, std::uint16_t width, std::uint16_t height) = 0;

    /// Global display brightness, 0..100.
    virtual void setBrightness(std::uint8_t percent) = 0;

    /// Flush buffered display commands to the device.
    virtual void flush() = 0;
};

// -----------------------------------------------------------------------------
// RGB capability (keyboards, mice, deck backlights)
// -----------------------------------------------------------------------------
enum class RgbEffect : std::uint8_t {
    Static = 0,
    Breathing,
    Wave,
    ReactiveRipple,
    ColorCycle,
    Custom,
};

struct RgbZone {
    std::string name;
    std::uint16_t ledCount{0};
};

class IRgbCapable {
public:
    virtual ~IRgbCapable() = default;

    [[nodiscard]] virtual std::vector<RgbZone> rgbZones() const = 0;

    virtual void setRgbStatic(std::string_view zone, Rgb color) = 0;
    virtual void setRgbEffect(std::string_view zone, RgbEffect effect, std::uint8_t speed) = 0;

    /// Per-LED color buffer. Size must match the zone's `ledCount`.
    virtual void setRgbBuffer(std::string_view zone, std::span<Rgb const> colors) = 0;

    /// Global RGB brightness, 0..100.
    virtual void setRgbBrightness(std::uint8_t percent) = 0;
};

// -----------------------------------------------------------------------------
// Encoder / dial capability (stream deck plus / AKP05)
// -----------------------------------------------------------------------------
struct EncoderInfo {
    std::uint8_t count{0};
    bool pressable{false};
    bool hasScreens{false};              ///< tiny LCDs above encoders
    std::uint16_t stepsPerRevolution{0}; ///< 0 = endless/indeterminate
};

class IEncoderCapable {
public:
    virtual ~IEncoderCapable() = default;

    [[nodiscard]] virtual EncoderInfo encoderInfo() const noexcept = 0;

    /// Set the screen above an encoder (when `hasScreens`), RGBA8.
    virtual void setEncoderImage(std::uint8_t index,
                                 std::span<std::uint8_t const> rgba,
                                 std::uint16_t width,
                                 std::uint16_t height) = 0;
};

// -----------------------------------------------------------------------------
// Keyboard remapping / macros (VIA-style devices)
// -----------------------------------------------------------------------------
struct KeyboardLayout {
    std::uint8_t rows{0};
    std::uint8_t cols{0};
    std::uint8_t layers{0};
};

class IKeyRemappable {
public:
    virtual ~IKeyRemappable() = default;

    [[nodiscard]] virtual KeyboardLayout layout() const noexcept = 0;

    /// Assign an HID usage code to a (layer, row, col) key.
    virtual void
    setKeycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col, std::uint16_t keycode) = 0;

    /// Read the current keycode for a (layer, row, col) slot.
    [[nodiscard]] virtual std::uint16_t
    keycode(std::uint8_t layer, std::uint8_t row, std::uint8_t col) const = 0;

    /// Upload a macro identified by 0..N-1.
    virtual void setMacro(std::uint8_t slot, std::span<std::uint8_t const> bytes) = 0;

    /// Persist current keymap to device EEPROM.
    virtual void commit() = 0;
};

// -----------------------------------------------------------------------------
// Mouse capability (DPI, polling, buttons)
// -----------------------------------------------------------------------------
struct DpiStage {
    std::uint16_t dpi{0};
    Rgb indicator{};
};

class IMouseCapable {
public:
    virtual ~IMouseCapable() = default;

    [[nodiscard]] virtual std::uint8_t dpiStageCount() const noexcept = 0;
    virtual void setDpiStages(std::span<DpiStage const> stages) = 0;
    virtual void setActiveDpiStage(std::uint8_t index) = 0;

    virtual void setPollRateHz(std::uint16_t hz) = 0;
    [[nodiscard]] virtual std::uint16_t pollRateHz() const noexcept = 0;

    virtual void setLiftOffDistanceMm(float mm) = 0;

    /// Assign an HID usage code or macro id to a physical button.
    virtual void setButtonBinding(std::uint8_t button, std::uint32_t action) = 0;

    [[nodiscard]] virtual std::optional<std::uint8_t> batteryPercent() const = 0;
};

// -----------------------------------------------------------------------------
// Firmware
// -----------------------------------------------------------------------------
struct FirmwareInfo {
    std::string version;
    std::string buildDate;
    bool bootloaderAvailable{false};
};

class IFirmwareCapable {
public:
    virtual ~IFirmwareCapable() = default;

    [[nodiscard]] virtual FirmwareInfo firmwareInfo() const = 0;

    /// Initiate a firmware update with a verified image. Returns a progress
    /// token the UI can poll; throws if the device refuses the update.
    virtual std::uint32_t beginFirmwareUpdate(std::span<std::uint8_t const> image) = 0;

    [[nodiscard]] virtual std::uint8_t firmwareUpdateProgress(std::uint32_t token) const = 0;
};

} // namespace ajazz::core
