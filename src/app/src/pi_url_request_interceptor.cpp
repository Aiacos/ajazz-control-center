// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_url_request_interceptor.cpp
 * @brief Implementation of @ref ajazz::app::PIUrlRequestInterceptor.
 */
#include "pi_url_request_interceptor.hpp"

#include "ajazz/core/logger.hpp"
#include "pi_url_policy.hpp"

#include <QUrl>
#include <QWebEngineUrlRequestInfo>

#include <utility>

namespace ajazz::app {

namespace {

/// Map the policy decision to a short tag for the deny log. Keeping the
/// tags fixed and grep-able makes "show me every blocked request from
/// plugin X in the last 24h" trivial in a log explorer.
char const* describeDeny(UrlDecision decision) {
    switch (decision) {
    case UrlDecision::DenySchemeBlocked:
        return "scheme-blocked";
    case UrlDecision::DenyHttpRejected:
        return "http-blocked";
    case UrlDecision::DenyFileOutsidePiDir:
        return "file-outside-pi-dir";
    case UrlDecision::DenyHttpsNotAllowlist:
        return "https-not-in-allowlist";
    case UrlDecision::DenyMalformed:
        return "malformed";
    case UrlDecision::Allow:
        return "allow"; // unreachable in caller.
    }
    return "unknown";
}

} // namespace

PIUrlRequestInterceptor::PIUrlRequestInterceptor(QString pluginUuid, QString piDir, QObject* parent)
    : QWebEngineUrlRequestInterceptor(parent), pluginUuid_(std::move(pluginUuid)),
      piDir_(std::move(piDir)) {}

PIUrlRequestInterceptor::~PIUrlRequestInterceptor() = default;

void PIUrlRequestInterceptor::setPiDir(QString const& piDir) {
    piDir_ = piDir;
}

void PIUrlRequestInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info) {
    QUrl const url = info.requestUrl();
    UrlDecision const decision = isLoadUrlAllowed(url, piDir_);
    if (decision == UrlDecision::Allow) {
        return;
    }
    AJAZZ_LOG_WARN("plugin-pi",
                   "blocked request: plugin={} url='{}' reason={}",
                   pluginUuid_.toStdString(),
                   url.toString().toStdString(),
                   describeDeny(decision));
    info.block(true);
}

} // namespace ajazz::app
