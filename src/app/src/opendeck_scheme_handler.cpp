// SPDX-License-Identifier: GPL-3.0-or-later
#include "opendeck_scheme_handler.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QMimeType>
#include <QStandardPaths>
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

    // Plugin-asset bridge: opendeck://app/__pluginasset__/<dir>/<rel> serves the
    // real file from userPluginsDir()/<dir>/<rel>. The embedded OpenDeck SPA
    // rewrites our `opendeck/__pluginasset__/...` icon strings to this
    // origin-relative URL (ActionList.svelte / getImage strip the `opendeck`
    // prefix), so this is how installed-plugin icons actually load — a file:// or
    // local-webserver URL does not. `..` is already rejected above; a canonical
    // containment check guards against symlink escape.
    QString const kAssetPrefix = QStringLiteral("/__pluginasset__/");
    if (path.startsWith(kAssetPrefix)) {
        QString const root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                             QStringLiteral("/plugins");
        QFileInfo const target(QDir(root).filePath(path.mid(kAssetPrefix.size())));
        QString const canonical = target.canonicalFilePath();
        QString const canonicalRoot = QFileInfo(root).canonicalFilePath();
        if (canonical.isEmpty() || canonicalRoot.isEmpty() ||
            !canonical.startsWith(canonicalRoot + QLatin1Char('/'))) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        auto* assetFile = new QFile(canonical, job);
        if (!assetFile->open(QIODevice::ReadOnly)) {
            job->fail(QWebEngineUrlRequestJob::UrlNotFound);
            return;
        }
        QByteArray ct = QMimeDatabase().mimeTypeForFile(canonical).name().toUtf8();
        if (ct.isEmpty()) {
            ct = QByteArrayLiteral("application/octet-stream");
        }
        job->reply(ct, assetFile);
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
