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
#include "loaded_plugins_model.hpp"
#include "qt_app_fixture.hpp"

#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include <vector>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::LoadedPluginsModel;
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
