// SPDX-License-Identifier: GPL-3.0-or-later
/** @file test_sidecar_protocol.cpp
 *  @brief Unit tests for the pure streamdock-host sidecar wire protocol.
 *
 *  No process is spawned — these cover only the encode/decode functions, the
 *  testable heart of the sidecar device (Slice 3a). ASCII-only names per the
 *  cross-platform ctest filter rule.
 */
#include "sidecar_protocol.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <array>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::sidecar::parseEvent;
using ajazz::app::sidecar::SidecarEvent;

namespace {

QJsonObject reparse(QByteArray const& line) {
    return QJsonDocument::fromJson(line).object();
}

} // namespace

TEST_CASE("buildPing emits a single ping command line", "[sidecar]") {
    auto const line = ajazz::app::sidecar::buildPing();
    REQUIRE(line.endsWith('\n'));
    auto const obj = reparse(line);
    REQUIRE(obj.value("cmd").toString() == "ping");
}

TEST_CASE("buildSetBrightness clamps and carries serial", "[sidecar]") {
    auto const obj = reparse(ajazz::app::sidecar::buildSetBrightness("ABC123", 250));
    REQUIRE(obj.value("cmd").toString() == "set_brightness");
    REQUIRE(obj.value("serial").toString() == "ABC123");
    REQUIRE(obj.value("percent").toInt() == 100); // 250 clamped to 100
}

TEST_CASE("buildSetImage base64-encodes the RGBA payload losslessly", "[sidecar]") {
    // 2x1 RGBA image = 8 bytes.
    std::array<std::uint8_t, 8> rgba{1, 2, 3, 255, 4, 5, 6, 128};
    auto const obj = reparse(ajazz::app::sidecar::buildSetImage("S", 7, true, 2, 1, rgba));

    REQUIRE(obj.value("cmd").toString() == "set_image");
    REQUIRE(obj.value("key").toInt() == 7);
    REQUIRE(obj.value("touchzone").toBool() == true);
    REQUIRE(obj.value("width").toInt() == 2);
    REQUIRE(obj.value("height").toInt() == 1);

    auto const decoded = QByteArray::fromBase64(obj.value("rgba_b64").toString().toLatin1());
    REQUIRE(decoded.size() == 8);
    REQUIRE(static_cast<std::uint8_t>(decoded[3]) == 255);
    REQUIRE(static_cast<std::uint8_t>(decoded[7]) == 128);
}

TEST_CASE("parseEvent decodes a connected event", "[sidecar]") {
    auto const ev = parseEvent(
        R"({"event":"connected","serial":"0300D0","firmware":"V3.AKP05E","vid":768,"pid":12292})");
    REQUIRE(ev.has_value());
    REQUIRE(ev->type == SidecarEvent::Type::Connected);
    REQUIRE(ev->serial == "0300D0");
    REQUIRE(ev->firmware == "V3.AKP05E");
    REQUIRE(ev->vid == 0x0300);
    REQUIRE(ev->pid == 0x3004);
}

TEST_CASE("parseEvent decodes a ready event with device count", "[sidecar]") {
    auto const ev = parseEvent(R"({"event":"ready","device_count":1,"output_allowed":false})");
    REQUIRE(ev.has_value());
    REQUIRE(ev->type == SidecarEvent::Type::Ready);
    REQUIRE(ev->deviceCount == 1);
}

TEST_CASE("parseEvent decodes an input event", "[sidecar]") {
    auto const ev =
        parseEvent(R"({"event":"input","serial":"S","code":160,"state":1,"raw":"41434b"})");
    REQUIRE(ev.has_value());
    REQUIRE(ev->type == SidecarEvent::Type::Input);
    REQUIRE(ev->code == 160);
    REQUIRE(ev->state == 1);
    REQUIRE(ev->rawHex == "41434b");
}

TEST_CASE("parseEvent maps error and device_error to Error", "[sidecar]") {
    auto const e1 = parseEvent(R"({"event":"error","msg":"boom"})");
    REQUIRE(e1.has_value());
    REQUIRE(e1->type == SidecarEvent::Type::Error);
    REQUIRE(e1->message == "boom");

    auto const e2 = parseEvent(R"({"event":"device_error","serial":"S","msg":"lost"})");
    REQUIRE(e2.has_value());
    REQUIRE(e2->type == SidecarEvent::Type::Error);
}

TEST_CASE("parseEvent rejects malformed or event-less lines", "[sidecar]") {
    REQUIRE_FALSE(parseEvent("not json at all").has_value());
    REQUIRE_FALSE(parseEvent("{}").has_value());
    REQUIRE_FALSE(parseEvent(R"({"foo":"bar"})").has_value());
    REQUIRE_FALSE(parseEvent(R"({"event":123})").has_value()); // event not a string
}

TEST_CASE("parseEvent classifies an unknown event name", "[sidecar]") {
    auto const ev = parseEvent(R"({"event":"somethingnew"})");
    REQUIRE(ev.has_value());
    REQUIRE(ev->type == SidecarEvent::Type::Unknown);
}
