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
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QSysInfo>
#include <QTimer>

#include <algorithm>
#include <utility>

#if defined(Q_OS_LINUX)
#include <csignal>

#include <sys/prctl.h>
#include <unistd.h>
#endif

#if defined(AJAZZ_HAVE_WEBSOCKETS)
#include "sd_plugin_server.hpp"
#endif

#if defined(AJAZZ_HAVE_WEBENGINE)
#include "plugin_mirabox_shim.hpp"

#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineScriptCollection>
#include <QUrl>
// NOTE: no QtWebEngineQuick PRIVATE headers here. The HTML-plugin shim is
// injected through the PUBLIC WebEngineCore API (m_htmlProfile->scripts() —
// QWebEngineProfile::scripts() returns the public QWebEngineScriptCollection).
// A leftover include of qquickwebenginescriptcollection_p.h — needed only for
// the QML-side QQuickWebEngineProfile::userScripts(), which nothing calls —
// broke every aqt-provisioned CI/packaging leg (Release run 28623566703): aqt's
// qtwebengine module ships the Qt6WebEngineQuickPrivate CMake package but NOT
// the private headers themselves.
#endif

namespace ajazz::app {

// The emulated Stream Deck app version moved to plugin_manifest.cpp
// (emulatedStreamDeckVersion()) so the spawn gate and the installedActions
// picker share ONE authority — they diverged once (picker used the real app
// version 0.1.x and hid actions of plugins that were happily running).

// ---------------------------------------------------------------------------
// isSafeUuidComponent — UUID path-component validation (T-18-PATHTRAV mitigation).
// ---------------------------------------------------------------------------
/// isSafeUuidComponent — T-18-PATHTRAV mitigation.
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
// QSettings key helpers for the persisted user-disable set (D-27-4)
// ---------------------------------------------------------------------------
//
// Key pattern: plugins/disabled/<pluginId>  (value: bool true when disabled)
// Uses the DEFAULT QSettings scope (org/app already set globally by Application).
// QStandardPaths::setTestModeEnabled(true) in tests isolates the file to a
// temporary location, ensuring hermeticity without a custom path.
//
// SEPARATE from m_disabled (session-only crash-disable path) — a user-disable
// survives app restart; a crash-disable recovers on next launch (T-27-DISABLE-LEAK).

static constexpr char kDisabledGroup[] = "plugins/disabled";

/// Construct the full QSettings key for a pluginId.
[[nodiscard]] static QString disabledKey(QString const& pluginId) {
    return QStringLiteral("%1/%2").arg(QString::fromLatin1(kDisabledGroup), pluginId);
}

// ---------------------------------------------------------------------------
// shouldSkipSpawn() — single shared predicate for launch loop + rediscover()
// ---------------------------------------------------------------------------

bool PluginManager::shouldSkipSpawn(QString const& pluginId) {
    QSettings settings;
    return settings.value(disabledKey(pluginId), false).toBool();
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PluginManager::PluginManager(QString const& pluginsDir,
                             SdPluginServer* server,
                             NodeProbe probe,
                             std::function<qint64()> clock,
                             QObject* parent)
    : QObject(parent), m_pluginsDir(pluginsDir), m_server(server), m_probe(std::move(probe)),
      m_clock(clock ? std::move(clock)
                    : []() -> qint64 { return QDateTime::currentMSecsSinceEpoch(); }) {}

PluginManager::~PluginManager() {
    shutdown();
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

QString PluginManager::resolveCodePath(PluginManifest const& manifest) {
    // Delegates to the shared resolver in plugin_manifest.cpp so non-spawn
    // callers (installedActions picker) apply the IDENTICAL platform rule.
    return resolveEffectiveCodePath(manifest);
}

void PluginManager::setDevicesInfoProvider(std::function<QJsonArray()> provider) {
    m_devicesInfoProvider = std::move(provider);
}

QString PluginManager::buildInfoJson(PluginManifest const& manifest) const {
    // Full Elgato RegistrationInfo envelope (F1, PLUGIN-GAP-ANALYSIS). The prior
    // minimal {application:{version,platform},devices:[]} broke version- and
    // device-gated real .sdPlugin plugins, which read application.version,
    // plugin{}, and a populated devices[] at startup. Shape pinned against the
    // open-source SDK; see docs/protocols/streamdeck/elgato_plugin_protocol.md §2.3.

    // application: the EMULATED Stream Deck app identity (NOT our own app
    // version — plugins gate Software.MinimumVersion against the SD app, so
    // reporting 0.1.x would make them refuse to run). version is the 4-part
    // Elgato format ("6.9.0.0").
    QJsonObject app;
    app[QStringLiteral("font")] = QStringLiteral("Liberation Sans");
    // BCP-47 left part (e.g. "en" from "en_US"); Elgato uses a short language code.
    app[QStringLiteral("language")] = QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    app[QStringLiteral("platform")] = currentPlatformString();
    app[QStringLiteral("platformVersion")] = QSysInfo::productVersion();
    app[QStringLiteral("version")] = emulatedStreamDeckVersion() + QStringLiteral(".0.0");

    // colors: the standard Stream Deck dark-theme palette (8-digit RGBA hex).
    // Property Inspectors read these to match the host chrome.
    QJsonObject const colors{
        {QStringLiteral("buttonMouseOverBackgroundColor"), QStringLiteral("#464646FF")},
        {QStringLiteral("buttonPressedBackgroundColor"), QStringLiteral("#303030FF")},
        {QStringLiteral("buttonPressedBorderColor"), QStringLiteral("#646464FF")},
        {QStringLiteral("buttonPressedTextColor"), QStringLiteral("#969696FF")},
        {QStringLiteral("highlightColor"), QStringLiteral("#0090FFFF")},
    };

    // plugin: per-plugin identity. The plugin UUID is the PUUID alias when set,
    // else the bare .sdPlugin directory name (the canonical Elgato manifest UUID;
    // mirrors the pluginId derivation in spawn()), else the manifest Name. version
    // is the manifest Version. A plugin reads plugin.uuid to address its own
    // global settings / deep links.
    QString pluginUuid = manifest.puuid;
    if (pluginUuid.isEmpty()) {
        if (!manifest.sourceDir.isEmpty()) {
            pluginUuid = QFileInfo(manifest.sourceDir).fileName();
            if (pluginUuid.endsWith(QStringLiteral(".sdPlugin"))) {
                pluginUuid.chop(static_cast<int>(QStringLiteral(".sdPlugin").size()));
            }
        } else {
            pluginUuid = manifest.name;
        }
        // MiraBox SDVueSDK compatibility (RE 2026-07-02, docs/protocols/
        // streamdeck/mirabox_html_plugin_contract.md): SDVueSDK bundles compute
        // each action's dispatch gate as `info.plugin.uuid + ".actionN"` and
        // silently DROP every inbound frame whose `action` doesn't match. In
        // the vendor ecosystem the install dir IS the reverse-DNS namespace,
        // but our StreamDock-CDN installer names dirs by the numeric catalogue
        // id, so the fallback above would gate on "20250308000340.action1"
        // while real events carry "com.mirabox.streamdock.timeClock.action1" —
        // the plugin runs but never draws. When the dir-derived uuid is not a
        // dot-prefix of the declared actions, use the actions' common
        // namespace instead (first UUID minus its last segment, only when ALL
        // actions share it). registration uuid / routing are unaffected.
        if (!manifest.actions.empty()) {
            QString const dirPrefix = pluginUuid + QLatin1Char('.');
            bool const dirIsNamespace =
                std::all_of(manifest.actions.begin(),
                            manifest.actions.end(),
                            [&](PluginAction const& a) { return a.uuid.startsWith(dirPrefix); });
            if (!dirIsNamespace) {
                qsizetype const lastDot = manifest.actions.front().uuid.lastIndexOf(u'.');
                if (lastDot > 0) {
                    QString const ns = manifest.actions.front().uuid.left(lastDot);
                    QString const nsPrefix = ns + QLatin1Char('.');
                    bool const shared = std::all_of(
                        manifest.actions.begin(),
                        manifest.actions.end(),
                        [&](PluginAction const& a) { return a.uuid.startsWith(nsPrefix); });
                    if (shared) {
                        pluginUuid = ns;
                    }
                }
            }
        }
    }
    QJsonObject const plugin{
        {QStringLiteral("uuid"), pluginUuid},
        {QStringLiteral("version"), manifest.version},
    };

    QJsonObject envelope;
    envelope[QStringLiteral("application")] = app;
    envelope[QStringLiteral("colors")] = colors;
    envelope[QStringLiteral("devicePixelRatio")] = 1;
    envelope[QStringLiteral("devices")] =
        m_devicesInfoProvider ? m_devicesInfoProvider() : QJsonArray{};
    envelope[QStringLiteral("plugin")] = plugin;

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

        // Record the source directory so spawn() can run the child with the
        // plugin dir as its working directory (relative CodePath + resources),
        // and so the bundle dir is available for the WINPLG-01 PE-magic scan.
        opt->sourceDir = m_pluginsDir + QLatin1Char('/') + entry;

        // WINPLG-01: classify Windows plugins at SCAN time (locked CONTEXT decision —
        // NOT lazily at launch). The bundle dir (opt->sourceDir) exists here, so the
        // bounded PE-magic corroborator can run. Cache the verdict on the manifest so
        // it flows through spawn() into the live inventory and reaches plugins() / the
        // UI model WITHOUT a re-scan (PluginInfo carries no bundle path).
        opt->winClass = classifyWindowsPlugin(*opt, opt->sourceDir);

        // Step 4: apply the runnability gate. Pass the emulated Stream Deck version
        // (not our app version) so a plugin's Software.MinimumVersion (an Elgato
        // SD-app requirement) is compared against the right axis.
        //
        // WINPLG-02 native-run override: a WS-only-IPC win-only plugin (os=["windows"],
        // no PE binary) runs natively on Linux/macOS even though manifestRunnableHere
        // would otherwise strict-reject a non-matching explicit-windows OS array. We
        // accept when EITHER the base gate passes OR supportsCurrentPlatform() says the
        // classified plugin runs natively. The strict reject for VendorDll is preserved:
        // supportsCurrentPlatform returns false for VendorDll off Windows (Wine deferred).
        //
        // WR-01: the native-run override bypasses ONLY the OS gate — NOT the
        // Software.MinimumVersion floor. Being cross-platform-runnable does not exempt a
        // plugin from version gating, so a win-only WS plugin whose Software.MinimumVersion
        // exceeds the emulated Stream Deck version must still be rejected, exactly like a
        // plain plugin. Gate the native acceptance on manifestVersionGatePasses() so the OR
        // cannot short-circuit past the floor.
        QString const emulatedVer = emulatedStreamDeckVersion();
        bool const baseRunnable = manifestRunnableHere(*opt, currentPlatformString(), emulatedVer);
        bool const versionOk = manifestVersionGatePasses(*opt, emulatedVer);
        bool const winNativeRunnable =
            supportsCurrentPlatform(*opt, currentPlatformString(), opt->winClass) && versionOk;
        if (!baseRunnable && !winNativeRunnable) {
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

namespace {

/// Tie a plugin child's lifetime to the host process (Linux only).
///
/// The graceful shutdown protocol (exitApp -> terminate(1s) -> kill) lives in
/// the PluginManager destructor, so it never runs when the app dies uncleanly
/// (SIGTERM/SIGKILL, crash): Qt installs no signal handlers, destructors are
/// skipped, and every plugin child is orphaned. Observed live 2026-06-09: 11
/// stale `node code/index.js` processes accumulated across killed sessions.
/// PR_SET_PDEATHSIG delivers SIGTERM to the child the moment the parent dies,
/// regardless of how it died. The getppid() check closes the classic race
/// where the parent dies between fork() and prctl() (the child would be
/// re-parented already and the death signal would never fire).
void tieChildToParentLifetime(QProcess& proc) {
#if defined(Q_OS_LINUX)
    pid_t const parentPid = getpid();
    proc.setChildProcessModifier([parentPid]() {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        if (::getppid() != parentPid) {
            ::_exit(0); // parent already gone — don't outlive it
        }
    });
#else
    Q_UNUSED(proc);
#endif
}

/// Trace a plugin child's CLEAN exit (code 0). Plugins are long-running
/// daemons, so even a clean exit deserves a log line: a child that exits 0
/// before registering over the WebSocket was previously INDISTINGUISHABLE
/// from "never spawned" (observed with a comment-only dummy CodePath,
/// 2026-06-09). Shared by the node and native finished-handlers.
void logCleanPluginExit(QString const& pluginId) {
    qWarning("PluginManager: plugin '%s' exited cleanly (code 0) — "
             "it will receive no events until rediscover/app restart",
             qPrintable(pluginId));
}

} // namespace

void PluginManager::spawn(PluginManifest const& manifest) {
    // D-27-4 user-disable skip (T-27-DISABLE-BYPASS): compute the pluginId key first
    // so we can check the persisted disabled-set BEFORE doing any work.  The key
    // derivation here must mirror the one below (same priority order).
    // We check only with sourceDir to keep it cheap; the full key computation
    // happens again below for the actual spawn logic.
    {
        QString earlyId;
        if (!manifest.sourceDir.isEmpty()) {
            earlyId = QFileInfo(manifest.sourceDir).fileName();
        } else if (!manifest.codePath.isEmpty()) {
            earlyId = manifest.codePath;
        } else {
            earlyId = manifest.name;
        }
        if (!earlyId.isEmpty() && shouldSkipSpawn(earlyId)) {
            qInfo("PluginManager: skip spawn of '%s' (user-disabled via QSettings)",
                  qPrintable(earlyId));
            return;
        }
    }

    // Validate the code path as a filesystem path component (T-18-PATHTRAV).
    // The manifest's CodePath / Name is used as a key in m_live and potentially
    // in per-plugin settings directories. Reject any value that could traverse
    // path boundaries.
    // Stable plugin id used as the m_live key and -pluginUUID fallback. Derive
    // it from a path-free identifier (PUUID, else the .sdPlugin directory name,
    // else Name) rather than the CodePath: real plugins use subdir CodePaths
    // like "plugin/main.html", which are NOT valid single path components.
    // The m_live / m_lastNodeArgv KEY (distinct from the -pluginUUID arg, which
    // prefers PUUID below). Historical contract: id == CodePath for simple
    // single-component CodePaths. A subdir CodePath ("plugin/main.html") is not
    // a valid single component, so fall back to the .sdPlugin dir name, else Name.
    QString pluginId;
    if (!manifest.sourceDir.isEmpty()) {
        // Prefer the .sdPlugin directory name: it is UNIQUE per installed
        // plugin, whereas CodePath ("plugin.cjs", "code.html") collides across
        // plugins and would alias them to the same m_live key. sourceDir is
        // empty in unit tests (manifests built in-memory), which fall back to
        // the historical CodePath key below.
        pluginId = QFileInfo(manifest.sourceDir).fileName();
    } else if (!manifest.codePath.isEmpty() && isSafeUuidComponent(manifest.codePath)) {
        pluginId = manifest.codePath;
    } else {
        pluginId = manifest.name;
    }
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
    if (code.isEmpty()) {
        // Not a security rejection: the manifest simply has no runnable entry
        // point for this platform (e.g. a Windows-only native plugin shipping
        // only CodePathWin). Distinct message so the log doesn't read as a
        // path-traversal attempt.
        qInfo("PluginManager: skipping plugin '%s': no code path for this platform "
              "(CodePath/CodePath%s missing in manifest)",
              qPrintable(manifest.name),
              currentPlatformString() == QLatin1String("windows") ? "Win" : "Lin/Mac");
        return;
    }
    if (code.startsWith(QLatin1Char('/')) || code.contains(QLatin1Char('\\')) ||
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

        QString const infoJson = buildInfoJson(manifest);
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
        tieChildToParentLifetime(*rawProc);

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
                    } else {
                        logCleanPluginExit(pluginId);
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
        QString const infoJson = buildInfoJson(manifest);
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
        // B6: key by the registration UUID so disable/uninstall/shutdown can tear
        // this page down. Assigning over an existing entry destroys the old page
        // (a re-spawn replaces, never leaks).
        m_htmlPages[pluginUuid] = std::move(page);
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
        tieChildToParentLifetime(*rawProc);

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
                    } else {
                        logCleanPluginExit(pluginId);
                    }
                });

        // A native plugin IS its own program (no interpreter), so:
        //  (1) launch it by ABSOLUTE path — a bare relative name with no slash is
        //      PATH-searched and fails to start; and
        //  (2) give it the SAME Elgato connect argv a node plugin gets, minus the
        //      leading codePath (which for node is the script arg but here is the
        //      program itself). Without these args an OpenAction/OpenDeck binary
        //      has no -port/-pluginUUID and never connects to the WS server.
        // OpenAction plugins ship in a zip that does not preserve the +x bit, so
        // make the binary executable before launch.
        QString const absCode = QDir(manifest.sourceDir).filePath(code);
        QFile::setPermissions(absCode,
                              QFile::permissions(absCode) | QFileDevice::ExeOwner |
                                  QFileDevice::ExeUser | QFileDevice::ExeGroup);
        QString const infoJson = buildInfoJson(manifest);
        quint16 nativePort = 0;
#if defined(AJAZZ_HAVE_WEBSOCKETS)
        if (m_server) {
            nativePort = m_server->serverPort();
        }
#endif
        QStringList nativeArgv = buildNodeArgv(code, nativePort, pluginUuid, infoJson);
        nativeArgv.removeFirst(); // the native binary is the program, not an arg
        // T-18-ARGV: start(program, args) — no shell concatenation.
        rawProc->start(absCode, nativeArgv);
        m_live.emplace(std::piecewise_construct,
                       std::forward_as_tuple(pluginId),
                       std::forward_as_tuple(manifest, std::move(proc)));
    }
}

// ---------------------------------------------------------------------------
// rediscover()
// ---------------------------------------------------------------------------

void PluginManager::rediscover() {
    // Re-run the full dir scan.  discover() also extracts leftover *.sdPlugin
    // archives, so this is safe to call repeatedly.
    //
    // WR-04 / CR-01 invariant (READ BEFORE WIRING A NEW CALLER): rediscover()
    // does NOT itself run the Ed25519 verify gate. It trusts that every
    // *.sdPlugin directory it finds under the plugins dir has ALREADY been
    // verified by one of the promotion paths — the constructor launch-sweep
    // (plugin_catalog_model.cpp ctor), installFromFile(), or the network
    // install() handler — each of which quarantines (removeRecursively) any
    // Refused/tampered or unconsented-Unsigned dir before it can be discovered.
    // It is also idempotent: it diffs candidates against the already-live set
    // (m_live, keyed by .sdPlugin dir name) and spawns ONLY new entries, so
    // repeated/idempotent successes (e.g. an "already installed" install() or
    // an openUpstream-only fallback that flips installFinished(ok=true)) are
    // harmless no-ops. MUST NOT be called on an unverified plugins dir.
    std::vector<PluginManifest> const candidates = discover();

    int alreadyLive = 0;
    int newlySpawned = 0;

    for (auto const& manifest : candidates) {
        // Derive the .sdPlugin dir-name key the same way spawn() does: use the
        // leaf of manifest.sourceDir (e.g. "com.example.myplugin.sdPlugin").
        // This is the SAME key spawn() inserts into m_live (commit 5725cb0).
        // When sourceDir is empty (in-memory manifests from unit tests without
        // a real dir), fall back to the same logic spawn() uses (codePath else
        // name), giving discover()-sourced manifests the correct key.
        QString pluginKey;
        if (!manifest.sourceDir.isEmpty()) {
            pluginKey = QFileInfo(manifest.sourceDir).fileName();
        } else if (!manifest.codePath.isEmpty()) {
            pluginKey = manifest.codePath;
        } else {
            pluginKey = manifest.name;
        }

        if (m_live.count(pluginKey) > 0) {
            // Already running — do NOT tear down or re-spawn (D-27-3 idempotency).
            ++alreadyLive;
        } else {
            // New plugin not in the live set: spawn it.
            spawn(manifest);
            ++newlySpawned;
        }
    }

    qInfo(
        "PluginManager::rediscover: %d already-live, %d newly-spawned", alreadyLive, newlySpawned);
}

// ---------------------------------------------------------------------------
// onProcessFailed()
// ---------------------------------------------------------------------------

void PluginManager::onProcessFailed(QString const& uuid) {
    // HOST-02 (T-30-pre-reg): pre-registration-exit guard.
    // If the UUID is not in m_live, the process exited before its WebSocket sent
    // registerPlugin (or the entry was already torn down by uninstall/shutdown).
    // In either case, do NOT record a crash credit or call shouldDisable: this exit
    // must not count toward the 3-in-30s disable window. The sentinel UUID on the
    // SdPluginServer side (onNewConnection) and this m_live guard together enforce
    // the HOST-02 contract: a plugin that crashes mid-handshake never crashes the
    // host and never consumes a crash credit.
    if (m_live.find(uuid) == m_live.end()) {
        qInfo("PluginManager::onProcessFailed: pre-registration exit for '%s' — "
              "not counted toward crash window (not in m_live)",
              qPrintable(uuid));
        return;
    }

    // Record the crash and decide disable SYNCHRONOUSLY so observable state
    // (crash count, isDisabled, pluginDisabled signal) is updated immediately —
    // disableWithNotice only sets m_disabled + emits, it never touches a QProcess.
    qint64 const now = m_clock();
    m_crashTracker.recordCrash(uuid, now);
    if (m_crashTracker.shouldDisable(uuid, now)) {
        // 3-in-30s: disable + surface (akp_plugin_sdk.md §3 crash policy).
        disableWithNotice(uuid, QStringLiteral("crashed 3 times within 30 seconds"));
    }

    // DEFER the destructive teardown (m_live.erase deletes the QProcess, and the
    // respawn). This slot runs INSIDE the QProcess::finished / errorOccurred
    // emission; erasing m_live here would delete the QProcess from within its own
    // signal handler — a use-after-free that crashed the host when a plugin was
    // SIGKILLed. Coalesce per uuid so a double-fire (errorOccurred + finished) or
    // multiple synchronous failures schedule a single teardown.
    if (!m_failurePending.contains(uuid)) {
        m_failurePending.insert(uuid);
        QTimer::singleShot(0, this, [this, uuid]() { handleProcessFailure(uuid); });
    }
}

void PluginManager::handleProcessFailure(QString const& uuid) {
    m_failurePending.remove(uuid);
    auto it = m_live.find(uuid);
    if (it == m_live.end()) {
        return; // already torn down (uninstall/shutdown) or never process-backed
    }
    if (isDisabled(uuid)) {
        // Disabled synchronously in onProcessFailed: clean up, do NOT re-spawn.
        m_live.erase(it);
        return;
    }
    // Fewer/slower crashes: restart (teardown + re-spawn).
    // WR-02: guard against re-spawning HTML/WebEngine plugins (process == nullptr
    // in m_live). An HTML plugin runs in-process via Chromium and has no owned
    // QProcess; re-spawning it would re-inject the Mirabox shim. Only restart
    // process-backed entries.
    if (it->second.process != nullptr) {
        PluginManifest const manifest = it->second.manifest;
        m_live.erase(it);
        spawn(manifest);
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
// setPluginEnabled() — persist user-intent enable/disable (D-27-4)
// ---------------------------------------------------------------------------

void PluginManager::setPluginEnabled(QString const& pluginId, bool enabled) {
    if (enabled) {
        // Clear the persisted disabled flag.
        {
            QSettings settings;
            settings.remove(disabledKey(pluginId));
        }
        qInfo("PluginManager: user-enabled plugin '%s' (QSettings cleared)", qPrintable(pluginId));

        // If the manifest is discoverable and the plugin is not already live, spawn it now.
        if (m_live.count(pluginId) == 0) {
            // Re-scan to find the manifest for this pluginId.
            std::vector<PluginManifest> const candidates = discover();
            for (auto const& m : candidates) {
                QString candidateKey;
                if (!m.sourceDir.isEmpty()) {
                    candidateKey = QFileInfo(m.sourceDir).fileName();
                } else if (!m.codePath.isEmpty()) {
                    candidateKey = m.codePath;
                } else {
                    candidateKey = m.name;
                }
                if (candidateKey == pluginId) {
                    spawn(m);
                    break;
                }
            }
        }
    } else {
        // Persist the disabled flag.
        {
            QSettings settings;
            settings.setValue(disabledKey(pluginId), true);
        }
        qInfo("PluginManager: user-disabled plugin '%s' (QSettings written)", qPrintable(pluginId));

        // Tear down the live plugin if present (mirror the crash-path teardown).
        unloadPlugin(pluginId);
    }
}

// ---------------------------------------------------------------------------
// unloadPlugin() — full live-plugin teardown WITHOUT persisting disable intent
// ---------------------------------------------------------------------------

void PluginManager::unloadPlugin(QString const& pluginId) {
    auto it = m_live.find(pluginId);
    if (it == m_live.end()) {
        return;
    }
    // The child registered with -pluginUUID = manifest PUUID when present (see
    // spawn()); addressing exitApp to the m_live dir-name key silently missed
    // those plugins' state-flush (audit 3.14).
    QString const regUuid =
        it->second.manifest.puuid.isEmpty() ? pluginId : it->second.manifest.puuid;
#if defined(AJAZZ_HAVE_WEBSOCKETS)
    // Step 1: allow the plugin to flush its state.
    if (m_server) {
        m_server->sendEvent(regUuid, QStringLiteral("exitApp"));
    }
#endif
    // Step 2 + 3: terminate (1 s grace), then kill.
    if (it->second.process) {
        it->second.process->terminate();
        if (!it->second.process->waitForFinished(1000)) {
            it->second.process->kill();
        }
    }
    m_live.erase(it);
#if defined(AJAZZ_HAVE_WEBENGINE)
    // B6: an HTML plugin has no process; tear its in-process page down here so
    // it does not leak its QWebEnginePage. Pages are keyed by the REGISTRATION
    // uuid (PUUID preferred) — erase both spellings (audit 3.4).
    m_htmlPages.erase(pluginId);
    if (regUuid != pluginId) {
        m_htmlPages.erase(regUuid);
    }
#endif
}

QString PluginManager::infoJsonForPlugin(QString const& pluginId) const {
    auto it = m_live.find(pluginId);
    if (it == m_live.end()) {
        // Tolerate the registration PUUID spelling (make_info receives the
        // `plugin` field the SPA carries on the instance, which is the
        // install-dir name today but PUUID-shaped ids must not 404).
        for (auto const& [key, live] : m_live) {
            if (live.manifest.puuid == pluginId) {
                return buildInfoJson(live.manifest);
            }
        }
        return {};
    }
    return buildInfoJson(it->second.manifest);
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
// seedLiveForTest() — HOST-02 test seam
// ---------------------------------------------------------------------------

void PluginManager::seedLiveForTest(QString const& uuid) {
    // Insert a no-process LivePlugin entry so onProcessFailed treats this UUID
    // as a registered plugin (m_live authority). Tests that verify the 3-in-30s
    // crash-window logic without spawning a real process MUST call this first;
    // otherwise the m_live.find guard in onProcessFailed will treat the UUID as a
    // pre-registration exit and return early without counting the crash.
    if (m_live.count(uuid) == 0) {
        m_live.emplace(std::piecewise_construct,
                       std::forward_as_tuple(uuid),
                       std::forward_as_tuple(PluginManifest{}, nullptr));
    }
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
#if defined(AJAZZ_HAVE_WEBENGINE)
    // B6: destroy all in-process HTML plugin pages on shutdown (the profile
    // outlives them by reverse-of-declaration order — see plugin_manager.hpp).
    m_htmlPages.clear();
#endif
}

std::size_t PluginManager::htmlPageCountForTesting() const noexcept {
#if defined(AJAZZ_HAVE_WEBENGINE)
    return m_htmlPages.size();
#else
    return 0;
#endif
}

QString PluginManager::stateImagePath(QString const& actionUuid, int stateIndex) const {
    if (stateIndex < 0) {
        return {};
    }
    for (auto const& [key, live] : m_live) {
        for (PluginAction const& action : live.manifest.actions) {
            if (action.uuid != actionUuid) {
                continue;
            }
            if (stateIndex >= static_cast<int>(action.states.size())) {
                return {}; // action matched but no such state declared
            }
            // OpenDeck/Elgato default: a state with no Image falls back to the
            // ACTION icon (shared.rs:150 — state.image defaults to action.icon).
            QString const rel = action.states[static_cast<std::size_t>(stateIndex)].image.isEmpty()
                                    ? action.icon
                                    : action.states[static_cast<std::size_t>(stateIndex)].image;
            if (rel.isEmpty()) {
                return {};
            }
            QDir const base(live.manifest.sourceDir);
            QString const asDeclared = base.absoluteFilePath(rel);
            if (QFileInfo::exists(asDeclared)) {
                return asDeclared;
            }
            // Elgato image entries usually OMIT the extension (and ship a hi-dpi
            // @2x variant); probe the common raster/vector extensions.
            for (auto const* ext : {".png", "@2x.png", ".jpg", ".jpeg", ".svg", ".gif", ".bmp"}) {
                QString const probe = asDeclared + QLatin1String(ext);
                if (QFileInfo::exists(probe)) {
                    return probe;
                }
            }
            return {}; // matched action+state but the declared file is missing
        }
    }
    return {};
}

QString PluginManager::defaultSettingsForAction(QString const& actionUuid) const {
    for (auto const& [key, live] : m_live) {
        for (PluginAction const& action : live.manifest.actions) {
            if (action.uuid != actionUuid) {
                continue;
            }
            return QString::fromStdString(action.defaultSettings);
        }
    }
    return {};
}

std::pair<QString, QString> PluginManager::encoderLayoutInfo(QString const& actionUuid) const {
    for (auto const& [key, live] : m_live) {
        for (PluginAction const& action : live.manifest.actions) {
            if (action.uuid != actionUuid) {
                continue;
            }
            QString iconAbs;
            // Prefer the Encoder.Icon, else the action icon (Elgato fallback).
            QString const rel =
                action.encoderBlock.icon.isEmpty() ? action.icon : action.encoderBlock.icon;
            if (!rel.isEmpty()) {
                QDir const base(live.manifest.sourceDir);
                QString const asDeclared = base.absoluteFilePath(rel);
                if (QFileInfo::exists(asDeclared)) {
                    iconAbs = asDeclared;
                } else {
                    for (auto const* ext :
                         {".png", "@2x.png", ".jpg", ".jpeg", ".svg", ".gif", ".bmp"}) {
                        QString const probe = asDeclared + QLatin1String(ext);
                        if (QFileInfo::exists(probe)) {
                            iconAbs = probe;
                            break;
                        }
                    }
                }
            }
            // A non-"$" layout is a plugin-relative JSON layout file (Elgato
            // SD+ custom layouts — production audit blocker 5). Resolve it to
            // an absolute path here, where sourceDir is known, so the bridge
            // can just load the file. Unresolvable paths fall through verbatim
            // (the renderer then degrades to $X1 as before).
            QString layout = action.encoderBlock.layout;
            if (!layout.isEmpty() && !layout.startsWith(QLatin1Char('$'))) {
                QDir const base(live.manifest.sourceDir);
                QString const asDeclared = base.absoluteFilePath(layout);
                if (QFileInfo::exists(asDeclared)) {
                    layout = asDeclared;
                } else if (QFileInfo::exists(asDeclared + QLatin1String(".json"))) {
                    layout = asDeclared + QLatin1String(".json");
                }
            }
            return {layout, iconAbs};
        }
    }
    return {{}, {}};
}

std::pair<int, bool> PluginManager::actionStateMeta(QString const& actionUuid) const {
    for (auto const& [key, live] : m_live) {
        for (PluginAction const& action : live.manifest.actions) {
            if (action.uuid == actionUuid) {
                return {static_cast<int>(action.states.size()), action.disableAutomaticStates};
            }
        }
    }
    return {0, false}; // unknown action — caller treats as "no automatic states"
}

QString PluginManager::ownerForAction(QString const& actionUuid) const {
    if (actionUuid.isEmpty()) {
        return {};
    }
    for (auto const& [key, live] : m_live) {
        for (PluginAction const& action : live.manifest.actions) {
            if (action.uuid == actionUuid) {
                // Mirror spawn()'s -pluginUUID identity (WR-03): prefer PUUID,
                // else the m_live key (the .sdPlugin directory name). This is the
                // value the plugin registered with, so sendEvent(owner, ...) and
                // the ctx.pluginUuid ownership check both resolve correctly. This
                // is the OpenDeck stored-owner model (action.plugin stamped at
                // load) and removes the dotted-prefix requirement on action UUIDs.
                return live.manifest.puuid.isEmpty() ? key : live.manifest.puuid;
            }
        }
    }
    return {};
}

bool PluginManager::monitorsApplication(QString const& pluginUuid, QString const& appId) const {
    // WR-02: decide whether a registered plugin asked to be told about @p appId
    // via its manifest ApplicationsToMonitor list. Resolve the plugin by the SAME
    // identity registeredPlugins() carries: the -pluginUUID value (PUUID when
    // present, else the m_live key). An EMPTY ApplicationsToMonitor list means
    // "monitor everything" (backwards-compatible broadcast to that plugin); a
    // non-empty list filters delivery to the listed apps only. Unknown plugin ->
    // false (do not deliver to a plugin we cannot resolve).
    for (auto const& [key, live] : m_live) {
        QString const registeredId = live.manifest.puuid.isEmpty() ? key : live.manifest.puuid;
        if (registeredId != pluginUuid) {
            continue;
        }
        if (live.manifest.applicationsToMonitor.isEmpty()) {
            return true; // "monitor everything" (focus-based approximation)
        }
        QString const normalized = normalizeApplicationToken(appId);
        return live.manifest.applicationsToMonitor.contains(normalized);
    }
    return false;
}

// ---------------------------------------------------------------------------
// IPluginHost2 overrides (the .sdPlugin sub-host implementation)
// ---------------------------------------------------------------------------

bool PluginManager::dispatch(QString const& pluginUuid,
                             QString const& actionId,
                             QJsonObject const& payload) {
    // This is the .sdPlugin WebSocket dispatch path. Routes the event to the
    // live plugin via SdPluginServer::sendEvent. Python UUIDs are handled by
    // UnifiedPluginHost before they reach this method.
    //
    // HOST-03: no SKU-specific branch here. Device I/O lives in PluginDeviceBridge.
#if defined(AJAZZ_HAVE_WEBSOCKETS)
    if (!m_server) {
        return false;
    }
    if (m_live.find(pluginUuid) == m_live.end()) {
        return false; // plugin not registered / not live
    }
    // B5 (research D4): the second argument is an ACTION id, not an Elgato event
    // name — the same contract the Python sub-host uses (the wire event name is
    // carried in payload["event"], e.g. "keyDown"; actionId identifies which
    // action). The previous code passed actionId as the event name, so a plugin
    // received an event literally named by the action UUID, which it cannot
    // recognise. Build a well-formed {event, action, …payload} envelope: the
    // event comes from the payload, the action UUID is preserved in `action`,
    // and the remaining payload fields pass through. Falls back to actionId only
    // when the payload omits an explicit event, preserving the prior wire for any
    // caller that genuinely passed an event name as the second argument.
    QString const eventName = payload.value(QStringLiteral("event")).toString();
    if (eventName.isEmpty()) {
        return m_server->sendEvent(pluginUuid, actionId, payload);
    }
    QJsonObject envelope = payload;
    envelope.insert(QStringLiteral("event"), eventName);
    envelope.insert(QStringLiteral("action"), actionId);
    return m_server->sendEvent(pluginUuid, envelope);
#else
    Q_UNUSED(pluginUuid)
    Q_UNUSED(actionId)
    Q_UNUSED(payload)
    return false;
#endif
}

std::vector<plugins::PluginInfo> PluginManager::plugins() {
    // Return the .sdPlugin-only inventory. Python plugins are NOT included here —
    // the aggregator (UnifiedPluginHost) folds both inventories.
    std::vector<plugins::PluginInfo> result;
    result.reserve(m_live.size());
    for (auto const& [uuid, livePlugin] : m_live) {
        plugins::PluginInfo info;
        info.id = uuid.toStdString();
        info.name = livePlugin.manifest.name.toStdString();
        info.version = livePlugin.manifest.version.toStdString();
        info.authors = livePlugin.manifest.author.toStdString();
        for (auto const& action : livePlugin.manifest.actions) {
            info.actionIds.push_back(action.uuid.toStdString());
        }
        // WINPLG-01/02: hand the scan-time classification verdict to the UI model
        // (LoadedPluginsModel, Plan 02) without re-scanning. The plugins-tier int
        // mapping mirrors ajazz::app::WinPluginClass (0/1/2) — see PluginInfo::winClass.
        info.winClass = static_cast<int>(livePlugin.manifest.winClass);
        result.push_back(std::move(info));
    }
    return result;
}

int PluginManager::connectedPluginCount() const noexcept {
#if defined(AJAZZ_HAVE_WEBSOCKETS)
    return m_server ? m_server->connectedPluginCount() : 0;
#else
    return 0;
#endif
}

} // namespace ajazz::app
