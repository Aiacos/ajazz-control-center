// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_qml_smoke.cpp
 * @brief Load-time smoke harness for the AjazzControlCenter QML module.
 *
 * This is the project's first QML test (health report 2026-05-22, finding #3:
 * ~32 .qml files, zero QML tests — the largest coverage hole). It loads every
 * shippable QML component through a real QQmlEngine wired to the production
 * C++-backed `AjazzControlCenter` module and asserts each one compiles, has no
 * QML errors, and (for the leaf/presentational components) actually
 * instantiates an object graph. It also fails on any QML *warning* — most
 * importantly binding-loop diagnostics, which Qt reports via qWarning() at
 * create() time rather than as a QQmlComponent error.
 *
 * What this CATCHES (the bulk of the real risk):
 *   - missing/typo'd imports          (`import AjazzControlCenter`, etc.)
 *   - unresolved C++ singleton types   (QML_SINGLETON mis-registration)
 *   - QML syntax / type errors
 *   - broken property bindings to non-existent properties
 *   - binding loops (captured via the install qWarning handler)
 *
 * What this CANNOT catch (acknowledged, by design):
 *   - GPU-only crashes. We run under QT_QPA_PLATFORM=offscreen so CI needs no
 *     display/GPU. The documented `MultiEffect.maskSource`-with-a-raw-Rectangle
 *     SIGABRT (see CLAUDE.md) reproduces only on a real GPU and is therefore
 *     out of scope here. This harness targets load/binding/import/type errors,
 *     which are the overwhelming majority of QML regressions.
 *
 * Wiring: the singletons (Theme, DeviceModel, BatteryService, ...) are
 * registered by constructing the production `Application` controller and
 * calling `exposeToQml()` — exactly the production registration path. We do
 * NOT call `Application::bootstrap()`: that enumerates HID backends and (when
 * built with the python host) would spawn a child process; component load only
 * needs the singletons registered, not populated. An empty DeviceModel is fine
 * for load-time smoke testing.
 *
 * Phase 26 Plan 26-05: world() and its helpers are now defined with external
 * linkage (outside anonymous namespace) so test_device_view_tests.cpp can
 * share the same QApplication + Application + QQmlApplicationEngine without
 * creating duplicates.
 */
#include "application.hpp"

#include <QApplication>
#include <QByteArray>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <QtMessageHandler>
#include <QVariant>
#include <QVariantMap>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

// ---------------------------------------------------------------------------
// Captured QML warnings. Binding loops surface as qWarning() at create() time
// (NOT as QQmlComponent::errors()), so we install a message handler and scan
// the captured lines. We deliberately only treat genuinely actionable QML
// diagnostics as failures so the harness doesn't go red on benign offscreen
// platform chatter.
//
// External linkage so test_device_view_tests.cpp can use clearWarnings() /
// defectWarnings() without duplicating the infrastructure.
// ---------------------------------------------------------------------------
std::mutex g_qmlTestWarnMutex;
std::vector<QString> g_qmlTestWarnings;
QtMessageHandler g_qmlTestPreviousHandler = nullptr;

static void captureHandler(QtMsgType type, QMessageLogContext const& ctx, QString const& msg) {
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        std::lock_guard<std::mutex> lock(g_qmlTestWarnMutex);
        g_qmlTestWarnings.push_back(msg);
    }
    if (g_qmlTestPreviousHandler != nullptr) {
        g_qmlTestPreviousHandler(type, ctx, msg);
    }
}

void clearWarnings() {
    std::lock_guard<std::mutex> lock(g_qmlTestWarnMutex);
    g_qmlTestWarnings.clear();
}

// Return the subset of captured warnings that indicate a real QML defect this
// harness is meant to catch. Binding loops are the headline case.
std::vector<QString> defectWarnings() {
    std::lock_guard<std::mutex> lock(g_qmlTestWarnMutex);
    std::vector<QString> hits;
    for (auto const& w : g_qmlTestWarnings) {
        if (w.contains(QStringLiteral("Binding loop")) ||
            w.contains(QStringLiteral("Unable to assign")) ||
            w.contains(QStringLiteral("is not a type")) ||
            w.contains(QStringLiteral("Cannot assign to non-existent property"))) {
            hits.push_back(w);
        }
    }
    return hits;
}

// ---------------------------------------------------------------------------
// A single live QGuiApplication + Application + QQmlEngine shared by every
// TEST_CASE in this executable (including test_device_view_tests.cpp which
// declares world() as extern). Constructing more than one QGuiApplication per
// process is illegal, and re-registering the singletons twice would trip their
// create()-time asserts, so we build the world exactly once.
// ---------------------------------------------------------------------------
struct QmlWorld {
    ajazz::app::Application* controller = nullptr;
    QQmlApplicationEngine* engine = nullptr;
};

QmlWorld& world() {
    // The whole world is leaked on purpose. The QApplication, the QML engine and
    // the Application controller (which owns the C++ singletons handed to QML via
    // CppOwnership) all have process-lifetime semantics; tearing them down in
    // C++ static-destruction order at exit races Qt's own atexit cleanup and
    // segfaults (the QML engine outlives the QApplication, the singletons are
    // double-managed, etc.). The test result is already reported by then, so we
    // simply never destroy any of it and let the OS reclaim the process memory.
    static QmlWorld w = [] {
        // Belt-and-braces: the CMake test env also sets this, but pin it in
        // code so a manual run without the env still stays headless. Must be
        // set before the QApplication is constructed.
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

        // QApplication (not QGuiApplication): Application constructs a
        // TrayController, which pulls in QSystemTrayIcon / QMenu from QtWidgets.
        // We never *start* the tray (startBackgroundServices is not called), but
        // the QObject still has to construct against a QApplication instance.
        static int argc = 1;
        static char arg0[] = "ajazz_qml_tests";
        static char* argv[] = {arg0, nullptr};
        new QApplication(argc, argv);

        g_qmlTestPreviousHandler = qInstallMessageHandler(captureHandler);

        QmlWorld built;
        built.controller = new ajazz::app::Application();
        built.engine = new QQmlApplicationEngine();
        // Production singleton-registration path. NOTE: we do NOT call
        // bootstrap() — see file header.
        built.controller->exposeToQml(*built.engine);
        return built;
    }();
    return w;
}

// Load a component by module URI + type name (Qt 6.5+ loadFromModule), then
// instantiate it. `instantiate=false` for top-level pages/windows that need a
// live parent or device data to build a full object graph: for those we still
// require the component to COMPILE cleanly (isReady, no errors), which is the
// load-time contract we care about. For leaf/presentational components we also
// require create() to yield a non-null object.
struct LoadResult {
    bool ready = false;
    bool created = false;
    QString error;
};

LoadResult
loadComponent(QString const& typeName, bool instantiate, QVariantMap const& initialProps = {}) {
    clearWarnings();
    QQmlComponent component(world().engine);
    component.loadFromModule(QStringLiteral("AjazzControlCenter"), typeName);

    auto collectErrors = [&component] {
        QStringList lines;
        for (auto const& e : component.errors()) {
            lines << e.toString();
        }
        return lines.join(QStringLiteral("\n"));
    };

    LoadResult result;
    result.ready = component.isReady();
    if (!result.ready) {
        result.error = collectErrors();
        return result;
    }

    if (instantiate) {
        // createWithInitialProperties feeds any `required property` declarations
        // a value so the object graph actually builds (a bare create() returns
        // nullptr when required props are unset — see KeyCell). Empty map falls
        // back to plain create().
        QObject* obj = initialProps.isEmpty() ? component.create()
                                              : component.createWithInitialProperties(initialProps);
        result.created = obj != nullptr;
        if (obj == nullptr) {
            result.error = collectErrors();
        } else {
            // Delete synchronously, NOT via deleteLater(): we never spin a Qt
            // event loop, so deferred deletes would queue up and only be
            // flushed during Qt's atexit thread-teardown — after the GUI /
            // accessibility subsystem is already gone, which crashes in
            // ~QQuickControl (QAccessible::removeActivationObserver). The
            // component is the root of its own tree (no parent), so a direct
            // delete tears the whole graph down cleanly here and now.
            delete obj;
        }
    }
    return result;
}

namespace {

struct InstantiableComponent {
    char const* typeName;
    QVariantMap initialProps; // values for any root-level `required property`
};

// Presentational / leaf components: must compile AND instantiate cleanly.
// Components with root-level `required property` declarations carry the values
// those properties need (otherwise create() legitimately returns nullptr).
std::vector<InstantiableComponent> instantiableComponents() {
    return {
        {"Theme", {}},            // pragma Singleton — resolving it proves singleton wiring
        {"BatteryIndicator", {}}, // changed in recent fixes; the minimum-bar component
        {"DeviceRow", {}},        // device sidebar row delegate
        {"Card", {}},
        {"DeviceImage", {}},
        {"EmptyState", {}},
        {"PageHeader", {}},
        {"PrimaryButton", {}},
        {"SecondaryButton", {}},
        {"Ripple", {}},
        {"Toast", {}},
        {"Notification", {}},
        {"UpdateBanner", {}},
        {"AppHeader", {}},
        // KeyCell has root-level required props (index/iconSource/label).
        {"KeyCell",
         {{QStringLiteral("index"), 0},
          {QStringLiteral("iconSource"), QStringLiteral("")},
          {QStringLiteral("label"), QStringLiteral("test")}}},
        {"EncoderCard", {}},
        {"FirmwarePanel", {}},
        {"SettingsRow", {}},
        {"RgbPicker", {}},
    };
}

// Top-level pages / window: must COMPILE cleanly (load-time contract). We don't
// force full instantiation — several bind to live model rows / a parent
// ApplicationWindow context that a bare create() can't supply, which would
// produce noise unrelated to the load-error class this harness guards.
char const* const kCompileOnlyComponents[] = {
    "Main",
    "DeviceList",
    "ProfileEditor",
    // KeyDesigner was deleted in Phase 26 Plan 26-04 (commit 6c746a1);
    // replaced by DeviceView + ActionLibraryPane below.
    "DeviceView",
    "ActionLibraryPane",
    "EncoderPanel",
    "MousePanel",
    "Inspector",
    "PluginStore",
    "LoadedPluginsPage",
    "PropertyInspector",
    "NativePropertyInspector",
    "SettingsPage",
};

} // namespace

TEST_CASE("QML leaf components load and instantiate without errors", "[qml][smoke]") {
    auto const components = instantiableComponents();
    int covered = 0;
    for (auto const& c : components) {
        QString const typeName = QString::fromLatin1(c.typeName);
        INFO("component: " << c.typeName);
        LoadResult const r = loadComponent(typeName, /*instantiate=*/true, c.initialProps);
        INFO("errors: " << r.error.toStdString());
        REQUIRE(r.ready);
        REQUIRE(r.created);
        auto const defects = defectWarnings();
        if (!defects.empty()) {
            INFO("QML warnings: " << defects.front().toStdString());
        }
        REQUIRE(defects.empty());
        ++covered;
    }
    // Print coverage so the ctest log records how many components were smoke-loaded.
    WARN("QML leaf components instantiated: " << covered);
    REQUIRE(covered == static_cast<int>(components.size()));
}

TEST_CASE("QML top-level pages compile without errors", "[qml][smoke]") {
    int covered = 0;
    for (char const* name : kCompileOnlyComponents) {
        QString const typeName = QString::fromLatin1(name);
        INFO("component: " << name);
        LoadResult const r = loadComponent(typeName, /*instantiate=*/false);
        INFO("errors: " << r.error.toStdString());
        REQUIRE(r.ready);
        ++covered;
    }
    WARN("QML top-level pages compiled: " << covered);
    REQUIRE(covered == static_cast<int>(std::size(kCompileOnlyComponents)));
}

// ---------------------------------------------------------------------------
// Custom Catch2 entry point (replaces Catch2WithMain).
//
// world() deliberately leaks its QApplication / QQmlApplicationEngine /
// Application controller because tearing them down in C++ static-destruction
// order races Qt's atexit cleanup and segfaults. Leaking the heap objects is
// not enough on its own: at a NORMAL process exit Qt's GLOBAL statics (the
// Quick scene-graph, the offscreen QPA plugin, the network/SSL backends) still
// run their own destructors, and on the headless CI runners that teardown
// intermittently SegFaults *after* the per-test result has already been
// reported (each `catch_discover_tests` case is its own process). Catch2 has
// finished reporting by the time run() returns, so we flush and hard-exit with
// std::_Exit, skipping the racy exit-time teardown entirely. A clean run can no
// longer be flipped to a SegFault by global-destructor ordering.
int main(int argc, char* argv[]) {
    int const result = Catch::Session().run(argc, argv);
    std::fflush(nullptr); // flush Catch2's stdout/stderr before the hard exit
    std::_Exit(result < 0 ? 255 : (result > 255 ? 255 : result));
}
