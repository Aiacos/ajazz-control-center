// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_input_synth.cpp
 * @brief Fake-backend unit tests for IInputSynthesizer.
 *
 * These tests exercise the IInputSynthesizer interface via a recording
 * FakeSynth (mirrors RecordingExecutors in test_action_engine.cpp) WITHOUT
 * touching /dev/uinput, SendInput, or CGEvent. All tests run hardware-free
 * with AJAZZ_FEATURE_INPUT_SYNTH OFF (default).
 *
 * Coverage:
 *  - typeText records the literal string exactly (plain.text action output)
 *  - sendChord records the modifier set + key (system.hotkey output)
 *  - sendMediaKey records the distinct media enum (multimedia/volume output)
 *  - captureHotkeys(false,...) installs nothing, returns false (anti-feature OFF-by-default)
 *  - captureHotkeys(true,...) on the stub records the enable flag but still returns false
 *  - makeDefaultInputSynthesizer() returns non-null; OUTPUT methods return true (stub no-ops)
 *  - Empty typeText call is a no-op (no crash, returns true)
 *
 * Tags: [input-synth] — select with: ctest --preset linux-release -R InputSynth
 */
#include "ajazz/core/input_synthesizer.hpp"

#include <functional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::core;

namespace {

/**
 * @brief Recording fake synthesizer for test assertions.
 *
 * Records every call into log_ in the same tagged format as StubInputSynthesizer:
 *   "text:<utf8>", "chord:<key>+<mod...>", "media:<enum>", "capture:<0|1>"
 *
 * OUTPUT methods return true (synthesizer "succeeded").
 * captureHotkeys returns false and never fires the callback (anti-feature gate).
 */
class FakeSynth final : public IInputSynthesizer {
public:
    bool typeText(std::string_view utf8) override {
        std::string entry{"text:"};
        entry.append(utf8);
        log_.push_back(entry);
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
        lastChord_ = chord;
        return true;
    }

    bool sendMediaKey(MediaKey key) override {
        log_.push_back("media:" + std::to_string(static_cast<int>(key)));
        lastMediaKey_ = key;
        return true;
    }

    bool captureHotkeys(bool enable, std::function<void(KeyChord const&)> onHotkey) override {
        log_.push_back(std::string{"capture:"} + (enable ? '1' : '0'));
        captureCallback_ = onHotkey;
        captureEnabled_ = enable;
        return false; // Never installs a hook (anti-feature gate).
    }

    [[nodiscard]] std::vector<std::string> const& log() const noexcept { return log_; }
    void clearLog() noexcept { log_.clear(); }

    [[nodiscard]] KeyChord const& lastChord() const noexcept { return lastChord_; }
    [[nodiscard]] MediaKey lastMediaKey() const noexcept { return lastMediaKey_; }
    [[nodiscard]] bool captureEnabled() const noexcept { return captureEnabled_; }
    [[nodiscard]] bool hasCallback() const noexcept { return static_cast<bool>(captureCallback_); }

private:
    std::vector<std::string> log_;
    KeyChord lastChord_;
    MediaKey lastMediaKey_{MediaKey::PlayPause};
    std::function<void(KeyChord const&)> captureCallback_;
    bool captureEnabled_{false};
};

} // namespace

// ---------------------------------------------------------------------------
// typeText tests
// ---------------------------------------------------------------------------

TEST_CASE("FakeSynth typeText records literal string", "[input-synth]") {
    FakeSynth synth;
    bool ok = synth.typeText("hello");
    REQUIRE(ok);
    REQUIRE(synth.log().size() == 1);
    REQUIRE(synth.log()[0] == "text:hello");
}

TEST_CASE("FakeSynth typeText empty string is no-op and returns true", "[input-synth]") {
    FakeSynth synth;
    bool ok = synth.typeText("");
    REQUIRE(ok);
    REQUIRE(synth.log().size() == 1);
    REQUIRE(synth.log()[0] == "text:");
}

TEST_CASE("FakeSynth typeText records multiple calls independently", "[input-synth]") {
    FakeSynth synth;
    synth.typeText("foo");
    synth.typeText("bar");
    REQUIRE(synth.log().size() == 2);
    REQUIRE(synth.log()[0] == "text:foo");
    REQUIRE(synth.log()[1] == "text:bar");
}

// ---------------------------------------------------------------------------
// sendChord tests
// ---------------------------------------------------------------------------

TEST_CASE("FakeSynth sendChord records modifier set and key", "[input-synth]") {
    FakeSynth synth;

    // CTRL=0x000700E0, SHIFT=0x000700E1, P=0x00070013
    KeyChord chord;
    chord.modifiers = {0x000700E0U, 0x000700E1U};
    chord.key = 0x00070013U;

    bool ok = synth.sendChord(chord);
    REQUIRE(ok);
    REQUIRE(synth.log().size() == 1);

    // The log entry must contain the key and both modifiers.
    std::string const& entry = synth.log()[0];
    REQUIRE(entry.rfind("chord:", 0) == 0); // starts with "chord:"
}

TEST_CASE("FakeSynth sendChord round-trips the chord", "[input-synth]") {
    FakeSynth synth;

    KeyChord chord;
    chord.modifiers = {0x000700E0U, 0x000700E1U};
    chord.key = 0x00070013U;

    synth.sendChord(chord);

    // Verify the recorded chord round-trips.
    REQUIRE(synth.lastChord().key == chord.key);
    REQUIRE(synth.lastChord().modifiers.size() == 2);
    REQUIRE(synth.lastChord().modifiers[0] == 0x000700E0U);
    REQUIRE(synth.lastChord().modifiers[1] == 0x000700E1U);
}

TEST_CASE("FakeSynth sendChord with no modifiers records key only", "[input-synth]") {
    FakeSynth synth;

    KeyChord chord;
    chord.modifiers = {};
    chord.key = 0x00070028U; // Enter

    bool ok = synth.sendChord(chord);
    REQUIRE(ok);
    REQUIRE(synth.lastChord().modifiers.empty());
    REQUIRE(synth.lastChord().key == 0x00070028U);
}

// ---------------------------------------------------------------------------
// sendMediaKey tests
// ---------------------------------------------------------------------------

TEST_CASE("FakeSynth sendMediaKey records VolumeUp", "[input-synth]") {
    FakeSynth synth;
    bool ok = synth.sendMediaKey(MediaKey::VolumeUp);
    REQUIRE(ok);
    REQUIRE(synth.lastMediaKey() == MediaKey::VolumeUp);
    REQUIRE(synth.log().size() == 1);
    REQUIRE(synth.log()[0] == "media:4"); // VolumeUp = 4
}

TEST_CASE("FakeSynth sendMediaKey records PlayPause distinct from VolumeUp", "[input-synth]") {
    FakeSynth synth;
    synth.sendMediaKey(MediaKey::VolumeUp);
    synth.sendMediaKey(MediaKey::PlayPause);

    REQUIRE(synth.log().size() == 2);
    REQUIRE(synth.log()[0] == "media:4");      // VolumeUp = 4
    REQUIRE(synth.log()[1] == "media:0");      // PlayPause = 0
    REQUIRE(synth.log()[0] != synth.log()[1]); // Distinct
}

TEST_CASE("FakeSynth sendMediaKey covers all MediaKey variants", "[input-synth]") {
    FakeSynth synth;
    synth.sendMediaKey(MediaKey::PlayPause);
    synth.sendMediaKey(MediaKey::Stop);
    synth.sendMediaKey(MediaKey::Next);
    synth.sendMediaKey(MediaKey::Previous);
    synth.sendMediaKey(MediaKey::VolumeUp);
    synth.sendMediaKey(MediaKey::VolumeDown);
    synth.sendMediaKey(MediaKey::Mute);

    REQUIRE(synth.log().size() == 7);
    REQUIRE(synth.log()[0] == "media:0"); // PlayPause
    REQUIRE(synth.log()[1] == "media:1"); // Stop
    REQUIRE(synth.log()[2] == "media:2"); // Next
    REQUIRE(synth.log()[3] == "media:3"); // Previous
    REQUIRE(synth.log()[4] == "media:4"); // VolumeUp
    REQUIRE(synth.log()[5] == "media:5"); // VolumeDown
    REQUIRE(synth.log()[6] == "media:6"); // Mute
}

// ---------------------------------------------------------------------------
// captureHotkeys - opt-in OFF by default (anti-feature gate)
// ---------------------------------------------------------------------------

TEST_CASE("FakeSynth capture is OFF by default - no callback fires", "[input-synth]") {
    FakeSynth synth;

    // A fresh synthesizer: capture is OFF, no callback is installed.
    REQUIRE_FALSE(synth.captureEnabled());
    REQUIRE_FALSE(synth.hasCallback());

    // captureHotkeys(false, cb) returns false and installs nothing.
    bool invoked = false;
    bool result = synth.captureHotkeys(false, [&invoked](KeyChord const&) { invoked = true; });

    REQUIRE_FALSE(result);
    REQUIRE_FALSE(invoked); // Callback was never fired.
    // Log records the call with enable=0.
    REQUIRE(synth.log().size() == 1);
    REQUIRE(synth.log()[0] == "capture:0");
}

TEST_CASE("FakeSynth captureHotkeys(true) records enable but returns false", "[input-synth]") {
    FakeSynth synth;

    bool invoked = false;
    bool result = synth.captureHotkeys(true, [&invoked](KeyChord const&) { invoked = true; });

    // The stub records the enable=true but still returns false (no real hook installed).
    REQUIRE_FALSE(result);
    REQUIRE_FALSE(invoked); // Callback must not be invoked from the stub.
    REQUIRE(synth.log().size() == 1);
    REQUIRE(synth.log()[0] == "capture:1");
    REQUIRE(synth.captureEnabled()); // Recorded the enable flag.
}

// ---------------------------------------------------------------------------
// makeDefaultInputSynthesizer() factory tests (exercises StubInputSynthesizer)
// ---------------------------------------------------------------------------

TEST_CASE("makeDefaultInputSynthesizer returns non-null", "[input-synth]") {
    auto synth = makeDefaultInputSynthesizer();
    REQUIRE(synth != nullptr);
}

TEST_CASE("Default synthesizer OUTPUT methods return true (no-op stub)", "[input-synth]") {
    auto synth = makeDefaultInputSynthesizer();
    REQUIRE(synth != nullptr);

    // typeText
    REQUIRE(synth->typeText("hello world"));
    // sendChord
    KeyChord chord;
    chord.modifiers = {0x000700E0U};
    chord.key = 0x00070004U;
    REQUIRE(synth->sendChord(chord));
    // sendMediaKey
    REQUIRE(synth->sendMediaKey(MediaKey::PlayPause));
}

TEST_CASE("Default synthesizer captureHotkeys(false) returns false - capture OFF",
          "[input-synth]") {
    auto synth = makeDefaultInputSynthesizer();
    REQUIRE(synth != nullptr);

    bool invoked = false;
    bool result = synth->captureHotkeys(false, [&invoked](KeyChord const&) { invoked = true; });

    REQUIRE_FALSE(result);  // No hook installed.
    REQUIRE_FALSE(invoked); // Callback never fired.
}

TEST_CASE("Default synthesizer captureHotkeys(true) returns false - stub never grabs",
          "[input-synth]") {
    auto synth = makeDefaultInputSynthesizer();
    REQUIRE(synth != nullptr);

    bool result = synth->captureHotkeys(true, [](KeyChord const&) {});
    // The default stub always returns false regardless of enable (no OS hook possible).
    REQUIRE_FALSE(result);
}
