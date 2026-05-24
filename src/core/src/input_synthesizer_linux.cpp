// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file input_synthesizer_linux.cpp
 * @brief Linux uinput @ref ajazz::core::IInputSynthesizer backend.
 *
 * Gated by both @c __linux__ and @c AJAZZ_FEATURE_INPUT_SYNTH. When either
 * guard is absent the entire TU is empty, contributing nothing to the build
 * (self-emptying TU pattern — mirrors macro_recorder gated-TU structure).
 *
 * Uses @c /dev/uinput (linux/uinput.h + plain syscalls) — NOT libXtst/XTest.
 * This is the Wayland-compatible choice (uinput is kernel-native; XTest is
 * X11-only and rejected per CLAUDE.md Pitfall 3 / Alternatives).
 *
 * On @c /dev/uinput open EACCES (root-only by default without a udev rule),
 * the backend logs a WARN and returns @c false from OUTPUT methods without
 * crashing. Real permissions are a Phase-25 operator concern (the
 * @c 70-ajazz.rules uaccess story). No udev rule is written from tooling
 * (CLAUDE.md hard rule: no system-level mutations).
 *
 * The @c captureHotkeys surface (opt-in evdev/libinput grab) returns @c false
 * unless @c enable==true AND a grab is wired. For Phase 21-01 the real grab
 * implementation is a documented TODO behind the same enable flag; OUTPUT
 * synthesis is the primary path. Capture is the gated, deferrable surface.
 */

#if defined(__linux__) && defined(AJAZZ_FEATURE_INPUT_SYNTH)

#include "ajazz/core/input_synthesizer.hpp"
#include "ajazz/core/logger.hpp"

#include <cerrno>
#include <cstring>
#include <memory>
#include <string>

#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <unistd.h>

namespace ajazz::core {

namespace {

/// Emit a single input_event + EV_SYN to the uinput fd.
static bool emitEvent(int fd, std::uint16_t type, std::uint16_t code, std::int32_t value) {
    struct input_event ev {};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if (::write(fd, &ev, sizeof(ev)) != static_cast<ssize_t>(sizeof(ev))) {
        return false;
    }
    struct input_event syn {};
    syn.type = EV_SYN;
    syn.code = SYN_REPORT;
    syn.value = 0;
    return ::write(fd, &syn, sizeof(syn)) == static_cast<ssize_t>(sizeof(syn));
}

/// Map a USB HID Usage ID (0x00070000 page, keyboard/keypad) to a Linux key code.
/// Returns 0 for unknown usages (caller skips emission).
static std::uint16_t hidUsageToLinuxKey(std::uint32_t usage) {
    // USB HID page 0x0007 (Keyboard/Keypad) — map the low byte to a KEY_* constant.
    // This is a minimal subset covering the most common keys.
    // A full map is available in <linux/input-event-codes.h> and the HID Usage Tables.
    const std::uint32_t page = usage >> 16U;
    const std::uint32_t id = usage & 0xFFFFU;
    if (page != 0x0007U) {
        return 0; // Non-keyboard page; synthesiser does not support it yet.
    }
    if (id >= 4U && id <= 29U) {
        // a-z: USB HID 0x04 (a) .. 0x1D (z) -> KEY_A(30) .. KEY_Z(55)
        return static_cast<std::uint16_t>(KEY_A + (id - 4U));
    }
    if (id >= 30U && id <= 38U) {
        // 1-9: USB HID 0x1E..0x26 -> KEY_1(2)..KEY_9(10)
        return static_cast<std::uint16_t>(KEY_1 + (id - 30U));
    }
    if (id == 39U) {
        return KEY_0;
    }
    // Modifiers
    switch (id) {
    case 0xE0:
        return KEY_LEFTCTRL;
    case 0xE1:
        return KEY_LEFTSHIFT;
    case 0xE2:
        return KEY_LEFTALT;
    case 0xE3:
        return KEY_LEFTMETA;
    case 0xE4:
        return KEY_RIGHTCTRL;
    case 0xE5:
        return KEY_RIGHTSHIFT;
    case 0xE6:
        return KEY_RIGHTALT;
    case 0xE7:
        return KEY_RIGHTMETA;
    // Common keys
    case 0x28:
        return KEY_ENTER;
    case 0x29:
        return KEY_ESC;
    case 0x2A:
        return KEY_BACKSPACE;
    case 0x2B:
        return KEY_TAB;
    case 0x2C:
        return KEY_SPACE;
    case 0x3A:
        return KEY_F1;
    case 0x3B:
        return KEY_F2;
    case 0x3C:
        return KEY_F3;
    case 0x3D:
        return KEY_F4;
    case 0x3E:
        return KEY_F5;
    case 0x3F:
        return KEY_F6;
    case 0x40:
        return KEY_F7;
    case 0x41:
        return KEY_F8;
    case 0x42:
        return KEY_F9;
    case 0x43:
        return KEY_F10;
    case 0x44:
        return KEY_F11;
    case 0x45:
        return KEY_F12;
    default:
        return 0;
    }
}

/// Map MediaKey enum to a Linux KEY_* media code.
static std::uint16_t mediaKeyToLinuxKey(MediaKey key) {
    switch (key) {
    case MediaKey::PlayPause:
        return KEY_PLAYPAUSE;
    case MediaKey::Stop:
        return KEY_STOPCD;
    case MediaKey::Next:
        return KEY_NEXTSONG;
    case MediaKey::Previous:
        return KEY_PREVIOUSSONG;
    case MediaKey::VolumeUp:
        return KEY_VOLUMEUP;
    case MediaKey::VolumeDown:
        return KEY_VOLUMEDOWN;
    case MediaKey::Mute:
        return KEY_MUTE;
    }
    return 0;
}

/**
 * @brief Linux uinput real backend.
 *
 * Opens @c /dev/uinput at construction, registers key + media event bits, and
 * creates a virtual keyboard device. On permission error, degrades gracefully:
 * the device descriptor @c fd_ stays -1 and OUTPUT methods return @c false.
 */
class UinputSynthesizer final : public IInputSynthesizer {
public:
    UinputSynthesizer() {
        fd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (fd_ < 0) {
            AJAZZ_LOG_WARN("input_synth",
                           "UinputSynthesizer: open /dev/uinput failed ({}); "
                           "OUTPUT methods will return false. "
                           "Add udev rule 70-ajazz.rules for unprivileged access "
                           "(Phase-25 operator concern).",
                           std::strerror(errno));
            return;
        }

        // Register key event type and the keys we will synthesise.
        ::ioctl(fd_, UI_SET_EVBIT, EV_KEY);
        // Keyboard range (4..231) + common modifiers + function keys
        for (int k = KEY_ESC; k <= KEY_COMPOSE; ++k) {
            ::ioctl(fd_, UI_SET_KEYBIT, k);
        }
        // Media keys
        ::ioctl(fd_, UI_SET_KEYBIT, KEY_PLAYPAUSE);
        ::ioctl(fd_, UI_SET_KEYBIT, KEY_STOPCD);
        ::ioctl(fd_, UI_SET_KEYBIT, KEY_NEXTSONG);
        ::ioctl(fd_, UI_SET_KEYBIT, KEY_PREVIOUSSONG);
        ::ioctl(fd_, UI_SET_KEYBIT, KEY_VOLUMEUP);
        ::ioctl(fd_, UI_SET_KEYBIT, KEY_VOLUMEDOWN);
        ::ioctl(fd_, UI_SET_KEYBIT, KEY_MUTE);

        struct uinput_setup usetup {};
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor = 0x0300; // AJAZZ vendor prefix
        usetup.id.product = 0x0001;
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        std::strncpy(usetup.name, "AJAZZ Input Synthesizer", sizeof(usetup.name) - 1U);

        if (::ioctl(fd_, UI_DEV_SETUP, &usetup) < 0 || ::ioctl(fd_, UI_DEV_CREATE) < 0) {
            AJAZZ_LOG_WARN("input_synth",
                           "UinputSynthesizer: UI_DEV_SETUP/CREATE failed; "
                           "OUTPUT methods will return false.");
            ::close(fd_);
            fd_ = -1;
        }
    }

    ~UinputSynthesizer() override {
        if (fd_ >= 0) {
            ::ioctl(fd_, UI_DEV_DESTROY);
            ::close(fd_);
        }
    }

    // Non-copyable, non-movable (holds an fd).
    UinputSynthesizer(UinputSynthesizer const&) = delete;
    UinputSynthesizer& operator=(UinputSynthesizer const&) = delete;
    UinputSynthesizer(UinputSynthesizer&&) = delete;
    UinputSynthesizer& operator=(UinputSynthesizer&&) = delete;

    bool typeText(std::string_view utf8) override {
        if (fd_ < 0) {
            return false;
        }
        if (utf8.empty()) {
            return true;
        }
        // Per-codepoint key synthesis: map UTF-8 bytes to key events.
        // For ASCII printable characters, use standard key codes.
        // Non-ASCII and non-printable characters are skipped in this initial impl.
        // TODO: full Unicode compose sequence for Phase 25.
        bool ok = true;
        for (unsigned char c : utf8) {
            std::uint16_t keyCode = 0;
            bool needShift = false;
            if (c >= 'a' && c <= 'z') {
                keyCode = static_cast<std::uint16_t>(KEY_A + (c - 'a'));
            } else if (c >= 'A' && c <= 'Z') {
                keyCode = static_cast<std::uint16_t>(KEY_A + (c - 'A'));
                needShift = true;
            } else if (c >= '1' && c <= '9') {
                keyCode = static_cast<std::uint16_t>(KEY_1 + (c - '1'));
            } else if (c == '0') {
                keyCode = KEY_0;
            } else if (c == ' ') {
                keyCode = KEY_SPACE;
            } else if (c == '\n') {
                keyCode = KEY_ENTER;
            } else {
                continue; // Skip unsupported characters.
            }
            if (needShift) {
                ok = ok && emitEvent(fd_, EV_KEY, KEY_LEFTSHIFT, 1);
            }
            ok = ok && emitEvent(fd_, EV_KEY, keyCode, 1);
            ok = ok && emitEvent(fd_, EV_KEY, keyCode, 0);
            if (needShift) {
                ok = ok && emitEvent(fd_, EV_KEY, KEY_LEFTSHIFT, 0);
            }
        }
        return ok;
    }

    bool sendChord(KeyChord const& chord) override {
        if (fd_ < 0) {
            return false;
        }
        bool ok = true;
        // Press modifiers
        for (auto mod : chord.modifiers) {
            const auto linuxKey = hidUsageToLinuxKey(mod);
            if (linuxKey != 0) {
                ok = ok && emitEvent(fd_, EV_KEY, linuxKey, 1);
            }
        }
        // Press + release main key
        const auto mainKey = hidUsageToLinuxKey(chord.key);
        if (mainKey != 0) {
            ok = ok && emitEvent(fd_, EV_KEY, mainKey, 1);
            ok = ok && emitEvent(fd_, EV_KEY, mainKey, 0);
        }
        // Release modifiers in reverse order
        for (auto it = chord.modifiers.rbegin(); it != chord.modifiers.rend(); ++it) {
            const auto linuxKey = hidUsageToLinuxKey(*it);
            if (linuxKey != 0) {
                ok = ok && emitEvent(fd_, EV_KEY, linuxKey, 0);
            }
        }
        return ok;
    }

    bool sendMediaKey(MediaKey key) override {
        if (fd_ < 0) {
            return false;
        }
        const auto linuxKey = mediaKeyToLinuxKey(key);
        if (linuxKey == 0) {
            return false;
        }
        bool ok = emitEvent(fd_, EV_KEY, linuxKey, 1);
        ok = ok && emitEvent(fd_, EV_KEY, linuxKey, 0);
        return ok;
    }

    bool captureHotkeys(bool enable, std::function<void(KeyChord const&)> /*onHotkey*/) override {
        if (!enable) {
            return false; // Capture is OFF by default; no-op.
        }
        // TODO (Phase 25): implement evdev/libinput grab for opt-in hotkey capture.
        // The grab requires AJAZZ_FEATURE_INPUT_SYNTH ON + an explicit enable flag.
        // For Phase 21-01 the real grab is deferred; OUTPUT synthesis is the primary path.
        AJAZZ_LOG_INFO("input_synth",
                       "UinputSynthesizer::captureHotkeys(true) called; "
                       "evdev grab not yet implemented (deferred Phase 25).");
        return false;
    }

private:
    int fd_{-1};
};

} // namespace

std::unique_ptr<IInputSynthesizer> makePlatformInputSynthesizer() {
    return std::make_unique<UinputSynthesizer>();
}

} // namespace ajazz::core

#endif // defined(__linux__) && defined(AJAZZ_FEATURE_INPUT_SYNTH)
