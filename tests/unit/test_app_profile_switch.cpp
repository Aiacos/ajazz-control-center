// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_app_profile_switch.cpp
 * @brief Per-app profile auto-switch matcher (APROF-02, Phase 34-04).
 *
 * GREEN as of Plan 04: the foreground appId -> Profile::applicationHints match
 * (case-insensitive) -> resolve that profile; no match -> the device's default
 * profile; identical re-switch -> idempotent no-op. The matcher lives in
 * ProfileController::resolveProfileForApp + the pure free helper
 * appIdMatchesHints; the idempotent guard is the same comparison Application
 * applies before loadProfileById (resolved == activeProfileId -> skip).
 *
 * The Wave-0 synthetic-injection seam (StubActiveWindowWatcher::injectForeground)
 * is exercised here to prove the watcher onChange -> resolver path end to end,
 * mirroring the Application wiring (resolve, idempotent guard, would-activate).
 *
 * Tags: [app_profile_switch] — select with:
 *   ctest --preset linux-release -R app_profile_switch
 */
#include "ajazz/core/active_window_watcher.hpp"
#include "ajazz/core/profile.hpp"
#include "profile_controller.hpp"
#include "qt_app_fixture.hpp"

#include <QString>
#include <QStringList>

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace ajazz::core;
using ajazz::app::appIdMatchesHints;
using ajazz::app::ProfileController;

namespace {

/// Create a device-scoped profile with the given name + applicationHints and
/// persist it; returns its id. Leaves it active.
QString makeProfileWithHints(ProfileController& ctrl,
                             QString const& name,
                             QString const& device,
                             QStringList const& hints) {
    QString const id = ctrl.createProfile(name, device);
    ctrl.setApplicationHints(hints); // persists + rescans
    return id;
}

} // namespace

// ---- pure matcher --------------------------------------------------------

TEST_CASE("app_profile_switch appIdMatchesHints is case-insensitive", "[app_profile_switch]") {
    CHECK(appIdMatchesHints(QStringLiteral("firefox"), {QStringLiteral("firefox")}));
    CHECK(appIdMatchesHints(QStringLiteral("Firefox"), {QStringLiteral("firefox")}));
    CHECK(appIdMatchesHints(QStringLiteral("FIREFOX"),
                            {QStringLiteral("code"), QStringLiteral("firefox")}));
    CHECK_FALSE(appIdMatchesHints(QStringLiteral("chromium"), {QStringLiteral("firefox")}));
    CHECK_FALSE(appIdMatchesHints(QStringLiteral(""), {QStringLiteral("firefox")}));
    CHECK_FALSE(appIdMatchesHints(QStringLiteral("firefox"), {}));
}

// ---- hint match selects that profile -------------------------------------

TEST_CASE("app_profile_switch foreground appId matching applicationHints selects that profile",
          "[app_profile_switch]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-aprof-match");
    QString const browser =
        makeProfileWithHints(ctrl, QStringLiteral("Browser"), dev, {QStringLiteral("firefox")});
    QString const editor =
        makeProfileWithHints(ctrl, QStringLiteral("Editor"), dev, {QStringLiteral("code")});

    // A foreground appId equal (case-insensitively) to a hint resolves to that
    // profile, regardless of which profile is currently active.
    CHECK(ctrl.resolveProfileForApp(QStringLiteral("firefox"), dev) == browser);
    CHECK(ctrl.resolveProfileForApp(QStringLiteral("FireFox"), dev) == browser);
    CHECK(ctrl.resolveProfileForApp(QStringLiteral("code"), dev) == editor);

    ctrl.deleteProfile(browser);
    ctrl.deleteProfile(editor);
}

// ---- no match falls back to the device default ---------------------------

TEST_CASE("app_profile_switch no applicationHints match falls back to the default profile",
          "[app_profile_switch]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    // Default = first profile for the device by name. "Alpha" sorts before
    // "Zeta", so an unmatched app resolves to Alpha.
    QString const dev = QStringLiteral("test-aprof-fallback");
    QString const alpha =
        makeProfileWithHints(ctrl, QStringLiteral("Alpha"), dev, {QStringLiteral("term")});
    QString const zeta =
        makeProfileWithHints(ctrl, QStringLiteral("Zeta"), dev, {QStringLiteral("mail")});

    QString const resolved =
        ctrl.resolveProfileForApp(QStringLiteral("unknown-app-with-no-hint"), dev);
    CHECK(resolved == alpha);

    ctrl.deleteProfile(alpha);
    ctrl.deleteProfile(zeta);
}

// ---- idempotent guard: identical re-switch is a no-op --------------------

TEST_CASE("app_profile_switch re-resolving the active profile is an idempotent no-op",
          "[app_profile_switch]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-aprof-idem");
    QString const browser =
        makeProfileWithHints(ctrl, QStringLiteral("Browser"), dev, {QStringLiteral("firefox")});

    // Make Browser active, then resolve its own app again. The Application
    // idempotent guard is exactly this comparison: resolved == activeProfileId.
    ctrl.loadProfileById(browser);
    REQUIRE(ctrl.activeProfileId() == browser);

    QString const resolved = ctrl.resolveProfileForApp(QStringLiteral("firefox"), dev);
    CHECK(resolved == browser);
    bool const wouldSwitch = !resolved.isEmpty() && resolved != ctrl.activeProfileId();
    CHECK_FALSE(wouldSwitch); // guard short-circuits: no loadProfileById call

    ctrl.deleteProfile(browser);
}

// ---- watcher onChange seam drives the resolver ---------------------------

TEST_CASE("app_profile_switch watcher injectForeground feeds the resolver onChange path",
          "[app_profile_switch]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-aprof-seam");
    QString const browser =
        makeProfileWithHints(ctrl, QStringLiteral("Browser"), dev, {QStringLiteral("firefox")});
    QString const editor =
        makeProfileWithHints(ctrl, QStringLiteral("Editor"), dev, {QStringLiteral("code")});

    // Start from Browser active so the first inject ("code") is a real switch
    // (not an idempotent no-op against the just-created Editor).
    ctrl.loadProfileById(browser);
    REQUIRE(ctrl.activeProfileId() == browser);

    // Mirror the Application onChange wiring: resolve + idempotent guard +
    // (would) activate, driven through the synthetic-injection seam.
    StubActiveWindowWatcher stub;
    QString lastActivated;
    stub.start([&](ActiveWindowInfo info) {
        QString const appId = QString::fromStdString(info.appId);
        QString const resolved = ctrl.resolveProfileForApp(appId, dev);
        if (resolved.isEmpty() || resolved == ctrl.activeProfileId()) {
            return; // idempotent guard
        }
        ctrl.loadProfileById(resolved);
        lastActivated = ctrl.activeProfileId();
    });

    stub.injectForeground(ActiveWindowInfo{"code", ""});
    CHECK(lastActivated == editor);

    stub.injectForeground(ActiveWindowInfo{"firefox", ""});
    CHECK(lastActivated == browser);

    ctrl.deleteProfile(browser);
    ctrl.deleteProfile(editor);
}
