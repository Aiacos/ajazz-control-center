// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file input_synthesizer_mac.cpp
 * @brief macOS CGEvent @ref ajazz::core::IInputSynthesizer backend.
 *
 * Gated by both @c __APPLE__ and @c AJAZZ_FEATURE_INPUT_SYNTH. When either
 * guard is absent the entire TU is empty, contributing nothing to the build
 * (self-emptying TU pattern — mirrors macro_recorder gated-TU structure).
 *
 * This is a STUB-BUT-COMPILING implementation: the CGEventCreateKeyboardEvent /
 * CGEventPost call shape is referenced but the full Unicode → CGKeyCode
 * translation and live send path are deferred to Phase-25 (macOS live witness).
 * All OUTPUT methods currently return @c false to signal "not yet implemented"
 * on macOS.
 *
 * Apple-clang -Werror compliance:
 * - No @c inline constexpr at file scope (triggers -Wunused-const-variable).
 * - No unused variables or parameters.
 * - Accessibility permission is required at runtime for CGEventPost from a
 *   non-privileged process — this is a Phase-25 user-consent concern and
 *   is NOT a reason to mutate system configuration from tooling
 *   (CLAUDE.md hard rule).
 */

#if defined(__APPLE__) && defined(AJAZZ_FEATURE_INPUT_SYNTH)

#include "ajazz/core/input_synthesizer.hpp"
#include "ajazz/core/logger.hpp"

// CoreGraphics for CGEventCreateKeyboardEvent / CGEventPost.
// ApplicationServices transitively includes CoreGraphics on macOS.
#include <ApplicationServices/ApplicationServices.h>

namespace ajazz::core {

namespace {

/**
 * @brief macOS CGEvent stub backend (Phase 25 live witness).
 *
 * Implements the @ref IInputSynthesizer interface. All OUTPUT methods return
 * @c false on this platform until the full CGKeyCode translation and
 * CGEventPost path land in Phase 25. The class compiles clean under
 * Apple-clang -Werror.
 */
class CGEventSynthesizer final : public IInputSynthesizer {
public:
    bool typeText(std::string_view utf8) override {
        // Phase 25: iterate UTF-8 codepoints, call
        // CGEventCreateKeyboardEvent(src, kVK_ANSI_*, true/false) per codepoint,
        // then CGEventPost(kCGSessionEventTap, event).
        // Requires Accessibility permission (AXIsProcessTrusted()).
        AJAZZ_LOG_INFO("input_synth",
                       "CGEventSynthesizer::typeText — stub (Phase 25). length={}",
                       utf8.size());
        return false;
    }

    bool sendChord(KeyChord const& chord) override {
        // Phase 25: map HID Usage IDs to kVK_* constants via IOHIDUsageToAnsiKeyCode,
        // build CGEventCreateKeyboardEvent pairs for modifiers + main key,
        // post via CGEventPost(kCGSessionEventTap, ...).
        AJAZZ_LOG_INFO("input_synth",
                       "CGEventSynthesizer::sendChord — stub (Phase 25). "
                       "key=0x{:08X} mods={}",
                       chord.key,
                       chord.modifiers.size());
        return false;
    }

    bool sendMediaKey(MediaKey key) override {
        // Phase 25: map MediaKey enum to kVK_* or NX_KEYTYPE_PLAY / NX_KEYTYPE_MUTE
        // etc. via CGEventCreateKeyboardEvent or NSSystemDefinedEvent.
        AJAZZ_LOG_INFO("input_synth",
                       "CGEventSynthesizer::sendMediaKey — stub (Phase 25). key={}",
                       static_cast<int>(key));
        return false;
    }

    bool captureHotkeys(bool enable, std::function<void(KeyChord const&)> /*onHotkey*/) override {
        if (!enable) {
            return false;
        }
        // Phase 25 (opt-in): CGEventTapCreate for hotkey capture.
        // LOCKED OFF by default; the anti-feature tap is never always-on.
        // Requires Accessibility permission (user-consent; NOT a tooling mutation).
        AJAZZ_LOG_INFO("input_synth",
                       "CGEventSynthesizer::captureHotkeys(true) — stub (Phase 25).");
        return false;
    }
};

} // namespace

std::unique_ptr<IInputSynthesizer> makePlatformInputSynthesizer() {
    return std::make_unique<CGEventSynthesizer>();
}

} // namespace ajazz::core

#endif // defined(__APPLE__) && defined(AJAZZ_FEATURE_INPUT_SYNTH)
