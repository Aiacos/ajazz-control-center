// SPDX-License-Identifier: GPL-3.0-or-later
#include "plugin_asset_server.hpp"

#include "ajazz/core/logger.hpp"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

namespace ajazz::app {

namespace {

/// Plugins root — mirrors plugin_catalog_model.cpp userPluginsDir() (the
/// non-override path; the test override does not apply to the runtime server).
[[nodiscard]] QString pluginsRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
           QStringLiteral("/plugins");
}

void sendStatus(QTcpSocket* sock, char const* status) {
    sock->write(QByteArrayLiteral("HTTP/1.1 "));
    sock->write(status);
    sock->write(QByteArrayLiteral("\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"));
}

void sendBody(QTcpSocket* sock, QByteArray const& contentType, QByteArray const& body) {
    QByteArray header = QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: ");
    header += contentType;
    header += QByteArrayLiteral("\r\nContent-Length: ");
    header += QByteArray::number(body.size());
    // The SPA origin is opendeck://app — allow it to read the asset (and the PI
    // iframe, served cross-origin from this loopback webserver).
    header += QByteArrayLiteral("\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
    sock->write(header);
    sock->write(body);
}

/// Property-inspector marker suffixes OpenDeck's SPA appends to the iframe `src`
/// (PropertyInspectorView.svelte -> getWebserverUrl(pi + "|opendeck_property_inspector")).
constexpr QLatin1String kPiMarker{"|opendeck_property_inspector"};
constexpr QLatin1String kPiChildMarker{"|opendeck_property_inspector_child"};

/// The shim OpenDeck's webserver appends to every property-inspector HTML page.
/// A PI is served from this loopback origin (port base + 2), a DIFFERENT origin
/// than the SPA, so the SPA cannot call connectElgatoStreamDeckSocket on it
/// directly — it postMessages a "connect" event the injected script consumes.
/// The script also re-implements window.open (as an in-page iframe) and
/// window.fetch (proxied through the SPA to bypass CORS). Kept byte-for-byte in
/// sync with src-tauri/src/plugins/webserver.rs in the OpenDeck submodule — the
/// PI contract lives there; do not diverge without re-checking upstream.
[[nodiscard]] QByteArray piInspectorShim() {
    return QByteArrayLiteral(R"HTML(
				<div id="opendeck_iframe_container" style="position: absolute; z-index: 100; top: 0; left: 0; width: 100%; height: 100%; display: none;"></div>
				<script>
					const opendeck_window_open = window.open;
					const opendeck_iframe_container = document.getElementById("opendeck_iframe_container");

					window.addEventListener("message", (event) => {
						const data = event.data;
						if (data.event == "connect") {
							event.stopImmediatePropagation();
							if (typeof connectOpenActionSocket === "function") connectOpenActionSocket(...data.payload);
							else connectElgatoStreamDeckSocket(...data.payload);
						} else if (data.event == "windowClosed") {
							event.stopImmediatePropagation();
							if (opendeck_iframe_container.firstElementChild) opendeck_iframe_container.firstElementChild.remove();
							opendeck_iframe_container.style.display = "none";
						}
					});

					window.open = (url, target) => {
						if (target && !(target == "_self" || target == "_top")) {
							top.postMessage({ event: "openUrl", payload: url.startsWith("http") ? url : new URL(url, window.location.href).href }, "*");
							return;
						}
						let iframe = document.createElement("iframe");
						iframe.style.flexGrow = "1";
						iframe.onload = () => {
							iframe.contentWindow.opener = window;
							iframe.contentWindow.onbeforeunload = () => top.postMessage({ event: "windowClosed", payload: window.name }, "*");
							iframe.contentWindow.close = () => { iframe.contentWindow.onbeforeunload(); iframe.remove(); };
							iframe.contentWindow.document.body.style.overflowY = "auto";
						};
						iframe.src = url.startsWith("http") ? url : url + "|opendeck_property_inspector_child";
						if (opendeck_iframe_container.firstElementChild) opendeck_iframe_container.firstElementChild.remove();
						opendeck_iframe_container.appendChild(iframe);
						opendeck_iframe_container.style.display = "flex";
						top.postMessage({ event: "windowOpened", payload: window.name }, "*");
						return iframe.contentWindow;
					};

					const opendeck_window_fetch = window.fetch;
					let opendeck_fetch_count = 0;
					let opendeck_fetch_promises = {};
					window.addEventListener("message", (event) => {
						const data = event.data;
						if (data.event == "fetchResponse") {
							event.stopImmediatePropagation();
							const response = new Response(data.payload.response.body, data.payload.response);
							Object.defineProperty(response, "url", { value: data.payload.response.url });
							opendeck_fetch_promises[data.payload.id].resolve(response);
							delete opendeck_fetch_promises[data.payload.id];
						} else if (data.event == "fetchError") {
							event.stopImmediatePropagation();
							opendeck_fetch_promises[data.payload.id].reject(data.payload.error);
							delete opendeck_fetch_promises[data.payload.id];
						}
					});
					window.fetch = (...args) => {
						if (args.length) args[0] = new URL(args[0], window.location.href).href;
						top.postMessage({ event: "fetch", payload: { args, context: window.name, id: ++opendeck_fetch_count }}, "*");
						return new Promise((resolve, reject) => { opendeck_fetch_promises[opendeck_fetch_count] = { resolve, reject }; });
					};
				</script>
			)HTML");
}

} // namespace

PluginAssetServer::PluginAssetServer(QObject* parent) : QObject(parent) {}

PluginAssetServer::~PluginAssetServer() = default;

bool PluginAssetServer::start(quint16 port) {
    if (m_server != nullptr) {
        return true;
    }
    m_server = new QTcpServer(this);
    // Loopback only — never expose plugin files to the network.
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        AJAZZ_LOG_WARN("plugin-asset-server",
                       "failed to listen on 127.0.0.1:{}: {} (plugin icons will be blank)",
                       port,
                       m_server->errorString().toStdString());
        m_server->deleteLater();
        m_server = nullptr;
        return false;
    }
    m_port = port;
    connect(m_server, &QTcpServer::newConnection, this, &PluginAssetServer::onNewConnection);
    AJAZZ_LOG_INFO("plugin-asset-server", "serving plugin assets on 127.0.0.1:{}", port);
    return true;
}

void PluginAssetServer::onNewConnection() {
    while (m_server != nullptr && m_server->hasPendingConnections()) {
        QTcpSocket* sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        connect(sock, &QTcpSocket::readyRead, sock, [sock]() {
            // Minimal HTTP/1.1: we only need the request line `GET <path> HTTP/1.x`.
            // Wait until at least the first line is available.
            if (!sock->canReadLine()) {
                return;
            }
            QByteArray const line = sock->readLine(8192).trimmed();
            QList<QByteArray> const parts = line.split(' ');
            auto const finish = [sock]() {
                sock->flush();
                sock->disconnectFromHost();
            };
            if (parts.size() < 2 || parts.at(0) != QByteArrayLiteral("GET")) {
                sendStatus(sock, "400 Bad Request");
                finish();
                return;
            }
            // Decode + strip the query string.
            QString reqPath = QUrl::fromPercentEncoding(parts.at(1));
            qsizetype const q = reqPath.indexOf(QLatin1Char('?'));
            if (q >= 0) {
                reqPath = reqPath.left(q);
            }
            // Property-inspector requests carry a `|opendeck_property_inspector`
            // (or `…_child`) marker the SPA appends to the iframe src. Strip it
            // BEFORE the prefix/traversal/containment checks so the bare path
            // resolves to the real PI HTML, then inject the OpenDeck PI shim so
            // the cross-origin PI can talk back to the SPA. Mirrors upstream
            // src-tauri/src/plugins/webserver.rs.
            enum class PiMode { None, Inspector, Child };
            PiMode piMode = PiMode::None;
            if (reqPath.endsWith(kPiChildMarker)) {
                piMode = PiMode::Child;
                reqPath.chop(kPiChildMarker.size());
            } else if (reqPath.endsWith(kPiMarker)) {
                piMode = PiMode::Inspector;
                reqPath.chop(kPiMarker.size());
            }
            // Builtin Property Inspectors: `/__builtinpi__/<file>` serves the
            // in-tree PI pages for the fallback OpenDeck builtins from the
            // bundled :/builtinpi resource tree (flat namespace — any path
            // separator or traversal is rejected). The PI marker handling
            // above applies to them exactly like plugin PIs, so the SPA's
            // connect bootstrap shim is injected the same way.
            QString const kBuiltinPrefix = QStringLiteral("/__builtinpi__/");
            if (reqPath.startsWith(kBuiltinPrefix)) {
                QString const name = reqPath.mid(kBuiltinPrefix.size());
                if (name.isEmpty() || name.contains(QLatin1Char('/')) ||
                    name.contains(QStringLiteral(".."))) {
                    sendStatus(sock, "404 Not Found");
                    finish();
                    return;
                }
                QFile res(QStringLiteral(":/builtinpi/") + name);
                if (!res.open(QIODevice::ReadOnly)) {
                    sendStatus(sock, "404 Not Found");
                    finish();
                    return;
                }
                QByteArray body = res.readAll();
                res.close();
                if (piMode == PiMode::Inspector) {
                    body += piInspectorShim();
                    sendBody(sock, QByteArrayLiteral("text/html"), body);
                    finish();
                    return;
                }
                QByteArray ctype = QMimeDatabase().mimeTypeForFile(name).name().toUtf8();
                if (ctype.isEmpty()) {
                    ctype = QByteArrayLiteral("application/octet-stream");
                }
                sendBody(sock, ctype, body);
                finish();
                return;
            }
            // Only the plugin-asset namespace is served; reject anything else and
            // any traversal attempt outright.
            QString const kPrefix = QStringLiteral("/__pluginasset__/");
            if (!reqPath.startsWith(kPrefix) || reqPath.contains(QStringLiteral(".."))) {
                sendStatus(sock, "404 Not Found");
                finish();
                return;
            }
            QString const root = pluginsRoot();
            QFileInfo const target(QDir(root).filePath(reqPath.mid(kPrefix.size())));
            QString const canonical = target.canonicalFilePath();
            QString const canonicalRoot = QFileInfo(root).canonicalFilePath();
            if (canonical.isEmpty() || canonicalRoot.isEmpty() ||
                !canonical.startsWith(canonicalRoot + QLatin1Char('/'))) {
                sendStatus(sock, "404 Not Found");
                finish();
                return;
            }
            QFile f(canonical);
            if (!f.open(QIODevice::ReadOnly)) {
                sendStatus(sock, "404 Not Found");
                finish();
                return;
            }
            QByteArray body = f.readAll();
            f.close();
            if (piMode == PiMode::Inspector) {
                body += piInspectorShim();
                sendBody(sock, QByteArrayLiteral("text/html"), body);
                finish();
                return;
            }
            if (piMode == PiMode::Child) {
                // Nested PI window (plugin called window.open on another PI page).
                // Split the "??=" token across two literals so the C++ parser
                // does not read "??=" as the `#` trigraph (-Werror=trigraphs).
                body.prepend(QByteArrayLiteral("<script>window.opener ?") +
                             QByteArrayLiteral("?= window.parent;</script>"));
                sendBody(sock, QByteArrayLiteral("text/html"), body);
                finish();
                return;
            }
            QByteArray ctype = QMimeDatabase().mimeTypeForFile(canonical).name().toUtf8();
            if (ctype.isEmpty()) {
                ctype = QByteArrayLiteral("application/octet-stream");
            }
            sendBody(sock, ctype, body);
            finish();
        });
    }
}

} // namespace ajazz::app
