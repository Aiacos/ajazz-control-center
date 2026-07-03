// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_loaded_plugins_model.cpp
 * @brief Unit tests for @ref ajazz::app::LoadedPluginsModel — the
 *        platformStatus derive (WINPLG-03) and the role contract.
 *
 * The platformStatus derive (`platformStatusOf`) is a private static, so
 * these tests exercise it end-to-end through the public model surface:
 * populate the model with @ref ajazz::plugins::PluginInfo fixtures whose
 * @c winClass is set to the Plan-01 mapping (0=NotWindowsOnly,
 * 1=WsOnlyIpc, 2=VendorDll) and read the @c PlatformStatusRole back via
 * @c data(). This both locks the derive AND proves the role is wired into
 * @c data() / @c roleNames() (the QML chip's data source).
 *
 * Catch2 titles are feature-token-prefixed ("LoadedPluginsModel ...") so
 * `ctest --preset linux-release -R "loaded-plugins|LoadedPlugins"` matches
 * by NAME (ctest filters by test name, not by Catch2 tag).
 */
#include "i_plugin_host2.hpp"
#include "loaded_plugins_model.hpp"
#include "qt_app_fixture.hpp"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include <vector>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::IPluginHost2;
using ajazz::app::LoadedPluginsModel;
using ajazz::app::PluginManifest;
using ajazz::app::SdPluginServer;
using ajazz::plugins::PluginInfo;
using ajazz::tests::qtApp;

namespace {

/// Build a one-row model whose single PluginInfo carries @p winClass, then
/// return the @c PlatformStatusRole string for row 0. Exercises the private
/// static derive through the public role surface.
QString platformStatusForWinClass(int winClass) {
    qtApp(); // ensure a QCoreApplication exists for the QObject-based model
    LoadedPluginsModel model(nullptr);
    std::vector<PluginInfo> rows;
    PluginInfo info;
    info.id = "com.example.test";
    info.name = "Test";
    info.version = "1.0.0";
    info.winClass = winClass;
    rows.push_back(info);
    model.setPlugins(std::move(rows));
    return model.data(model.index(0, 0), LoadedPluginsModel::PlatformStatusRole).toString();
}

/// A minimal in-process IPluginHost2 whose plugins() returns a MERGED vector
/// shaped exactly like UnifiedPluginHost::plugins() (a .sdPlugin entry with a
/// real winClass followed by a Python entry with winClass==0). Exercises the
/// REAL production population path (setPluginHost2 + refresh), not the derive.
class FakeMergedHost2 final : public IPluginHost2 {
public:
    std::vector<PluginManifest> discover() override { return {}; }
    void spawn(PluginManifest const&) override {}
    void shutdown() override {}
    bool dispatch(QString const&, QString const&, QJsonObject const&) override { return false; }
    [[nodiscard]] int connectedPluginCount() const noexcept override { return 0; }
    [[nodiscard]] SdPluginServer* pluginServer() const noexcept override { return nullptr; }

    [[nodiscard]] std::vector<PluginInfo> plugins() override {
        std::vector<PluginInfo> out;
        // .sdPlugin entry: win-only WS/IPC -> winClass=1 (the only inventory
        // that ever carries a nonzero winClass; this is what CR-01 surfaces).
        PluginInfo sd;
        sd.id = "com.example.winonly";
        sd.name = "WinOnly";
        sd.version = "1.0.0";
        sd.winClass = 1;
        out.push_back(sd);
        // Python entry: always winClass=0 (NotWindowsOnly) — must remain present
        // in the merged list so CR-01 does not regress the Python plugin list.
        PluginInfo py;
        py.id = "com.example.python";
        py.name = "PyPlugin";
        py.version = "2.0.0";
        py.winClass = 0;
        out.push_back(py);
        return out;
    }
};

} // namespace

TEST_CASE("LoadedPluginsModel platformStatus WsOnlyIpc maps to native", "[loaded-plugins-model]") {
    // winClass=1 (WsOnlyIpc) runs natively on any platform — the WS-only-IPC
    // plugin needs no Wine, so the chip is always "native".
    REQUIRE(platformStatusForWinClass(1) == QStringLiteral("native"));
}

TEST_CASE("LoadedPluginsModel platformStatus VendorDll maps to unsupported (wine false)",
          "[loaded-plugins-model]") {
    // winClass=2 (VendorDll) with wineAvailable hard-false this phase ->
    // "unsupported". The explicit wine branch is retained in the derive so a
    // future WINPLG-03 launch phase flips one input, not the derive shape.
    REQUIRE(platformStatusForWinClass(2) == QStringLiteral("unsupported"));
}

TEST_CASE("LoadedPluginsModel platformStatus NotWindowsOnly maps to empty",
          "[loaded-plugins-model]") {
    // winClass=0 (NotWindowsOnly): no Windows-classification chip — empty
    // string hides it (the chip binds `visible: row.platformStatus !== ""`).
    REQUIRE(platformStatusForWinClass(0).isEmpty());
}

TEST_CASE("LoadedPluginsModel exposes platformStatus in roleNames", "[loaded-plugins-model]") {
    qtApp();
    LoadedPluginsModel model(nullptr);
    QHash<int, QByteArray> const roles = model.roleNames();
    REQUIRE(roles.contains(LoadedPluginsModel::PlatformStatusRole));
    REQUIRE(roles.value(LoadedPluginsModel::PlatformStatusRole) ==
            QByteArrayLiteral("platformStatus"));
}

// ---------------------------------------------------------------------------
// WR-02: production-path coverage. The earlier tests call setPlugins() with a
// hand-built winClass row, which masked CR-01 (the model was never fed from the
// unified host in the shipping build). This test drives the REAL wiring path:
// setPluginHost2() + refresh() pulling the MERGED inventory, and asserts both
// that a .sdPlugin's nonzero winClass surfaces a non-empty platformStatus AND
// that the Python (winClass=0) entry is still present (CR-01 must not regress
// the Python plugin list).
// ---------------------------------------------------------------------------

TEST_CASE("LoadedPluginsModel refresh from unified host surfaces win platformStatus (WR-02)",
          "[loaded-plugins-model]") {
    qtApp();
    LoadedPluginsModel model(nullptr);
    FakeMergedHost2 host;

    // Wire the unified host and pull through the production refresh() path.
    model.setPluginHost2(&host);
    model.refresh();

    // Merged inventory: both the .sdPlugin and the Python entry are present.
    REQUIRE(model.rowCount() == 2);

    // Find each row by id and assert the platformStatus the QML chip binds to.
    auto statusForId = [&model](QString const& id) -> QString {
        for (int r = 0; r < model.rowCount(); ++r) {
            QModelIndex const idx = model.index(r, 0);
            if (model.data(idx, LoadedPluginsModel::IdRole).toString() == id) {
                return model.data(idx, LoadedPluginsModel::PlatformStatusRole).toString();
            }
        }
        return QStringLiteral("<missing>");
    };

    // The .sdPlugin (winClass=1, WsOnlyIpc) surfaces a non-empty chip -> "native".
    // This is exactly what CR-01 fixed: in the old wiring this row never reached
    // the model at all, so the chip was dead. A direct setPlugins() test could not
    // catch that regression.
    REQUIRE(statusForId(QStringLiteral("com.example.winonly")) == QStringLiteral("native"));

    // The Python entry (winClass=0) is still present with an empty (hidden) chip.
    REQUIRE(statusForId(QStringLiteral("com.example.python")).isEmpty());
}
