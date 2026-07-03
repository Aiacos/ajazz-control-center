// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_debug_logging.cpp
 * @brief Unit tests for the pure Qt->core log bridge mappings.
 *
 * Covers the contract that funnels Qt's own diagnostics into the core
 * logger: every QtMsgType maps to a sensible LogLevel, and the
 * AJAZZ_LOG_LEVEL name parser is case-insensitive with a safe fallback.
 * The install() side (global sink swap + qInstallMessageHandler) is a
 * process-wide effect and is exercised at runtime, not here.
 */
#include "debug_logging.hpp"

#include <QString>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("qtMsgTypeToLevel maps every Qt severity", "[debuglog]") {
    using namespace ajazz::app;
    using ajazz::core::LogLevel;

    REQUIRE(qtMsgTypeToLevel(QtDebugMsg) == LogLevel::Debug);
    REQUIRE(qtMsgTypeToLevel(QtInfoMsg) == LogLevel::Info);
    REQUIRE(qtMsgTypeToLevel(QtWarningMsg) == LogLevel::Warn);
    REQUIRE(qtMsgTypeToLevel(QtCriticalMsg) == LogLevel::Error);
    REQUIRE(qtMsgTypeToLevel(QtFatalMsg) == LogLevel::Critical);
}

TEST_CASE("parseLogLevel is case-insensitive with aliases", "[debuglog]") {
    using namespace ajazz::app;
    using ajazz::core::LogLevel;

    REQUIRE(parseLogLevel(QStringLiteral("trace"), LogLevel::Info) == LogLevel::Trace);
    REQUIRE(parseLogLevel(QStringLiteral("DEBUG"), LogLevel::Info) == LogLevel::Debug);
    REQUIRE(parseLogLevel(QStringLiteral(" Info "), LogLevel::Error) == LogLevel::Info);
    REQUIRE(parseLogLevel(QStringLiteral("warning"), LogLevel::Info) == LogLevel::Warn);
    REQUIRE(parseLogLevel(QStringLiteral("warn"), LogLevel::Info) == LogLevel::Warn);
    REQUIRE(parseLogLevel(QStringLiteral("error"), LogLevel::Info) == LogLevel::Error);
    REQUIRE(parseLogLevel(QStringLiteral("crit"), LogLevel::Info) == LogLevel::Critical);
    REQUIRE(parseLogLevel(QStringLiteral("critical"), LogLevel::Info) == LogLevel::Critical);
}

TEST_CASE("parseLogLevel falls back on empty or unknown names", "[debuglog]") {
    using namespace ajazz::app;
    using ajazz::core::LogLevel;

    REQUIRE(parseLogLevel(QString(), LogLevel::Warn) == LogLevel::Warn);
    REQUIRE(parseLogLevel(QStringLiteral("verbose"), LogLevel::Error) == LogLevel::Error);
}

TEST_CASE("defaultLogFilePath ends in the expected log file", "[debuglog]") {
    using namespace ajazz::app;
    auto const path = DebugLogging::defaultLogFilePath();
    REQUIRE(path.endsWith(QStringLiteral("/logs/ajazz-control-center.log")));
}
