// SPDX-License-Identifier: GPL-3.0-or-later
/** @file akp815_wire.cpp
 *  @brief AJAZZ AKP815 wire-protocol command builders and input parsing.
 *
 *  Stateless builders for the AKP family framing used by the AKP815 backend
 *  (@ref akp815.cpp): 512-byte "CRT"-prefixed packets with a 3-byte ASCII
 *  command word at offsets 5..7. These builders were shared verbatim with the
 *  (now removed) C++ AKP153 backend; they were relocated here so the AKP815
 *  carve-out owns its wire definitions outright.
 *
 *  The protocol is a clean-room reconstruction from the notes in
 *  docs/protocols/streamdeck/akp815.md and akp153.md; no third-party source is
 *  incorporated.
 */
#include "akp815_wire.hpp"

#include <algorithm>

namespace ajazz::streamdeck::akp815 {

namespace {

/** @brief Allocate a zero-initialised 512-byte packet. */
std::array<std::uint8_t, PacketSize> emptyPacket() noexcept {
    std::array<std::uint8_t, PacketSize> pkt{};
    return pkt;
}

/** @brief Construct a 512-byte command packet with the AKP "CRT" prefix and a
 *         three-byte ASCII command word.
 *
 *  Byte layout produced:
 *  - Bytes 0–2:  "CRT" prefix (0x43 0x52 0x54)
 *  - Bytes 3–4:  0x00 0x00
 *  - Bytes 5–7:  @p cmd (three ASCII command bytes)
 *  - Bytes 8–511: zero-padded payload area (filled by callers)
 *
 *  @param cmd  Three-byte ASCII command identifier (e.g. CmdLight, CmdBat).
 *  @return     Fully initialised command packet.
 */
std::array<std::uint8_t, PacketSize> buildCmdHeader(std::array<std::uint8_t, 3> const& cmd) {
    auto pkt = emptyPacket();
    pkt[0] = CmdPrefix[0];
    pkt[1] = CmdPrefix[1];
    pkt[2] = CmdPrefix[2];
    pkt[3] = 0x00;
    pkt[4] = 0x00;
    pkt[5] = cmd[0];
    pkt[6] = cmd[1];
    pkt[7] = cmd[2];
    return pkt;
}

} // namespace

std::array<std::uint8_t, PacketSize> buildSetBrightness(std::uint8_t percent) {
    auto pkt = buildCmdHeader(CmdLight);
    pkt[10] = std::min<std::uint8_t>(percent, 100);
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildClearAll() {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x00;
    pkt[11] = 0xff;
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildClearKey(std::uint8_t keyIndex) {
    auto pkt = buildCmdHeader(CmdClear);
    pkt[10] = 0x00;
    pkt[11] = keyIndex;
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildVersionRequest() {
    return buildCmdHeader(CmdVersion);
}

std::array<std::uint8_t, PacketSize> buildUploadFinished() {
    // ULEND is a 5-byte command at offsets 5..9 (not the standard 3-byte at 5..7).
    // Shared across the AKP family per akp05_vendor.md §3.
    std::array<std::uint8_t, PacketSize> pkt{};
    pkt[0] = CmdPrefix[0];
    pkt[1] = CmdPrefix[1];
    pkt[2] = CmdPrefix[2];
    pkt[5] = UploadFinishedMarker[0];
    pkt[6] = UploadFinishedMarker[1];
    pkt[7] = UploadFinishedMarker[2];
    pkt[8] = UploadFinishedMarker[3];
    pkt[9] = UploadFinishedMarker[4];
    return pkt;
}

std::array<std::uint8_t, PacketSize> buildImageHeader(std::uint8_t keyIndex,
                                                      std::uint16_t jpegSize) {
    auto pkt = buildCmdHeader(CmdBat);
    // Big-endian 16-bit size at offsets 10..11.
    pkt[10] = static_cast<std::uint8_t>((jpegSize >> 8) & 0xffu);
    pkt[11] = static_cast<std::uint8_t>(jpegSize & 0xffu);
    pkt[12] = keyIndex;
    return pkt;
}

std::optional<KeyEvent> parseInputReport(std::span<std::uint8_t const> frame) {
    if (frame.size() < 16) {
        return std::nullopt;
    }

    // ACK frames start with "ACK".
    if (frame[0] == 0x41 && frame[1] == 0x43 && frame[2] == 0x4b) {
        return std::nullopt;
    }

    // Key index at byte 9.
    auto const keyIndex = frame[9];
    if (keyIndex == 0 || keyIndex > KeyCount) {
        return std::nullopt;
    }

    // TODO(WR-03): the release-byte format is not documented in akp815.md /
    // akp153.md §Input-reports as of 2026-05-24. Until a hardware capture
    // surfaces the release encoding, always report pressed=true (known honest
    // limitation — we do NOT invent the wire format).
    return KeyEvent{.keyIndex = keyIndex, .pressed = true};
}

} // namespace ajazz::streamdeck::akp815
