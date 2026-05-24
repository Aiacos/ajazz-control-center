// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file plugin_manager.cpp
 * @brief PluginManager implementation — discovery + spawn dispatch + crash lifecycle.
 *
 * Sources (per the plan's read_first list):
 *   - akp_plugin_sdk.md §3: discovery dirs, spawn-by-extension rules, crash policy,
 *     shutdown protocol (exitApp -> terminate(1s) -> kill).
 *   - 18-RESEARCH.md Pattern 3 (crash window) + Pitfall 5 (terminate no-op on Windows).
 *   - 18-RESEARCH.md A2 (infoJson minimal shape) + A3 (exitApp-then-kill required).
 *   - src/app/src/pi_bridge.cpp:66 (isSafeUuidComponent — reuse for UUID path validation).
 *   - SdPluginServer::sendEvent signature confirmed from 17-02-SUMMARY.md.
 *
 * COD-031: QJson/Qt-Core only — no nlohmann::json anywhere in this file.
 * T-18-ARGV: QStringList to QProcess::start(program, args) — never a shell string.
 * T-18-PATHTRAV: isSafeUuidComponent validates any UUID used in a filesystem path.
 * T-18-CHILD-PERSIST: owned QProcess + exitApp-then-terminate-then-kill.
 * T-18-ELEVATE: RunAsAdministrator honoured Windows-only; Linux/macOS log+refuse.
 *
 * Phase: 18-plugin-manifest-discovery-lifecycle-spawn / Plan 18-04 (PLUGIN-07/08)
 */
#include "plugin_manager.hpp"

#include "node_runner.hpp"
#include "plugin_crash_tracker.hpp"
#include "plugin_manifest.hpp"
#include "sdplugin_extractor.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

#include <utility>

#if defined(AJAZZ_HAVE_WEBSOCKETS)
#include "sd_plugin_server.hpp"
#endif

#if defined(AJAZZ_HAVE_WEBENGINE)
#include "plugin_mirabox_shim.hpp"
#include "property_inspector_controller.hpp"
// MAINTAINER NOTE (WR-01): QQuickWebEngineScriptCollection is only forward-declared in the
// public qquickwebengineprofile.h header; its complete type definition lives in the private
// header below. There is no public-API alternative in Qt 6.7 that lets callers call
// userScripts()->insert() without the private header. This is a known Qt API gap.
// If this include fails after a Qt minor-version bump, verify the private header path
// has not moved and update Qt6::WebEngineQuickPrivate in CMakeLists.txt accordingly.
// Tracked against Qt bug tracker for a public QQuickWebEngineScriptCollection declaration.
static_assert(QT_VERSION >= QT_VERSION_CHECK(6, 7, 0),
              "qquickwebenginescriptcollection_p.h layout may have changed; verify include path");
#include <QtWebEngineQuick/private/qquickwebenginescriptcollection_p.h>
#endif

namespace ajazz::app {

// ---------------------------------------------------------------------------
// isSafeUuidComponent — reuse from pi_bridge.cpp:66 (T-18-PATHTRAV mitigation).
// Declared here as a local helper to avoid exposing pi_bridge's private namespace.
// The logic is identical to the original.
// ---------------------------------------------------------------------------
/// isSafeUuidComponent — reuse from pi_bridge.cpp:66 (T-18-PATHTRAV mitigation).
/// Validates any plugin UUID before using it as a filesystem path component.
[[nodiscard]] static bool isSafeUuidComponent(QString const& s) {
    if (s.isEmpty() || s.size() > 256) {
        return false;
    }
    for (QChar const c : s) {
        ushort const u = c.unicode();
        if (u < 0x20 || u == 0x7f) {
            return false;
        }
        if (c == QLatin1Char('/') || c == QLatin1Char('\\')) {
            return false;
        }
    }
    if (s == QLatin1String(".") || s == QLatin1String("..")) {
        return false;
    }
    if (s.contains(QLatin1String(".."))) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PluginManager::PluginManager(QString const& pluginsDir,
                             SdPluginServer* server,
                             NodeProbe probe,
                             PropertyInspectorController* piController,
                             std::function<qint64()> clock,
                             QObject* parent)
    : QObject(parent), m_pluginsDir(pluginsDir), m_server(server), m_probe(std::move(probe)),
      m_piController(piController), m_clock(clock ? std::move(clock) : []() -> qint64 {
          return QDateTime::currentMSecsSinceEpoch();
      }) {}

PluginManager::~PluginManager() {
    shutdown();
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

QString PluginManager::resolveCodePath(PluginManifest const& manifest) {
#if defined(Q_OS_WIN)
    if (!manifest.codePathWin.isEmpty()) {
        return manifest.codePathWin;
    }
#elif defined(Q_OS_MACOS)
    if (!manifest.codePathMac.isEmpty()) {
        return manifest.codePathMac;
    }
#endif
    return manifest.codePath;
}

QString PluginManager::buildInfoJson() {
    // Minimal Elgato application-info envelope.
    // Shape: {application:{version,platform},devicePixelRatio:1,devices:[]}.
    // Source: 18-RESEARCH.md A2 — exact shape pinned against a real package in Phase 25;
    //         this minimal form is sufficient for the plugin host to identify itself.
    QJsonObject app;
    app[QStringLiteral("version")] = QCoreApplication::applicationVersion();
    app[QStringLiteral("platform")] = currentPlatformString();

    QJsonObject envelope;
    envelope[QStringLiteral("application")] = app;
    envelope[QStringLiteral("devicePixelRatio")] = 1;
    envelope[QStringLiteral("devices")] = QJsonArray{};

    return QString::fromUtf8(QJsonDocument(envelope).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// discover()
// ---------------------------------------------------------------------------

std::vector<PluginManifest> PluginManager::discover() {
    // Step 1: extract any leftover *.sdPlugin archives (REUSE — zip-slip-guarded).
    extractStandalonePluginArchives(m_pluginsDir);

    // Step 2: scan pluginsDir for <x>.sdPlugin directories containing manifest.json.
    QDir const dir(m_pluginsDir);
    std::vector<PluginManifest> runnable;

    for (auto const& entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!entry.endsWith(QStringLiteral(".sdPlugin"), Qt::CaseInsensitive)) {
            continue;
        }
        QString const manifestPath =
            m_pluginsDir + QLatin1Char('/') + entry + QLatin1String("/manifest.json");
        QFile f(manifestPath);
        if (!f.open(QIODevice::ReadOnly)) {
            qWarning("PluginManager: cannot open manifest at %s", qPrintable(manifestPath));
            continue;
        }
        // IN-01: size cap before readAll() — guard against multi-megabyte or corrupt manifests.
        // The plugins directory is nominally trusted but is user-configurable; an oversized
        // manifest.json would be fully read into memory before the parser could reject it.
        constexpr qint64 kMaxManifestBytes =
            1LL * 1024 * 1024; // 1 MiB is generous for any real manifest
        if (f.size() > kMaxManifestBytes) {
            qWarning("PluginManager: manifest at %s is suspiciously large (%lld bytes); skipping",
                     qPrintable(manifestPath),
                     static_cast<long long>(f.size()));
            f.close();
            continue;
        }
        QByteArray const data = f.readAll();
        f.close();

        // Step 3: parse manifest.
        auto opt = parsePluginManifest(data);
        if (!opt.has_value()) {
            qWarning("PluginManager: failed to parse manifest at %s", qPrintable(manifestPath));
            continue;
        }

        // Step 4: apply runnability gate.
        if (!manifestRunnableHere(
                *opt, currentPlatformString(), QCoreApplication::applicationVersion())) {
            qWarning("PluginManager: skipping %s (not runnable on this platform/version)",
                     qPrintable(opt->name));
            continue;
        }

        runnable.push_back(std::move(*opt));
    }

    return runnable;
}

// ---------------------------------------------------------------------------
// spawn()
// ---------------------------------------------------------------------------

void PluginManager::spawn(PluginManifest const& manifest) {
    // Validate the code path as a filesystem path component (T-18-PATHTRAV).
    // The manifest's CodePath / Name is used as a key in m_live and potentially
    // in per-plugin settings directories. Reject any value that could traverse
    // path boundaries.
    QString const pluginId = manifest.codePath.isEmpty() ? manifest.name : manifest.codePath;
    if (!isSafeUuidComponent(pluginId)) {
        qWarning("PluginManager: rejecting plugin with unsafe UUID/path component: '%s'",
                 qPrintable(pluginId));
        return;
    }

    // CR-03: Validate the ACTUAL resolved code path (not just pluginId).
    // resolveCodePath() may return codePathWin or codePathMac which bypass the
    // isSafeUuidComponent check above. Reject any path containing directory separators
    // or traversal components regardless of which platform-override was selected
    // (T-18-PATHTRAV bypass fix).
    QString const code = resolveCodePath(manifest);
    if (code.isEmpty() || code.contains(QLatin1Char('/')) || code.contains(QLatin1Char('\\')) ||
        code.contains(QLatin1String(".."))) {
        qWarning("PluginManager: rejecting plugin '%s': resolved code path '%s' is unsafe "
                 "(contains directory separator or traversal component)",
                 qPrintable(manifest.name),
                 qPrintable(code));
        return;
    }

    // WR-03: Use manifest.puuid as the plugin identity UUID passed to the child process
    // (akp_plugin_sdk.md §2 PUUID field). Fall back to pluginId (codePath-based key) only
    // when PUUID is absent. Validate puuid with the same guard when non-empty.
    QString const pluginUuid = manifest.puuid.isEmpty() ? pluginId : manifest.puuid;
    if (!manifest.puuid.isEmpty() && !isSafeUuidComponent(pluginUuid)) {
        qWarning("PluginManager: rejecting plugin '%s': PUUID '%s' is unsafe",
                 qPrintable(manifest.name),
                 qPrintable(pluginUuid));
        return;
    }

    QString const ext = QFileInfo(code).suffix().toLower();

    if (ext == QLatin1String("js") || ext == QLatin1String("mjs") || ext == QLatin1String("cjs")) {
        // Node.js dispatch (akp_plugin_sdk.md §3).
        auto const nodeExeOpt = resolveNode20Plus(m_probe);
        if (!nodeExeOpt.has_value()) {
            disableWithNotice(pluginId, QStringLiteral("Node.js >= 20 not found"));
            return;
        }

        QString const infoJson = buildInfoJson();
        quint16 port = 0;
#if defined(AJAZZ_HAVE_WEBSOCKETS)
        if (m_server) {
            port = m_server->serverPort();
        }
#endif
        // Pass pluginUuid (PUUID when available) as the -pluginUUID argument (WR-03).
        QStringList const argv = buildNodeArgv(code, port, pluginUuid, infoJson);

        // Store argv for test assertions (no process launched in tests via inject).
        m_lastNodeArgv[pluginId] = argv;

        auto proc = std::make_unique<QProcess>();
        QProcess* rawProc = proc.get();

        // Wire crash signals BEFORE start (owned QProcess — crash signals require ownership).
        connect(rawProc, &QProcess::errorOccurred, this, [this, pluginId](QProcess::ProcessError) {
            onProcessFailed(pluginId);
        });
        connect(rawProc,
                &QProcess::finished,
                this,
                [this, pluginId](int exitCode, QProcess::ExitStatus status) {
                    if (status == QProcess::CrashExit || exitCode != 0) {
                        onProcessFailed(pluginId);
                    }
                });

        // T-18-ARGV: QStringList passed to QProcess::start(program, args) — no shell.
        rawProc->start(*nodeExeOpt, argv);
        m_live.emplace(std::piecewise_construct,
                       std::forward_as_tuple(pluginId),
                       std::forward_as_tuple(manifest, std::move(proc)));

    } else if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
        // HTML / WebEngine path (PLUGIN-08, PLUGIN-11).
#if defined(AJAZZ_HAVE_WEBENGINE)
        if (m_piController) {
            // Attach Mirabox shim to the per-plugin profile before loading index.html.
            // akp_plugin_sdk.md §9 compat shim + Pattern 2 from 18-RESEARCH.md.
            // userScripts() returns QQuickWebEngineScriptCollection* (Qt 6 API;
            // scripts() does not exist on QQuickWebEngineProfile).
            auto* profile = m_piController->activeProfile();
            if (profile) {
                profile->userScripts()->insert(makeMiraboxShim());
            }
            // NOTE: in-process HTML plugin page-load (the Chromium view that renders
            // the plugin's index.html as its main UI, not the per-action PI settings
            // panel) requires the Phase-19 device<->plugin bridge surface and the
            // Phase-20 WebEngine view-routing work. PropertyInspectorController::
            // loadInspector() is the Phase-20 per-action PI loader (4-arg API:
            // pluginUuid, htmlAbsPath, actionUuid, contextUuid) — calling it here with
            // placeholder args would be a semantic misuse and would corrupt the active
            // PI state. The plugin is registered in m_live below so lifecycle tracking
            // (crash, shutdown, exitApp) is fully active; only the Chromium page-load
            // is deferred. (Phase-19/20 will wire the plugin main view once the bridge
            // surface is ready.)
            qInfo("PluginManager: HTML plugin '%s' registered in m_live; "
                  "in-process WebEngine page-load deferred to Phase 19/20 bridge work",
                  qPrintable(manifest.name));
        }
#else
        qWarning("PluginManager: HTML plugin '%s' requires WebEngine (not available)",
                 qPrintable(manifest.name));
#endif
        // HTML plugins don't have an owned QProcess (they run in-process via Chromium).
        m_live.emplace(std::piecewise_construct,
                       std::forward_as_tuple(pluginId),
                       std::forward_as_tuple(manifest, nullptr));

    } else {
        // Native QProcess dispatch (.exe / .app / no extension).
        // T-18-ELEVATE: RunAsAdministrator honoured ONLY on Windows.

#if defined(Q_OS_WIN)
        if (manifest.runAsAdministrator) {
            // Windows UAC elevation via ShellExecuteEx("runas").
            // For now log + start normally; full UAC integration is Phase 22.
            qInfo("PluginManager: RunAsAdministrator requested for '%s' (Windows)",
                  qPrintable(manifest.name));
        }
#else
        if (manifest.runAsAdministrator) {
            // Linux/macOS: log and refuse elevation — no silent elevation path (T-18-ELEVATE).
            qWarning("PluginManager: RunAsAdministrator requested for '%s' but platform is "
                     "Linux/macOS — elevation refused; starting unelevated",
                     qPrintable(manifest.name));
        }
#endif
        auto proc = std::make_unique<QProcess>();
        QProcess* rawProc = proc.get();

        connect(rawProc, &QProcess::errorOccurred, this, [this, pluginId](QProcess::ProcessError) {
            onProcessFailed(pluginId);
        });
        connect(rawProc,
                &QProcess::finished,
                this,
                [this, pluginId](int exitCode, QProcess::ExitStatus status) {
                    if (status == QProcess::CrashExit || exitCode != 0) {
                        onProcessFailed(pluginId);
                    }
                });

        // T-18-ARGV: start(program, args) — no shell concatenation.
        rawProc->start(code, QStringList{});
        m_live.emplace(std::piecewise_construct,
                       std::forward_as_tuple(pluginId),
                       std::forward_as_tuple(manifest, std::move(proc)));
    }
}

// ---------------------------------------------------------------------------
// onProcessFailed()
// ---------------------------------------------------------------------------

void PluginManager::onProcessFailed(QString const& uuid) {
    qint64 const now = m_clock();
    m_crashTracker.recordCrash(uuid, now);

    if (m_crashTracker.shouldDisable(uuid, now)) {
        // 3-in-30s: disable + surface (akp_plugin_sdk.md §3 crash policy).
        disableWithNotice(uuid, QStringLiteral("crashed 3 times within 30 seconds"));
        // Clean up process but do NOT re-spawn.
        m_live.erase(uuid);
    } else {
        // Fewer/slower crashes: restart (teardown + re-spawn).
        // WR-02: guard against re-spawning HTML/WebEngine plugins (process == nullptr in m_live).
        // An HTML plugin runs in-process via Chromium and has no owned QProcess. Re-spawning it
        // would re-inject the Mirabox shim script, causing duplicate inserts into the WebEngine
        // profile. Only restart plugins that have an actual process-backed entry.
        auto it = m_live.find(uuid);
        if (it != m_live.end() && it->second.process != nullptr) {
            PluginManifest const manifest = it->second.manifest;
            m_live.erase(it);
            spawn(manifest);
        }
    }
}

// ---------------------------------------------------------------------------
// disableWithNotice()
// ---------------------------------------------------------------------------

void PluginManager::disableWithNotice(QString const& uuid, QString const& reason) {
    qWarning("PluginManager: disabling plugin '%s': %s", qPrintable(uuid), qPrintable(reason));
    m_disabled[uuid] = reason;
    emit pluginDisabled(uuid, reason);
}

// ---------------------------------------------------------------------------
// isDisabled()
// ---------------------------------------------------------------------------

bool PluginManager::isDisabled(QString const& uuid) const {
    return m_disabled.contains(uuid);
}

// ---------------------------------------------------------------------------
// lastNodeArgvForTesting()
// ---------------------------------------------------------------------------

QStringList PluginManager::lastNodeArgvForTesting(QString const& uuid) const {
    return m_lastNodeArgv.value(uuid);
}

// ---------------------------------------------------------------------------
// shutdown()
// ---------------------------------------------------------------------------

void PluginManager::shutdown() {
    // Source: akp_plugin_sdk.md §3 shutdown + Pitfall 5 (terminate is a no-op for
    // windowless Windows children; exitApp allows self-cleanup; final kill prevents zombies).
    for (auto& [uuid, livePlugin] : m_live) {
        // Step 1: send exitApp so the plugin can persist state (Phase-17 sendEvent seam).
        // A closed socket returns false — silently ignored (shutdown continues).
        // akp_plugin_sdk.md §3 shutdown + Pitfall 5.
#if defined(AJAZZ_HAVE_WEBSOCKETS)
        if (m_server) {
            m_server->sendEvent(uuid, QStringLiteral("exitApp"));
        }
#endif

        // Steps 2 + 3: terminate (1 s grace) then kill.
        if (livePlugin.process) {
            livePlugin.process->terminate();
            if (!livePlugin.process->waitForFinished(1000)) {
                livePlugin.process->kill();
            }
        }
    }
    m_live.clear();
}

} // namespace ajazz::app
