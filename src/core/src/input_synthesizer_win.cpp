// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file input_synthesizer_win.cpp
 * @brief Windows SendInput @ref ajazz::core::IInputSynthesizer backend.
 *
 * Gated by both @c _WIN32 and @c AJAZZ_FEATURE_INPUT_SYNTH. When either
 * guard is absent the entire TU is empty, contributing nothing to the build
 * (self-emptying TU pattern — mirrors macro_recorder gated-TU structure).
 *
 * This is a STUB-BUT-COMPILING implementation: the SendInput call shape is
 * sketched and the class satisfies the IInputSynthesizer interface, but the
 * full Unicode → VK translation table and the live send path are deferred to
 * Phase-25 (Windows live witness). All OUTPUT methods currently return @c false
 * to signal "not yet implemented" on Windows.
 *
 * MSVC /W4 /WX compliance:
 * - No @c sprintf / @c _wgetenv — _s variants used where CRT calls are needed.
 * - No unused variables or parameters with values (all unused params are
 *   commented out to avoid C4100 without triggering other warnings).
 * - No deprecated APIs (C4996 = hard error on MSVC /WX).
 */

#if defined(_WIN32) && defined(AJAZZ_FEATURE_INPUT_SYNTH)

#include "ajazz/core/input_synthesizer.hpp"
#include "ajazz/core/logger.hpp"

// Windows headers — include order matters (Windows.h before others).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ajazz::core {

namespace {

/**
 * @brief Windows SendInput stub backend (Phase 25 live witness).
 *
 * Implements the @ref IInputSynthesizer interface. All OUTPUT methods return
 * @c false on this platform until the full VK translation table and SendInput
 * call path land in Phase 25. The class compiles clean under MSVC /W4 /WX.
 */
class SendInputSynthesizer final : public IInputSynthesizer {
public:
    bool typeText(std::string_view utf8) override {
        // Phase 25: iterate UTF-8 codepoints, build INPUT array of
        // INPUT_KEYBOARD records with KEYEVENTF_UNICODE, call SendInput.
        // The stub returns false (not implemented) and logs.
        AJAZZ_LOG_INFO("input_synth",
                       "SendInputSynthesizer::typeText — stub (Phase 25). text length={}",
                       utf8.size());
        return false;
    }

    bool sendChord(KeyChord const& chord) override {
        // Phase 25: map HID Usage IDs to Windows VK codes via MapVirtualKeyW,
        // build INPUT array (press modifiers, press key, release key, release mods),
        // call SendInput.
        AJAZZ_LOG_INFO("input_synth",
                       "SendInputSynthesizer::sendChord — stub (Phase 25). "
                       "key=0x{:08X} mods={}",
                       chord.key,
                       chord.modifiers.size());
        return false;
    }

    bool sendMediaKey(MediaKey key) override {
        // Phase 25: map MediaKey enum to Windows VK_MEDIA_* codes and call
        // SendInput with a single INPUT_KEYBOARD record.
        AJAZZ_LOG_INFO("input_synth",
                       "SendInputSynthesizer::sendMediaKey — stub (Phase 25). key={}",
                       static_cast<int>(key));
        return false;
    }

    bool captureHotkeys(bool enable, std::function<void(KeyChord const&)> /*onHotkey*/) override {
        if (!enable) {
            return false;
        }
        // Phase 25 (opt-in): SetWindowsHookExW WH_KEYBOARD_LL for hotkey capture.
        // LOCKED OFF by default; the anti-feature hook is never always-on.
        AJAZZ_LOG_INFO("input_synth",
                       "SendInputSynthesizer::captureHotkeys(true) — stub (Phase 25).");
        return false;
    }
};

} // namespace

std::unique_ptr<IInputSynthesizer> makePlatformInputSynthesizer() {
    return std::make_unique<SendInputSynthesizer>();
}

} // namespace ajazz::core

#endif // defined(_WIN32) && defined(AJAZZ_FEATURE_INPUT_SYNTH)
