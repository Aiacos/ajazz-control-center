// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_url_request_interceptor.hpp
 * @brief @c QWebEngineUrlRequestInterceptor that enforces the PI URL policy.
 *
 * Installed per @c QWebEngineProfile by
 * @ref ajazz::app::PropertyInspectorController::loadInspector. Carries the
 * plugin UUID + the Property Inspector directory so deny logs are
 * attributable, and delegates the allow / deny decision to
 * @ref ajazz::app::isLoadUrlAllowed in @ref pi_url_policy.hpp — that
 * separation keeps the policy logic in a pure-C++ TU that the unit-test
 * binary can link without dragging QtWebEngine in.
 *
 * Compiled only when @c AJAZZ_HAVE_WEBENGINE is defined; the host CMake
 * gate guarantees the QtWebEngineCore symbols this header pulls in are
 * available. The @c #ifdef wrapper below is what makes the header safe
 * to include unconditionally from `find src -name '*.cpp'` clang-tidy
 * runs on environments where QtWebEngine is not installed (CI lint
 * job): when the macro is undefined, the file expands to nothing.
 *
 * @see docs/architecture/PLUGIN-SDK.md for the security model.
 */
#pragma once

#ifdef AJAZZ_HAVE_WEBENGINE

#include <QObject>
#include <QString>
#include <QWebEngineUrlRequestInterceptor>

namespace ajazz::app {

class PIUrlRequestInterceptor final : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PIUrlRequestInterceptor)

public:
    /**
     * @param pluginUuid Plugin manifest UUID — surfaced in deny logs so
     *                   the noise from one bad plugin can be grepped out.
     * @param piDir      Absolute path to the Property Inspector directory
     *                   (parent of the @c htmlAbsPath passed to
     *                   @c PropertyInspectorController::loadInspector).
     * @param parent     QObject parent (must be the
     *                   @c QWebEngineProfile so the interceptor's lifetime
     *                   matches the profile's).
     */
    PIUrlRequestInterceptor(QString pluginUuid, QString piDir, QObject* parent);

    ~PIUrlRequestInterceptor() override;

    /// Update the PI directory in place. The same plugin profile may host
    /// multiple actions whose PI files live in different subdirectories of
    /// the bundle; rather than allocate a fresh interceptor per action, we
    /// just rebase the directory scope when the controller swaps pages.
    void setPiDir(QString const& piDir);

    void interceptRequest(QWebEngineUrlRequestInfo& info) override;

private:
    QString pluginUuid_;
    QString piDir_;
};

} // namespace ajazz::app

#endif // AJAZZ_HAVE_WEBENGINE
