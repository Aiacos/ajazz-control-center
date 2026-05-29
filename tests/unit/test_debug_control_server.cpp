// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_debug_control_server.cpp
 * @brief Unit tests for the JSON-RPC framing + dispatch of the debug control
 *        channel.
 *
 * processRequestLine() is the heart of the protocol and is synchronous, so
 * the request->response contract (ok/result, error shape, id echo, malformed
 * input, unknown method, handler-reported errors) is covered here without a
 * live socket. The QLocalServer transport is thin Qt glue exercised at
 * runtime.
 */
#include "debug_control_server.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <algorithm>

#include <catch2/catch_test_macros.hpp>

namespace {

QJsonObject decode(QByteArray const& line) {
    return QJsonDocument::fromJson(line.trimmed()).object();
}

} // namespace

TEST_CASE("control server answers the built-in ping", "[debugctl]") {
    ajazz::app::DebugControlServer server;
    auto const resp = decode(
        server.processRequestLine(R"({"id": 7, "method": "ping", "params": {"echo": "hi"}})"));

    REQUIRE(resp.value("id").toInt() == 7);
    REQUIRE(resp.value("ok").toBool());
    auto const result = resp.value("result").toObject();
    REQUIRE(result.value("pong").toBool());
    REQUIRE(result.value("echo").toString() == QStringLiteral("hi"));
}

TEST_CASE("control server lists registered methods", "[debugctl]") {
    ajazz::app::DebugControlServer server;
    server.registerMethod("zeta.do", [](QJsonObject const&, QString&) { return QJsonObject{}; });

    auto const names = server.methodNames();
    // Built-ins plus the registered one, sorted.
    REQUIRE(names.contains(QStringLiteral("ping")));
    REQUIRE(names.contains(QStringLiteral("methods")));
    REQUIRE(names.contains(QStringLiteral("zeta.do")));
    REQUIRE(std::is_sorted(names.begin(), names.end()));
}

TEST_CASE("control server dispatches a custom method with params", "[debugctl]") {
    ajazz::app::DebugControlServer server;
    server.registerMethod("math.double", [](QJsonObject const& params, QString&) {
        return QJsonObject{{"value", params.value("n").toInt() * 2}};
    });

    auto const resp = decode(
        server.processRequestLine(R"({"id": 1, "method": "math.double", "params": {"n": 21}})"));
    REQUIRE(resp.value("ok").toBool());
    REQUIRE(resp.value("result").toObject().value("value").toInt() == 42);
}

TEST_CASE("control server reports handler-raised errors as ok:false", "[debugctl]") {
    ajazz::app::DebugControlServer server;
    server.registerMethod("boom", [](QJsonObject const&, QString& error) {
        error = QStringLiteral("kaboom");
        return QJsonObject{{"ignored", true}};
    });

    auto const resp = decode(server.processRequestLine(R"({"id": 2, "method": "boom"})"));
    REQUIRE(resp.value("id").toInt() == 2);
    REQUIRE_FALSE(resp.value("ok").toBool());
    REQUIRE(resp.value("error").toString() == QStringLiteral("kaboom"));
    REQUIRE_FALSE(resp.contains("result"));
}

TEST_CASE("control server rejects an unknown method", "[debugctl]") {
    ajazz::app::DebugControlServer server;
    auto const resp = decode(server.processRequestLine(R"({"id": 3, "method": "nope"})"));
    REQUIRE_FALSE(resp.value("ok").toBool());
    REQUIRE(resp.value("error").toString().contains(QStringLiteral("unknown method")));
}

TEST_CASE("control server rejects malformed JSON", "[debugctl]") {
    ajazz::app::DebugControlServer server;
    auto const resp = decode(server.processRequestLine("{not json"));
    REQUIRE_FALSE(resp.value("ok").toBool());
    REQUIRE(resp.value("error").toString().contains(QStringLiteral("malformed")));
}

TEST_CASE("control server rejects a request with no method", "[debugctl]") {
    ajazz::app::DebugControlServer server;
    auto const resp = decode(server.processRequestLine(R"({"id": 5, "params": {}})"));
    REQUIRE_FALSE(resp.value("ok").toBool());
    REQUIRE(resp.value("error").toString().contains(QStringLiteral("missing 'method'")));
}

TEST_CASE("control server ignores a blank line", "[debugctl]") {
    ajazz::app::DebugControlServer server;
    REQUIRE(server.processRequestLine("   ").isEmpty());
}

TEST_CASE("control server enable gate follows the environment", "[debugctl]") {
    qunsetenv("AJAZZ_DEBUG_CONTROL");
    REQUIRE_FALSE(ajazz::app::DebugControlServer::enabledFromEnv());
    qputenv("AJAZZ_DEBUG_CONTROL", "1");
    REQUIRE(ajazz::app::DebugControlServer::enabledFromEnv());
    qunsetenv("AJAZZ_DEBUG_CONTROL");
}

TEST_CASE("control server honours an explicit socket path in the env", "[debugctl]") {
    qputenv("AJAZZ_DEBUG_CONTROL", "/tmp/custom-ajazz.sock");
    REQUIRE(ajazz::app::DebugControlServer::defaultSocketPath() ==
            QStringLiteral("/tmp/custom-ajazz.sock"));
    qunsetenv("AJAZZ_DEBUG_CONTROL");
}
