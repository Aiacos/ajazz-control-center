// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_mirabox_github_installer.cpp
 * @brief Pure-logic coverage for MiraboxGithubInstaller: bundle-root detection
 *        (where manifest.json lives in a plugin subtree) and path-safety. The
 *        async GitHub fetch is exercised by live verification, not here.
 */
#include "mirabox_github_installer.hpp"

#include <QStringList>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::MiraboxGithubInstaller;

TEST_CASE("findBundleRoot locates the manifest.json directory",
          "[plugins][mirabox-github][install]") {
    // manifest.json at the subtree root → bundle root is "".
    {
        QStringList const paths{"manifest.json", "en.json", "plugin/index.js"};
        auto const root = MiraboxGithubInstaller::findBundleRoot(paths);
        REQUIRE(root.has_value());
        CHECK(root->isEmpty());
    }
    // manifest.json nested in a *.sdPlugin subdir (the battery/WebCam shape).
    {
        QStringList const paths{
            "CMakeLists.txt",
            "Ghub.cpp",
            "com.mirabox.streamdock.battery.sdPlugin/manifest.json",
            "com.mirabox.streamdock.battery.sdPlugin/plugin/app.js",
        };
        auto const root = MiraboxGithubInstaller::findBundleRoot(paths);
        REQUIRE(root.has_value());
        CHECK(*root == QStringLiteral("com.mirabox.streamdock.battery.sdPlugin"));
    }
    // Source-only project (no manifest.json anywhere) → nullopt.
    {
        QStringList const paths{"index.html", "package.json", "src/main.ts", "vite.config.ts"};
        CHECK_FALSE(MiraboxGithubInstaller::findBundleRoot(paths).has_value());
    }
    // Two manifests at different depths → the shallowest wins.
    {
        QStringList const paths{
            "wrapper/inner/manifest.json",
            "top/manifest.json",
        };
        auto const root = MiraboxGithubInstaller::findBundleRoot(paths);
        REQUIRE(root.has_value());
        CHECK(*root == QStringLiteral("top"));
    }
}

TEST_CASE("isSafeRelPath rejects traversal and absolute paths",
          "[plugins][mirabox-github][install]") {
    using I = MiraboxGithubInstaller;
    // Safe.
    CHECK(I::isSafeRelPath(QStringLiteral("manifest.json")));
    CHECK(I::isSafeRelPath(QStringLiteral("plugin/app.js")));
    CHECK(I::isSafeRelPath(QStringLiteral("a/b/c/icon.png")));
    // Unsafe.
    CHECK_FALSE(I::isSafeRelPath(QString{}));                        // empty
    CHECK_FALSE(I::isSafeRelPath(QStringLiteral("/etc/passwd")));    // absolute
    CHECK_FALSE(I::isSafeRelPath(QStringLiteral("../escape")));      // parent traversal
    CHECK_FALSE(I::isSafeRelPath(QStringLiteral("a/../../escape"))); // embedded traversal
    CHECK_FALSE(I::isSafeRelPath(QStringLiteral("C:/Windows/x")));   // drive prefix
    CHECK_FALSE(I::isSafeRelPath(QStringLiteral("a\\b")));           // backslash
}
