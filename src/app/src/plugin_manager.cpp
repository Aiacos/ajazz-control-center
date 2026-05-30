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
#include <QProcessEnvironment>
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

#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineScriptCollection>
#include <QUrl>
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

// Emulated Stream Deck app version advertised to plugins and used for the
// Software.MinimumVersion runnability gate. A manifest's MinimumVersion refers
// to the Elgato Stream Deck app (e.g. "4.1", "6.5"), NOT to this app's version,
// so gating against our own 0.1.0 would refuse every real plugin. We emulate
// the SD v6 plugin API surface; advertise a generous v6 version so v4/v5/v6
// plugins pass. (A plugin requiring a strictly newer SD than this is genuinely
// out of scope and correctly skipped.)
static constexpr char kEmulatedSdVersion[] = "6.9";

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
// buildChildEnv() — minimal allowlist environment for plugin children (T-18-CHILD-ENV).
// Only propagates variables the plugin runtime legitimately needs. Critically EXCLUDES:
//   - DBUS_SESSION_BUS_ADDRESS (dbus socket access, host resource leak)
//   - XDG_RUNTIME_DIR (user runtime dir, Wayland sockets, etc.)
//   - any *_TOKEN / *_SECRET / *_KEY / *_PASSWORD variables (CI and shell credentials)
//   - DBUS_* family (session bus address variants)
// ---------------------------------------------------------------------------
[[nodiscard]] static QProcessEnvironment buildChildEnv() {
    // QProcessEnvironment() default-constructs an empty environment (Qt 6.7 compatible).
    // Qt 6.8+ renamed this to QProcessEnvironment::empty() but the default ctor is stable.
    QProcessEnvironment childEnv;
    QProcessEnvironment const sysEnv = QProcessEnvironment::systemEnvironment();
    // Explicitly-allowed keys only (T-18-CHILD-ENV mitigation).
    for (auto const* key : {
             "PATH",
             "HOME",
             "TMPDIR",
             "TEMP",
             "TMP",
             "LANG",
             "LC_ALL",
             "LC_CTYPE",
             "DISPLAY",         // X11 display — node plugins that open windows need this
             "WAYLAND_DISPLAY", // Wayland display socket name
             // Intentionally NOT forwarded: DBUS_SESSION_BUS_ADDRESS, XDG_RUNTIME_DIR,
             // or any *_TOKEN/*_SECRET/*_KEY/*_PASSWORD credentials.
         }) {
        QString const val = sysEnv.value(QLatin1String(key));
        if (!val.isEmpty()) {
            childEnv.insert(QLatin1String(key), val);
        }
    }
    return childEnv;
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
    // The advertised version is the EMULATED Stream Deck app version (not our own
    // app version): plugins compare against the Stream Deck app, so reporting our
    // 0.1.0 would make version-gated plugins refuse to run. See kEmulatedSdVersion.
    QJsonObject app;
    app[QStringLiteral("version")] = QString::fromLatin1(kEmulatedSdVersion);
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

        // Step 4: apply runnability gate. Pass the emulated Stream Deck version
        // (not our app version) so a plugin's Software.MinimumVersion (an Elgato
        // SD-app requirement) is compared against the right axis.
        if (!manifestRunnableHere(
                *opt, currentPlatformString(), QString::fromLatin1(kEmulatedSdVersion))) {
            qWarning("PluginManager: skipping %s (not runnable on this platform/version)",
                     qPrintable(opt->name));
            continue;
        }

        // Record the source directory so spawn() can run the child with the
        // plugin dir as its working directory (relative CodePath + resources).
        opt->sourceDir = m_pluginsDir + QLatin1Char('/') + entry;
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
    // Stable plugin id used as the m_live key and -pluginUUID fallback. Derive
    // it from a path-free identifier (PUUID, else the .sdPlugin directory name,
    // else Name) rather than the CodePath: real plugins use subdir CodePaths
    // like "plugin/main.html", which are NOT valid single path components.
    QString const pluginId =
        !manifest.puuid.isEmpty()
            ? manifest.puuid
            : (!manifest.sourceDir.isEmpty() ? QFileInfo(manifest.sourceDir).fileName()
                                             : manifest.name);
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
    // Allow forward-slash SUBDIRECTORIES (real plugins ship CodePath like
    // "plugin/main.html" or "bin/index.js"); the child runs with sourceDir as
    // CWD so a relative subpath stays inside the plugin dir. Still reject
    // traversal (".."), absolute paths (leading '/'), and backslashes
    // (Windows separators / traversal). (T-18-PATHTRAV.)
    if (code.isEmpty() || code.startsWith(QLatin1Char('/')) || code.contains(QLatin1Char('\\')) ||
        code.contains(QLatin1String(".."))) {
        qWarning("PluginManager: rejecting plugin '%s': resolved code path '%s' is unsafe "
                 "(absolute, backslash, or traversal component)",
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

        // Run with the plugin dir as CWD so a relative CodePath (e.g.
        // "plugin.cjs") and the plugin's own relative resource paths resolve.
        // Without this the child runs from the app CWD, cannot find its entry
        // point, exits non-zero, and the crash tracker disables it.
        if (!manifest.sourceDir.isEmpty()) {
            rawProc->setWorkingDirectory(manifest.sourceDir);
        }

        // T-18-CHILD-ENV: apply explicit allowlist env — do NOT inherit full host env.
        rawProc->setProcessEnvironment(buildChildEnv());

        // CR-02: guard against double-fire of onProcessFailed on FailedToStart.
        // Qt emits both errorOccurred(FailedToStart) AND finished(-2, CrashExit).
        // Route FailedToStart exclusively through finished; errorOccurred handles only
        // other errors that do not produce a finished signal.
        connect(
            rawProc, &QProcess::errorOccurred, this, [this, pluginId](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) {
                    // FailedToStart: Qt also fires finished(CrashExit) — handle there only.
                    onProcessFailed(pluginId);
                }
            });
        connect(rawProc,
                &QProcess::finished,
                this,
                [this, pluginId](int exitCode, QProcess::ExitStatus status) {
                    // Covers FailedToStart (emits finished(-2, CrashExit)) and abnormal exits.
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
        // Load the plugin's index.html in a headless Chromium page and invoke
        // its connectElgatoStreamDeckSocket() entry point so it registers over
        // the loopback WebSocket exactly like a Node plugin (akp_plugin_sdk.md
        // §3 HTML run mode). The shared profile carries the Mirabox compat shim
        // (connectMiraBoxSDSocket alias) installed at document creation.
        quint16 htmlPort = 0;
#if defined(AJAZZ_HAVE_WEBSOCKETS)
        if (m_server) {
            htmlPort = m_server->serverPort();
        }
#endif
        QString const infoJson = buildInfoJson();
        QString const htmlAbs =
            manifest.sourceDir.isEmpty() ? code : manifest.sourceDir + QLatin1Char('/') + code;

        if (!m_htmlProfile) {
            m_htmlProfile =
                std::make_unique<QWebEngineProfile>(QStringLiteral("ajazz-html-plugins"));
            m_htmlProfile->scripts()->insert(makeMiraboxShim());
        }
        auto page = std::make_unique<QWebEnginePage>(m_htmlProfile.get());
        QWebEnginePage* rawPage = page.get();
        QString const regUuid = pluginUuid;
        QString const pname = manifest.name;
        connect(rawPage,
                &QWebEnginePage::loadFinished,
                this,
                [rawPage, htmlPort, regUuid, infoJson, pname](bool ok) {
                    if (!ok) {
                        qWarning("PluginManager: HTML plugin '%s' page failed to load",
                                 qPrintable(pname));
                        return;
                    }
                    // The plugin's JS defines connectElgatoStreamDeckSocket (or the
                    // connectSocket alias). inInfo is a JSON *string* the plugin
                    // JSON.parses; JSON.stringify(<infoJson literal>) yields it.
                    QString const js =
                        QStringLiteral("(function(){var f=window.connectElgatoStreamDeckSocket||"
                                       "window.connectSocket;if(typeof f==='function'){"
                                       "f(%1,'%2','registerPlugin',JSON.stringify(%3));}})();")
                            .arg(QString::number(htmlPort), regUuid, infoJson);
                    rawPage->runJavaScript(js);
                });
        rawPage->load(QUrl::fromLocalFile(htmlAbs));
        m_htmlPages.push_back(std::move(page));
        qInfo("PluginManager: HTML plugin '%s' loading %s (port=%u)",
              qPrintable(manifest.name),
              qPrintable(htmlAbs),
              static_cast<unsigned>(htmlPort));
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

        // Run with the plugin dir as CWD so a relative CodePath (e.g.
        // "plugin.cjs") and the plugin's own relative resource paths resolve.
        // Without this the child runs from the app CWD, cannot find its entry
        // point, exits non-zero, and the crash tracker disables it.
        if (!manifest.sourceDir.isEmpty()) {
            rawProc->setWorkingDirectory(manifest.sourceDir);
        }

        // T-18-CHILD-ENV: apply explicit allowlist env — do NOT inherit full host env.
        rawProc->setProcessEnvironment(buildChildEnv());

        // CR-02: guard against double-fire of onProcessFailed on FailedToStart.
        connect(
            rawProc, &QProcess::errorOccurred, this, [this, pluginId](QProcess::ProcessError err) {
                if (err != QProcess::FailedToStart) {
                    onProcessFailed(pluginId);
                }
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
// buildChildEnvironmentForTesting()
// ---------------------------------------------------------------------------

QProcessEnvironment PluginManager::buildChildEnvironmentForTesting() {
    return buildChildEnv();
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
