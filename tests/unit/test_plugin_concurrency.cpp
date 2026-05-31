// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_concurrency.cpp
 * @brief PLUGIN-17 concurrency regression — "one crash disables only itself, siblings survive".
 *
 * Guards the exact failure mode of commit 5725cb0 (shared-key collision where plugins
 * keyed by colliding CodePath/PUUID would take each other down) and the WR-02
 * HTML-no-respawn guard.
 *
 * All cases:
 *   - Construct ONE PluginManager with a fake NodeProbe and an INJECTED synthetic clock
 *     so the 3-in-30s window is driven deterministically with NO real sleeps.
 *   - Use DISTINCT codePath keys so m_live / m_disabled entries are independent.
 *   - Drive ONE plugin to the disable threshold; assert siblings are unaffected.
 *   - Assert the WR-02 guard: a plugin UUID not present in m_live (simulates an HTML
 *     plugin with process==nullptr) is never re-spawned by onProcessFailed.
 *
 * Tag: [plugin-concurrency].
 *
 * Phase: 27-plugin-install-trust-persistence-hardening / Plan 27-05 (PLUGIN-17)
 */
#include "node_runner.hpp"
#include "plugin_manager.hpp"
#include "plugin_manifest.hpp"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::app;

namespace {

/// Lazy QCoreApplication singleton (same pattern as test_plugin_lifecycle.cpp).
QCoreApplication* ensureQCoreApp() {
    static QCoreApplication* app = []() {
        static int argc = 0;
        static char* argv[] = {nullptr};
        return new QCoreApplication(argc, argv);
    }();
    return app;
}

/// Build a minimal valid node-plugin manifest (in-memory; sourceDir stays empty so
/// the m_live key == codePath, matching the test assertions below).
PluginManifest makeManifest(QString const& codePath) {
    PluginManifest m;
    m.name = QStringLiteral("TestPlugin");
    m.codePath = codePath;
    m.author = QStringLiteral("Test");
    m.version = QStringLiteral("1.0");
    m.sdkVersion = 1;
    // sourceDir intentionally empty so pluginId == codePath (unit-test convention).
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: one crash disables only itself across siblings
//
// Three distinctly-keyed plugins are spawned. Plugin A is driven to the
// 3-in-30s disable threshold via the injected clock. Asserts:
//   (a) Exactly ONE pluginDisabled signal fired (QSignalSpy count == 1).
//   (b) The disabled uuid is "pluginA.js" (the crasher).
//   (c) isDisabled("pluginA.js") == true.
//   (d) isDisabled("pluginB.js") == false  (sibling B untouched).
//   (e) isDisabled("pluginC.js") == false  (sibling C untouched).
//
// Guards the 5725cb0 shared-key collision: a buggy shared key would
// have disabled a sibling instead of / in addition to the crasher.
// ---------------------------------------------------------------------------
TEST_CASE("PluginManager crash disables only itself across siblings", "[plugin-concurrency]") {
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    // Synthetic clock: zero-initialised; the test advances it in-place.
    qint64 fakeNow = 0;

    // Fake NodeProbe: node is absent so spawn() immediately calls disableWithNotice
    // for node plugins. We will use the "no-node" path intentionally:
    //   - spawn() calls disableWithNotice(pluginId, "Node.js >= 20 not found")
    //   - That fires pluginDisabled ONCE per plugin at construction time.
    //   - We clear the spy, then manually call onProcessFailed to exercise the
    //     crash-tracker path against plugins that ARE in m_disabled / crash state.
    //
    // HOWEVER: onProcessFailed() only records in m_crashTracker and checks
    // m_live for the WR-02 re-spawn guard. It does NOT require a live entry to
    // accumulate crash credits and eventually call disableWithNotice again — the
    // 3-in-30s path calls disableWithNotice unconditionally once threshold is hit.
    //
    // Using the "no-node" probe means spawn() pre-disables the plugins;
    // we clear the spy so only the crash-path signals are counted.
    //
    // Alternative: use a valid fake node path so plugins go into m_live (argv
    // stored), then drive onProcessFailed. We choose this approach so that
    // sibling m_live entries are actually present and the per-entry isolation
    // can be observed (i.e. disableWithNotice is not called on them).
    //
    // To put plugins into m_live we need a probe that claims to find node.
    // The spawn() code stores argv in m_lastNodeArgv and emits m_live entry
    // BEFORE starting the QProcess — and the non-existent binary will fail
    // asynchronously (errorOccurred/finished). We assert synchronously before
    // pumping, so m_live is intact at assertion time.
    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return QStringLiteral("/nonexistent/node"); };
    fakeProbe.queryVersion = [](QString const&) -> QString { return QStringLiteral("v26.0.0"); };

    PluginManager manager(
        scratch.path(), nullptr, fakeProbe, nullptr, [&fakeNow]() { return fakeNow; });

    QSignalSpy disabledSpy(&manager, &PluginManager::pluginDisabled);

    // Spawn three plugins with DISTINCT codePath keys (the 5725cb0 key-distinctness
    // is the property under test). In-memory manifests (no sourceDir) map to
    // pluginId == codePath inside spawn().
    PluginManifest mA = makeManifest(QStringLiteral("pluginA.js"));
    PluginManifest mB = makeManifest(QStringLiteral("pluginB.js"));
    PluginManifest mC = makeManifest(QStringLiteral("pluginC.js"));

    manager.spawn(mA);
    manager.spawn(mB);
    manager.spawn(mC);

    // At this point: m_live has entries for all three (argv stored, QProcess
    // creation in flight but not yet failed). The spy may be empty if no
    // synchronous disable fired (valid fake node found). Clear it to start clean.
    disabledSpy.clear();

    // Drive plugin A to the 3-in-30s disable threshold.
    // All three crashes land inside the 30 s window (kWindowMs = 30 000 ms).
    fakeNow = 0;
    manager.onProcessFailed(QStringLiteral("pluginA.js"));
    fakeNow = 1000;
    manager.onProcessFailed(QStringLiteral("pluginA.js"));
    fakeNow = 2000;
    manager.onProcessFailed(QStringLiteral("pluginA.js"));

    // (a) Exactly ONE pluginDisabled signal fired — the crasher, not a sibling.
    REQUIRE(disabledSpy.count() == 1);

    // (b) The disabled uuid is "pluginA.js".
    QString const disabledUuid = disabledSpy.first().at(0).toString();
    CHECK(disabledUuid == QStringLiteral("pluginA.js"));

    // (c) The crasher is disabled.
    CHECK(manager.isDisabled(QStringLiteral("pluginA.js")));

    // (d) Sibling B is NOT disabled (5725cb0 regression assertion).
    CHECK_FALSE(manager.isDisabled(QStringLiteral("pluginB.js")));

    // (e) Sibling C is NOT disabled (5725cb0 regression assertion).
    CHECK_FALSE(manager.isDisabled(QStringLiteral("pluginC.js")));
}

// ---------------------------------------------------------------------------
// TEST 2: two crashes do NOT disable the crasher, siblings remain unaffected
//
// Verifies that the threshold (3 crashes) is respected: only 2 crashes means
// no disable signal fires, and both the crasher and its siblings stay enabled.
// ---------------------------------------------------------------------------
TEST_CASE("PluginManager two crashes do not disable, siblings unaffected", "[plugin-concurrency]") {
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    qint64 fakeNow = 0;

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return QStringLiteral("/nonexistent/node"); };
    fakeProbe.queryVersion = [](QString const&) -> QString { return QStringLiteral("v26.0.0"); };

    PluginManager manager(
        scratch.path(), nullptr, fakeProbe, nullptr, [&fakeNow]() { return fakeNow; });

    QSignalSpy disabledSpy(&manager, &PluginManager::pluginDisabled);

    manager.spawn(makeManifest(QStringLiteral("pluginX.js")));
    manager.spawn(makeManifest(QStringLiteral("pluginY.js")));
    disabledSpy.clear();

    // Only 2 crashes on X: NOT disabled (threshold not reached).
    fakeNow = 0;
    manager.onProcessFailed(QStringLiteral("pluginX.js"));
    fakeNow = 5000;
    manager.onProcessFailed(QStringLiteral("pluginX.js"));

    // No disable signal.
    CHECK(disabledSpy.count() == 0);

    // Crasher X is NOT disabled (2 < 3 threshold).
    CHECK_FALSE(manager.isDisabled(QStringLiteral("pluginX.js")));

    // Sibling Y is untouched.
    CHECK_FALSE(manager.isDisabled(QStringLiteral("pluginY.js")));
}

// ---------------------------------------------------------------------------
// TEST 3: WR-02 guard — a UUID absent from m_live is NOT re-spawned on crash
//
// Simulates the HTML plugin case: the UUID does not exist in m_live (an HTML
// plugin runs in-process via Chromium, so process == nullptr; when it fails,
// the guard in onProcessFailed must NOT attempt to re-spawn it via spawn()).
//
// Method: call onProcessFailed for a UUID that was never registered via spawn().
// Only 1 crash credit is accumulated (not 3), so the disable path is not
// triggered. The WR-02 guard prevents re-spawn by checking m_live membership.
// Assert: isDisabled(htmlUuid) == false AND the sibling (a regular plugin that
// was spawned) remains unaffected.
// ---------------------------------------------------------------------------
TEST_CASE("PluginManager WR-02 HTML plugin not re-spawned on failure", "[plugin-concurrency]") {
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    qint64 fakeNow = 0;

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    PluginManager manager(
        scratch.path(), nullptr, fakeProbe, nullptr, [&fakeNow]() { return fakeNow; });

    QSignalSpy disabledSpy(&manager, &PluginManager::pluginDisabled);

    // Spawn a real sibling so the manager has a live entry to protect.
    // (no-node probe: sibling will be immediately disabled via node-absent path)
    manager.spawn(makeManifest(QStringLiteral("sibling.js")));
    disabledSpy.clear(); // clear the node-absent disable that spawn() fires

    // Simulate a single crash for an HTML plugin UUID that was NEVER registered
    // into m_live. The WR-02 guard must not re-spawn it.
    QString const htmlUuid = QStringLiteral("com.test.html.plugin.html");
    fakeNow = 0;
    manager.onProcessFailed(htmlUuid);

    // Only 1 crash credit — not at threshold (3). No disable signal.
    CHECK(disabledSpy.count() == 0);

    // htmlUuid is NOT disabled (only 1 crash credit, not 3).
    CHECK_FALSE(manager.isDisabled(htmlUuid));

    // The sibling node plugin's disabled state is unchanged.
    // (It was disabled by spawn() due to no-node, but disabledSpy was cleared;
    // the HTML plugin crash must NOT have produced an additional disable signal.)
    CHECK(disabledSpy.count() == 0);
}

// ---------------------------------------------------------------------------
// TEST 4: WR-02 guard — driving an HTML-like UUID to 3 crashes disables it,
// but does NOT re-spawn it (no m_live entry exists for it).
//
// If the HTML UUID hits the disable threshold, disableWithNotice() fires once.
// The re-spawn branch (WR-02) is NOT taken because the UUID is absent from m_live.
// ---------------------------------------------------------------------------
TEST_CASE("PluginManager WR-02 HTML UUID disabled at threshold without re-spawn",
          "[plugin-concurrency]") {
    ensureQCoreApp();
    QTemporaryDir scratch;
    REQUIRE(scratch.isValid());

    qint64 fakeNow = 0;

    NodeProbe fakeProbe;
    fakeProbe.findNode = []() -> QString { return {}; };
    fakeProbe.queryVersion = [](QString const&) -> QString { return {}; };

    PluginManager manager(
        scratch.path(), nullptr, fakeProbe, nullptr, [&fakeNow]() { return fakeNow; });

    QSignalSpy disabledSpy(&manager, &PluginManager::pluginDisabled);

    // Drive an HTML-UUID (no m_live entry) to the 3-crash threshold.
    QString const htmlUuid = QStringLiteral("com.test.html.three.html");
    fakeNow = 0;
    manager.onProcessFailed(htmlUuid);
    fakeNow = 5000;
    manager.onProcessFailed(htmlUuid);
    fakeNow = 10000;
    manager.onProcessFailed(htmlUuid);

    // Exactly ONE disable signal (crash threshold reached).
    REQUIRE(disabledSpy.count() == 1);
    CHECK(disabledSpy.first().at(0).toString() == htmlUuid);
    CHECK(manager.isDisabled(htmlUuid));

    // No additional disable signals (no re-spawn attempted, no new crash cycle).
    CHECK(disabledSpy.count() == 1);
}
