// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_ak980_clock_sync_e2e.cpp
 * @brief End-to-end byte-level test of the AK980 PRO firmware-RTC sync
 *        envelope (ARCH-05.1 amendment): 4 HID Feature reports + 100ms settle.
 *
 * Previously only the per-packet builders had unit coverage; the full
 * orchestration in ProprietaryKeyboard::setTime() was untested. This test
 * wires a MockTransport behind the keyboard's DI seam, calls setTime() with
 * a deterministic time_point, and asserts that EXACTLY four feature reports
 * were written in EXACTLY the expected order with the expected byte payloads.
 *
 * Catches regressions like:
 *   - writeFeature() being swapped for write() (silent no-op against hw)
 *   - one of the four packets being dropped
 *   - wrong opcode in the START / SAVE control packets
 *   - tm_wday being hard-coded back to 0x04 (gohv corpus shipped that)
 *   - byte 8 marker missing from the START / PREAMBLE packets
 *   - 0xAA 0x55 tail missing from the DATA packet
 *
 * Test fixtures use 2026-01-15 12:34:56 Thursday so we can verify:
 *   - year wire byte == 2026 - 2000 == 26 (0x1A)
 *   - tm_wday == 4 (Thursday) propagates to byte 10
 */
#include "ajazz/core/capabilities.hpp"
#include "ajazz/core/device.hpp"
#include "ajazz/keyboard/keyboard.hpp"
#include "fixtures/mock_transport.hpp"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz;

namespace {

/// Build a DeviceDescriptor matching the registered AK980 PRO entry.
core::DeviceDescriptor makeDescriptor() {
    core::DeviceDescriptor d{};
    d.vendorId = 0x0c45;  // Microdia (Sonix licensee)
    d.productId = 0x8009; // AK980 PRO Sonix SN32F299 family
    d.family = core::DeviceFamily::Keyboard;
    d.model = "AK980 PRO (test)";
    d.codename = "ak980pro";
    return d;
}

core::DeviceId makeId() {
    core::DeviceId id{};
    id.vendorId = 0x0c45;
    id.productId = 0x8009;
    id.serial = "TEST";
    return id;
}

/// Build a deterministic time_point: 2026-01-15 12:34:56 local time.
/// Day-of-week for 2026-01-15 is Thursday (tm_wday == 4).
std::chrono::system_clock::time_point makeReferenceTime() {
    std::tm local{};
    local.tm_year = 2026 - 1900;
    local.tm_mon = 0; // January
    local.tm_mday = 15;
    local.tm_hour = 12;
    local.tm_min = 34;
    local.tm_sec = 56;
    local.tm_isdst = -1; // let the C library decide DST so the round-trip
                         // through localtime_s/r in setTime() recovers the
                         // same calendar components on every host.
    std::time_t const tt = std::mktime(&local);
    REQUIRE(tt != static_cast<std::time_t>(-1));
    return std::chrono::system_clock::from_time_t(tt);
}

} // namespace

TEST_CASE("AK980 PRO setTime emits the full 4-packet HID Feature envelope",
          "[ak980][clock][CAPTURE-04][vendor-re]") {
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();

    auto device = keyboard::makeProprietaryKeyboardWithTransport(
        makeDescriptor(), makeId(), std::move(transport));
    REQUIRE(device != nullptr);

    auto* clock = dynamic_cast<core::IClockCapable*>(device.get());
    REQUIRE(clock != nullptr);

    auto const tp = makeReferenceTime();
    auto const result = clock->setTime(tp);
    REQUIRE(result == core::TimeSyncResult::Ok);

    // Exactly four feature reports, no plain writes.
    REQUIRE(observer->writeFeatureCount() == 4);
    REQUIRE(observer->writeCount() == 4);

    auto const& writes = observer->writes();
    REQUIRE(writes.size() == 4);

    // Hardware-verified wire format (Frida capture of DeviceDriver.exe vs a
    // physical AK980 PRO, 2026-05-21): 65-byte feature reports whose HID Report
    // ID is 0x00; the 0x04 is the first DATA byte, opcode follows at byte 2.
    // --- Packet 1: START control (00 04 18 …) ---
    auto const& p1 = writes.at(0);
    REQUIRE(p1.size() == 65); // TimeReportSize
    REQUIRE(p1[0] == 0x00);   // HID report id (unnumbered)
    REQUIRE(p1[1] == 0x04);   // first data byte
    REQUIRE(p1[2] == 0x18);   // CMD_START = CmdStartTime

    // --- Packet 2: PREAMBLE (00 04 28 … 01 @ byte[9]) ---
    auto const& p2 = writes.at(1);
    REQUIRE(p2.size() == 65);
    REQUIRE(p2[0] == 0x00);
    REQUIRE(p2[1] == 0x04);
    REQUIRE(p2[2] == 0x28); // CmdSetTime — the load-bearing opcode
    REQUIRE(p2[9] == 0x01); // configure-mode marker

    // --- Packet 3: DATA (00 00 01 5A YY MM DD hh mm ss 00 dow … AA 55) ---
    auto const& p3 = writes.at(2);
    REQUIRE(p3.size() == 65);
    REQUIRE(p3[0] == 0x00); // HID report id
    REQUIRE(p3[1] == 0x00);
    REQUIRE(p3[2] == 0x01);                          // LCD-select index + 1
    REQUIRE(p3[3] == 0x5a);                          // magic
    REQUIRE(p3[4] == static_cast<std::uint8_t>(26)); // 2026 - 2000
    REQUIRE(p3[5] == static_cast<std::uint8_t>(1));  // January
    REQUIRE(p3[6] == static_cast<std::uint8_t>(15)); // day
    REQUIRE(p3[7] == static_cast<std::uint8_t>(12)); // hour
    REQUIRE(p3[8] == static_cast<std::uint8_t>(34)); // minute
    REQUIRE(p3[9] == static_cast<std::uint8_t>(56)); // second
    REQUIRE(p3[10] == 0x00);
    REQUIRE(p3[11] == static_cast<std::uint8_t>(4)); // Thursday (tm_wday)
    REQUIRE(p3[63] == 0xaa);                         // tail magic
    REQUIRE(p3[64] == 0x55);

    // --- Packet 4: SAVE control (00 04 02 …) ---
    auto const& p4 = writes.at(3);
    REQUIRE(p4.size() == 65);
    REQUIRE(p4[0] == 0x00);
    REQUIRE(p4[1] == 0x04);
    REQUIRE(p4[2] == 0x02); // CmdSaveRtc — dedicated RTC save opcode (NOT 0x0E EEPROM commit)
}

TEST_CASE("AK980 PRO setTime: tm_wday is not hard-coded to Thursday",
          "[ak980][clock][regression][vendor-re]") {
    // gohv corpus shipped pkt[10] = 0x04 hard-coded ("Thursday" forever);
    // our implementation reads the real tm_wday. Pin this with a second
    // reference point on a different weekday.
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device = keyboard::makeProprietaryKeyboardWithTransport(
        makeDescriptor(), makeId(), std::move(transport));
    auto* clock = dynamic_cast<core::IClockCapable*>(device.get());
    REQUIRE(clock != nullptr);

    // 2026-01-18 is a Sunday (tm_wday == 0).
    std::tm local{};
    local.tm_year = 2026 - 1900;
    local.tm_mon = 0;
    local.tm_mday = 18;
    local.tm_hour = 6;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    auto const tp = std::chrono::system_clock::from_time_t(std::mktime(&local));

    REQUIRE(clock->setTime(tp) == core::TimeSyncResult::Ok);
    REQUIRE(observer->writes().size() == 4);
    REQUIRE(observer->writes().at(2)[11] == 0); // Sunday, not the gohv-hard-coded 4
}

TEST_CASE("AK980 PRO setTime maps year < 2000 to byte 0 instead of underflowing",
          "[ak980][clock][robustness]") {
    auto transport = std::make_unique<tests::MockTransport>();
    auto* observer = transport.get();
    transport->open();
    auto device = keyboard::makeProprietaryKeyboardWithTransport(
        makeDescriptor(), makeId(), std::move(transport));
    auto* clock = dynamic_cast<core::IClockCapable*>(device.get());
    REQUIRE(clock != nullptr);

    // Year 1970 - if the builder used static_cast<uint8_t>(year - 2000) without
    // the >= 2000 guard, the year byte would wrap to 226. The implementation pins it to 0.
    auto const epoch = std::chrono::system_clock::from_time_t(0);
    REQUIRE(clock->setTime(epoch) == core::TimeSyncResult::Ok);
    REQUIRE(observer->writes().size() == 4);
    REQUIRE(observer->writes().at(2)[4] == 0);
}
