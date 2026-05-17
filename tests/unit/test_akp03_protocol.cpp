// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_akp03_protocol.cpp
 * @brief Unit tests for the AKP03 (Mirabox N3) StreamDeck protocol packet
 *        builders and input-report parser.
 *
 * Validates every command builder produces the correct wire bytes and that
 * `parseInputReport()` correctly classifies:
 *
 * - LCD-key press / release for keys 1..6
 * - Side-button release-only events for buttons 7..9 (action codes 0x25 /
 *   0x30 / 0x31 per `[ajazz-sdk]`)
 * - Rotation CW / CCW for all three encoders (0x90/0x91, 0x50/0x51,
 *   0x60/0x61)
 * - Encoder press / release for all three encoders (0x33 / 0x35 / 0x34)
 * - ACK / short / NOP frames silently discarded
 *
 * The action-code constants come from
 * `mishamyrt/ajazz-sdk/src/protocol/codes.rs` cross-checked against
 * `4ndv/opendeck-akp03` and `tomekceszke/ajazz-akp03e` - see
 * `docs/protocols/streamdeck/_research-sources.md`.
 */
#include "akp03_protocol.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::streamdeck::akp03;

// -----------------------------------------------------------------------------
// Output packet builders
// -----------------------------------------------------------------------------

/// `buildSetBrightness(60)` must produce a CRT-prefixed LIG packet with byte 10 == 60.
TEST_CASE("akp03 brightness packet has CRT prefix and LIG command", "[akp03][protocol]") {
    auto const pkt = buildSetBrightness(60);
    REQUIRE(pkt.size() == PacketSize);
    REQUIRE(pkt[0] == 0x43); // C
    REQUIRE(pkt[1] == 0x52); // R
    REQUIRE(pkt[2] == 0x54); // T
    REQUIRE(pkt[5] == 0x4c); // L
    REQUIRE(pkt[6] == 0x49); // I
    REQUIRE(pkt[7] == 0x47); // G
    REQUIRE(pkt[10] == 60);
}

/// Values above 100 must be clamped to 100 before encoding.
TEST_CASE("akp03 brightness clamps to 100", "[akp03][protocol]") {
    auto const pkt = buildSetBrightness(250);
    REQUIRE(pkt[10] == 100);
}

/// `buildClearAll()` must encode `0xFF` at byte 11 (all-keys sentinel).
TEST_CASE("akp03 clear-all encodes 0xff", "[akp03][protocol]") {
    auto const pkt = buildClearAll();
    REQUIRE(pkt[5] == 0x43);
    REQUIRE(pkt[6] == 0x4c);
    REQUIRE(pkt[7] == 0x45);
    REQUIRE(pkt[10] == 0x00);
    REQUIRE(pkt[11] == 0xff);
}

/// `buildClearKey(3)` must encode the 1-based key index at byte 11.
TEST_CASE("akp03 clear-key uses 1-based key index", "[akp03][protocol]") {
    auto const pkt = buildClearKey(3);
    REQUIRE(pkt[10] == 0x00);
    REQUIRE(pkt[11] == 3);
}

/// `buildImageHeader()` must encode the JPEG (`BAT`) opcode + big-endian size + key id.
///
/// Wire byte: `[CRT]…[BAT]…[size_hi][size_lo][keyIndex]`. Source:
/// `[ajazz-sdk]/info.rs::Kind::key_image_format` (JPEG @ 60×60 for AKP03).
TEST_CASE("akp03 image header encodes BAT command and big-endian size", "[akp03][protocol]") {
    auto const pkt = buildImageHeader(4, 0x1234);
    REQUIRE(pkt[5] == 0x42); // B
    REQUIRE(pkt[6] == 0x41); // A
    REQUIRE(pkt[7] == 0x54); // T
    REQUIRE(pkt[10] == 0x12);
    REQUIRE(pkt[11] == 0x34);
    REQUIRE(pkt[12] == 4);
}

// -----------------------------------------------------------------------------
// Input parser - discrimination of every event class
// -----------------------------------------------------------------------------

/// Frames whose first three bytes are `ACK` are device acknowledgements and
/// must be silently dropped (no event emitted).
TEST_CASE("akp03 parser rejects ACK frames", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{'A', 'C', 'K', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    REQUIRE(!parseInputReport(frame).has_value());
}

/// Frames shorter than 16 bytes are not legal HID reports for this device.
TEST_CASE("akp03 parser rejects short frames", "[akp03][protocol]") {
    std::array<std::uint8_t, 8> frame{};
    REQUIRE(!parseInputReport(frame).has_value());
}

/// `tag == 0x00` is the NOP / keep-alive idle frame; discard silently.
TEST_CASE("akp03 parser drops NOP frames", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = ActionNop;
    REQUIRE(!parseInputReport(frame).has_value());
}

// ---- LCD key 1..6 -----------------------------------------------------------

/// A v3-firmware frame with byte 10 == 1 must decode as `KeyPressed` with
/// the 1-based key index from byte 9.
TEST_CASE("akp03 parser decodes LCD key press", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 2;  // LCD key 2
    frame[10] = 1; // pressed edge
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::KeyPressed);
    REQUIRE(ev->index == 2);
}

/// A v3-firmware frame with byte 10 == 0 must decode as `KeyReleased`.
TEST_CASE("akp03 parser decodes LCD key release", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 4;
    frame[10] = 0;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::KeyReleased);
    REQUIRE(ev->index == 4);
}

/// All six LCD-key indices (1..6) must decode without falling into the
/// side-button range. Defends against off-by-one regressions if `KeyCount`
/// changes meaning later.
TEST_CASE("akp03 parser decodes every LCD key 1..6", "[akp03][protocol]") {
    for (std::uint8_t k = ActionLcdKey1; k <= ActionLcdKey6; ++k) {
        std::array<std::uint8_t, 16> frame{};
        frame[9] = k;
        frame[10] = 1;
        auto const ev = parseInputReport(frame);
        REQUIRE(ev.has_value());
        REQUIRE(ev->kind == InputEvent::Kind::KeyPressed);
        REQUIRE(ev->index == k);
    }
}

// ---- Side buttons 7..9 (release-only on the wire) ---------------------------

/// Action code `0x25` is non-LCD button 7 (left of the bottom row).
TEST_CASE("akp03 parser decodes side button 7", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = ActionSideButton7;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::SideButton);
    REQUIRE(ev->index == 7);
}

/// Action codes `0x30` and `0x31` cover the centre / right side buttons.
TEST_CASE("akp03 parser decodes side buttons 8 and 9", "[akp03][protocol]") {
    {
        std::array<std::uint8_t, 16> frame{};
        frame[9] = ActionSideButton8;
        auto const ev = parseInputReport(frame);
        REQUIRE(ev.has_value());
        REQUIRE(ev->kind == InputEvent::Kind::SideButton);
        REQUIRE(ev->index == 8);
    }
    {
        std::array<std::uint8_t, 16> frame{};
        frame[9] = ActionSideButton9;
        auto const ev = parseInputReport(frame);
        REQUIRE(ev.has_value());
        REQUIRE(ev->kind == InputEvent::Kind::SideButton);
        REQUIRE(ev->index == 9);
    }
}

// ---- Encoder rotation -------------------------------------------------------

/// `0x90` / `0x91` are encoder 0 CCW / CW respectively.
TEST_CASE("akp03 parser decodes encoder 0 rotation", "[akp03][protocol]") {
    {
        std::array<std::uint8_t, 16> frame{};
        frame[9] = ActionEncoder0Ccw;
        auto const ev = parseInputReport(frame);
        REQUIRE(ev.has_value());
        REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
        REQUIRE(ev->index == 0);
        REQUIRE(ev->delta == -1);
    }
    {
        std::array<std::uint8_t, 16> frame{};
        frame[9] = ActionEncoder0Cw;
        auto const ev = parseInputReport(frame);
        REQUIRE(ev.has_value());
        REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
        REQUIRE(ev->index == 0);
        REQUIRE(ev->delta == +1);
    }
}

/// `0x50` / `0x51` are encoder 1 CCW / CW respectively.
TEST_CASE("akp03 parser decodes encoder 1 rotation", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = ActionEncoder1Cw;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
    REQUIRE(ev->index == 1);
    REQUIRE(ev->delta == +1);
}

/// `0x60` / `0x61` are encoder 2 CCW / CW respectively.
TEST_CASE("akp03 parser decodes encoder 2 rotation", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = ActionEncoder2Ccw;
    auto const ev = parseInputReport(frame);
    REQUIRE(ev.has_value());
    REQUIRE(ev->kind == InputEvent::Kind::EncoderTurned);
    REQUIRE(ev->index == 2);
    REQUIRE(ev->delta == -1);
}

// ---- Encoder press / release ------------------------------------------------

/// `0x33` is encoder 0 press; byte 10 == 0 indicates a release edge on
/// firmware revisions that emit one.
TEST_CASE("akp03 parser decodes encoder 0 press and release", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = ActionEncoder0Press;
    frame[10] = 0x01;
    auto const press = parseInputReport(frame);
    REQUIRE(press.has_value());
    REQUIRE(press->kind == InputEvent::Kind::EncoderPressed);
    REQUIRE(press->index == 0);

    frame[10] = 0x00;
    auto const release = parseInputReport(frame);
    REQUIRE(release.has_value());
    REQUIRE(release->kind == InputEvent::Kind::EncoderReleased);
    REQUIRE(release->index == 0);
}

/// `0x35` and `0x34` are encoder 1 and 2 press respectively. Tests confirm
/// the index decoding matches `[ajazz-sdk]`'s mapping (the SDK assigns
/// 0x33->0, **0x35->1**, **0x34->2** - note the non-monotonic ordering).
TEST_CASE("akp03 parser decodes encoder 1 and 2 press", "[akp03][protocol]") {
    {
        std::array<std::uint8_t, 16> frame{};
        frame[9] = ActionEncoder1Press;
        frame[10] = 0x01;
        auto const ev = parseInputReport(frame);
        REQUIRE(ev.has_value());
        REQUIRE(ev->kind == InputEvent::Kind::EncoderPressed);
        REQUIRE(ev->index == 1);
    }
    {
        std::array<std::uint8_t, 16> frame{};
        frame[9] = ActionEncoder2Press;
        frame[10] = 0x01;
        auto const ev = parseInputReport(frame);
        REQUIRE(ev.has_value());
        REQUIRE(ev->kind == InputEvent::Kind::EncoderPressed);
        REQUIRE(ev->index == 2);
    }
}

/// Unknown action codes outside the documented table must be discarded
/// silently. Defends against synthetic / fuzzed frames.
TEST_CASE("akp03 parser drops unknown action codes", "[akp03][protocol]") {
    std::array<std::uint8_t, 16> frame{};
    frame[9] = 0x7E;
    REQUIRE(!parseInputReport(frame).has_value());
}

// ---------------------------------------------------------------------------
// Vendor-RE-discovered opcodes (akp05_vendor.md §3, shared AKP-family wire).
// ---------------------------------------------------------------------------

TEST_CASE("akp03 version request uses CRT prefix + VER command",
          "[akp03][protocol][vendor-re]") {
    auto const pkt = buildVersionRequest();
    REQUIRE(pkt.size() == PacketSize);
    REQUIRE(pkt[0] == 0x43); // C
    REQUIRE(pkt[1] == 0x52); // R
    REQUIRE(pkt[2] == 0x54); // T
    REQUIRE(pkt[5] == 0x56); // V
    REQUIRE(pkt[6] == 0x45); // E
    REQUIRE(pkt[7] == 0x52); // R
    for (std::size_t i = 8; i < PacketSize; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
}

TEST_CASE("akp03 upload-finished encodes ULEND at 5..9",
          "[akp03][protocol][vendor-re]") {
    auto const pkt = buildUploadFinished();
    REQUIRE(pkt.size() == PacketSize);
    REQUIRE(pkt[0] == 0x43); // C
    REQUIRE(pkt[1] == 0x52); // R
    REQUIRE(pkt[2] == 0x54); // T
    REQUIRE(pkt[5] == 0x55); // U
    REQUIRE(pkt[6] == 0x4c); // L
    REQUIRE(pkt[7] == 0x45); // E
    REQUIRE(pkt[8] == 0x4e); // N
    REQUIRE(pkt[9] == 0x44); // D
    for (std::size_t i = 10; i < PacketSize; ++i) {
        REQUIRE(pkt[i] == 0x00);
    }
}
