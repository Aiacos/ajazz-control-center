// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file debug_logging.cpp
 * @brief Implementation of the unified log tee + Qt message-handler bridge.
 */
#include "debug_logging.hpp"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QMessageLogContext>
#include <QStandardPaths>
#include <QString>

#include <string>
#include <utility>
#include <vector>

namespace ajazz::app {
namespace {

/// The active ring sink, kept alive for the process lifetime so the Qt
/// message handler (a plain function pointer, no captures) and any later
/// control channel observe the same buffer. Replaced wholesale by a new
/// install() call.
std::shared_ptr<core::TeeSink> gActiveTee; // NOLINT(*-avoid-non-const-global-variables)

/**
 * @brief Qt -> core bridge. Installed via qInstallMessageHandler so every
 *        qDebug / qWarning / qCDebug(category) record flows through the same
 *        tee as the project's own AJAZZ_LOG_* output.
 *
 * The previous handler is deliberately NOT chained: the tee already owns a
 * stderr leg, so re-invoking Qt's default handler would double-print every
 * line. Fatal messages are forwarded at Critical but Qt still aborts the
 * process afterwards, exactly as before.
 */
void qtMessageHandler(QtMsgType type, QMessageLogContext const& context, QString const& message) {
    // Qt sets category to "default" for uncategorised qDebug/qWarning; keep
    // that verbatim so a reader can tell categorised logs apart.
    char const* category = (context.category != nullptr) ? context.category : "qt";
    core::log(qtMsgTypeToLevel(type), category, message.toStdString());
}

} // namespace

core::LogLevel qtMsgTypeToLevel(QtMsgType type) noexcept {
    switch (type) {
    case QtDebugMsg:
        return core::LogLevel::Debug;
    case QtInfoMsg:
        return core::LogLevel::Info;
    case QtWarningMsg:
        return core::LogLevel::Warn;
    case QtCriticalMsg:
        return core::LogLevel::Error;
    case QtFatalMsg:
        return core::LogLevel::Critical;
    }
    return core::LogLevel::Info;
}

core::LogLevel parseLogLevel(QString const& name, core::LogLevel fallback) noexcept {
    QString const n = name.trimmed().toLower();
    if (n == QStringLiteral("trace")) {
        return core::LogLevel::Trace;
    }
    if (n == QStringLiteral("debug")) {
        return core::LogLevel::Debug;
    }
    if (n == QStringLiteral("info")) {
        return core::LogLevel::Info;
    }
    if (n == QStringLiteral("warn") || n == QStringLiteral("warning")) {
        return core::LogLevel::Warn;
    }
    if (n == QStringLiteral("error")) {
        return core::LogLevel::Error;
    }
    if (n == QStringLiteral("critical") || n == QStringLiteral("crit")) {
        return core::LogLevel::Critical;
    }
    return fallback;
}

core::LogLevel logLevelFromEnv(core::LogLevel fallback) noexcept {
    auto const raw = qEnvironmentVariable("AJAZZ_LOG_LEVEL");
    return parseLogLevel(raw, fallback);
}

std::shared_ptr<core::RingBufferSink>
DebugLogging::install(QString const& logFilePath, core::LogLevel level, std::size_t ringCapacity) {
    auto ring = std::make_shared<core::RingBufferSink>(ringCapacity);

    std::vector<std::shared_ptr<core::LogSink>> sinks;
    sinks.push_back(core::makeStderrSink());

    if (!logFilePath.isEmpty()) {
        // Ensure the parent directory exists; FileSink no-ops on a bad path
        // rather than aborting, but creating it up front means logs actually
        // land on a fresh install.
        QDir().mkpath(QFileInfo(logFilePath).absolutePath());
        auto file = std::make_shared<core::FileSink>(logFilePath.toStdString());
        if (file->isOpen()) {
            sinks.push_back(std::move(file));
        }
    }

    sinks.push_back(ring);

    auto tee = std::make_shared<core::TeeSink>(std::move(sinks));
    gActiveTee = tee; // keep alive for the process lifetime
    core::setLogLevel(level);
    core::setLogSink(std::move(tee));

    // Route Qt's own diagnostics through the same pipeline. Safe to call
    // after the sink is installed; the handler only ever calls core::log.
    qInstallMessageHandler(qtMessageHandler);

    return ring;
}

QString DebugLogging::defaultLogFilePath() {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    return base + QStringLiteral("/logs/ajazz-control-center.log");
}

} // namespace ajazz::app
