// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file test_obs_client.cpp
 * @brief Hardware-free mock OBS QWebSocketServer tests for ObsClient.
 *
 * Proves the obs-websocket v5 auth handshake in BOTH directions:
 *   (a) Correct auth bytes vs a known (password, salt, challenge) -> auth vector.
 *   (b) Refusal with zero Identify messages when auth is required but no password is set.
 * Also proves the auth-disabled server path and a well-formed op:6
 * SetCurrentProgramScene request.
 *
 * All tests are AJAZZ_HAVE_WEBSOCKETS-gated. Hardware-free: no live OBS, no
 * network access beyond the loopback interface. Test infrastructure cloned from
 * test_sd_plugin_server.cpp (ensureQCoreApp + pump + waitForSpy + loopback server).
 *
 * ASCII-only TEST_CASE / SECTION titles (CLAUDE.md Win32 ctest codepage rule).
 * Tag: [obs-client]  ->  ctest --preset linux-release -R ObsClient
 */
#if defined(AJAZZ_HAVE_WEBSOCKETS)

#include "obs_client.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QWebSocket>
#include <QWebSocketServer>

#include <catch2/catch_test_macros.hpp>

using ajazz::app::ObsClient;

namespace {

// ---------------------------------------------------------------------------
// Test infrastructure (mirror of test_sd_plugin_server.cpp)
// ---------------------------------------------------------------------------

/// Pump the Qt event loop for `ms` milliseconds so async network events drain.
void pump(int ms) {
    auto const until = QDateTime::currentMSecsSinceEpoch() + ms;
    while (QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

/// Lazy QCoreApplication singleton.
/// Heap-allocated and never deleted — avoids static-destruction races.
/// (see test_sd_plugin_server.cpp and Qt docs "Static destruction order fiasco")
QCoreApplication* ensureQCoreApp() {
    static QCoreApplication* app = []() {
        static int argc = 0;
        static char* argv[] = {nullptr};
        return new QCoreApplication(argc, argv);
    }();
    return app;
}

/// Wait up to `timeout_ms` for `spy` to accumulate at least one entry.
bool waitForSpy(QSignalSpy& spy, int timeout_ms = 3000) {
    auto const until = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    while (spy.count() == 0 && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return spy.count() > 0;
}

/// Wait up to `timeout_ms` for `condition` to become true.
bool waitFor(std::function<bool()> condition, int timeout_ms = 3000) {
    auto const until = QDateTime::currentMSecsSinceEpoch() + timeout_ms;
    while (!condition() && QDateTime::currentMSecsSinceEpoch() < until) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return condition();
}

// ---------------------------------------------------------------------------
// obs-websocket v5 opcodes
// ---------------------------------------------------------------------------
constexpr int kOpHello = 0;
constexpr int kOpIdentify = 1;
constexpr int kOpIdentified = 2;
constexpr int kOpRequest = 6;

/// Build a Hello(op:0) frame.
///   withAuth = true: includes d.authentication{challenge,salt}.
///   withAuth = false: omits d.authentication (auth-disabled OBS).
QString buildHello(bool withAuth,
                   QString const& challenge = QStringLiteral("TestChallenge"),
                   QString const& salt = QStringLiteral("TestSalt")) {
    QJsonObject d;
    d[QStringLiteral("rpcVersion")] = 1;
    if (withAuth) {
        QJsonObject auth;
        auth[QStringLiteral("challenge")] = challenge;
        auth[QStringLiteral("salt")] = salt;
        d[QStringLiteral("authentication")] = auth;
    }
    QJsonObject frame;
    frame[QStringLiteral("op")] = kOpHello;
    frame[QStringLiteral("d")] = d;
    return QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
}

/// Build an Identified(op:2) frame.
QString buildIdentified() {
    QJsonObject d;
    d[QStringLiteral("negotiatedRpcVersion")] = 1;
    QJsonObject frame;
    frame[QStringLiteral("op")] = kOpIdentified;
    frame[QStringLiteral("d")] = d;
    return QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// Loopback mock OBS server setup.
//
// Returns a running QWebSocketServer bound to 127.0.0.1 on an OS-assigned
// port. Fills `serverSocket` and `received` as the client connects and sends.
// Must be kept alive for the test duration (use unique_ptr).
//
// Note: this helper intentionally avoids a Q_OBJECT class in an anonymous
// namespace (which triggers moc in header-less CPP files). Instead it wires
// lambdas directly to the accepted socket.
// ---------------------------------------------------------------------------
struct MockObsSetup {
    std::unique_ptr<QWebSocketServer> server;
    QWebSocket* serverSocket{nullptr};
    QStringList received;
};

/// Create a loopback mock OBS server and accept the first connection.
/// Returns nullptr on listen failure.
std::unique_ptr<MockObsSetup> makeMockServer() {
    auto setup = std::make_unique<MockObsSetup>();
    setup->server = std::make_unique<QWebSocketServer>(QStringLiteral("MockOBS"),
                                                       QWebSocketServer::NonSecureMode);
    if (!setup->server->listen(QHostAddress::LocalHost, 0)) {
        return nullptr;
    }
    return setup;
}

/// Accept the pending connection on `setup` (call after the client has connected).
/// Wires the serverSocket and fills setup->received on each message.
/// Returns the number of messages currently in setup->received.
void acceptConn(MockObsSetup& setup) {
    if (!setup.server->hasPendingConnections()) {
        return;
    }
    QWebSocket* sock = setup.server->nextPendingConnection();
    if (!sock) {
        return;
    }
    setup.serverSocket = sock;
    QObject::connect(sock, &QWebSocket::textMessageReceived, sock, [&setup](QString const& msg) {
        setup.received << msg;
    });
}

/// Pump the loop until setup->received has at least `n` entries or timeout.
bool waitForMessages(MockObsSetup& setup, int n, int timeout_ms = 3000) {
    return waitFor([&setup, n]() { return setup.received.count() >= n; }, timeout_ms);
}

} // namespace

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// 1. Known-vector auth: computeObsAuth matches a precomputed SHA256/base64
//    result obtained offline via Python hashlib + base64.
//
// Test vector:
//   password  = "TestPassword123"
//   salt      = "LKh7UrMBgfHMBFfK"
//   challenge = "fKgXbBIJCgJHhZMU"
//   expected  = "HLL27GMebtHyur73QikTYg0tUEFVb9i2x+4zASY/TQY="
//
// Derivation (documented):
//   secret = base64(SHA256("TestPassword123" + "LKh7UrMBgfHMBFfK"))
//          = "4EM513iBbOy1B8EFXoZETlv4EXZil5AnB6skC9FmBJo="
//   auth   = base64(SHA256(secret_string + "fKgXbBIJCgJHhZMU"))
//          = "HLL27GMebtHyur73QikTYg0tUEFVb9i2x+4zASY/TQY="
// ---------------------------------------------------------------------------
TEST_CASE("ObsClient computeObsAuth known-vector matches documented SHA256/base64 steps",
          "[obs-client]") {
    ensureQCoreApp();

    // Fixed test inputs - NOT real credentials, purely for algorithm verification.
    QString const password = QStringLiteral("TestPassword123");
    QString const salt = QStringLiteral("LKh7UrMBgfHMBFfK");
    QString const challenge = QStringLiteral("fKgXbBIJCgJHhZMU");

    // Offline precomputed expected value (Python: hashlib + base64, see file header).
    QString const kExpectedAuth = QStringLiteral("HLL27GMebtHyur73QikTYg0tUEFVb9i2x+4zASY/TQY=");

    // Compute step-by-step inline (proves the algorithm is correct, not just memorised).
    QByteArray const secretHash =
        QCryptographicHash::hash((password + salt).toUtf8(), QCryptographicHash::Sha256);
    QByteArray const secret = secretHash.toBase64();
    QByteArray const authHashBytes =
        QCryptographicHash::hash(secret + challenge.toUtf8(), QCryptographicHash::Sha256);
    QString const expectedInline = QString::fromLatin1(authHashBytes.toBase64());

    // Inline computation must agree with offline expected value.
    REQUIRE(expectedInline == kExpectedAuth);

    // The function under test must also agree with both.
    QString const actual = ObsClient::computeObsAuth(password, salt, challenge);
    REQUIRE(actual == kExpectedAuth);
}

TEST_CASE("ObsClient computeObsAuth is deterministic - same inputs give same output",
          "[obs-client]") {
    ensureQCoreApp();
    QString const a =
        ObsClient::computeObsAuth(QStringLiteral("pw"), QStringLiteral("s"), QStringLiteral("c"));
    QString const b =
        ObsClient::computeObsAuth(QStringLiteral("pw"), QStringLiteral("s"), QStringLiteral("c"));
    REQUIRE(a == b);
    REQUIRE_FALSE(a.isEmpty());
}

// ---------------------------------------------------------------------------
// 2. Identify-on-auth: mock server sends Hello with d.authentication;
//    client (password set) replies Identify(op:1) with d.rpcVersion=1 and
//    d.authentication == computeObsAuth(pw, salt, challenge);
//    server sends Identified(op:2); client emits connected().
// ---------------------------------------------------------------------------
TEST_CASE("ObsClient Identify-on-auth - sends correct auth and reaches connected", "[obs-client]") {
    ensureQCoreApp();

    QString const password = QStringLiteral("SecretOBSPassword");
    QString const salt = QStringLiteral("TestSaltValue123");
    QString const challenge = QStringLiteral("TestChallengeABC");

    auto setup = makeMockServer();
    REQUIRE(setup != nullptr);

    ObsClient client;
    QSignalSpy connectedSpy(&client, &ObsClient::connected);
    QSignalSpy authFailedSpy(&client, &ObsClient::authFailed);

    client.connectToObs(QStringLiteral("127.0.0.1"), setup->server->serverPort(), password);

    // Accept connection and wire message capture.
    pump(300);
    if (setup->server->hasPendingConnections()) {
        acceptConn(*setup);
    }
    REQUIRE(setup->serverSocket != nullptr);

    // Server sends Hello with authentication required.
    setup->serverSocket->sendTextMessage(buildHello(true, challenge, salt));

    // Wait for client to send Identify.
    REQUIRE(waitForMessages(*setup, 1));
    REQUIRE(authFailedSpy.count() == 0);

    // Parse the Identify frame.
    QJsonObject const identifyFrame =
        QJsonDocument::fromJson(setup->received.at(0).toUtf8()).object();
    REQUIRE(identifyFrame.value(QStringLiteral("op")).toInt() == kOpIdentify);

    QJsonObject const d = identifyFrame.value(QStringLiteral("d")).toObject();
    REQUIRE(d.value(QStringLiteral("rpcVersion")).toInt() == 1);

    // The authentication value must equal computeObsAuth(password, salt, challenge).
    QString const expectedAuth = ObsClient::computeObsAuth(password, salt, challenge);
    REQUIRE(d.value(QStringLiteral("authentication")).toString() == expectedAuth);

    // Server sends Identified(op:2).
    setup->serverSocket->sendTextMessage(buildIdentified());
    REQUIRE(waitForSpy(connectedSpy));
    REQUIRE(connectedSpy.count() == 1);
    REQUIRE(authFailedSpy.count() == 0);
}

// ---------------------------------------------------------------------------
// 3. Refuse-when-no-password: mock server demands auth; client has EMPTY
//    password; client emits authFailed and the server records ZERO Identify
//    messages (auth default-on proven in the refuse direction; Pitfall 5).
// ---------------------------------------------------------------------------
TEST_CASE("ObsClient refuse-when-no-password - authFailed emitted, zero Identify sent",
          "[obs-client]") {
    ensureQCoreApp();

    auto setup = makeMockServer();
    REQUIRE(setup != nullptr);

    ObsClient client;
    QSignalSpy connectedSpy(&client, &ObsClient::connected);
    QSignalSpy authFailedSpy(&client, &ObsClient::authFailed);

    // Empty password - MUST be refused when server requires auth.
    client.connectToObs(QStringLiteral("127.0.0.1"), setup->server->serverPort(), QString{});

    pump(300);
    if (setup->server->hasPendingConnections()) {
        acceptConn(*setup);
    }
    REQUIRE(setup->serverSocket != nullptr);

    // Server sends Hello with authentication required.
    setup->serverSocket->sendTextMessage(buildHello(true));

    // Client must emit authFailed.
    REQUIRE(waitForSpy(authFailedSpy));
    REQUIRE(authFailedSpy.count() == 1);
    REQUIRE_FALSE(authFailedSpy.first().at(0).toString().isEmpty());

    // The server must have received ZERO Identify messages (auth default-on).
    // Give any delayed frame a chance to arrive.
    pump(300);
    REQUIRE(setup->received.count() == 0);
    REQUIRE(connectedSpy.count() == 0);
}

// ---------------------------------------------------------------------------
// 4. Auth-disabled server: Hello omits d.authentication; client sends Identify
//    WITHOUT d.authentication field; client reaches connected().
// ---------------------------------------------------------------------------
TEST_CASE("ObsClient auth-disabled server - Identify has no authentication field", "[obs-client]") {
    ensureQCoreApp();

    auto setup = makeMockServer();
    REQUIRE(setup != nullptr);

    ObsClient client;
    QSignalSpy connectedSpy(&client, &ObsClient::connected);
    QSignalSpy authFailedSpy(&client, &ObsClient::authFailed);

    // Any password is fine - auth-disabled server ignores it.
    client.connectToObs(
        QStringLiteral("127.0.0.1"), setup->server->serverPort(), QStringLiteral("ignored"));

    pump(300);
    if (setup->server->hasPendingConnections()) {
        acceptConn(*setup);
    }
    REQUIRE(setup->serverSocket != nullptr);

    // Server sends Hello WITHOUT d.authentication.
    setup->serverSocket->sendTextMessage(buildHello(false));

    // Client must send Identify.
    REQUIRE(waitForMessages(*setup, 1));

    QJsonObject const identifyFrame =
        QJsonDocument::fromJson(setup->received.at(0).toUtf8()).object();
    REQUIRE(identifyFrame.value(QStringLiteral("op")).toInt() == kOpIdentify);

    QJsonObject const d = identifyFrame.value(QStringLiteral("d")).toObject();
    REQUIRE(d.value(QStringLiteral("rpcVersion")).toInt() == 1);
    // Identify MUST NOT include the authentication field when server did not require it.
    REQUIRE(d.value(QStringLiteral("authentication")).isUndefined());

    setup->serverSocket->sendTextMessage(buildIdentified());
    REQUIRE(waitForSpy(connectedSpy));
    REQUIRE(connectedSpy.count() == 1);
    REQUIRE(authFailedSpy.count() == 0);
}

// ---------------------------------------------------------------------------
// 5. Well-formed request: after Identified, setScene("Scene 2") makes the mock
//    server receive op:6 SetCurrentProgramScene with requestData.sceneName ==
//    "Scene 2" and a non-empty requestId.
// ---------------------------------------------------------------------------
TEST_CASE("ObsClient setScene after Identified sends well-formed op6 request", "[obs-client]") {
    ensureQCoreApp();

    auto setup = makeMockServer();
    REQUIRE(setup != nullptr);

    ObsClient client;
    QSignalSpy connectedSpy(&client, &ObsClient::connected);

    // Use auth-disabled server for simplicity.
    client.connectToObs(QStringLiteral("127.0.0.1"), setup->server->serverPort(), QString{});

    pump(300);
    if (setup->server->hasPendingConnections()) {
        acceptConn(*setup);
    }
    REQUIRE(setup->serverSocket != nullptr);

    // Auth-disabled Hello -> Identify -> Identified.
    setup->serverSocket->sendTextMessage(buildHello(false));
    REQUIRE(waitForMessages(*setup, 1)); // Identify
    setup->serverSocket->sendTextMessage(buildIdentified());
    REQUIRE(waitForSpy(connectedSpy));

    // Send a scene-switch request.
    client.setScene(QStringLiteral("Scene 2"));
    REQUIRE(waitForMessages(*setup, 2)); // Identify + Request

    // The last received message is the op:6 Request.
    QJsonObject const requestFrame =
        QJsonDocument::fromJson(setup->received.last().toUtf8()).object();
    REQUIRE(requestFrame.value(QStringLiteral("op")).toInt() == kOpRequest);

    QJsonObject const d = requestFrame.value(QStringLiteral("d")).toObject();
    REQUIRE(d.value(QStringLiteral("requestType")).toString() ==
            QStringLiteral("SetCurrentProgramScene"));
    REQUIRE_FALSE(d.value(QStringLiteral("requestId")).toString().isEmpty());

    QJsonObject const requestData = d.value(QStringLiteral("requestData")).toObject();
    REQUIRE(requestData.value(QStringLiteral("sceneName")).toString() == QStringLiteral("Scene 2"));
}

#endif // defined(AJAZZ_HAVE_WEBSOCKETS)
