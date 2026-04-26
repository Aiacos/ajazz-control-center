// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_url_policy.cpp
 * @brief Implementation of the Property Inspector URL allow/deny helpers.
 *
 * Pure C++ — no Qt WebEngine includes — so the test binary can link
 * against this TU without dragging WebEngine into a unit-test build that
 * does not have it. The @ref PIUrlRequestInterceptor wrapper that calls
 * these helpers from a real @c QWebEngineUrlRequestInterceptor lives in
 * @ref pi_bridge.cpp where the WebEngine surface is already pulled in.
 */
#include "pi_url_policy.hpp"

#include <QByteArray>
#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QString>

namespace ajazz::app {

namespace {

/// Compare two filesystem paths after canonicalisation. Both inputs must be
/// absolute. Returns true iff @p child resolves to a path equal to or below
/// @p parent. We deliberately use a textual compare on cleaned paths rather
/// than @c QFileInfo::canonicalFilePath, because canonicalising follows
/// symlinks — and the goal here is to refuse a request whose textual path
/// escapes the PI dir, regardless of whether the symlink target happens to
/// land back inside.
bool pathIsInsideDir(QString const& child, QString const& parent) {
    if (child.isEmpty() || parent.isEmpty()) {
        return false;
    }
    QString const cleanChild = QDir::cleanPath(child);
    QString cleanParent = QDir::cleanPath(parent);
    if (!cleanParent.endsWith(QLatin1Char('/'))) {
        cleanParent.append(QLatin1Char('/'));
    }
    // Allow exact-equality (the PI dir itself, harmless) plus any path that
    // begins with `<piDir>/`. The trailing slash on cleanParent guarantees
    // we don't match `/foo/bar` against piDir `/foo/ba`.
    QString const cleanParentSansSlash = cleanParent.left(cleanParent.size() - 1);
    if (cleanChild == cleanParentSansSlash) {
        return true;
    }
    return cleanChild.startsWith(cleanParent);
}

bool hostInAllowlist(QString const& host, std::span<std::string_view const> allowlist) {
    if (host.isEmpty()) {
        return false;
    }
    QByteArray const hostUtf8 = host.toUtf8();
    std::string_view const sv{hostUtf8.constData(), static_cast<std::size_t>(hostUtf8.size())};
    for (std::string_view const allowed : allowlist) {
        if (sv == allowed) {
            return true;
        }
    }
    return false;
}

} // namespace

UrlDecision isLoadUrlAllowed(QUrl const& url,
                             QString const& piDir,
                             std::span<std::string_view const> allowlist) {
    if (!url.isValid() || url.isEmpty()) {
        return UrlDecision::DenyMalformed;
    }
    QString const scheme = url.scheme().toLower();
    if (scheme.isEmpty()) {
        return UrlDecision::DenyMalformed;
    }

    // Qt-internal schemes used by the QWebChannel / Qt resource system.
    // Letting these through is required for `qrc:///qtwebchannel/qwebchannel.js`
    // and for inline data URIs. blob: is what FileReader / fetch produce
    // for in-memory blobs; data: is fine for embedded base64 images.
    if (scheme == QLatin1String("qrc") || scheme == QLatin1String("blob") ||
        scheme == QLatin1String("data")) {
        return UrlDecision::Allow;
    }

    if (scheme == QLatin1String("file")) {
        // No piDir scope means the interceptor was installed without a PI
        // directory to bound. Refuse all file:// to stay defence-in-depth.
        if (piDir.isEmpty()) {
            return UrlDecision::DenyFileOutsidePiDir;
        }
        QString const localPath = url.toLocalFile();
        if (localPath.isEmpty()) {
            return UrlDecision::DenyMalformed;
        }
        if (pathIsInsideDir(localPath, piDir)) {
            return UrlDecision::Allow;
        }
        return UrlDecision::DenyFileOutsidePiDir;
    }

    if (scheme == QLatin1String("https")) {
        if (hostInAllowlist(url.host(), allowlist)) {
            return UrlDecision::Allow;
        }
        return UrlDecision::DenyHttpsNotAllowlist;
    }

    if (scheme == QLatin1String("http")) {
        // Defence-in-depth: even with LocalContentCanAccessRemoteUrls=false
        // we never want a PI page to issue plain HTTP requests. Block them
        // explicitly so the interceptor produces a security-review-friendly
        // log entry instead of a silent ChromeNet redirect.
        return UrlDecision::DenyHttpRejected;
    }

    return UrlDecision::DenySchemeBlocked;
}

UrlDecision isLoadUrlAllowed(QUrl const& url, QString const& piDir) {
    return isLoadUrlAllowed(url, piDir, std::span<std::string_view const>{kPiHttpsCdnAllowlist});
}

UrlDecision isOpenUrlAllowed(QUrl const& url) {
    if (!url.isValid() || url.isEmpty()) {
        return UrlDecision::DenyMalformed;
    }
    QString const scheme = url.scheme().toLower();
    if (scheme.isEmpty()) {
        return UrlDecision::DenyMalformed;
    }
    if (scheme != QLatin1String("https")) {
        // Reject http:, file:, mailto:, javascript:, anything else. Opening
        // external apps from plugin code needs a separate first-call
        // confirmation; until that lands the policy is "https only".
        if (scheme == QLatin1String("http")) {
            return UrlDecision::DenyHttpRejected;
        }
        return UrlDecision::DenySchemeBlocked;
    }
    if (url.host().isEmpty()) {
        return UrlDecision::DenyMalformed;
    }
    return UrlDecision::Allow;
}

UrlDecision isOpenUrlAllowed(QString const& url) {
    // QUrl::StrictMode rejects malformed URLs up front so we don't have to
    // reimplement the parser. javascript: parses as a valid URL with that
    // scheme, which is exactly what we want to detect-and-reject.
    QUrl const parsed{url, QUrl::StrictMode};
    return isOpenUrlAllowed(parsed);
}

} // namespace ajazz::app
