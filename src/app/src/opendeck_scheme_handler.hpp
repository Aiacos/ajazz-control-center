// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file opendeck_scheme_handler.hpp
 * @brief Serves the bundled OpenDeck SPA over a custom `opendeck://app/` URL
 *        scheme so the SvelteKit client routes correctly and `fetch()` works.
 *
 * Loading the SPA directly from `qrc:/opendeck/index.html` fails: the SvelteKit
 * router derives its route from `location.pathname` (`/opendeck/index.html` →
 * no match → 404) and the `qrc:` scheme rejects the Fetch API
 * (`version.json`). A custom secure scheme with host syntax gives the SPA a
 * clean web origin (`opendeck://app/` → route `/`) backed by the `:/opendeck`
 * Qt resource, with Fetch + CORS enabled. See docs/opendeck-ui/.
 *
 * WebEngine-only: this header is compiled solely on the AJAZZ_HAVE_WEBENGINE
 * path (it includes QtWebEngineCore types).
 */
#pragma once

#include <QByteArray>
#include <QWebEngineUrlSchemeHandler>

class QWebEngineUrlRequestJob;

namespace ajazz::app {

/// The custom scheme name. Registered (once, before QtWebEngine init) via
/// registerOpenDeckScheme(); served by OpenDeckSchemeHandler.
inline constexpr char kOpenDeckScheme[] = "opendeck";
/// Canonical root URL the WebUiHost loads.
inline constexpr char kOpenDeckRootUrl[] = "opendeck://app/";

/// Register the `opendeck` URL scheme with the flags the SPA needs (secure,
/// local-access, CORS + Fetch enabled). MUST be called before
/// QtWebEngineQuick::initialize() / any QWebEngine use. Idempotent-safe to call
/// once at startup.
void registerOpenDeckScheme();

/**
 * @class OpenDeckSchemeHandler
 * @brief Maps `opendeck://app/<path>` requests onto the `:/opendeck/<path>` Qt
 *        resource, serving `index.html` for the root.
 *
 * Install on a QWebEngineProfile via setUrlSchemeHandler(kOpenDeckScheme, ...).
 */
class OpenDeckSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit OpenDeckSchemeHandler(QObject* parent = nullptr);
    void requestStarted(QWebEngineUrlRequestJob* job) override;
};

} // namespace ajazz::app
