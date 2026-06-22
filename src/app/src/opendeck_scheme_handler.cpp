// SPDX-License-Identifier: GPL-3.0-or-later
#include "opendeck_scheme_handler.hpp"

#include <QFile>
#include <QMimeDatabase>
#include <QMimeType>
#include <QString>
#include <QUrl>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace ajazz::app {

void registerOpenDeckScheme() {
    // Only register once (registering the same name twice warns).
    if (QWebEngineUrlScheme::schemeByName(kOpenDeckScheme).name() == kOpenDeckScheme) {
        return;
    }
    QWebEngineUrlScheme scheme(kOpenDeckScheme);
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setDefaultPort(QWebEngineUrlScheme::PortUnspecified);
    // Secure + local-access so the SvelteKit SPA hydrates and its same-origin
    // Fetch (version.json) + module loads are permitted.
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::LocalAccessAllowed |
                    QWebEngineUrlScheme::CorsEnabled | QWebEngineUrlScheme::FetchApiAllowed);
    QWebEngineUrlScheme::registerScheme(scheme);
}

OpenDeckSchemeHandler::OpenDeckSchemeHandler(QObject* parent)
    : QWebEngineUrlSchemeHandler(parent) {}

void OpenDeckSchemeHandler::requestStarted(QWebEngineUrlRequestJob* job) {
    QUrl const url = job->requestUrl();
    QString path = url.path();
    if (path.isEmpty() || path == QStringLiteral("/")) {
        path = QStringLiteral("/index.html");
    }
    // Map opendeck://app/<path> -> :/opendeck/<path>. Reject path traversal.
    if (path.contains(QStringLiteral(".."))) {
        job->fail(QWebEngineUrlRequestJob::UrlInvalid);
        return;
    }
    QString const resourcePath = QStringLiteral(":/opendeck") + path;
    auto* file = new QFile(resourcePath, job);
    if (!file->open(QIODevice::ReadOnly)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }
    QMimeType const mime = QMimeDatabase().mimeTypeForFile(resourcePath);
    QByteArray contentType = mime.name().toUtf8();
    // SvelteKit ships ESM .js — ensure the JS MIME so module scripts execute.
    if (path.endsWith(QStringLiteral(".js")) || path.endsWith(QStringLiteral(".mjs"))) {
        contentType = "text/javascript";
    } else if (path.endsWith(QStringLiteral(".json"))) {
        contentType = "application/json";
    } else if (path.endsWith(QStringLiteral(".css"))) {
        contentType = "text/css";
    }
    job->reply(contentType, file);
}

} // namespace ajazz::app
