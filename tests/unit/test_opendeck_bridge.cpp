// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_opendeck_bridge.cpp
 * @brief Unit tests for the OpenDeck bridge JSON shaping (Phase 1 of the
 *        OpenDeck UI integration). Exercises the PURE free functions in
 *        ajazz::app::opendeck_detail only (no DeviceModel/ProfileController),
 *        pinning the our-types -> OpenDeck-shape contract from
 *        docs/opendeck-ui/01-contract.md. TEST_CASE titles are ASCII-only.
 */
#include "ajazz/core/action_instance.hpp"
#include "ajazz/core/profile.hpp"
#include "opendeck_bridge.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::app::opendeck_detail;

TEST_CASE("deviceInfoJson maps capabilities to a DeviceInfo (Stream Deck +)", "[opendeck]") {
    QVariantMap caps;
    caps["model"] = QStringLiteral("AJAZZ AKP05E (Stream Dock Plus)");
    caps["keyRows"] = 2;
    caps["gridColumns"] = 5;
    caps["keyCount"] = 10;
    caps["encoderCount"] = 4;
    caps["touchZoneCount"] = 4;

    QJsonObject const d = deviceInfoJson(QStringLiteral("akp05e"), caps);
    REQUIRE(d.value("id").toString() == "akp05e");
    REQUIRE(d.value("name").toString() == "AJAZZ AKP05E (Stream Dock Plus)");
    REQUIRE(d.value("rows").toInt() == 2);
    REQUIRE(d.value("columns").toInt() == 5);
    REQUIRE(d.value("encoders").toInt() == 4);
    REQUIRE(d.value("touchpoints").toInt() == 4);
    REQUIRE(d.value("type").toInt() == 7); // encoders > 0 -> Stream Deck +
}

TEST_CASE("deviceInfoJson picks Mini/XL/standard types by key count", "[opendeck]") {
    QVariantMap mini;
    mini["keyCount"] = 6;
    REQUIRE(deviceInfoJson(QStringLiteral("akp03"), mini).value("type").toInt() == 1);
    QVariantMap xl;
    xl["keyCount"] = 32;
    REQUIRE(deviceInfoJson(QStringLiteral("xl"), xl).value("type").toInt() == 2);
    QVariantMap std15;
    std15["keyCount"] = 15;
    REQUIRE(deviceInfoJson(QStringLiteral("akp153"), std15).value("type").toInt() == 0);
}

TEST_CASE("categoriesJson always includes the six OpenDeck built-ins", "[opendeck]") {
    QJsonObject const cats = categoriesJson(QVariantList{});
    REQUIRE(cats.contains("OpenDeck"));
    // Category value is an object { actions: Action[] } (ActionList contract).
    QJsonArray const builtins = cats.value("OpenDeck").toObject().value("actions").toArray();
    REQUIRE(builtins.size() == 6);
    QStringList uuids;
    for (QJsonValue const& v : builtins) {
        uuids.append(v.toObject().value("uuid").toString());
    }
    REQUIRE(uuids.contains("opendeck.multiaction"));
    REQUIRE(uuids.contains("opendeck.toggleaction"));
    REQUIRE(uuids.contains("opendeck.brightness"));
    // full Action shape on a built-in
    QJsonObject const first = builtins.at(0).toObject();
    REQUIRE(first.contains("name"));
    REQUIRE(first.contains("controllers"));
    REQUIRE(first.value("states").toArray().size() == 1);
}

TEST_CASE("categoriesJson groups installed actions by plugin name", "[opendeck]") {
    QVariantList installed;
    auto entry = [](QString id, QString name, QString plugin) {
        QVariantMap m;
        m["actionId"] = id;
        m["actionName"] = name;
        m["pluginName"] = plugin;
        m["controllers"] = QVariantList{QStringLiteral("Keypad")};
        return m;
    };
    installed << entry("com.ajazz.sysmon.cpu", "CPU Usage", "System Monitor");
    installed << entry("com.ajazz.sysmon.ram", "RAM Usage", "System Monitor");
    installed << entry("com.jk.weather.action", "Weather", "Weather");

    QJsonObject const cats = categoriesJson(installed);
    auto actionsOf = [&](QString const& group) {
        return cats.value(group).toObject().value("actions").toArray();
    };
    REQUIRE(actionsOf("System Monitor").size() == 2);
    REQUIRE(actionsOf("Weather").size() == 1);
    QJsonObject const cpu = actionsOf("System Monitor").at(0).toObject();
    REQUIRE(cpu.value("uuid").toString() == "com.ajazz.sysmon.cpu");
    REQUIRE(cpu.value("name").toString() == "CPU Usage");
    REQUIRE(cpu.value("plugin").toString() == "System Monitor");
}

TEST_CASE("actionStateJson emits the full ActionState shape with defaults", "[opendeck]") {
    QJsonObject const s = actionStateJson(ajazz::core::KeyState{});
    REQUIRE(s.value("background_colour").toString() == "#000000");
    REQUIRE(s.value("colour").toString() == "#f2f2f2");
    REQUIRE(s.value("text").toString() == "");
    REQUIRE(s.value("image").toString() == "");
    REQUIRE(s.value("show").toBool() == true);
    REQUIRE(s.value("size").toInt() == 14);
    REQUIRE(s.value("alignment").toString() == "middle");
}

TEST_CASE("actionStateJson reflects a populated KeyState", "[opendeck]") {
    ajazz::core::KeyState ks;
    ks.text = "ON";
    ks.imagePath = "file:///x.png";
    ks.background = ajazz::core::Rgb{0x12, 0x34, 0x56};
    ks.fontSize = 20;
    QJsonObject const s = actionStateJson(ks);
    REQUIRE(s.value("text").toString() == "ON");
    REQUIRE(s.value("image").toString() == "file:///x.png");
    REQUIRE(s.value("background_colour").toString() == "#123456");
    REQUIRE(s.value("size").toInt() == 20);
}

TEST_CASE("profileJson lays out keys/sliders with null for empty slots", "[opendeck]") {
    ajazz::core::Profile p;
    p.deviceCodename = "akp05e";
    p.name = "Default";
    ajazz::core::Binding b;
    b.state.text = "ON";
    b.onPress.push_back(
        ajazz::core::Action{ajazz::core::ActionKind::Plugin, "com.ajazz.sysmon.cpu", "", "CPU", 0});
    p.keys[0] = b;

    QJsonObject const prof = profileJson(p, 3, 2, 0);
    REQUIRE(prof.value("device").toString() == "akp05e");
    REQUIRE(prof.value("id").toString() == "Default"); // name presented as the OpenDeck id
    QJsonArray const keys = prof.value("keys").toArray();
    REQUIRE(keys.size() == 3);
    REQUIRE(keys.at(0).isObject());
    REQUIRE(keys.at(1).isNull());
    REQUIRE(keys.at(2).isNull());
    QJsonObject const inst = keys.at(0).toObject();
    REQUIRE(inst.value("context").toString() == "akp05e.Default.Keypad.0");
    REQUIRE(inst.value("action").toObject().value("uuid").toString() == "com.ajazz.sysmon.cpu");
    REQUIRE(inst.value("states").toArray().size() == 1);
    REQUIRE(inst.value("states").toArray().at(0).toObject().value("text").toString() == "ON");

    QJsonArray const sliders = prof.value("sliders").toArray();
    REQUIRE(sliders.size() == 2);
    REQUIRE(sliders.at(0).isNull());
    REQUIRE(sliders.at(1).isNull());
}

TEST_CASE("profileJson appends touch-strip zones after the keypad in keys[]", "[opendeck]") {
    ajazz::core::Profile p;
    p.deviceCodename = "akp05e";
    p.name = "Default";
    // A bound touch zone 0 with a visible label.
    ajazz::core::TouchZoneBinding tz;
    tz.state.text = "Vol";
    p.touchZones[0] = tz;

    // keyCount 10 (keypad) + touchCount 4 -> keys[] length 14; zones at 10..13.
    QJsonObject const prof = profileJson(p, 10, 4, 4);
    QJsonArray const keys = prof.value("keys").toArray();
    REQUIRE(keys.size() == 14);
    // keypad slots are empty (null) here...
    REQUIRE(keys.at(0).isNull());
    // ...and the first touch zone (position 10) is the bound instance.
    QJsonObject const touch = keys.at(10).toObject();
    REQUIRE(touch.value("context").toString() == "akp05e.Default.Keypad.10");
    REQUIRE(touch.value("states").toArray().at(0).toObject().value("text").toString() == "Vol");
    REQUIRE(keys.at(11).isNull());
}

TEST_CASE("jsonToString round-trips scalars and containers", "[opendeck]") {
    REQUIRE(jsonToString(QJsonValue(57116)) == "57116");
    REQUIRE(jsonToString(QJsonValue(QStringLiteral("hi"))) == "\"hi\"");
    REQUIRE(jsonToString(QJsonValue(QJsonValue::Null)) == "null");
    QJsonArray a;
    a.append(1);
    a.append(2);
    REQUIRE(jsonToString(a) == "[1,2]");
}
