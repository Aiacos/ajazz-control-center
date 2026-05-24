// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file node_runner.cpp
 * @brief Implementation of buildNodeArgv, resolveNode20Plus, and makeDefaultNodeProbe.
 *
 * Source of truth for the CLI contract: docs/protocols/streamdeck/akp_plugin_sdk.md §3 step 3.
 * No nlohmann::json (COD-031 boundary). Qt-Core only.
 *
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-02 (PLUGIN-08)
 */
#include "node_runner.hpp"

#include <QProcess>
#include <QStandardPaths>
#include <QVersionNumber>

namespace ajazz::app {

// ---------------------------------------------------------------------------
// makeDefaultNodeProbe
// ---------------------------------------------------------------------------

NodeProbe makeDefaultNodeProbe() {
    NodeProbe probe;

    // findNode: delegate to Qt's cross-platform executable resolver so tests don't
    // need to hand-roll a PATH walk (and Windows .exe resolution is handled correctly).
    probe.findNode = [] { return QStandardPaths::findExecutable(QStringLiteral("node")); };

    // queryVersion: launch `<node> --version` (blocking, 5 s timeout) and return
    // the raw stdout.  Only called when findNode() returned a non-empty path.
    probe.queryVersion = [](QString const& nodeExe) -> QString {
        QProcess proc;
        proc.start(nodeExe, {QStringLiteral("--version")});
        if (!proc.waitForFinished(5000))
            return {};
        return QString::fromUtf8(proc.readAllStandardOutput());
    };

    return probe;
}

// ---------------------------------------------------------------------------
// buildNodeArgv
// ---------------------------------------------------------------------------

QStringList buildNodeArgv(QString const& codePath,
                          quint16 port,
                          QString const& pluginUuid,
                          QString const& infoJson) {
    // akp_plugin_sdk.md §3 step 3 spawn CLI (AJAZZ/Elgato-compatible):
    //   <node> <codePath> -port <port> -pluginUUID <uuid> -registerEvent registerPlugin -info
    //   <info>
    // codePath is argv[0] (Pitfall 3: node binary is passed as the program arg to QProcess,
    // NOT inserted here — use QProcess::start(nodeExe, buildNodeArgv(...))).
    return {
        codePath,
        QStringLiteral("-port"),
        QString::number(port),
        QStringLiteral("-pluginUUID"),
        pluginUuid,
        QStringLiteral("-registerEvent"),
        QStringLiteral("registerPlugin"),
        QStringLiteral("-info"),
        infoJson,
    };
}

// ---------------------------------------------------------------------------
// resolveNode20Plus
// ---------------------------------------------------------------------------

std::optional<QString> resolveNode20Plus(NodeProbe const& probe) {
    // Step 1: locate node on PATH via the injected finder.
    QString const exe = probe.findNode();
    if (exe.isEmpty())
        return std::nullopt; // node not installed -> plugin disabled with notice (caller's concern)

    // Step 2: query the version string (e.g. "v26.0.0\n").
    QString v = probe.queryVersion(exe);

    // Step 3: strip the leading 'v' (node always emits it) and trim whitespace/newline.
    if (v.startsWith(QLatin1Char('v')))
        v = v.mid(1);
    v = v.trimmed();

    // Step 4: parse with QVersionNumber (handles "2.10" > "2.9" correctly unlike string compare).
    QVersionNumber const n = QVersionNumber::fromString(v);

    // Step 5: reject anything below major version 20.
    if (n.majorVersion() < 20)
        return std::nullopt;

    // Step 6: acceptable — return the path.
    return exe;
}

} // namespace ajazz::app
