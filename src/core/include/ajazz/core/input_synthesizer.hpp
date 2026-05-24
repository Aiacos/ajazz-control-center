// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file input_synthesizer.hpp
 * @brief Pure-core OS input synthesis interface: type text, send key chords,
 *        and emit media keys — the OUTPUT side of the @c system.hotkey,
 *        @c plain.text, @c system.multimedia, and @c system.volume built-in
 *        actions.
 *
 * The interface deliberately contains NO Qt and NO nlohmann::json (COD-031
 * boundary — public headers install alongside ajazz_core and must not
 * drag in Qt or JSON implementation headers for consumers).
 *
 * ## Backend gating
 *
 * The native OS backends are gated behind the CMake option
 * @c AJAZZ_FEATURE_INPUT_SYNTH (default OFF). When the option is OFF,
 * @ref makeDefaultInputSynthesizer returns a recording stub that logs every
 * call and returns @c true for OUTPUT methods — suitable for tests and for
 * wiring the registry without a real @c /dev/uinput handle or Accessibility
 * permission.
 *
 * ## Opt-in capture gate (anti-feature — LOCKED)
 *
 * The @ref IInputSynthesizer::captureHotkeys entry point is provided for
 * future global hotkey CAPTURE (triggering an action from a key-combo). It is
 * OFF by default: a fresh synthesizer installs NO hook until
 * @c captureHotkeys(true, ...) is called explicitly. The stub and all native
 * backends enforce this contract. The OUTPUT methods (@ref typeText,
 * @ref sendChord, @ref sendMediaKey) are the primary path; capture is the
 * gated, deferrable surface.
 *
 * Mirrors @c macro_recorder.hpp per-OS-backend + @c AJAZZ_FEATURE_* gate
 * pattern.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ajazz::core {

/**
 * @brief A single OS key-chord: zero or more modifier keys plus one main key.
 *
 * Keys are represented as platform-neutral USB HID Usage IDs (e.g.
 * @c 0x00070028 for Return, @c 0x000700E0 for Left Control). The native
 * backend translates them to OS-specific codes (XKB keysym / Win32 VK / CGKey).
 *
 * This is the value type for @c system.hotkey OUTPUT synthesis; the registry
 * parses the profile's @c settingsJson into a @c KeyChord and passes it to
 * @ref IInputSynthesizer::sendChord.
 */
struct KeyChord {
    std::vector<std::uint32_t> modifiers; ///< HID Usage IDs of held modifier keys (may be empty).
    std::uint32_t key{0};                 ///< HID Usage ID of the main key to press.
};

/**
 * @brief Bounded enumeration of OS-media / volume operations.
 *
 * The native backend maps each enumerator to the platform media key usage:
 *   - Linux  : @c KEY_PLAYPAUSE / @c KEY_VOLUMEUP / … (linux/input-event-codes.h)
 *   - Windows: @c VK_MEDIA_PLAY_PAUSE / … (WinUser.h)
 *   - macOS  : @c kVK_ANSI_* via CGEventPost
 *
 * This is the value type for @c system.multimedia and @c system.volume actions.
 */
enum class MediaKey : std::uint8_t {
    PlayPause = 0,  ///< Toggle play / pause.
    Stop = 1,       ///< Stop playback.
    Next = 2,       ///< Skip to next track.
    Previous = 3,   ///< Skip to previous track.
    VolumeUp = 4,   ///< Raise system volume by one step.
    VolumeDown = 5, ///< Lower system volume by one step.
    Mute = 6,       ///< Toggle mute.
};

/**
 * @brief OS input synthesis interface.
 *
 * Implementations are single-instance, owned via @c std::unique_ptr returned
 * by @ref makeDefaultInputSynthesizer. Thread-safety requirements are
 * per-implementation; the stub is not thread-safe (UI thread only).
 */
class IInputSynthesizer {
public:
    virtual ~IInputSynthesizer() = default;

    /**
     * @brief Type a UTF-8 string as synthetic keystrokes.
     *
     * This is the @c plain.text action output. The string is sent as a
     * literal — NOT interpreted as a shell command (no injection vector).
     * The native backend emits per-codepoint key-down / key-up pairs via
     * the OS API (uinput / SendInput / CGEvent).
     *
     * @param utf8 The text to type. An empty string is a no-op (returns true).
     * @return @c true on success; @c false if the backend is unavailable
     *         (e.g. @c /dev/uinput EACCES).
     */
    virtual bool typeText(std::string_view utf8) = 0;

    /**
     * @brief Send a key chord (modifiers + main key) as synthetic input.
     *
     * This is the @c system.hotkey action output. The backend presses the
     * modifier keys in order, presses the main key, then releases all in
     * reverse order.
     *
     * @param chord The modifier set + main key to synthesise.
     * @return @c true on success; @c false if the backend is unavailable.
     */
    virtual bool sendChord(KeyChord const& chord) = 0;

    /**
     * @brief Emit a single OS media / volume key event.
     *
     * This is the @c system.multimedia and @c system.volume action output.
     *
     * @param key  The media operation to synthesise.
     * @return @c true on success; @c false if the backend is unavailable.
     */
    virtual bool sendMediaKey(MediaKey key) = 0;

    /**
     * @brief Opt-in global hotkey CAPTURE gate (anti-feature — LOCKED OFF by default).
     *
     * When @p enable is @c false (or not called at all), this method installs
     * NO global hook and returns @c false. A fresh synthesizer always starts
     * with capture inactive — there is no always-on listener.
     *
     * Passing @p enable = @c true activates the capture path in the native
     * backend (evdev grab on Linux, SetWindowsHookExW on Windows, CGEventTap
     * on macOS). The stub always returns @c false and records the call
     * regardless of @p enable.
     *
     * @param enable   @c true to install the capture hook; @c false to remove it.
     * @param onHotkey Callback fired for each captured chord (ignored when
     *                 @p enable is @c false; never called by the stub).
     * @return @c true if a hook was successfully installed; @c false otherwise
     *         (including the stub and the default-OFF state).
     */
    virtual bool captureHotkeys(bool enable, std::function<void(KeyChord const&)> onHotkey) = 0;
};

/**
 * @brief Build the platform-default @ref IInputSynthesizer.
 *
 * Returns a @c StubInputSynthesizer when no native backend is compiled
 * (i.e. @c AJAZZ_FEATURE_INPUT_SYNTH is OFF — the default). The stub records
 * every call and returns @c true for OUTPUT methods so the registry path can
 * drive it green without @c /dev/uinput or OS permissions.
 *
 * When @c AJAZZ_FEATURE_INPUT_SYNTH is ON, returns the platform backend
 * (Linux uinput / Windows SendInput / macOS CGEvent) via
 * @c makePlatformInputSynthesizer().
 */
[[nodiscard]] std::unique_ptr<IInputSynthesizer> makeDefaultInputSynthesizer();

} // namespace ajazz::core
