// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file pi_url_policy.hpp
 * @brief Pure helpers that decide whether a Property Inspector URL is allowed.
 *
 * Lives in its own translation unit (no Qt WebEngine dependency, no
 * @c QWebEnginePage / @c QWebEngineUrlRequestInfo) so the allow / deny
 * decision can be unit-tested without booting the WebEngine surface. The
 * @ref PIUrlRequestInterceptor (defined in @ref pi_bridge.cpp alongside the
 * @ref PIBridge wiring) is a thin shim that calls these helpers from inside
 * a @c QWebEngineUrlRequestInterceptor::interceptRequest override.
 *
 * Two policies live here:
 *
 *   * @ref isLoadUrlAllowed — decides whether the WebEngine page is allowed
 *     to load a sub-resource at all (file://, https://, http://, qrc://,
 *     blob://, data:; everything else is rejected). file:// is constrained
 *     to the per-plugin Property Inspector directory; https:// to the CDN
 *     allowlist documented in @ref kPiHttpsCdnAllowlist.
 *
 *   * @ref isOpenUrlAllowed — decides whether @c \$SD.openUrl(url) is
 *     allowed to hand the URL to @c QDesktopServices::openUrl. Stricter
 *     than the load policy: only https:// is accepted. javascript:, file:,
 *     mailto:, http: and every other scheme are blocked.
 *
 * Both helpers operate on parsed URL fields (scheme + host + path) rather
 * than @c QUrl, so the test binary doesn't need to link Qt WebEngine.
 */
#pragma once

#include <QString>
#include <QStringView>
#include <QUrl>

#include <array>
#include <span>
#include <string_view>

namespace ajazz::app {

/**
 * @brief Phase 1 CDN allowlist for the Property Inspector https:// loader.
 *
 * Kept as a `static constexpr std::array<std::string_view, N>` so it is
 * grep-able for security review (`git grep kPiHttpsCdnAllowlist`). Adding
 * an entry is a separate PR with security review.
 *
 * jsdelivr + unpkg cover the overwhelming majority of vendor PI assets
 * (Stream Deck SDK uses unpkg-style `<script src="https://unpkg.com/...">`
 * imports, OpenDeck and AJAZZ Streamdock plugins lean on jsdelivr).
 *
 * Matching is host-equality: a request to e.g.
 * `https://evil.cdn.jsdelivr.net.attacker.example/foo.js` does NOT match
 * the `cdn.jsdelivr.net` entry because the comparison is the entire host
 * string, not a suffix.
 */
inline constexpr std::array<std::string_view, 2> kPiHttpsCdnAllowlist{
    "cdn.jsdelivr.net",
    "unpkg.com",
};

/**
 * @brief Decision returned by @ref isLoadUrlAllowed and @ref isOpenUrlAllowed.
 *
 * The deny variants are split so callers can pick a concrete log message
 * — "schemes other than https are not allowed" reads better than a flat
 * boolean false would.
 */
enum class UrlDecision {
    Allow,                 ///< The request may proceed.
    DenySchemeBlocked,     ///< Scheme is forbidden outright (e.g. ftp:, javascript:).
    DenyHttpRejected,      ///< Plain http:// — blocked even with LocalContent... = false.
    DenyFileOutsidePiDir,  ///< file:// path lies outside the per-plugin PI directory.
    DenyHttpsNotAllowlist, ///< https:// host is not in the CDN allowlist.
    DenyMalformed,         ///< URL did not parse / had no scheme / had no path.
};

/**
 * @brief Decide whether a sub-resource load from a Property Inspector page
 *        should be allowed.
 *
 * The decision tree (in evaluation order):
 *
 *   1. Empty / unparsable URL                  → @c DenyMalformed
 *   2. scheme == "qrc" / "blob" / "data"        → @c Allow (Qt-internal)
 *   3. scheme == "file"  →
 *        absolute(URL.path) starts with absolute(piDir)  → @c Allow
 *        otherwise                                       → @c DenyFileOutsidePiDir
 *   4. scheme == "https" →
 *        host ∈ allowlist  → @c Allow
 *        otherwise         → @c DenyHttpsNotAllowlist
 *   5. scheme == "http"                         → @c DenyHttpRejected
 *   6. anything else                            → @c DenySchemeBlocked
 *
 * @param url       The request URL (parsed by the caller).
 * @param piDir     Absolute path to the Property Inspector directory (the
 *                  parent dir of the @c htmlAbsPath passed to
 *                  @c PropertyInspectorController::loadInspector). Empty →
 *                  every file:// is rejected (defence-in-depth: the
 *                  interceptor was somehow installed without a directory
 *                  scope).
 * @param allowlist The https:// host allowlist (defaults to
 *                  @ref kPiHttpsCdnAllowlist; tests pass their own).
 */
UrlDecision isLoadUrlAllowed(QUrl const& url,
                             QString const& piDir,
                             std::span<std::string_view const> allowlist);

/// Convenience overload that uses @ref kPiHttpsCdnAllowlist.
UrlDecision isLoadUrlAllowed(QUrl const& url, QString const& piDir);

/**
 * @brief Decide whether @c \$SD.openUrl(url) should hand the URL to
 *        @c QDesktopServices::openUrl.
 *
 * Only https:// is accepted in this pass. javascript:, file:, mailto:, http:
 * and every other scheme are rejected — opening external apps or shells
 * from plugin code needs a separate first-call confirmation prompt that is
 * tracked under @c TODO(pi-prompt) in @ref pi_bridge.cpp.
 *
 * @param url The URL the PI's JS is asking us to open.
 */
UrlDecision isOpenUrlAllowed(QUrl const& url);

/// String overload — parses with @c QUrl::fromUserInput-equivalent strictness.
UrlDecision isOpenUrlAllowed(QString const& url);

} // namespace ajazz::app
