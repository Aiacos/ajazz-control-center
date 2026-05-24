---
phase: 21-builtin-in-process-actions
plan: 02
subsystem: app-obs-client
tags:
  [
    obs-websocket-v5,
    auth-default-on,
    sha256-challenge-response,
    tdd,
    anti-feature,
    cod-031,
    websockets,
    plugin-12,
  ]

# Dependency graph
requires:
  - phase: none
    provides: >
      obs_client.{hpp,cpp} is independent of 21-01 (synthesizer) and 21-03 (registry).
      Consumes only Qt6::WebSockets + Qt6::Core (already available via
      AJAZZ_HAVE_WEBSOCKETS gate). 21-03 wires ObsClient to the obsstudio UUID.

provides:
  - "ObsClient QObject (AJAZZ_HAVE_WEBSOCKETS gated): connectToObs(host, port, password)"
  - "computeObsAuth(pw, salt, challenge): Base64(SHA256(Base64(SHA256(pw+salt))+challenge))"
  - "setScene/setPreviewScene/toggleRecord/toggleStream (op:6 request subset)"
  - "connected/authFailed/errorOccurred/requestSucceeded signals"
  - "Auth default-on: refuses Identify + emits authFailed when Hello has d.authentication but
    password is empty (T-21-obsauth LOCKED anti-feature)"
  - "6 Catch2 test cases [obs-client]; 559/559 full suite green"

affects:
  - "21-03 (builtin registry): wires obsstudio UUID to ObsClient; reads host/port/password from
    QSettings; exposes via BuiltinActionsService holding an ObsClient instance."
  - "25 (hardware verify): live OBS connection and scene-switch witness (Phase-25 scope)"

# Tech tracking
tech-stack:
  added: []
  patterns:
    - "obs-websocket v5 Hello/Identify/Identified/Request handshake via QWebSocket + QJsonDocument"
    - "computeObsAuth: nested SHA256/base64 via QCryptographicHash::Sha256 (no bundled crypto)"
    - "Auth default-on gate: if d.authentication present + password empty, refuse Identify"
    - "sendRequest mints QUuid requestId per op:6 frame"
    - "Loopback mock OBS server: QWebSocketServer bound to 127.0.0.1 port 0; lambda-based message
      capture (no Q_OBJECT in anonymous namespace)"
    - "Known-vector auth pinning: offline Python + inline QCryptographicHash double-check"

key-files:
  created:
    - src/app/src/obs_client.hpp
    - src/app/src/obs_client.cpp
    - tests/unit/test_obs_client.cpp
  modified:
    - src/app/CMakeLists.txt
    - tests/unit/CMakeLists.txt
    - tests/qml/CMakeLists.txt

key-decisions:
  - ObsClient API: connectToObs(host, port, password); setScene/setPreviewScene/
    toggleRecord/toggleStream map to op:6 requestType strings; password never logged.
  - Auth default-on (LOCKED anti-feature T-21-obsauth): when Hello carries d.authentication,
    password MUST be set; if empty the client closes the socket and emits authFailed without
    sending Identify. The refuse direction is proven in both test cases.
  - sendRequest requires State::Identified; drops silently with a WARN log otherwise (safe
    because 21-03 wires actions only after connected() fires).
  - QSettings password-at-rest (T-21-obspw): the OBS password stored by 21-03 is plaintext
    at rest - no OS keychain used. Documented known limitation; no encryption is claimed.
  - obs_client.hpp added to ACC_QML_MODULE_SOURCES for AUTOMOC; obs_client.cpp added to both
    the app target and qml test target AJAZZ_HAVE_WEBSOCKETS blocks to resolve moc symbols.
  - Default port 4455 is documented in connectToObs(); 21-03 reads port from QSettings.
  - "Test vector: pw=TestPassword123, salt=LKh7UrMBgfHMBFfK, challenge=fKgXbBIJCgJHhZMU;
    expected output pinned offline (Python hashlib) and also verified inline in the test
    via QCryptographicHash (the actual value is in test_obs_client.cpp kExpectedAuth)."

requirements-completed: [PLUGIN-12]

# Metrics
duration: ~11 min
completed: 2026-05-24
---

# Phase 21 Plan 02: ObsClient obs-websocket v5 Client + Auth Default-On + Mock Test Summary

**obs-websocket v5 client (ObsClient) behind AJAZZ_HAVE_WEBSOCKETS: Hello/Identify SHA256
challenge/response handshake with auth default-on (refuses Identify when password unset),
four op:6 request methods, and 6-test loopback mock OBS suite pinning the algorithm against
a known auth vector.**

## Performance

- **Duration:** ~11 min
- **Started:** 2026-05-24T17:26:22Z
- **Completed:** 2026-05-24T17:37:57Z
- **Tasks:** 2/2 completed
- **Files modified:** 6 (3 created + 3 modified)

## Accomplishments

- Landed `ObsClient : QObject` gated by `AJAZZ_HAVE_WEBSOCKETS`; full class compiles away
  on minimal Qt installs (mirrors `sd_plugin_server.hpp`)
- `computeObsAuth(password, salt, challenge)` implements
  `Base64(SHA256(Base64(SHA256(password+salt))+challenge))` via `QCryptographicHash::Sha256`
  (no bundled crypto); deterministic, stateless, testable without a socket
- Hello(op:0) handler drives the auth-default-on gate (T-21-obsauth LOCKED anti-feature):
  - `d.authentication` present + password set: compute auth, send Identify(op:1)
  - `d.authentication` present + password EMPTY: emit `authFailed`, close socket, no Identify
  - `d.authentication` absent: send Identify(op:1) without authentication field
- Identified(op:2) handler emits `connected()` and transitions to State::Identified
- `sendRequest` mints a fresh `QUuid` requestId per call; builds op:6 JSON via QJsonDocument
  (COD-031: all JSON app-tier, no vendored JSON library)
- Four request methods map to obs-websocket v5 requestTypes: `SetCurrentProgramScene`,
  `SetCurrentPreviewScene`, `ToggleRecord`, `ToggleStream`
- `onSocketError` emits `errorOccurred` with the Qt error string (never logs the password)
- 6 Catch2 `[obs-client]` test cases via loopback `QWebSocketServer` mock:
  1. Known-vector auth (offline Python + inline double-check)
  1. Determinism check
  1. Identify-on-auth -> Identified -> `connected()` (auth bytes asserted)
  1. Refuse-when-no-password: `authFailed` emitted, zero Identify sent (auth default-on)
  1. Auth-disabled server: Identify without authentication field, `connected()`
  1. Well-formed op:6 SetCurrentProgramScene with non-empty requestId
- 559/559 full `ctest --preset linux-release` green (+6 new ObsClient tests from 553)

## API for 21-03 to Consume

```cpp
// Namespace: ajazz::app (gated AJAZZ_HAVE_WEBSOCKETS)
class ObsClient : public QObject {
    // Construction: ObsClient obs; (parent optional)
    void connectToObs(const QString& host, quint16 port, const QString& password);
    void setScene(const QString& sceneName);      // SetCurrentProgramScene
    void setPreviewScene(const QString& sceneName); // SetCurrentPreviewScene
    void toggleRecord();                           // ToggleRecord
    void toggleStream();                           // ToggleStream

    static QString computeObsAuth(const QString& pw, const QString& salt, const QString& challenge);

signals:
    void connected();                  // Identified(op:2) received
    void authFailed(QString reason);   // auth required but no password set
    void errorOccurred(QString reason); // socket/protocol error
    void requestSucceeded(QString requestId); // op:7 RequestResponse
};
```

**21-03 wiring recipe:**

- Hold an `ObsClient` in `BuiltinActionsService`
- Read host/port/password from `QSettings` (plaintext at rest — T-21-obspw documented)
- Call `connectToObs(host, port, password)` when the obsstudio built-in is first invoked
- Wire `connected()` to allow action execution; wire `authFailed`/`errorOccurred` to UI error

## Task Commits

1. **Task 1: ObsClient interface + impl + CMake** - `e57897d` (feat)
1. **Task 2: Mock OBS test suite (6 tests)** - `9f9d6d5` (feat)

## Files Created/Modified

- `src/app/src/obs_client.hpp` - ObsClient Q_OBJECT; computeObsAuth; connectToObs;
  setScene/setPreviewScene/toggleRecord/toggleStream; connected/authFailed/errorOccurred
  signals; gated AJAZZ_HAVE_WEBSOCKETS
- `src/app/src/obs_client.cpp` - Hello/Identify/Identified/Request handler; sendRequest;
  auth default-on gate; QJsonDocument framing (COD-031 clean)
- `tests/unit/test_obs_client.cpp` - 6 [obs-client] TEST_CASEs; loopback mock server;
  known-vector pin; refuse + auth paths; op:6 request assertion
- `src/app/CMakeLists.txt` - obs_client.cpp in AJAZZ_HAVE_WEBSOCKETS block; obs_client.hpp
  in ACC_QML_MODULE_SOURCES for AUTOMOC
- `tests/unit/CMakeLists.txt` - test_obs_client + obs_client.cpp in AJAZZ_HAVE_WEBSOCKETS block
- `tests/qml/CMakeLists.txt` - obs_client.cpp in QML test AJAZZ_HAVE_WEBSOCKETS block (moc)

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] QJsonObject default parameter syntax**

- **Found during:** Task 1 (first build attempt)
- **Issue:** `QJsonObject const& requestData = {}` default parameter rejected by GCC
  (incomplete type for brace-init of a class type in a default argument). Standard C++
  requires `QJsonObject{}` for explicit construction.
- **Fix:** Changed default to `QJsonObject const& requestData = QJsonObject{}` in header;
  added `#include <QJsonObject>` to header (was forward-declared only).
- **Files modified:** `src/app/src/obs_client.hpp`
- **Committed in:** `e57897d` (Task 1 commit, after hook re-stage)

**2. [Rule 1 - Bug] QML test linker: moc symbols for ObsClient unresolved**

- **Found during:** Task 2 (first cmake --build)
- **Issue:** `tests/qml/ajazz_qml_tests` failed to link: `ObsClient::onSocketError()`,
  `ObsClient::onTextMessageReceived()`, etc. undefined. Root cause: `obs_client.hpp` was
  added to `ACC_QML_MODULE_SOURCES` (for AUTOMOC in the app target), which caused the QML
  test target (which imports the same source list) to run moc on `obs_client.hpp` and
  generate a `moc_obs_client.cpp` that referenced ObsClient methods — but `obs_client.cpp`
  was not compiled into the QML test target.
- **Fix:** Added `obs_client.cpp` to the QML test target's `AJAZZ_HAVE_WEBSOCKETS` block
  in `tests/qml/CMakeLists.txt`, mirroring the existing `sd_plugin_server.cpp` pattern.
- **Files modified:** `tests/qml/CMakeLists.txt`
- **Committed in:** `9f9d6d5` (Task 2 commit)

**3. [Rule 1 - Bug] Unused function warning in test**

- **Found during:** Task 2 (second build attempt after adding QML test fix)
- **Issue:** `waitForConnection()` function defined but not used in the anonymous namespace
  (`-Werror=unused-function`). The function was added speculatively during initial test
  design but replaced by `pump()` + `hasPendingConnections()` in the actual test bodies.
- **Fix:** Removed `waitForConnection()` from the anonymous namespace.
- **Files modified:** `tests/unit/test_obs_client.cpp`
- **Committed in:** `9f9d6d5` (Task 2 commit, after hook re-stage)

______________________________________________________________________

**Total deviations:** 3 auto-fixed (compiler error, linker error, unused-function warning)
**Impact on plan:** All fixes are non-behavioral (type syntax, CMake link, dead code removal).
No scope creep.

## Known Stubs

None. The four request methods are fully implemented (op:6 frames with requestType/requestId/
requestData via sendRequest). The password persistence is a 21-03 concern (QSettings, T-21-obspw
documented above).

## Threat Mitigations Applied

| Threat         | Mitigation                                                                                      | Status    |
| -------------- | ----------------------------------------------------------------------------------------------- | --------- |
| T-21-obsauth   | Auth default-on: refuses Identify + emits authFailed when Hello has d.authentication but no pw  | LOCKED    |
| T-21-obscrypto | computeObsAuth via QCryptographicHash::Sha256; known-vector pin; no hand-rolled crypto          | MITIGATED |
| T-21-obsjson   | All inbound frames parsed defensively via QJsonDocument (isObject check + toObject + toString); |           |
|                | unknown opcodes ignored; no exceptions on missing keys                                          | MITIGATED |
| T-21-obspw     | Password never logged; QSettings plaintext-at-rest documented (no claim of encryption)          | ACCEPTED  |
| T-21-SC        | No package installs (pure in-tree C++/Qt6)                                                      | N/A       |

## Follow-up Items for 21-03

- Wire `com.hotspot.streamdock.obsstudio` UUID in `BuiltinActionsService` to an `ObsClient`
  instance; read host/port/password from `QSettings`
- Map the four action sub-types from the binding's `settingsJson` to setScene/setPreviewScene/
  toggleRecord/toggleStream on the held `ObsClient`
- Surface `authFailed`/`errorOccurred` signals to the QML UI as toast/status notifications
- Accept `connected()` before allowing action dispatch (guard with m_state check or connect
  the action methods after `connected()` fires)

## Follow-up Items for Phase 25 (Hardware Verify)

- Live OBS connection: verify the handshake and scene-switch against a real OBS Studio 28+
  instance with obs-websocket v5 enabled
- macOS Accessibility permission is not required for this client (synthesis-only concern)

## Self-Check: PASSED

Files confirmed present:

- `src/app/src/obs_client.hpp` - FOUND (class ObsClient, computeObsAuth, connectToObs,
  setScene/setPreviewScene/toggleRecord/toggleStream, connected/authFailed/errorOccurred)
- `src/app/src/obs_client.cpp` - FOUND (Hello/Identify/Identified/Request handler,
  sendRequest, auth default-on gate)
- `tests/unit/test_obs_client.cpp` - FOUND (6 TEST_CASEs [obs-client])

Commits confirmed:

- `e57897d` (Task 1) - FOUND
- `9f9d6d5` (Task 2) - FOUND

Invariants confirmed:

- `grep -c nlohmann src/app/src/obs_client.cpp` == 0 - PASS (COD-031 clean)
- `grep -c computeObsAuth src/app/src/obs_client.cpp` == 2 - PASS
- No changes to `src/core/**` - PASS
- No changes to `akp05.cpp` / `akp05_protocol.hpp` - PASS
- ASCII-only TEST_CASE titles - PASS
- 559/559 ctest --preset linux-release - PASS
- AJAZZ_HAVE_WEBSOCKETS gate: obs_client.{hpp,cpp} + test are fully gated - PASS

______________________________________________________________________

*Phase: 21-builtin-in-process-actions*
*Completed: 2026-05-24*
