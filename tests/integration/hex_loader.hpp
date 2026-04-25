// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file hex_loader.hpp
 * @brief Tiny header-only loader for `.hex` HID capture fixtures.
 *
 * The integration test suite (`test_capture_replay.cpp`) replays canned
 * USB-HID frames stored under `tests/integration/fixtures/`. The fixtures
 * are plain text so they survive code review and `git diff`. This header
 * parses them into a contiguous `std::vector<std::uint8_t>` for the parser
 * to consume.
 *
 * Grammar accepted by @ref loadHexFixture():
 *   - Whitespace and newlines are ignored.
 *   - `#` starts a comment that runs to end-of-line.
 *   - Bytes are written as two hex digits, optionally prefixed with `0x`.
 *
 * The loader is header-only and exception-free on bad input: malformed
 * tokens cause a `std::runtime_error` so a test can `REQUIRE_THROWS` on
 * adversarial fixtures.
 */
#pragma once

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ajazz::tests {

/**
 * @brief Strip comments and whitespace; convert hex tokens to bytes.
 *
 * @param path Absolute or repo-relative path to a `.hex` fixture.
 * @return Raw byte sequence ready to feed to a protocol parser.
 *
 * @throws std::runtime_error When the file cannot be opened or contains
 *         an odd number of hex digits / a non-hex character.
 */
[[nodiscard]] inline std::vector<std::uint8_t> loadHexFixture(std::filesystem::path const& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("hex_loader: cannot open " + path.string());
    }

    std::vector<std::uint8_t> bytes;
    std::string line;
    while (std::getline(in, line)) {
        // Drop comments.
        if (auto const hash = line.find('#'); hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream tokens(line);
        std::string token;
        while (tokens >> token) {
            // Permit "0x" / "0X" prefix.
            if (token.size() >= 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
                token.erase(0, 2);
            }
            if (token.size() != 2) {
                throw std::runtime_error("hex_loader: bad token '" + token + "' in " +
                                         path.string());
            }
            auto hexNibble = [&](char c) -> int {
                if (c >= '0' && c <= '9') {
                    return c - '0';
                }
                if (c >= 'a' && c <= 'f') {
                    return c - 'a' + 10;
                }
                if (c >= 'A' && c <= 'F') {
                    return c - 'A' + 10;
                }
                throw std::runtime_error("hex_loader: non-hex char '" + std::string(1, c) +
                                         "' in " + path.string());
            };
            int const hi = hexNibble(token[0]);
            int const lo = hexNibble(token[1]);
            bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        }
    }
    return bytes;
}

} // namespace ajazz::tests
