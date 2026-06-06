# Phase 17: Plugin Protocol Completion - Research

**Researched:** 2026-05-23
**Domain:** Elgato Stream Deck v6-compatible WebSocket protocol on Qt 6 (`QWebSocketServer`/`QWebSocket`), JSON envelope routing (`QJsonObject`), salt/challenge auth (`QCryptographicHash::Sha256`)
**Confidence:** HIGH

## Summary

Phase 17 extends the **existing** `src/app/src/sd_plugin_server.{hpp,cpp}` (257 LoC) to close three protocol gaps: (1) route the **26 AJAZZ-only actions** so nothing falls through to `unhandledEventReceived` (PLUGIN-03), (2) add a host→plugin `sendEvent` writer covering all §4.4 events incl. the encoder `dialDown`/`dialUp`/`dialRotate` family (PLUGIN-04), and (3) implement the `passHello` + salt/challenge auth handshake with `sha256(password+salt)` and rejection after N attempts (PLUGIN-05). PLUGIN-01 (LocalHost-only bind) and PLUGIN-02 (envelope round-trip) are largely built — they need test-pinning and extension, not new code.

This is a **pure protocol-surface phase**: device-agnostic, hardware-free, verified entirely against a loopback `QWebSocket` test client. All the building blocks already exist in-tree — `QWebSocketServer` (loopback bind, established), `QJsonObject`/`QJsonDocument` parsing (the dispatch already uses it), `QCryptographicHash::Sha256` (QtCore, already used in `single_instance_guard.cpp`), and the Catch2 + `QSignalSpy` + loopback-client test harness (`tests/unit/test_sd_plugin_server.cpp`, 7 cases). **No new external packages.** No new CMake module — extend the existing files only (per §9 of the spec which proposes a `src/host/plugin-host/` module, the CONTEXT.md **overrides** that with an explicit LOCKED decision to extend in place).

**Primary recommendation:** Convert the existing `kStandardActions[13]` array into a single membership set of all 39 routed events; add per-`PluginConnection` auth state (salt + attempt counter) and a `passHello` emission right after `registerPlugin`; add a `sendEvent(context-or-uuid, eventName, payload)` writer that serializes a `QJsonObject` to `sendTextMessage`. Use **one routed `actionReceived(uuid, msg)` seam plus targeted typed signals** only where Phase 19 needs structured data. Verify everything with new Catch2 cases driving a loopback `QWebSocket` client, mirroring the §9 target test names.

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **Extend the existing `src/app/src/sd_plugin_server.{hpp,cpp}`** — do NOT create a `src/host/plugin-host/` module (the spec §9's speculative path). `nlohmann::json` may be used PRIVATE in the app/plugin layer only; the server already uses `QJsonObject` (prefer it here).
- Keep the **LocalHost-only** bind invariant (already present + test-pinned) — never `Any`.
- Implement the JSON envelope per §4.2 and route ALL plugin→host actions per §4.3 — the 13 standard PLUS the **26 AJAZZ-only**: `setBG`, `setBackground`, `clearIcon`, `sendToDevice`, `openTouchbarSecondaryMenu`/`exitTouchbarSecondaryMenu`, `enterGatheringEvent`, `registrationScreenSaverEvent`/`unRegistrationScreenSaverEvent`, `setText`, `setFeedback`, `lockScreen`/`unLockScreen`, `getScreenshot`, `getSystemAudioVolume`, `getUserInfo`, `setAcImgTop`, `onSwitchToFolderProfile`/`onSwitchFromFolderProfile`, `deleteAction`, `stopBackground`, `exitFullScreen`, `touchTap`, `getDetectedSensorsData`, `startAudioCapture`/`stopAudioCapture`, `sendUserInfo`.
- **Routing vs fulfillment:** Phase 17 PARSES + ROUTES every message so **nothing falls through to `unhandledEventReceived`**. Actions needing a host capability not yet built (`getScreenshot`, `getSystemAudioVolume`, `getUserInfo`, `getDetectedSensorsData`) return a structured/empty ack or a logged stub — full fulfillment is owned by later phases. Device-targeting actions (`setImage`/`setTitle`/`setState`/`setBG`/`setFeedback`/`setText`) continue to surface via signals for the Phase-19 bridge.
- Add a `sendEvent(pluginUuid/context, eventName, payload)` surface writing a JSON text frame to the right plugin socket. Cover §4.4: `keyDown`/`keyUp`, `dialDown`/`dialUp`/`dialRotate` (+ legacy `keyDownCord`/`keyUpCord`), `touchTap`, `willAppear`/`willDisappear`, `deviceDidConnect`/`deviceDidDisconnect`, `applicationDidLaunch`/`applicationDidTerminate`, `titleParametersDidChange`, `systemDidWakeUp`, `didReceiveSettings`/`didReceiveGlobalSettings`, `sendToPlugin`/`sendToPropertyInspector`.
- After `registerPlugin`, host sends **`passHello`** with `{device, deviceInfo, authentication:{challenge, salt}}` (salt random per connection). When a password is configured, plugin replies `{event:"authentication", challenge: sha256(password+salt)}`; host verifies and **rejects after N (e.g. 5) bad attempts** (close the socket). Default (no password) = passHello sent, connection accepted (loopback-only by design; **no TLS**).

### Claude's Discretion

- Typed signals per action-class vs a single routed `actionReceived(uuid, msg)` the app switches on — your call; keep it the seam Phase 19 consumes.
- Where the `sendEvent` API lives (method on `SdPluginServer`) and the context→socket lookup.
- Salt/challenge storage + attempt-counter location.

### Deferred Ideas (OUT OF SCOPE)

- Plugin process spawn / manifest parse / discovery / lifecycle / Mirabox shim → Phase 18 (PLUGIN-06/07/08).
- Device↔plugin bridge (actionReceived→device; device input→sendEvent; setImage e2e) → Phase 19.
- Property Inspector + settings persistence → Phase 20 (PLUGIN-09).
- Built-in in-process actions (page/profile nav, hotkey, OBS, …) → Phase 21.
- Host-capability fulfillment for getScreenshot/getSystemAudioVolume/getUserInfo/getDetectedSensorsData → the phases that own those capabilities.

## Phase Requirements

| ID        | Description                                                                                                                                      | Research Support                                                                                                                                                                                                                                                                                                                                    |
| --------- | ------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLUGIN-01 | WS server stays `QHostAddress::LocalHost`-only on a random free port, loopback invariant test-pinned                                             | Already built (`start()` hard-codes `QHostAddress::LocalHost`, sd_plugin_server.cpp:55; `bindAddress()` returns it, :107). Test already pins it (test_sd_plugin_server.cpp:55-66). Phase 17 keeps + re-asserts. Optional `QTcpServer` dual-stack is mentioned in REQUIREMENTS but **not** in CONTEXT.md locked scope — treat as discretionary/skip. |
| PLUGIN-02 | JSON envelope (`event`/`context`/`device`/`action`/`payload` + `controller`/`coordinates`/`ticks`/`pressed`) round-trips every supported message | Parsing already via `QJsonDocument::fromJson` → `QJsonObject` (sd_plugin_server.cpp:168-176). `actionReceived` passes the verbatim `QJsonObject` (:234), so envelope round-trips today for the 13. Extend the round-trip guarantee to all 39 + the host→plugin writer.                                                                              |
| PLUGIN-03 | 26 AJAZZ-only actions routed (not "unhandled") on top of the 13 standard                                                                         | Replace `kStandardActions[13]` (sd_plugin_server.cpp:212-224) with a 39-entry membership set; route all via `actionReceived` (+ optional typed signals). §4.3 of spec is the complete authoritative list (39 plugin→host actions).                                                                                                                  |
| PLUGIN-04 | All host→plugin events SENT to a registered plugin's socket incl. encoder events                                                                 | New `sendEvent(...)` method serializing `QJsonObject` → `QWebSocket::sendTextMessage`. §4.4 of spec is the complete event list + payload shapes. Need a `context`→socket lookup (currently only `uuid`→socket exists).                                                                                                                              |
| PLUGIN-05 | `passHello` + salt/challenge auth (`sha256(password+salt)`; reject after N; no TLS)                                                              | `QCryptographicHash::Sha256` (QtCore, no extra link — already used in `single_instance_guard.cpp:11,85`). Add per-connection salt + attempt counter to `PluginConnection`. §4.5 of spec is the authoritative sequence.                                                                                                                              |

## Architectural Responsibility Map

| Capability                                                    | Primary Tier                                          | Secondary Tier | Rationale                                                                                                        |
| ------------------------------------------------------------- | ----------------------------------------------------- | -------------- | ---------------------------------------------------------------------------------------------------------------- |
| Plugin→host action routing                                    | API/Backend (`SdPluginServer` in app layer)           | —              | The WS server is the protocol boundary; it parses + dispatches. Device fulfillment is Phase 19's tier.           |
| Host→plugin event sending                                     | API/Backend (`SdPluginServer`)                        | —              | Server owns the sockets; `sendEvent` is a server method writing to a `QWebSocket`.                               |
| salt/challenge auth                                           | API/Backend (`SdPluginServer` + per-connection state) | —              | Auth state is connection-scoped; lives in `PluginConnection`. SHA-256 via QtCore.                                |
| Device targeting (setImage→HID, dialRotate from real encoder) | Device/Backend (Phase 19 bridge)                      | —              | OUT OF SCOPE this phase; Phase 17 only provides the `actionReceived` signal + `sendEvent` seam the bridge calls. |
| Manifest / spawn / discovery                                  | Process/Lifecycle (Phase 18)                          | —              | OUT OF SCOPE.                                                                                                    |

**Tier guard:** Everything in Phase 17 lives in the **app layer** (`src/app/`), never `ajazz_core`. COD-031 is naturally satisfied because the work uses `QJsonObject` (Qt), not `nlohmann::json`, and lives in the app target where nlohmann is PRIVATE-permitted anyway.

## Standard Stack

### Core

| Library                                     | Version    | Purpose                                                           | Why Standard                                                                                                                   |
| ------------------------------------------- | ---------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `Qt6::WebSockets`                           | Qt 6.7+    | `QWebSocketServer` + `QWebSocket` (server + loopback test client) | Already the transport; CMake gates on `AJAZZ_HAVE_WEBSOCKETS` [VERIFIED: src/app/CMakeLists.txt:88-92]                         |
| `Qt6::Core` (`QJsonObject`/`QJsonDocument`) | Qt 6.7+    | JSON envelope parse + serialize                                   | Already used by the dispatcher; COD-031-safe (no nlohmann) [VERIFIED: sd_plugin_server.cpp:12,168]                             |
| `Qt6::Core` (`QCryptographicHash`)          | Qt 6.7+    | `Sha256` for `sha256(password+salt)`                              | In QtCore — **no extra link target needed**. Already used in-tree [VERIFIED: src/app/src/single_instance_guard.cpp:11,85]      |
| `Qt6::Test` (`QSignalSpy`)                  | Qt 6.7+    | Drive + observe async signals in tests                            | Already the test harness pattern [VERIFIED: tests/unit/test_sd_plugin_server.cpp:13,72]                                        |
| Catch2 v3                                   | (vendored) | Unit test framework                                               | Project standard; `catch_discover_tests` runs each `TEST_CASE` in its own subprocess [VERIFIED: tests/unit/CMakeLists.txt:358] |

### Supporting

| Library                     | Version | Purpose                                 | When to Use                                                                                                                                                                                           |
| --------------------------- | ------- | --------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `QRandomGenerator` (QtCore) | Qt 6.7+ | Generate the per-connection random salt | For PLUGIN-05 salt. Use `QRandomGenerator::system()->generate()` or fill a `QByteArray`, hex-encode. \[ASSUMED — verify exact API; alternative is `QUuid::createUuid().toString()` as salt material\] |

### Alternatives Considered

| Instead of                                                                               | Could Use                     | Tradeoff                                                                                                                                                                                                                                                                                                  |
| ---------------------------------------------------------------------------------------- | ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `QJsonObject`                                                                            | `nlohmann::json`              | nlohmann is PRIVATE-permitted in the app layer (COD-031), but the dispatcher already speaks `QJsonObject` and the test client builds raw JSON strings — switching gains nothing and adds a boundary risk. **Stay with QJsonObject.**                                                                      |
| One routed `actionReceived`                                                              | Typed signal per action class | Typed signals help Phase 19 consume structured encoder data, but 39 signals is over-engineering for a routing phase. **Recommendation: keep `actionReceived(uuid, msg)` as the universal seam; add 1-2 typed signals only if Phase 19's needs are concrete now (they are not — Phase 19 is downstream).** |
| `QHostAddress::LocalHost` + `QTcpServer` dual-stack (REQUIREMENTS PLUGIN-01 mentions it) | WS-only                       | The CONTEXT.md locked scope does **not** include the TCP dual-stack; the spec §4.1 notes it served "legacy plugins that did not pull in a WebSocket library." **Skip the TCP server** unless the planner finds a concrete need — it is not in the locked decisions.                                       |

**Installation:** None. All dependencies are already linked. No `npm`/`pip`/`cargo` packages.

## Package Legitimacy Audit

**Not applicable.** Phase 17 installs **zero external packages**. All capabilities use Qt 6 modules already linked in the build (`Qt6::WebSockets`, `Qt6::Core`, `Qt6::Test`) and the vendored Catch2. No registry interaction, no slopcheck needed.

## Architecture Patterns

### System Architecture Diagram

```
                         loopback only (127.0.0.1:<random port>)
   Plugin / PI / test client                          SdPluginServer (app layer)
   ──────────────────────────                         ───────────────────────────────────────
   QWebSocket.open(ws://127…) ─── TCP connect ──────▶ onNewConnection()
                                                         │ push PluginConnection{uuid="", socket, salt="", attempts=0}
                                                         ▼
   send {"event":"registerPlugin",         ── text ──▶ dispatchClientMessage()
         "uuid":"com.x.action1"}                         │ event=="registerPlugin" → bind uuid
                                                         │ emit pluginRegistered(uuid)
                                                         │ generate salt → store on connection
                                  ◀── text frame ────────┤ sendEvent("passHello", {device, deviceInfo,
   recv {"event":"passHello",                            │            authentication:{challenge, salt}})
         authentication:{salt,…}}                        ▼
                                                       (if password configured: await "authentication")
   send {"event":"authentication",         ── text ──▶ dispatchClientMessage()
         "challenge": sha256(pw+salt)}                   │ verify == sha256(password+salt)
                                                         │   match → keep open
                                                         │   mismatch → ++attempts; if attempts>=N close socket
                                                         ▼
   send {"event":"setBG",                  ── text ──▶ dispatchClientMessage()
         "context":"c1","payload":{…}}                   │ event ∈ 39-action set → emit actionReceived(uuid,msg)
                                                         │   (NEVER unhandledEventReceived for the 39)
                                                         ▼
                                                       ── Phase 19 bridge consumes actionReceived ──▶ (device)

   ◀── text frame (host→plugin) ─────────────────────── sendEvent(context|uuid, "dialRotate",
   recv {"event":"dialRotate",                                      {ticks, pressed, controller,
         "payload":{ticks,pressed,…}}                                deviceCoordinates, settings})
                                                       ▲
                                                       └── Phase 19 calls this with real encoder input
```

### Recommended File Structure

```
src/app/src/
├── sd_plugin_server.hpp      # extend: add sendEvent(...), auth state, new signals
└── sd_plugin_server.cpp      # extend: 39-action set, passHello emit, auth verify, sendEvent body
tests/unit/
└── test_sd_plugin_server.cpp # extend: add §9-named cases (auth, action-routing, sendEvent round-trip)
```

No new files required (LOCKED: extend in place).

### Pattern 1: Membership set for action routing (replaces the 13-array)

**What:** A single sorted/`std::array` (or `QSet<QString>`) of all 39 event names that route to `actionReceived` instead of `unhandledEventReceived`.
**When to use:** In `dispatchClientMessage`, after the `registerPlugin`/`registerPropertyInspector`/`authentication` special cases.
**Example:**

```cpp
// Source: extends existing kStandardActions, sd_plugin_server.cpp:212-224 [VERIFIED: in-tree]
// 13 standard + 26 AJAZZ = 39 routed actions (spec §4.3).
static constexpr std::array<char const*, 39> kRoutedActions = {
    // 13 standard:
    "setTitle","setImage","setState","showAlert","showOk","getSettings","setSettings",
    "getGlobalSettings","setGlobalSettings","switchToProfile","openUrl","logMessage","sendToPlugin",
    // sendToPropertyInspector is standard too (relay) — note registerPlugin/registerPropertyInspector
    // are handled BEFORE this set, not inside it.
    "sendToPropertyInspector",
    // 26 AJAZZ-only (spec §4.3):
    "setBG","setBackground","clearIcon","sendToDevice","openTouchbarSecondaryMenu",
    "exitTouchbarSecondaryMenu","enterGatheringEvent","registrationScreenSaverEvent",
    "unRegistrationScreenSaverEvent","setText","setFeedback","lockScreen","unLockScreen",
    "getScreenshot","getSystemAudioVolume","getUserInfo","setAcImgTop","onSwitchToFolderProfile",
    "onSwitchFromFolderProfile","deleteAction","stopBackground","exitFullScreen","touchTap",
    "getDetectedSensorsData","startAudioCapture","stopAudioCapture","sendUserInfo",
};
```

> **Counting note:** spec §4.3 lists 13 "standard Elgato" + a set marked "AJAZZ-only." `setState` and `sendToPlugin`/`sendToPropertyInspector` are standard. CONTEXT.md enumerates the **26 AJAZZ-only** explicitly — verify the final array totals 13 standard + 26 AJAZZ = 39 routed names, with `registerPlugin`/`registerPropertyInspector`/`authentication` handled separately (not in this set). [VERIFIED: spec §4.3 + CONTEXT.md list]

### Pattern 2: `sendEvent` writer with context→socket lookup

**What:** A method that finds the target `QWebSocket` (by `context` or `uuid`) and writes a serialized envelope.
**When to use:** Every host→plugin event (§4.4); the seam Phase 19 calls.
**Example:**

```cpp
// Source: new method; serialization mirrors QJsonDocument round-trip in onClientTextMessage
//         (sd_plugin_server.cpp:169) [pattern VERIFIED in-tree]
bool SdPluginServer::sendEvent(QString const& target, QString const& eventName,
                               QJsonObject const& payload) {
    QWebSocket* sock = socketForTarget(target);   // NEW lookup: uuid OR context
    if (!sock) return false;
    QJsonObject env;
    env.insert(QStringLiteral("event"), eventName);
    if (!payload.isEmpty()) env.insert(QStringLiteral("payload"), payload);
    // envelope keys (context/device/action) added by overload or by caller as needed
    sock->sendTextMessage(QString::fromUtf8(QJsonDocument(env).toJson(QJsonDocument::Compact)));
    return true;
}
```

> **Lookup gap:** today only `uuid`→socket exists (`uuidForClient`, sd_plugin_server.cpp:247). The §4.4 events are addressed per-`context` (an action instance), but Phase 17 has no context registry yet (that arrives with `willAppear`/settings in Phase 19/20). **Recommendation:** address `sendEvent` by `uuid` (the registered plugin) for Phase 17; accept a `context` string that is echoed into the envelope but resolve the socket by `uuid`. Add the `context`→socket map when Phase 19/20 introduces action instances. [ASSUMED — confirm with planner]

### Pattern 3: `passHello` + auth state on `PluginConnection`

**What:** Extend the `PluginConnection` struct with `QString salt; int authAttempts{0}; bool authenticated{false};`. Emit `passHello` right after binding the UUID in the `registerPlugin` branch.
**Example:**

```cpp
// Source: extends PluginConnection (sd_plugin_server.hpp:143-146) + registerPlugin branch
//         (sd_plugin_server.cpp:185-205) [VERIFIED in-tree]
struct PluginConnection {
    QString uuid;
    QWebSocket* socket{nullptr};
    QString salt;            // NEW: random per connection
    int authAttempts{0};     // NEW: bad-attempt counter (reject after N)
    bool authenticated{false};
};
// In registerPlugin handling, after it->uuid = uuid:
it->salt = makeRandomSaltHex();
QJsonObject auth{ {"challenge", /* server challenge or empty */ QString{}}, {"salt", it->salt} };
QJsonObject hello{ {"device", device}, {"deviceInfo", deviceInfo}, {"authentication", auth} };
sendEvent(uuid, QStringLiteral("passHello"), /* payload-or-envelope per §4.5 */ ...);
```

> **Envelope shape caution (§4.5 vs §4.2):** spec §4.5 step 2 shows `passHello` with `salt` at the **top level** (`{"event":"passHello","device":…,"salt":…}`), but §4.4 and CONTEXT.md describe `authentication:{challenge, salt}` **nested**. These two spec passages disagree. CONTEXT.md (the locked decision) says nested `authentication:{challenge, salt}` — **follow CONTEXT.md.** Flag this as a test-shape decision: the loopback test asserts whatever shape the plan locks. [CITED: spec §4.4/§4.5 — internal contradiction; CONTEXT.md resolves it]

### Anti-Patterns to Avoid

- **Letting any of the 39 fall through to `unhandledEventReceived`:** the existing test (test_sd_plugin_server.cpp:171-193) asserts `setBG` → `unhandledEventReceived`. **That test must be inverted/replaced** — after Phase 17, `setBG` routes via `actionReceived`. `unhandledEventReceived` should remain only for genuinely-unknown events (good to keep for forward-compat).
- **Adding a bind-address setter:** the loopback invariant is "no setter exists" (test comment, test_sd_plugin_server.cpp:63-65). Do not add one.
- **Hand-rolling SHA-256:** use `QCryptographicHash::Sha256`. (See Don't Hand-Roll.)
- **Non-ASCII test names:** ctest filter mangles em-dash/arrows on Win32 CMD codepage (CLAUDE.md). Use `-` and `->`.
- **Blocking the event loop in tests:** use the existing `waitForSpy`/`pump` helpers, never `sleep`.

## Don't Hand-Roll

| Problem                    | Don't Build            | Use Instead                                                  | Why                                                                                                                                    |
| -------------------------- | ---------------------- | ------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------- |
| SHA-256 of `password+salt` | Custom hash            | `QCryptographicHash::hash(data, QCryptographicHash::Sha256)` | Already in QtCore + used in-tree; constant-time concerns aside, this is the standard.                                                  |
| JSON parse/serialize       | Manual string building | `QJsonDocument`/`QJsonObject`                                | Already the dispatcher's idiom; COD-031-safe.                                                                                          |
| Random salt                | `rand()`               | `QRandomGenerator::system()` or `QUuid`                      | Cryptographically-seeded; `rand()` is not.                                                                                             |
| WebSocket framing          | Manual TCP framing     | `QWebSocket::sendTextMessage`                                | Qt handles framing/masking; the test client already uses it.                                                                           |
| Per-test process isolation | Custom main()          | `catch_discover_tests` (already wired)                       | Each TEST_CASE gets its own subprocess — this is **why** the leaked-QCoreApplication pattern exists (test_sd_plugin_server.cpp:44-51). |

**Key insight:** This phase is almost entirely *wiring existing Qt primitives into the established dispatcher shape*. The risk is not "missing a library" — it's protocol-shape fidelity (envelope keys, the §4.5 contradiction) and not regressing the `unhandledEventReceived` test.

## Common Pitfalls

### Pitfall 1: The `setBG → unhandledEventReceived` test will break (by design)

**What goes wrong:** `tests/unit/test_sd_plugin_server.cpp:171-193` currently asserts `setBG` surfaces as unhandled. After PLUGIN-03, `setBG` routes. A blind test run shows a "regression."
**Why it happens:** The MVP deliberately deferred the 26; that test encoded the MVP behavior.
**How to avoid:** Plan an explicit task to **rewrite that test** — assert `setBG` now reaches `actionReceived`, and pick a genuinely-unknown event name (e.g. `"someEventThatDoesNotExist"`) to keep an `unhandledEventReceived` assertion alive.
**Warning signs:** A failing `[plugin-server][extensions]` case after implementing routing.

### Pitfall 2: salt shape contradiction (§4.4 nested vs §4.5 top-level)

**What goes wrong:** Test asserts top-level `salt`, impl emits nested `authentication.salt` (or vice-versa) → silent mismatch.
**Why it happens:** The spec itself disagrees (§4.4/CONTEXT say nested; §4.5 step 2 shows top-level).
**How to avoid:** Lock the **nested** `authentication:{challenge, salt}` shape per CONTEXT.md; write the test and impl to the same shape; add a one-line comment citing the spec contradiction.
**Warning signs:** Auth round-trip test passes manually but plugin can't find the salt.

### Pitfall 3: Static-destruction crash with QtWebSockets in tests

**What goes wrong:** A stack `QCoreApplication` destructor races QtWebSockets' background thread at process exit → SIGSEGV.
**Why it happens:** Documented in test_sd_plugin_server.cpp:38-50.
**How to avoid:** Reuse the existing leaked `ensureQCoreApp()` singleton in every new test case. Do not allocate a new `QCoreApplication`.
**Warning signs:** Tests pass assertions then crash at teardown.

### Pitfall 4: Sending to a half-shut / disconnected socket after auth rejection

**What goes wrong:** After rejecting (closing) a socket on the Nth bad attempt, a later `sendEvent` to that uuid touches a deleted `QWebSocket`.
**Why it happens:** `stop()`/disconnect erase slots (sd_plugin_server.cpp:150-153); `sendEvent` must re-check the slot exists.
**How to avoid:** `sendEvent` looks up the live slot each call and returns `false` if not found; never cache raw `QWebSocket*`.
**Warning signs:** Use-after-free / crash under the `rejectsAfter5BadAttempts` test.

### Pitfall 5: `connectedPluginCount()` semantics vs auth

**What goes wrong:** Counting authenticated vs merely-registered plugins ambiguously.
**Why it happens:** `connectedPluginCount` today counts non-empty-uuid slots (sd_plugin_server.cpp:113-118). Auth adds a third state.
**How to avoid:** Decide explicitly whether the count includes pre-auth registered plugins; keep current semantics (registered = counted) unless a test needs otherwise, and don't silently change it (a v1.x lesson: TODO `[x]` items drift).

## Code Examples

### Verify a challenge (PLUGIN-05)

```cpp
// Source: QCryptographicHash idiom mirrors single_instance_guard.cpp:85 [VERIFIED in-tree]
static QString challengeFor(QString const& password, QString const& saltHex) {
    QByteArray const data = password.toUtf8() + saltHex.toUtf8();   // "password + salt" per §4.5
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}
// On "authentication" message:
//   if (challengeFor(configuredPassword, conn.salt) == msg["challenge"].toString()) accept;
//   else if (++conn.authAttempts >= kMaxAuthAttempts) conn.socket->close();
```

> Confirm exact concatenation order/encoding (`password+salt` as UTF-8 bytes, salt as hex string) against a real `.sdPlugin` JS shim if one is available; the spec says `sha256(password + salt)` without specifying encoding. [ASSUMED — encoding detail]

### Loopback test driving a host→plugin event (PLUGIN-04 round-trip)

```cpp
// Source: extends the established harness, test_sd_plugin_server.cpp:121-142 [VERIFIED in-tree]
TEST_CASE("SdPluginProtocolTest roundTrip dialRotate carries ticks pressed controller",
          "[plugin-server][events][encoder]") {
    ensureQCoreApp();
    SdPluginServer server; REQUIRE(server.start(0));
    QWebSocket client;
    QSignalSpy connSpy(&client, &QWebSocket::connected);
    QSignalSpy msgSpy(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    REQUIRE(waitForSpy(connSpy));
    client.sendTextMessage(R"({"event":"registerPlugin","uuid":"com.test.x"})");
    // ... drain passHello, then:
    server.sendEvent("com.test.x", "dialRotate",
        QJsonObject{{"ticks",2},{"pressed",false},{"controller","Encoder"}});
    REQUIRE(waitForSpy(msgSpy));
    auto obj = QJsonDocument::fromJson(msgSpy.last().at(0).toString().toUtf8()).object();
    REQUIRE(obj["event"].toString() == "dialRotate");
    REQUIRE(obj["payload"].toObject()["ticks"].toInt() == 2);
}
```

## Runtime State Inventory

Not applicable — Phase 17 is additive protocol code with no rename/refactor/migration. No stored data, live-service config, OS-registered state, secrets, or build artifacts carry a renamed string. **None — verified by the additive, in-place nature of the locked scope (extend existing files, no symbol renames).**

## State of the Art

| Old Approach                                 | Current Approach                                | When Changed      | Impact                                                                                                                                                |
| -------------------------------------------- | ----------------------------------------------- | ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| MVP: 13 actions, `setBG` → unhandled         | 39 actions routed, auth, `sendEvent`            | This phase        | The `unhandled` test inverts; dispatcher grows a 39-set + auth state.                                                                                 |
| `src/host/plugin-host/` new module (spec §9) | Extend `src/app/src/sd_plugin_server.{hpp,cpp}` | CONTEXT.md LOCKED | The spec §9 file paths and `PluginHostTest::*` names are **superseded** — use the in-app server. Adapt §9 test names to the `SdPluginServer` harness. |

**Deprecated/outdated for this phase:**

- Spec §9's `src/host/plugin-host/...` paths, `node_runner.cpp`, `cef_replacement.cpp`, `plugin_manifest.cpp` — those are Phase 18/20, not 17. Ignore for planning Phase 17.

## Assumptions Log

| #   | Claim                                                                                                            | Section                          | Risk if Wrong                                                                                                                                                                            |
| --- | ---------------------------------------------------------------------------------------------------------------- | -------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | `sendEvent` addresses by `uuid` for Phase 17; `context`→socket map deferred to Phase 19/20                       | Pattern 2                        | If a §4.4 event genuinely needs per-context addressing now, the seam needs a context registry sooner. Low risk — no action instances exist until willAppear (Phase 19).                  |
| A2  | salt is a hex-encoded random `QByteArray`; `QRandomGenerator::system()` or `QUuid` for material                  | Supporting stack / Code Examples | Wrong salt material weakens auth, but it's loopback-only + no TLS by design, so low security impact; verify API exists.                                                                  |
| A3  | `sha256(password+salt)` = SHA-256 of UTF-8(`password`)+UTF-8(`saltHex`)                                          | Code Examples                    | If a real plugin uses different encoding/order, auth fails interop. Verify against a real `.sdPlugin` JS shim if available; otherwise the test defines the contract for our own plugins. |
| A4  | passHello uses **nested** `authentication:{challenge, salt}` (CONTEXT) not top-level `salt` (spec §4.5 step 2)   | Pattern 3 / Pitfall 2            | Wrong shape breaks interop with real plugins. CONTEXT.md is the locked authority; flag for the planner to pin in the test.                                                               |
| A5  | Routed-action set totals exactly 39 (13 standard incl. setState/sendToPlugin/sendToPropertyInspector + 26 AJAZZ) | Pattern 1                        | An off-by-one (the doc itself warns "off by one" elsewhere) leaves one action unrouted. Mitigation: a test that sends each of the 39 and asserts none hits `unhandledEventReceived`.     |
| A6  | TCP dual-stack (REQUIREMENTS PLUGIN-01) is out of locked scope                                                   | Alternatives                     | If the planner reads PLUGIN-01 literally, they may add a `QTcpServer`. CONTEXT.md does not lock it. Recommend skip; confirm with planner.                                                |

## Open Questions

1. **Top-level vs nested salt in passHello (A4).**

   - What we know: CONTEXT.md says nested `authentication:{challenge, salt}`; spec §4.5 step 2 shows top-level `salt`.
   - What's unclear: which a real Mirabox/Elgato plugin's JS shim expects.
   - Recommendation: follow CONTEXT.md (nested); the loopback test defines our contract; revisit in Phase 18 when real plugins are spawned.

1. **What `challenge` value does the host put in `passHello`?**

   - What we know: §4.5 step 2 omits `challenge` (only `salt`); §4.4/CONTEXT show `authentication:{challenge, salt}`.
   - What's unclear: whether the host-side `challenge` is the salt echoed, an empty string, or a server nonce.
   - Recommendation: emit `salt` always; `challenge` empty when no password configured; document the choice. Plugin's reply `challenge` = `sha256(password+salt)`. (The two "challenge" fields are different directions.)

1. **Is there a configured password at all in Phase 17?**

   - What we know: default (no password) = passHello sent, connection accepted.
   - What's unclear: where the password is configured (settings? Phase 20?).
   - Recommendation: support a settable test-only password (constructor param or setter on the server) so `rejectsAfter5BadAttempts` is exercisable; default empty = no-auth path.

## Environment Availability

| Dependency                                                            | Required By          | Available                         | Version  | Fallback                                                                                     |
| --------------------------------------------------------------------- | -------------------- | --------------------------------- | -------- | -------------------------------------------------------------------------------------------- |
| `Qt6::WebSockets`                                                     | server + test client | ✓ (gated `AJAZZ_HAVE_WEBSOCKETS`) | Qt 6.7+  | App builds without it; plugin host disabled at runtime [VERIFIED: src/app/CMakeLists.txt:88] |
| `Qt6::Core` (`QCryptographicHash`, `QJsonObject`, `QRandomGenerator`) | auth, JSON, salt     | ✓                                 | Qt 6.7+  | — (QtCore always present)                                                                    |
| `Qt6::Test` (`QSignalSpy`)                                            | tests                | ✓                                 | Qt 6.7+  | —                                                                                            |
| Catch2 v3                                                             | tests                | ✓                                 | vendored | —                                                                                            |

**Missing dependencies with no fallback:** none.
**Missing dependencies with fallback:** `Qt6::WebSockets` is build-gated; when absent the whole feature (and its tests) is compiled out — same discipline as today. The plan's tests must be inside the `if(AJAZZ_HAVE_WEBSOCKETS)` block (tests/unit/CMakeLists.txt:335).

## Validation Architecture

### Test Framework

| Property           | Value                                                                           |
| ------------------ | ------------------------------------------------------------------------------- |
| Framework          | Catch2 v3 (`catch2/catch_test_macros.hpp`)                                      |
| Config file        | `tests/unit/CMakeLists.txt` (target `ajazz_unit_tests`, `catch_discover_tests`) |
| Quick run command  | `ctest --preset linux-release -R "plugin-server"`                               |
| Full suite command | `ctest --preset linux-release`                                                  |

> **ctest gotcha (CLAUDE.md):** the filter flag is `--tests-regex` / `-R`, NOT `--test-regex`. Test names must be ASCII-only.

### Phase Requirements → Test Map

| Req ID    | Behavior                                                                | Test Type | Automated Command                                           | File Exists?                                                                                              |
| --------- | ----------------------------------------------------------------------- | --------- | ----------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| PLUGIN-01 | binds LocalHost only, never Any                                         | unit      | `ctest --preset linux-release -R "loopback-only"`           | ✅ (test_sd_plugin_server.cpp:55) — keep                                                                  |
| PLUGIN-02 | envelope round-trips (event/context/payload + controller/ticks/pressed) | unit      | `ctest --preset linux-release -R "plugin-server.*envelope"` | ❌ Wave 0 (extend existing actionReceived test)                                                           |
| PLUGIN-03 | all 39 actions route, none hit unhandled                                | unit      | `ctest --preset linux-release -R "plugin-server.*actions"`  | ⚠️ partial — invert the `setBG→unhandled` case (test_sd_plugin_server.cpp:171); add a "all 39 route" case |
| PLUGIN-04 | host→plugin events arrive (dialRotate carries ticks/pressed/controller) | unit      | `ctest --preset linux-release -R "plugin-server.*events"`   | ❌ Wave 0                                                                                                 |
| PLUGIN-05 | passHello+salt sent; correct challenge accepted; reject after 5         | unit      | `ctest --preset linux-release -R "PluginAuth"`              | ❌ Wave 0                                                                                                 |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R "plugin-server|PluginAuth|SdPluginProtocol"`
- **Per wave merge:** `ctest --preset linux-release` (full suite, ~408 cases)
- **Phase gate:** Full suite green before `/gsd:verify-work`.

### Wave 0 Gaps

- [ ] New cases in `tests/unit/test_sd_plugin_server.cpp` (no new file — reuse the harness + `ensureQCoreApp`/`waitForSpy`):
  - `PluginAuthTest rejectsAfter5BadAttempts` (ASCII; spec §9 name minus `::`) — `[plugin-server][auth]`
  - `SdPluginProtocolTest roundTrip setImage decodesAndReencodes`-style routing case — but **note** image decode/HID is Phase 19; for Phase 17 assert routing only (rename to `routesSetImageToActionReceived`).
  - `SdPluginProtocolTest roundTrip dialRotate carries ticks pressed controller` — `[plugin-server][events][encoder]`
  - "all 39 actions route, none unhandled" data-driven case — `[plugin-server][actions]`
  - passHello-after-registerPlugin case asserting salt present — `[plugin-server][auth][handshake]`
- [ ] **Invert** the existing `surfaces unknown events via unhandledEventReceived` case so it uses a truly-unknown event, and add the `setBG → actionReceived` assertion.
- Framework install: none — Catch2 + WebSockets already wired.

> **Note on §9 target test names:** `CompatTest miraboxSocket_aliasedToElgato` is the **Mirabox JS shim** — that is Phase 18, NOT Phase 17. Do not add it here.

## Security Domain

`security_enforcement` is not set to `false` in config (config.json only has `workflow`), so it is treated as enabled.

### Applicable ASVS Categories

| ASVS Category         | Applies | Standard Control                                                                                                          |
| --------------------- | ------- | ------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | yes     | salt/challenge `sha256(password+salt)`; reject after N attempts (PLUGIN-05). Loopback-only, no TLS by design.             |
| V3 Session Management | partial | Per-connection auth state (`PluginConnection`); socket closed on rejection. No tokens/cookies.                            |
| V4 Access Control     | yes     | **LocalHost-only bind** is the access control — never `QHostAddress::Any`. Test-pinned (no setter exists).                |
| V5 Input Validation   | yes     | `QJsonDocument::fromJson` rejects malformed JSON (sd_plugin_server.cpp:170); validate `uuid`/`event` presence before use. |
| V6 Cryptography       | yes     | `QCryptographicHash::Sha256` — never hand-roll. Random salt via `QRandomGenerator::system()`.                             |

### Known Threat Patterns for a loopback WebSocket plugin server

| Pattern                                                                     | STRIDE                             | Standard Mitigation                                                                                                                          |
| --------------------------------------------------------------------------- | ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| LAN exposure of the plugin server (vendor's `QHostAddress::Any` regression) | Information Disclosure / Elevation | Bind `QHostAddress::LocalHost` only — already enforced + test-pinned. Do not add a widening setter.                                          |
| Brute-forcing the auth challenge                                            | Spoofing                           | Reject after N (5) bad attempts, close socket (PLUGIN-05).                                                                                   |
| Use-after-free sending to a closed/rejected socket                          | Tampering / DoS                    | `sendEvent` re-looks-up the live slot each call; never cache `QWebSocket*` (Pitfall 4).                                                      |
| Malformed/oversized JSON frame                                              | DoS                                | Existing parse guard drops malformed frames (sd_plugin_server.cpp:170-175); keep it.                                                         |
| Replay of a captured challenge                                              | Spoofing                           | Per-connection random salt makes a captured challenge useless on a new connection.                                                           |
| Timing side-channel on challenge compare                                    | Information Disclosure             | Loopback + no-TLS posture makes this low-priority; `QByteArray ==` is acceptable for this threat model. Note for the planner; not a blocker. |

## Sources

### Primary (HIGH confidence)

- `docs/protocols/streamdeck/akp_plugin_sdk.md` §4.2 (envelope), §4.3 (39 plugin→host actions), §4.4 (host→plugin events), §4.5 (auth handshake), §9 (corrections + test names) — the spec, RE-derived.
- `src/app/src/sd_plugin_server.{hpp,cpp}` — the server to extend (dispatch :179, kStandardActions :212, signals :103-125, PluginConnection :143, uuidForClient :247).
- `tests/unit/test_sd_plugin_server.cpp` — the loopback harness (ensureQCoreApp :44, waitForSpy :92, registerPlugin case :121, setBG→unhandled case :171).
- `src/app/CMakeLists.txt:86-92` + `tests/unit/CMakeLists.txt:333-342` — `AJAZZ_HAVE_WEBSOCKETS` gating.
- `src/app/src/single_instance_guard.cpp:11,85` — `QCryptographicHash` in-tree usage (proves QtCore-only, no extra link).
- `.planning/phases/17-plugin-protocol-completion/17-CONTEXT.md` — LOCKED decisions.
- `.planning/REQUIREMENTS.md:55-59,276-280` — PLUGIN-01..05 ownership.
- `.planning/ROADMAP.md:282-300` — Phase 17 success criteria.
- `./CLAUDE.md` — COD-031, ASCII test names, ctest `-R` flag, Qt gotchas.

### Secondary (MEDIUM confidence)

- Spec §4.5 vs §4.4 contradiction on salt placement — flagged as Open Question (resolved by CONTEXT.md).

### Tertiary (LOW confidence)

- Exact challenge encoding/order (`password+salt` UTF-8) — [ASSUMED]; verify against a real `.sdPlugin` JS shim in Phase 18 if available.

## Project Constraints (from CLAUDE.md)

- **COD-031:** no `nlohmann::json` in `ajazz_core`/public headers. This phase lives in `src/app/` and uses `QJsonObject` — naturally compliant. Do not introduce nlohmann into the server.
- **Schema doc is source of truth for JSON wire keys** — `akp_plugin_sdk.md` §4.2-4.5 governs envelope/event/payload key names; never invent keys.
- **ASCII-only test names** — Catch2 case names use `-`/`->`, no em-dash/arrows (Win32 ctest filter).
- **ctest filter flag is `-R`/`--tests-regex`**, not `--test-regex`.
- **Direct-to-`main` workflow / atomic commits / Conventional Commits** — land transport-pin, actions, auth, sendEvent as separate atomic commits (`feat(plugins): ...`). Never `--no-verify` unless the hook itself is broken.
- **Cap concurrent execute agents at 2.**
- **No system-level mutations** — code-only inside the repo.

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — all deps in-tree, verified by file:line.
- Architecture: HIGH — extends a concrete existing dispatcher; patterns mirror existing code.
- Pitfalls: HIGH — derived from in-tree test + documented spec contradiction.
- Auth wire-shape detail: MEDIUM — spec self-contradicts on salt placement (resolved by CONTEXT.md); encoding of `password+salt` is ASSUMED.

**Research date:** 2026-05-23
**Valid until:** 2026-06-22 (stable — internal codebase + RE doc; Qt 6 APIs stable)
