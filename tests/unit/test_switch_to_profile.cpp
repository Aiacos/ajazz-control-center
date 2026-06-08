// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_switch_to_profile.cpp
 * @brief Inbound switchToProfile host-command token resolution (EVENT-03,
 *        Phase 34-04).
 *
 * The Application actionReceived branch maps an UNTRUSTED plugin-supplied
 * profile token to a known profile id via resolveSwitchToProfileToken (exact id
 * match, then a name-or-id match scoped to the device), V5-bounding the token
 * length and rejecting empty/unresolvable tokens without activating anything
 * (no crash). This test drives that pure resolver directly against a live
 * ProfileController library — proving valid tokens resolve and bad tokens are
 * rejected.
 *
 * Tags: [switch_to_profile] — select with:
 *   ctest --preset linux-release -R switch_to_profile
 */
#include "profile_controller.hpp"
#include "qt_app_fixture.hpp"

#include <QString>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::ProfileController;
using ajazz::app::resolveSwitchToProfileToken;

TEST_CASE("switch_to_profile resolves an exact profile id token", "[switch_to_profile]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-stp-id");
    QString const id = ctrl.createProfile(QStringLiteral("Gaming"), dev);
    REQUIRE_FALSE(id.isEmpty());

    // An exact id token resolves to that id (device scope irrelevant for id match).
    CHECK(resolveSwitchToProfileToken(ctrl, id, dev) == id);
    CHECK(resolveSwitchToProfileToken(ctrl, id, QString{}) == id);

    ctrl.deleteProfile(id);
}

TEST_CASE("switch_to_profile resolves a profile name token scoped to the device",
          "[switch_to_profile]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-stp-name");
    QString const id = ctrl.createProfile(QStringLiteral("Streaming"), dev);
    REQUIRE_FALSE(id.isEmpty());

    // A user-visible name token resolves to the matching profile's id.
    CHECK(resolveSwitchToProfileToken(ctrl, QStringLiteral("Streaming"), dev) == id);

    ctrl.deleteProfile(id);
}

TEST_CASE("switch_to_profile rejects an empty token", "[switch_to_profile]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-stp-empty");
    QString const id = ctrl.createProfile(QStringLiteral("Default"), dev);

    CHECK(resolveSwitchToProfileToken(ctrl, QString{}, dev).isEmpty());
    CHECK(resolveSwitchToProfileToken(ctrl, QStringLiteral("   "), dev).isEmpty());

    ctrl.deleteProfile(id);
}

TEST_CASE("switch_to_profile rejects an unknown token without activation", "[switch_to_profile]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    QString const dev = QStringLiteral("test-stp-unknown");
    QString const id = ctrl.createProfile(QStringLiteral("Real"), dev);
    QString const activeBefore = ctrl.activeProfileId();

    // A token that matches no id and no name on the device resolves to "".
    CHECK(resolveSwitchToProfileToken(ctrl, QStringLiteral("does-not-exist"), dev).isEmpty());

    // The resolver is read-only: the active profile is unchanged (no activation
    // happens on a rejected token — Application skips loadProfileById when "").
    CHECK(ctrl.activeProfileId() == activeBefore);

    ctrl.deleteProfile(id);
}

TEST_CASE("switch_to_profile does not match a name from a different device",
          "[switch_to_profile]") {
    ajazz::tests::qtApp();
    ProfileController ctrl(nullptr);

    QString const devA = QStringLiteral("test-stp-devA");
    QString const devB = QStringLiteral("test-stp-devB");
    QString const idA = ctrl.createProfile(QStringLiteral("Shared"), devA);
    QString const idB = ctrl.createProfile(QStringLiteral("Other"), devB);

    // Name "Shared" exists only on devA: scoping the lookup to devB must NOT
    // resolve it (cross-device isolation).
    CHECK(resolveSwitchToProfileToken(ctrl, QStringLiteral("Shared"), devB).isEmpty());
    CHECK(resolveSwitchToProfileToken(ctrl, QStringLiteral("Shared"), devA) == idA);

    ctrl.deleteProfile(idA);
    ctrl.deleteProfile(idB);
}
