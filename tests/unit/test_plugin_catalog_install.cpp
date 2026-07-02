// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_plugin_catalog_install.cpp
 * @brief US1 (002-streamdeck-plugin-ui, FR-001/FR-002/FR-006): the plugin-store
 *        install affordance is in-app only.
 *
 * Asserts the behavioural contract from contracts/plugin-store-ui.md:
 *   - A row with a resolvable https downloadUrl is reported installableInApp
 *     == true with an empty unavailableReason.
 *   - A row with no / non-https downloadUrl is reported installableInApp
 *     == false with a short "not installable in-app" reason.
 *   - install(uuid) on a NON-installable row is a NO-OP returning false (the old
 *     openUpstream/QDesktopServices browser fallback is removed) and emits
 *     installFinished(uuid, false, <reason>). This is the regression guard for
 *     "no browser launch from the install action": previously install() returned
 *     the openUpstream() result (often true) for such rows.
 *
 * ASCII-only TEST_CASE names + tags per CLAUDE.md (Win32 CMD codepage guard).
 */
#include "plugin_catalog_model.hpp"
#include "qt_app_fixture.hpp"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <QVariant>

#include <vector>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::CatalogEntry;
using ajazz::app::PluginCatalogModel;
using ajazz::tests::qtApp;

namespace ajazz::app {
/// Test seam (befriended by PluginCatalogModel) — injects rows via the private
/// replaceStreamdockRows without widening the production API.
struct PluginCatalogTestAccess {
    static void setRows(PluginCatalogModel& model, std::vector<CatalogEntry> rows) {
        model.replaceStreamdockRows(std::move(rows));
    }
};
} // namespace ajazz::app

namespace {

/// RAII env var guard (mirrors test_catalog_offline.cpp) — keep the catalog
/// fetchers offline so construction does no network and rows are deterministic.
struct EnvGuard {
    QString const name;
    QString const prev;
    bool const hadPrev;
    EnvGuard(QString const& varName, QString const& value)
        : name(varName), prev(qEnvironmentVariable(varName.toLatin1().constData())),
          hadPrev(qEnvironmentVariableIsSet(varName.toLatin1().constData())) {
        qputenv(varName.toLatin1().constData(), value.toLatin1());
    }
    ~EnvGuard() {
        if (hadPrev) {
            qputenv(name.toLatin1().constData(), prev.toLatin1());
        } else {
            qunsetenv(name.toLatin1().constData());
        }
    }
};

/// Find the model row index carrying @p uuid (rows include the bundled snapshot
/// plus any injected via replaceStreamdockRows), or -1.
int rowForUuid(PluginCatalogModel const& model, QString const& uuid) {
    for (int r = 0; r < model.rowCount(); ++r) {
        auto const idx = model.index(r, 0);
        if (model.data(idx, PluginCatalogModel::UuidRole).toString() == uuid) {
            return r;
        }
    }
    return -1;
}

CatalogEntry makeEntry(QString const& uuid, QString const& downloadUrl) {
    CatalogEntry e;
    e.uuid = uuid;
    e.name = uuid;
    e.version = QStringLiteral("1.0.0");
    e.author = QStringLiteral("test");
    e.compatibility = QStringLiteral("streamdock");
    e.source = QStringLiteral("streamdock");
    e.downloadUrl = QUrl(downloadUrl);
    return e;
}

} // namespace

TEST_CASE("PluginCatalogInstall installableInApp roles reflect downloadUrl", "[catalog-install]") {
    qtApp();
    EnvGuard const sd(QStringLiteral("ACC_STREAMDOCK_CATALOG_URL"), QStringLiteral("disabled"));
    EnvGuard const od(QStringLiteral("ACC_OPENDECK_CATALOG_URL"), QStringLiteral("disabled"));
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    PluginCatalogModel::setPluginsDirOverride(tmp.filePath("plugins"));

    PluginCatalogModel model(nullptr);
    std::vector<CatalogEntry> rows;
    rows.push_back(makeEntry(QStringLiteral("com.test.installable"),
                             QStringLiteral("https://example.invalid/pkg.sdPlugin")));
    rows.push_back(makeEntry(QStringLiteral("com.test.nourl"), QString{}));
    rows.push_back(makeEntry(QStringLiteral("com.test.httponly"),
                             QStringLiteral("http://example.invalid/pkg.zip")));
    ajazz::app::PluginCatalogTestAccess::setRows(model, std::move(rows));

    int const okRow = rowForUuid(model, QStringLiteral("com.test.installable"));
    int const noUrlRow = rowForUuid(model, QStringLiteral("com.test.nourl"));
    int const httpRow = rowForUuid(model, QStringLiteral("com.test.httponly"));
    REQUIRE(okRow >= 0);
    REQUIRE(noUrlRow >= 0);
    REQUIRE(httpRow >= 0);

    auto installable = [&](int r) {
        return model.data(model.index(r, 0), PluginCatalogModel::InstallableInAppRole).toBool();
    };
    auto reason = [&](int r) {
        return model.data(model.index(r, 0), PluginCatalogModel::UnavailableReasonRole).toString();
    };

    // Resolvable https → installable, no reason.
    CHECK(installable(okRow));
    CHECK(reason(okRow).isEmpty());

    // Empty URL → not installable, with a short reason.
    CHECK_FALSE(installable(noUrlRow));
    CHECK_FALSE(reason(noUrlRow).isEmpty());

    // Non-https (http) URL → not installable (we never install over plain http).
    CHECK_FALSE(installable(httpRow));
    CHECK_FALSE(reason(httpRow).isEmpty());

    PluginCatalogModel::setPluginsDirOverride(QString{});
}

TEST_CASE("PluginCatalogInstall non-installable row install is a no-op false (no browser)",
          "[catalog-install]") {
    qtApp();
    EnvGuard const sd(QStringLiteral("ACC_STREAMDOCK_CATALOG_URL"), QStringLiteral("disabled"));
    EnvGuard const od(QStringLiteral("ACC_OPENDECK_CATALOG_URL"), QStringLiteral("disabled"));
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    PluginCatalogModel::setPluginsDirOverride(tmp.filePath("plugins"));

    PluginCatalogModel model(nullptr);
    std::vector<CatalogEntry> rows;
    rows.push_back(makeEntry(QStringLiteral("com.test.nourl"), QString{}));
    ajazz::app::PluginCatalogTestAccess::setRows(model, std::move(rows));

    QSignalSpy finished(&model, &PluginCatalogModel::installFinished);
    REQUIRE(finished.isValid());

    // The regression guard: a non-installable row used to fall through to
    // openUpstream() (a browser launch) and could return true. It MUST now be a
    // pure no-op returning false — no browser, nothing installed.
    bool const result = model.install(QStringLiteral("com.test.nourl"));
    CHECK_FALSE(result);

    REQUIRE(finished.count() == 1);
    auto const args = finished.takeFirst();
    CHECK(args.at(0).toString() == QStringLiteral("com.test.nourl"));
    CHECK_FALSE(args.at(1).toBool());             // success == false
    CHECK_FALSE(args.at(2).toString().isEmpty()); // a non-empty reason

    PluginCatalogModel::setPluginsDirOverride(QString{});
}

TEST_CASE("Bundled plugin seeding: first-run copy, consent persist, delete respected",
          "[catalog-install]") {
    auto& app = qtApp();
    Q_UNUSED(app);
    EnvGuard sdGuard("ACC_STREAMDOCK_CATALOG_URL", "disabled");
    EnvGuard odGuard("ACC_OPENDECK_CATALOG_URL", "disabled");

    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());

    // Unique uuid per run so the QSettings seed marker from a previous test
    // run (dev boxes share the org settings file) can never leak in.
    QString const uuid = QStringLiteral("com.test.seed.") +
                         QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QString const dirName = uuid + QStringLiteral(".sdPlugin");

    // Fake bundled payload: bundled/<uuid>.sdPlugin/manifest.json.
    QString const bundledRoot = tmp.filePath(QStringLiteral("bundled"));
    REQUIRE(QDir().mkpath(bundledRoot + QLatin1Char('/') + dirName));
    {
        QFile f(bundledRoot + QLatin1Char('/') + dirName + QStringLiteral("/manifest.json"));
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("{}");
    }
    EnvGuard bundleGuard("AJAZZ_BUNDLED_PLUGINS_DIR", bundledRoot);

    QString const pluginsDir = tmp.filePath(QStringLiteral("plugins"));
    PluginCatalogModel::setPluginsDirOverride(pluginsDir);

    // First construction seeds the bundle and persists per-plugin consent.
    {
        PluginCatalogModel model(nullptr);
        REQUIRE(QFile::exists(pluginsDir + QLatin1Char('/') + dirName +
                              QStringLiteral("/manifest.json")));
        QSettings settings;
        REQUIRE(settings.value(QStringLiteral("plugins/allowed/") + uuid, false).toBool());
        REQUIRE(settings.value(QStringLiteral("plugins/seeded/") + uuid, false).toBool());
    }

    // The user deletes the seeded plugin: the marker must prevent a re-seed.
    REQUIRE(QDir(pluginsDir + QLatin1Char('/') + dirName).removeRecursively());
    {
        PluginCatalogModel model(nullptr);
        REQUIRE_FALSE(QDir(pluginsDir + QLatin1Char('/') + dirName).exists());
    }

    // Cleanup the settings keys this test wrote.
    QSettings settings;
    settings.remove(QStringLiteral("plugins/allowed/") + uuid);
    settings.remove(QStringLiteral("plugins/seeded/") + uuid);
}
