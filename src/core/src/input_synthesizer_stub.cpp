// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file input_synthesizer_stub.cpp
 * @brief Always-compiled recording stub for @ref ajazz::core::IInputSynthesizer.
 *
 * @c StubInputSynthesizer is used as the default implementation when no native
 * OS backend is built (@c AJAZZ_FEATURE_INPUT_SYNTH OFF, the default). It
 * records every call into a string log (for test assertions) and logs via
 * @ref AJAZZ_LOG_INFO. OUTPUT methods return @c true (they "succeeded" at the
 * no-op) so the registry path can drive the synthesizer green without
 * @c /dev/uinput, Accessibility permission, or any OS access.
 *
 * The capture gate is always OFF in the stub: @c captureHotkeys records the
 * @c enable flag and returns @c false without installing any hook, regardless
 * of the @c enable argument. This satisfies the anti-feature contract:
 * no always-on listener is ever installed through the stub path.
 *
 * @ref makeDefaultInputSynthesizer is also defined here so the default path
 * is always present even when no OS TU is compiled. When
 * @c AJAZZ_FEATURE_INPUT_SYNTH is ON, the factory delegates to
 * @c makePlatformInputSynthesizer() (declared here, defined by the OS TU).
 *
 * Mirrors @c macro_recorder.cpp StubMacroRecorder + factory pattern.
 */
#include "ajazz/core/input_synthesizer.hpp"
#include "ajazz/core/logger.hpp"

#include <cstdint>
#include <string>

namespace ajazz::core {

// Forward declaration for the platform backend (defined in the per-OS TU when
// AJAZZ_FEATURE_INPUT_SYNTH is ON). Must have EXTERNAL linkage (namespace
// ajazz::core, NOT the anonymous namespace) so it links against the per-OS
// definition; an anonymous-namespace decl is internal and "never defined".
#if defined(AJAZZ_FEATURE_INPUT_SYNTH)
std::unique_ptr<IInputSynthesizer> makePlatformInputSynthesizer();
#endif

namespace {

/**
 * @brief Recording no-op stub used as the default synthesizer.
 *
 * Every call appends a tagged entry to @c log_ in the form:
 *   - @c "text:<utf8>"
 *   - @c "chord:<key>+<mod0>+<mod1>..."
 *   - @c "media:<enum-value>"
 *   - @c "capture:<0|1>"
 *
 * Tests retrieve the log via @c StubInputSynthesizer directly (the unit
 * test uses a @c FakeSynth subclass; the stub is exercised through the
 * factory test path).
 */
class StubInputSynthesizer final : public IInputSynthesizer {
public:
    bool typeText(std::string_view utf8) override {
        std::string entry{"text:"};
        entry.append(utf8);
        log_.push_back(entry);
        AJAZZ_LOG_INFO("input_synth", "stub typeText: {}", entry);
        return true;
    }

    bool sendChord(KeyChord const& chord) override {
        std::string entry{"chord:"};
        entry += std::to_string(chord.key);
        for (auto mod : chord.modifiers) {
            entry += '+';
            entry += std::to_string(mod);
        }
        log_.push_back(entry);
        AJAZZ_LOG_INFO("input_synth", "stub sendChord: {}", entry);
        return true;
    }

    bool sendMediaKey(MediaKey key) override {
        std::string entry{"media:"};
        entry += std::to_string(static_cast<std::uint8_t>(key));
        log_.push_back(entry);
        AJAZZ_LOG_INFO("input_synth", "stub sendMediaKey: {}", entry);
        return true;
    }

    // captureHotkeys: records the enable flag, installs NO hook, returns false.
    // The anti-feature contract: a fresh synthesizer has capture OFF; passing
    // enable=false (or never calling this) keeps it OFF.
    bool captureHotkeys(bool enable, std::function<void(KeyChord const&)> /*onHotkey*/) override {
        std::string entry{"capture:"};
        entry += (enable ? '1' : '0');
        log_.push_back(entry);
        AJAZZ_LOG_INFO(
            "input_synth", "stub captureHotkeys: {} (no hook installed)", static_cast<int>(enable));
        return false; // Stub never installs a hook.
    }

    /// Read-only access to the recorded call log (for test assertions).
    [[nodiscard]] std::vector<std::string> const& log() const noexcept { return log_; }

    /// Clear the call log (e.g. between test cases).
    void clearLog() noexcept { log_.clear(); }

private:
    std::vector<std::string> log_;
};

} // namespace

std::unique_ptr<IInputSynthesizer> makeDefaultInputSynthesizer() {
#if defined(AJAZZ_FEATURE_INPUT_SYNTH)
    return makePlatformInputSynthesizer();
#else
    return std::make_unique<StubInputSynthesizer>();
#endif
}

} // namespace ajazz::core
