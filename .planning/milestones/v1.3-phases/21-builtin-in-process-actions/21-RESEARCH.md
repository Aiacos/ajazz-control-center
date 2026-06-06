# Phase 21: Built-in In-Process Actions - Research

**Researched:** 2026-05-23
**Domain:** Native (in-process) dispatch of `com.hotspot.streamdock.*` built-in action UUIDs; OS input synthesis behind a per-OS interface; an obs-websocket v5 client. Qt6 / C++20, hardware-free + headless.
**Confidence:** HIGH (in-tree architecture, all verified by grep/read); MEDIUM (OBS wire format — cited to the official protocol doc).

## Summary

Phase 21 makes the built-in actions the original "Stream Dock AJAZZ.exe" handles in-process real, instead of spawning a plugin. The single load-bearing seam is the `ActionEngine`'s **`plugin` executor callback** (`std::function<void(std::string_view id, std::string_view settingsJson)>`, `action_engine.hpp:78`). When that callback sees an `id` that starts with `com.hotspot.streamdock.`, a **`BuiltinActionRegistry`** intercepts it and dispatches natively; otherwise it falls through to the Phase-19 plugin-host bridge. Most built-ins reuse machinery that already exists or is already planned: `browser` → the `openUrl` executor; page/profile nav → `pushPage`/`popPage` + the Phase-16 page carousel + the Phase-15 `pageNavRequested` path; `device.brightness` → the Phase-14 control service `setBrightness` (`LIG`); `multiactions` → the existing `ActionChain` walk. The genuinely new surfaces are (a) **OS input synthesis** (`system.hotkey`/`plain.text`/`system.multimedia`/`system.volume`) behind an `IInputSynthesizer` interface with per-OS backends, which makes the Phase-15 `keyPress` executor stub real, and (b) an **obs-websocket v5 client** over `QWebSocket`.

**Critical sequencing finding \[VERIFIED: git log + filesystem\]:** Phases 15 and 16 are **PLANNED but NOT executed** on this `feat/streamdock` branch. `src/app/src/stream_dock_input_service.*` does not exist; no `15-*-SUMMARY.md` / `16-*-SUMMARY.md` files exist; the `ActionExecutors.keyPress`/`.plugin` callbacks are **not yet wired anywhere** (`grep keyPress src/app/` = 0 hits; `grep '\.plugin' src/app/` = 0 hits). The ROADMAP execution chain is 14 → 15 → 16, then 17 → 18 → 19 → 20 → 21. **Phase 21 has a hard dependency on Phases 15, 16, 19 being executed first** — it builds directly on `StreamDockInputService`, the page carousel, the control service, and the plugin-host bridge. The planner must either (a) treat 15/16/19 as merged-before-execution prerequisites, or (b) explicitly fall back to the planned interfaces if executing out of order. This is the single biggest risk to a clean Phase-21 plan.

**Primary recommendation:** Build a core-side `BuiltinActionRegistry` (UUID → handler) that the app's `plugin` executor consults first; build OS synthesis behind a core `IInputSynthesizer` interface (mirroring the existing `macro_recorder.hpp` per-OS-backend pattern), Linux `uinput` primary with the other backends stubbed but compiling; build the OBS client app-side behind `AJAZZ_HAVE_WEBSOCKETS` (auth default-on). Verify everything with an injected fake synthesizer + a loopback `QWebSocketServer` mock OBS — the exact pattern `test_sd_plugin_server.cpp` already uses.

## User Constraints (from CONTEXT.md)

### Locked Decisions

- **Dispatch:** A `BuiltinActionRegistry` maps each `com.hotspot.streamdock.*` UUID → a native handler, invoked when `ActionEngine`'s `plugin` executor (the Phase-15 stub) sees a built-in UUID — **in-process, no plugin spawn**. Reuse `ActionEngine` kinds where they map: `browser` → `OpenUrl`; page/profile nav → `OpenFolder`/`BackToParent` + the Phase-16 page model + the Phase-15 `pageNavRequested` path; `device.brightness` → the control service; `multiactions` → the existing `ActionChain` walk.
- **OS input synthesis** (`system.hotkey`/`plain.text`/`multimedia`/`volume`): behind an **`IInputSynthesizer` interface** with per-OS backends (Linux `uinput`/`XTest`, Windows `SendInput`, macOS `CGEvent`). This makes the Phase-15 `keyPress` executor stub real.
- **`system.hotkey` global hook is OPT-IN** — gated by a settings toggle, never always-on (anti-feature: always-on `WH_KEYBOARD_LL`/global hook). OUTPUT synthesis (typing/hotkeys) is the primary path; *capturing* a global hotkey to trigger an action is the opt-in surface.
- **OBS (`obsstudio`):** a `QWebSocket` obs-websocket client; **auth default-on** — require the user's OBS password, never connect to an unauthenticated OBS by default (anti-feature: plaintext OBS).
- **Honesty / anti-features (NOT replicated):** No always-on global keyboard hook; no auto-connect to unauthenticated OBS.

### Claude's Discretion

- Registry shape (UUID → `std::function` / handler objects) and where it lives (app-layer or core).
- Whether OS synthesis lands all four backends now or Linux-first with the interface + the other backends stubbed (cross-platform parity is a CI concern — the interface must compile on all 3).
- OBS action subset (scene switch / source toggle / record-stream) for the first cut.

### Deferred Ideas (OUT OF SCOPE)

- `vmix` / `youtube` / `network` / `mouse.event` / `device.k1proLED+-` / `pageindicatororgoto` / `pagebackorforword` → v1.3+ backlog (NOT in the success criteria).
- Plugin store → Phase 22. Auxiliary surfaces → Phase 23. Live witness → Phase 25.

## Phase Requirements

| ID        | Description                                                                                                                                                                                                                                                                                                                                                                      | Research Support                                                                                                                                                                                                                                                                            |
| --------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLUGIN-12 | Core built-in actions implemented in-process (sdk §1): page nav (`page.previous`/`next`/`goto`/`indicator`/`change`), profile nav (`profile.openchild`/`backtoparent`/`rotate`), `device.brightness`, `system.hotkey` (opt-in global hook), `system.multimedia`, `system.volume`, `plain.text`, `browser`/`openUrl`, `multiactions` (+ carousel), `obsstudio` (auth default-on). | `BuiltinActionRegistry` dispatch table (Architecture Patterns); `IInputSynthesizer` for the four synthesis actions; reuse of `pushPage`/`popPage` + page carousel for nav; control-service `setBrightness` for brightness; `ActionChain` walk for multiactions; `QWebSocket` OBS v5 client. |

## Architectural Responsibility Map

| Capability                                | Primary Tier                                                     | Secondary Tier                                      | Rationale                                                                                                                                                                                                                                                                                                                                                                           |
| ----------------------------------------- | ---------------------------------------------------------------- | --------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Built-in UUID dispatch (registry)         | Core (`ajazz_core`) OR App                                       | —                                                   | The `plugin` executor is a `std::function` the app supplies (`action_engine.hpp:78`). The registry can live in core (pure, COD-031-safe, table of `std::function`) and be invoked from the app's `plugin` lambda; OR be app-side. Recommend **core** for the pure dispatch table + handler signatures, **app** for the handlers that touch Qt/OS (synthesis, OBS, control service). |
| Page nav (`page.*`)                       | App (Phase-16 carousel + Phase-15 `pageNavRequested`)            | Core `ActionEngine` page-state authority            | Page navigation is host-side (PROFILE-02, no device opcode). The `ActionEngine` owns `currentPageId()`; the app owns the carousel ordering + repaint.                                                                                                                                                                                                                               |
| Profile nav (`profile.*`)                 | Core `ActionEngine` (`pushPage`/`popPage`)                       | App (profile.rotate switches active profile)        | `openchild`/`backtoparent` = `OpenFolder`/`BackToParent` on the nav stack; `profile.rotate` cycles the *active profile* (app/ProfileController concern).                                                                                                                                                                                                                            |
| `device.brightness`                       | App control service (`setBrightness` → `LIG`)                    | Core `IDisplayCapable`/`IFirmwareLightingCapable`   | Brightness is a wire write on the held device handle; the Phase-14 control service owns it.                                                                                                                                                                                                                                                                                         |
| OS key/media synthesis                    | OS backend behind core `IInputSynthesizer`                       | App (injects the real backend; tests inject a fake) | Synthesis is platform-native (uinput/SendInput/CGEvent). The interface is core (pure, mirrors `macro_recorder.hpp`); the OS TUs are per-platform.                                                                                                                                                                                                                                   |
| `system.hotkey` global *capture* (opt-in) | OS backend (same per-OS pattern as macro recorder INPUT capture) | App settings toggle gate                            | INPUT capture (a global hotkey that *triggers* an action) is the anti-feature-sensitive opt-in surface — same OS hook layer the `macro_recorder` backends would use; gated OFF by default.                                                                                                                                                                                          |
| OBS client                                | App (`QWebSocket`, JSON, auth)                                   | —                                                   | Network + JSON + crypto are app-tier (COD-031 forbids `nlohmann` in core; QWebSocket is Qt).                                                                                                                                                                                                                                                                                        |

## Standard Stack

### Core

| Library                                          | Version    | Purpose                                                                                                                                                              | Why Standard                                                                                                                                                               |
| ------------------------------------------------ | ---------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Qt6::WebSockets                                  | 6.7+       | `QWebSocket` OBS client (also the existing `SdPluginServer` transport)                                                                                               | Already a (build-gated) project dependency; same module the plugin host uses [VERIFIED: src/app/CMakeLists.txt:90].                                                        |
| Qt6::Core                                        | 6.7+       | `QCryptographicHash` (SHA256 for OBS auth), `QSettings` (opt-in toggle + OBS password), `QJsonDocument`/`QJsonObject` (OBS messages), `QProcess`, `QDesktopServices` | Already in the app; `QCryptographicHash` already used in `single_instance_guard.cpp` [VERIFIED: grep].                                                                     |
| Catch2                                           | (vendored) | Unit tests                                                                                                                                                           | Project standard test framework \[VERIFIED: tests/unit/test_action_engine.cpp uses `catch2/catch_test_macros.hpp`\].                                                       |
| Linux uinput (`/dev/uinput`, `<linux/uinput.h>`) | kernel     | Linux key/media synthesis backend                                                                                                                                    | Compositor-agnostic (works on X11 AND Wayland); the only synthesis path that works under Wayland without per-compositor portals. See Pitfall 4. [CITED: kernel.org uinput] |

**No new external packages.** Phase 21 is pure in-tree C++/Qt + the OS synthesis syscalls. The Package Legitimacy Gate is **N/A** (no npm/pip/cargo installs) — same posture recorded in 15/16-RESEARCH.

### Supporting

| Library                       | Version | Purpose                             | When to Use                                                                                     |
| ----------------------------- | ------- | ----------------------------------- | ----------------------------------------------------------------------------------------------- |
| X11 `XTest` (`libXtst`)       | —       | Alternative Linux synthesis backend | ONLY under X11; does NOT work on Wayland. Prefer `uinput`. Document as fallback only. [ASSUMED] |
| Win32 `SendInput`             | —       | Windows synthesis backend           | Windows TU only; behind `#ifdef _WIN32`. Use the `_s` CRT variants per CLAUDE.md. [ASSUMED]     |
| macOS `CGEvent`/`CGEventPost` | —       | macOS synthesis backend             | macOS TU only; needs Accessibility permission at runtime (note for Phase 25). [ASSUMED]         |

### Alternatives Considered

| Instead of                        | Could Use                                  | Tradeoff                                                                                                                                                                                                                                                                                                                                                                               |
| --------------------------------- | ------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `uinput` (Linux synthesis)        | `XTest`                                    | XTest is simpler but X11-only — broken under Wayland (the project's primary dev env is niri/Wayland per CLAUDE.md). `uinput` is compositor-agnostic but needs `/dev/uinput` write perms (udev rule or `input` group). Pick `uinput`; note the perms gap is a Phase-25 hardware/permission concern, not a code blocker (the interface + fake backend test green without `/dev/uinput`). |
| Core-side `BuiltinActionRegistry` | App-side registry                          | Core keeps it COD-031-pure (table of `std::function`, no Qt). App-side is simpler to wire to Qt handlers but risks pulling Qt into a core-adjacent class. Recommend: core holds the *dispatch table type* + handler signatures; app constructs the table and supplies Qt-touching handlers.                                                                                            |
| `QWebSocket` raw JSON for OBS     | A third-party obs-websocket C++ client lib | No mature, slop-free C++ obs-websocket client exists; the v5 protocol is ~6 messages for our subset. Hand-rolling the client over `QWebSocket` + `QJsonDocument` is correct here (NOT a "don't hand-roll" — the protocol is small and the dependency surface is the risk).                                                                                                             |

**Installation:** None. Verify Qt module availability:

```bash
# Confirms the WebSockets module the OBS client needs is present (already build-gated)
cmake --preset linux-release 2>&1 | grep -i websockets   # expect "Qt6::WebSockets found"
```

## Package Legitimacy Audit

**N/A — no external package installs.** Phase 21 is pure in-tree C++20/Qt6 plus OS-native syscalls (`uinput`/`SendInput`/`CGEvent`) and `QWebSocket`. No npm/PyPI/crates dependency is added. The Package Legitimacy Gate produces no `[SLOP]`/`[SUS]` exposure (consistent with the 15/16/19-RESEARCH audits). slopcheck was not run because there are no candidate packages.

## Architecture Patterns

### System Architecture Diagram

```
  device input (Phase 15)            profile-authored chain
  KeyPressed/Encoder/Touch                  │
        │                                    │
        ▼                                    ▼
  StreamDockInputService.dispatch ──► ActionEngine.run(chain)
                                            │  walks each Action step by ActionKind
                          ┌─────────────────┼───────────────────────────────┐
                          ▼                 ▼                                 ▼
                   Kind::OpenUrl     Kind::OpenFolder/BackToParent      Kind::Plugin
                   Kind::KeyPress    (pushPage/popPage)                 (id, settingsJson)
                   Kind::RunCommand         │                                 │
                   Kind::Sleep              │                                 ▼
                          │                 │                    ┌──────────────────────────┐
                          │                 │                    │  app `plugin` executor    │
                          │                 │                    │  lambda                   │
                          ▼                 ▼                    │   id.startsWith            │
                  app executors      Phase-16 page              │  "com.hotspot.streamdock."?│
                  (openUrl,          carousel + repaint          └────────────┬──────┬───────┘
                   keyPress→             │                            yes      │      │ no
                   IInputSynthesizer)    ▼                                     ▼      ▼
                          │       control service repaint        BuiltinActionRegistry   Phase-19
                          ▼                                       .dispatch(uuid, json)   plugin-host
                  IInputSynthesizer                                       │               bridge (spawn)
              ┌───────────┼───────────┐                    ┌─────────────┼──────────────┐
              ▼           ▼           ▼                     ▼             ▼              ▼
         uinput     SendInput     CGEvent           page.* / profile.*  device.brightness  obsstudio
         (Linux)    (Windows)     (macOS)           → nav + carousel    → control service  → QWebSocket
                                                    multiactions        system.hotkey       OBS v5 client
                                                    → ActionChain walk   system.multimedia   (auth default-on)
                                                                         system.volume
                                                                         plain.text
                                                                         → IInputSynthesizer
```

**Key insight:** the `plugin` executor is the ONE interception point. The registry sits *inside* the app's `plugin` lambda and short-circuits built-in UUIDs before they ever reach the Phase-19 plugin-host bridge.

### Recommended Project Structure

```
src/core/include/ajazz/core/
├── input_synthesizer.hpp     # IInputSynthesizer interface + KeyCombo/MediaKey types + makeDefaultInputSynthesizer() (mirrors macro_recorder.hpp)
└── builtin_action_registry.hpp  # BuiltinActionRegistry: UUID -> std::function<void(string_view settingsJson, BuiltinContext&)> (pure; no Qt/nlohmann)
src/core/src/
├── input_synthesizer_stub.cpp   # default stub (no-op, records calls) — always compiled
├── input_synthesizer_linux.cpp  # uinput backend — gated AJAZZ_FEATURE_INPUT_SYNTH (mirrors macro_recorder gating)
├── input_synthesizer_win.cpp    # SendInput backend — #ifdef _WIN32, gated
├── input_synthesizer_mac.cpp    # CGEvent backend — #ifdef __APPLE__, gated
└── builtin_action_registry.cpp  # the dispatch table population (handlers injected by the app)
src/app/src/
├── builtin_actions_service.{hpp,cpp}  # app glue: builds the registry, supplies Qt-touching handlers (brightness, OBS, openUrl, synthesis), holds the opt-in toggle
└── obs_client.{hpp,cpp}               # QWebSocket OBS v5 client (auth default-on) — gated AJAZZ_HAVE_WEBSOCKETS
tests/unit/
├── test_builtin_action_registry.cpp   # each UUID -> right handler (fake handlers / spies)
├── test_input_synthesizer.cpp         # fake backend asserts synthesized key/combo/text; opt-in capture OFF by default
└── test_obs_client.cpp                # loopback QWebSocketServer mock OBS; auth handshake asserted; refuses unauthenticated
```

### Pattern 1: BuiltinActionRegistry — UUID dispatch table inside the `plugin` executor

**What:** A map from built-in UUID → handler. The app's `plugin` executor checks the prefix and dispatches.
**When to use:** Every `Kind::Plugin` step whose `id` is a `com.hotspot.streamdock.*` built-in.
**Example:**

```cpp
// Source: derived from action_engine.hpp:78 (plugin executor signature) + test_action_engine.cpp:34 (executor pack)
// app-side: the plugin executor lambda supplied to ActionExecutors
executors.plugin = [&registry, &pluginHostBridge](std::string_view id, std::string_view settingsJson) {
    if (id.starts_with("com.hotspot.streamdock.")) {
        registry.dispatch(id, settingsJson);   // BuiltinActionRegistry — in-process
        return;
    }
    pluginHostBridge.forward(id, settingsJson); // Phase-19 spawn path (untouched)
};
```

Note: `std::string_view::starts_with` is C++20 — available (project is C++20).

### Pattern 2: IInputSynthesizer — per-OS backend behind a pure interface (mirror macro_recorder.hpp)

**What:** A core interface with a `makeDefaultInputSynthesizer()` factory returning a stub when no native backend is built — exactly the shape of `macro_recorder.hpp`/`.cpp` already in-tree.
**When to use:** `keyPress` executor (Phase-15 stub becomes real), `plain.text`, `system.multimedia`, `system.volume`, and the OUTPUT side of `system.hotkey`.
**Example:**

```cpp
// Source: pattern lifted verbatim from src/core/include/ajazz/core/macro_recorder.hpp:61-100
namespace ajazz::core {
struct KeyChord { std::vector<std::uint32_t> modifiers; std::uint32_t key; };  // platform-neutral
class IInputSynthesizer {
public:
    virtual ~IInputSynthesizer() = default;
    virtual bool typeText(std::string_view utf8) = 0;          // plain.text
    virtual bool sendChord(KeyChord const& chord) = 0;         // system.hotkey OUTPUT
    virtual bool sendMediaKey(std::uint32_t mediaUsage) = 0;   // system.multimedia / system.volume
};
// Returns a no-op StubInputSynthesizer when AJAZZ_FEATURE_INPUT_SYNTH is OFF — UI/tests still drive the path.
[[nodiscard]] std::unique_ptr<IInputSynthesizer> makeDefaultInputSynthesizer();
} // namespace ajazz::core
```

The fake backend for tests is just a recording subclass (same as `RecordingExecutors` in `test_action_engine.cpp:30`).

### Pattern 3: OBS WebSocket v5 client (auth default-on)

**What:** A `QWebSocket` that connects to OBS (default port **4455**, RPC version **1**), completes the Hello/Identify auth handshake, then sends Requests.
**When to use:** the `obsstudio` built-in.
**Auth algorithm \[CITED: github.com/obsproject/obs-websocket protocol.md\]:**
`auth = Base64( SHA256( Base64( SHA256( password + salt ) ) + challenge ) )`
**Example:**

```cpp
// Source: OBS protocol.md (master) + Qt QCryptographicHash docs; auth steps cited verbatim above
QString computeObsAuth(QString const& password, QString const& salt, QString const& challenge) {
    auto b64 = [](QByteArray const& b){ return QString::fromLatin1(b.toBase64()); };
    QByteArray secret = QCryptographicHash::hash((password + salt).toUtf8(),
                                                 QCryptographicHash::Sha256).toBase64();
    QByteArray authHash = QCryptographicHash::hash(secret + challenge.toUtf8(),
                                                   QCryptographicHash::Sha256);
    return b64(authHash);
}
// Hello (op 0) carries d.authentication.{challenge,salt} + d.rpcVersion when auth is enabled.
// Identify (op 1): { "op":1, "d":{ "rpcVersion":1, "authentication": <auth>, "eventSubscriptions":0 } }
// Request (op 6):  { "op":6, "d":{ "requestType":"SetCurrentProgramScene", "requestId":"<uuid>",
//                                   "requestData":{ "sceneName":"<name>" } } }
```

**Recommended first-cut request subset:** `SetCurrentProgramScene` (scene switch), `SetCurrentPreviewScene`, `ToggleRecord`, `ToggleStream` — the four highest-value, simplest-payload requests. [CITED: protocol.md request types]

**Anti-feature gate:** if the Hello message contains `d.authentication`, a password is REQUIRED — refuse to send Identify (and surface an error) when no password is configured. Do NOT connect to an OBS that omits `authentication` *and* silently proceed without warning; auth is default-on (LOCKED).

### Pattern 4: multiactions / LunBo carousel

**What:** `multiactions` binds an ordered `ActionChain` — the `ActionEngine` already walks it (`run(chain)`); the registry just hands the decoded chain to the engine. `multiactions.LunBo` cycles through a list of actions on repeated presses (one-per-press), so it needs **per-key state** (a cursor index) held by the registry/service.
**When to use:** `multiactions` → no new mechanism (chain walk). `LunBo` → a small `std::unordered_map<keyId, size_t cursor>` advanced on each press.
**Anti-pattern:** Do NOT re-implement chain sequencing — reuse `ActionEngine::run`. For LunBo, the only new state is the cursor; the per-step execution still goes through the engine.

### Anti-Patterns to Avoid

- **Always-on global keyboard hook** (`WH_KEYBOARD_LL` / persistent uinput grab / CGEventTap listener at startup): explicit anti-feature (LOCKED + REQUIREMENTS.md:28). INPUT capture for `system.hotkey` is opt-in, gated by a settings toggle, default OFF.
- **Auto-connecting to an unauthenticated OBS:** anti-feature (plaintext OBS). Auth default-on.
- **Pulling `nlohmann::json` into core for the registry/synthesizer:** COD-031 release-blocker. The registry handlers receive `settingsJson` as `std::string_view` and parse it in the app layer (where `nlohmann`/`QJsonDocument` is allowed), exactly as the `ActionEngine` already hands `settingsJson` verbatim to executors.
- **Inventing a device page opcode for page nav:** page navigation is host-side (PROFILE-02). `STP` page-magic is legacy-only. Reuse the Phase-16 carousel + `pushPage`/`popPage`.
- **A second nav stack:** the `ActionEngine` is the single page-state authority (Phase-16 Decision 4). `profile.openchild`/`backtoparent` map onto `OpenFolder`/`BackToParent`.

## Don't Hand-Roll

| Problem                           | Don't Build                | Use Instead                                                                                                                | Why                                                                                      |
| --------------------------------- | -------------------------- | -------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| Chain sequencing for multiactions | A new step interpreter     | `ActionEngine::run(chain)` (already walks chains, honors Sleep/delayMs via the injected Executor)                          | The engine already does ordered dispatch + non-blocking Sleep (`action_engine.hpp:131`). |
| Page navigation state             | A new page stack           | `ActionEngine` `pushPage`/`popPage` + `currentPageId()` (`action_engine.hpp:148-151`) + Phase-16 carousel                  | Single page-state authority; no device opcode.                                           |
| Brightness wire write             | A new `LIG` packet builder | The Phase-14 control service `setBrightness` (`akp05.cpp:615`, `CmdLight{0x4c,0x49,0x47}` in `akp_common_protocol.hpp:30`) | RE source of truth; never re-derive a wire builder (CLAUDE.md).                          |
| URL open                          | `QProcess xdg-open`        | The Phase-15 `openUrl` executor (`QDesktopServices::openUrl`)                                                              | Already wired/planned; `browser` is just an `OpenUrl` step.                              |
| SHA256 for OBS auth               | A bundled crypto lib       | `QCryptographicHash::Sha256` (already used in `single_instance_guard.cpp`)                                                 | Qt ships it; no new dependency.                                                          |
| JSON for OBS messages             | `nlohmann` in core         | `QJsonDocument`/`QJsonObject` in the app-tier `obs_client.cpp`                                                             | COD-031: keep core nlohmann-free; OBS client is app-tier.                                |

**Key insight:** Phase 21 is overwhelmingly *wiring existing machinery to new UUIDs*. The only genuinely new code is (1) the small dispatch table, (2) the per-OS synthesis backends (and even those clone the `macro_recorder.hpp` shape), and (3) the OBS client (~6 messages).

## Common Pitfalls

### Pitfall 1: Building on Phases 15/16/19 that are not yet executed

**What goes wrong:** The plan references `StreamDockInputService`, the page carousel, the control service `setBrightness`, and the plugin-host bridge as if they exist on disk. They are PLANNED but not executed on this branch.
**Why it happens:** v1.3 was "planned ahead" (STATE.md:31); 14→15→16→17→18→19→20→21 must execute in order.
**How to avoid:** The planner must declare the hard dependency (`depends_on` 15, 16, 19 executed) and read the 15/16/19 SUMMARY files at execute time. If executing out of order, fall back to the planned interfaces (the `plugin` executor signature in `action_engine.hpp:78` is the only contract that *exists* today and is stable).
**Warning signs:** `ls src/app/src/stream_dock_input_service.* ` returns nothing (it does today); no `15-*-SUMMARY.md`.

### Pitfall 2: COD-031 — nlohmann in core

**What goes wrong:** A core-side `BuiltinActionRegistry` or synthesizer that parses `settingsJson` pulls `nlohmann::json` into `ajazz_core` → release-blocker.
**Why it happens:** It's tempting to decode the settings near the dispatch table.
**How to avoid:** The registry passes `settingsJson` as `std::string_view` to handlers; JSON parsing happens in the app-tier handlers (`QJsonDocument` allowed). Verify: `grep -rn nlohmann src/core/include/` must return 0.
**Warning signs:** `#include <nlohmann/json.hpp>` in any `src/core/**` header.

### Pitfall 3: uinput perms / Wayland on the dev box

**What goes wrong:** The Linux synthesis backend opens `/dev/uinput` and gets EACCES; or a developer reaches for `XTest` and it silently no-ops under Wayland (niri).
**Why it happens:** `/dev/uinput` is root-only by default; `XTest` is X11-only.
**How to avoid:** Use `uinput` (compositor-agnostic). Keep the OS backend behind the build flag and the interface so the **fake backend** test passes with no `/dev/uinput` access (hardware-free gating). The real-perms witness is Phase 25 (same model as the `uaccess` ACL story in CLAUDE.md — perms are an operator/hardware concern, not a code blocker). Do NOT add a udev/system mutation from project tooling (CLAUDE.md hard rule).
**Warning signs:** A test that requires `/dev/uinput`; an `XTest` include without an X11 guard.

### Pitfall 4: Cross-platform compile (the interface must build on all 3)

**What goes wrong:** Synthesis types or `#ifdef`'d backends break the MSVC `/W4 /WX` or Apple-Clang `-Werror` build.
**Why it happens:** Per-OS code, CRT deprecation (C4996), unused `inline constexpr` at file scope.
**How to avoid:** The `IInputSynthesizer` interface + the stub are platform-neutral and always compiled; OS backends are in separate TUs behind `#ifdef`/build-flag. Use MSVC `_s` variants. ASCII-only test names. (CLAUDE.md cross-platform strictness — all verified hard rules.)
**Warning signs:** `sprintf`/`_wgetenv` in the Windows backend; em-dash in a `TEST_CASE` title.

### Pitfall 5: OBS auth bypass / refusing-when-unauthenticated logic inverted

**What goes wrong:** The client connects to an OBS that omits `d.authentication` and proceeds, or sends Identify without auth when a password is configured.
**Why it happens:** The auth-default-on gate is a small conditional that's easy to invert.
**How to avoid:** If Hello carries `d.authentication` → a configured password is MANDATORY (else refuse + error). The test asserts BOTH directions: (a) correct auth string computed against a known challenge/salt, and (b) the client refuses to connect when the mock server demands auth but no password is set.
**Warning signs:** A test that only checks the happy-path handshake.

### Pitfall 6: LunBo carousel state leaking across keys/profiles

**What goes wrong:** A single global cursor is shared across all LunBo keys, so two carousels interfere.
**Why it happens:** Forgetting the cursor is per-key.
**How to avoid:** Key the cursor by the binding identity (key index + page id, or a stable action instance id); reset on profile change.
**Warning signs:** A single `size_t m_lunboCursor` member.

## Code Examples

### Dispatching a built-in vs. forwarding to the plugin host

```cpp
// Source: action_engine.hpp:78 (plugin executor) + test_action_engine.cpp:34 (executor pack shape)
executors.plugin = [this](std::string_view id, std::string_view settingsJson) {
    if (m_builtinRegistry.handles(id)) {      // id.starts_with("com.hotspot.streamdock.")
        m_builtinRegistry.dispatch(id, settingsJson);
        return;
    }
    m_pluginBridge.forward(id, settingsJson); // Phase-19 path, unchanged
};
```

### Recording fake synthesizer for tests (no OS access)

```cpp
// Source: pattern from test_action_engine.cpp:30 RecordingExecutors + macro_recorder StubMacroRecorder
struct FakeSynth final : ajazz::core::IInputSynthesizer {
    std::vector<std::string> log;
    bool typeText(std::string_view t) override { log.emplace_back("text:" + std::string{t}); return true; }
    bool sendChord(ajazz::core::KeyChord const& c) override { log.emplace_back("chord:" + std::to_string(c.key)); return true; }
    bool sendMediaKey(std::uint32_t u) override { log.emplace_back("media:" + std::to_string(u)); return true; }
};
```

### Loopback mock OBS server for tests

```cpp
// Source: test_sd_plugin_server.cpp:100-130 (QWebSocketServer on loopback + QWebSocket client + QSignalSpy)
// A QWebSocketServer bound to 127.0.0.1 sends Hello{op:0, d.authentication{challenge,salt}};
// the OBS client must reply Identify{op:1, d.authentication=<computed>}; the server validates and
// replies Identified{op:2}. The test asserts the computed auth matches and that no-password -> no Identify.
```

## State of the Art

| Old Approach                                              | Current Approach                                                                                                | When Changed                    | Impact                                             |
| --------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- | ------------------------------- | -------------------------------------------------- |
| obs-websocket v4 (no SHA256-of-SHA256, different opcodes) | obs-websocket v5 (Hello/Identify, `Base64(SHA256(Base64(SHA256(pw+salt))+challenge))`, port 4455, rpcVersion 1) | OBS Studio 28 (2022) bundled v5 | Target v5 only; v4 is legacy. [CITED: protocol.md] |
| XTest for Linux synthesis                                 | uinput (Wayland-compatible)                                                                                     | Wayland adoption                | XTest is X11-only; uinput works on both.           |

**Deprecated/outdated:**

- obs-websocket v4 protocol: superseded by v5 (bundled in OBS ≥28). Do not implement v4.

## Assumptions Log

| #   | Claim                                                                                                                                                     | Section                          | Risk if Wrong                                                                                                                                                                                             |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | Windows `SendInput` / macOS `CGEvent` are the right synthesis backends and compile cleanly behind `#ifdef` with the project's MSVC/Apple-Clang strictness | Standard Stack / Supporting      | Low — the interface + stub compile regardless; only the real backends (deferred / Phase-25-witnessed) would need adjustment. Tagged `[ASSUMED]`.                                                          |
| A2  | The Phase-19 plugin-host bridge exposes a `forward(id, settingsJson)`-style entry the `plugin` executor can call after the built-in check                 | Architecture / Pattern 1         | Medium — depends on Phase-19's executed shape. The planner must confirm against `19-*-SUMMARY.md` at execute time.                                                                                        |
| A3  | `profile.rotate` cycles the *active profile* via the app/ProfileController (vs. a page concept)                                                           | Architectural Responsibility Map | Low-Medium — confirm the original app's `profile.rotate` semantics in akp_plugin_sdk.md / capture corpus before locking; could be a profile-list cycle vs. a folder rotate.                               |
| A4  | OBS default port 4455 and rpcVersion 1 are correct for the OBS subset targeted                                                                            | Pattern 3                        | Low — 4455/rpc1 is the well-known v5 default; the port is user-configurable anyway.                                                                                                                       |
| A5  | `page.indicator` / `page.change` (Knob) and `page.goto` map onto the existing carousel + a target-page index (no new device opcode)                       | Phase Requirements / Map         | Medium — `page.goto`/`page.indicator` semantics (absolute jump vs. relative; indicator = a non-action display hint) should be confirmed against the SDK §1 + Phase-16 carousel model during discuss/plan. |

## Open Questions

1. **Execution ordering of Phases 15/16/19 vs. 21.**

   - What we know: 21 hard-depends on 15 (input service + `keyPress`/`plugin` stubs), 16 (page carousel + control service repaint), 19 (plugin-host bridge). None are executed on this branch.
   - What's unclear: whether the operator will execute 14→...→20 before 21, or wants 21 planned against the planned interfaces.
   - Recommendation: plan with `depends_on: [15, 16, 19]` (executed), and instruct the executor to read the 15/16/19 SUMMARY files first; the only contract that exists *today* is `ActionExecutors.plugin` in `action_engine.hpp:78`.

1. **`profile.rotate` and `page.goto`/`page.indicator` exact semantics.**

   - What we know: §1 lists the UUIDs; the carousel + nav stack exist.
   - What's unclear: rotate = cycle active profiles? goto = absolute page index from settings? indicator = display-only (no dispatch)?
   - Recommendation: confirm against akp_plugin_sdk.md §1 + the capture corpus during planning; treat as the discretion-area "core set" scope, defaulting indicator to a no-op display hint if ambiguous.

1. **OBS action subset for the first cut.**

   - What we know: the four simple requests (`SetCurrentProgramScene`, `SetCurrentPreviewScene`, `ToggleRecord`, `ToggleStream`) cover the common cases.
   - Recommendation (Claude's discretion per CONTEXT): ship these four; defer source-toggle/filter requests.

## Environment Availability

| Dependency                                                     | Required By                             | Available                               | Version | Fallback                                                                    |
| -------------------------------------------------------------- | --------------------------------------- | --------------------------------------- | ------- | --------------------------------------------------------------------------- |
| Qt6::WebSockets                                                | OBS client (`obs_client.cpp`)           | ✓ (build-gated `AJAZZ_HAVE_WEBSOCKETS`) | 6.7+    | Compile-out the OBS built-in when absent (mirror `SdPluginServer`)          |
| Qt6::Core (`QCryptographicHash`, `QJsonDocument`, `QSettings`) | OBS auth, settings toggle, message JSON | ✓                                       | 6.7+    | —                                                                           |
| `/dev/uinput` + write perms                                    | Linux synthesis REAL backend            | ✗ (perms; not needed for tests)         | —       | Stub backend + fake-injected tests are hardware-free; real perms = Phase 25 |
| ctest / Catch2                                                 | Unit tests                              | ✓                                       | —       | —                                                                           |

**Missing dependencies with no fallback:** none (all gating tests are hardware-free with the fake synthesizer + loopback mock OBS).

**Missing dependencies with fallback:**

- `/dev/uinput` write access — not required for the gating tests (fake backend). Real synthesis witnessed in Phase 25.
- Qt6::WebSockets on minimal Qt installs — the OBS built-in compiles out behind `AJAZZ_HAVE_WEBSOCKETS`, exactly like `SdPluginServer`.

## Validation Architecture

### Test Framework

| Property           | Value                                                                                       |
| ------------------ | ------------------------------------------------------------------------------------------- |
| Framework          | Catch2 (vendored) + Qt offscreen (`QT_QPA_PLATFORM=offscreen` for QObject/QWebSocket tests) |
| Config file        | CMake presets (`tests/unit/CMakeLists.txt` per-test link blocks)                            |
| Quick run command  | \`ctest --preset linux-release -R "BuiltinAction                                            |
| Full suite command | `ctest --preset linux-release`                                                              |

### Phase Requirements → Test Map

| Req ID    | Behavior                                                                                                                       | Test Type | Automated Command                                       | File Exists?                                      |
| --------- | ------------------------------------------------------------------------------------------------------------------------------ | --------- | ------------------------------------------------------- | ------------------------------------------------- |
| PLUGIN-12 | Each built-in UUID dispatches to the right handler with the binding's settings                                                 | unit      | `ctest --preset linux-release -R BuiltinActionRegistry` | ❌ Wave 0                                         |
| PLUGIN-12 | `system.hotkey`/`plain.text`/`multimedia`/`volume` call `IInputSynthesizer` with the right chord/text/media key (fake backend) | unit      | `ctest --preset linux-release -R InputSynth`            | ❌ Wave 0                                         |
| PLUGIN-12 | Global-hook *capture* is OFF unless the opt-in toggle is set                                                                   | unit      | `ctest --preset linux-release -R InputSynth`            | ❌ Wave 0                                         |
| PLUGIN-12 | `device.brightness` calls the control service `setBrightness` (spy)                                                            | unit      | `ctest --preset linux-release -R BuiltinActionRegistry` | ❌ Wave 0 (reuse control-service spy)             |
| PLUGIN-12 | page/profile nav drives `pushPage`/`popPage` + the carousel (page-model assertions)                                            | unit      | `ctest --preset linux-release -R BuiltinActionRegistry` | ❌ Wave 0 (reuse ActionEngine/page-model harness) |
| PLUGIN-12 | `multiactions` runs the ordered chain; LunBo advances per press                                                                | unit      | `ctest --preset linux-release -R BuiltinActionRegistry` | ❌ Wave 0                                         |
| PLUGIN-12 | `browser` → `openUrl` executor fired                                                                                           | unit      | `ctest --preset linux-release -R BuiltinActionRegistry` | ❌ Wave 0                                         |
| PLUGIN-12 | OBS auth handshake: correct auth string vs known challenge/salt; refuses unauthenticated when password unset                   | unit      | `ctest --preset linux-release -R ObsClient`             | ❌ Wave 0 (mock QWebSocketServer)                 |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R "BuiltinAction|InputSynth|ObsClient"`
- **Per wave merge:** `ctest --preset linux-release`
- **Phase gate:** Full suite green before `/gsd:verify-work`. Live OS synthesis + a real OBS instance are Phase-25 witnesses.

### Wave 0 Gaps

- [ ] `tests/unit/test_builtin_action_registry.cpp` — covers PLUGIN-12 dispatch (each UUID → handler).
- [ ] `tests/unit/test_input_synthesizer.cpp` — covers the four synthesis actions via a fake backend + the opt-in-OFF assertion.
- [ ] `tests/unit/test_obs_client.cpp` — covers the OBS v5 auth handshake against a loopback `QWebSocketServer` mock (auth-on; refuses unauthenticated).
- [ ] Register all three in `tests/unit/CMakeLists.txt` (mirror the `test_action_engine.cpp` / `test_sd_plugin_server.cpp` link blocks; OBS test gated on `AJAZZ_HAVE_WEBSOCKETS`).
- [ ] No framework install needed — Catch2 + Qt offscreen are already in the suite.

## Security Domain

### Applicable ASVS Categories

| ASVS Category         | Applies           | Standard Control                                                                                                                                      |
| --------------------- | ----------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | yes (OBS)         | obs-websocket v5 SHA256 challenge/response; auth default-on; refuse unauthenticated                                                                   |
| V3 Session Management | no                | OBS connection is per-session, no persistent session store                                                                                            |
| V4 Access Control     | yes (opt-in hook) | Global-hook INPUT capture gated by an explicit settings toggle, default OFF                                                                           |
| V5 Input Validation   | yes               | `settingsJson` parsed in the app tier (`QJsonDocument`); bounded enums for key/media usages; clamp brightness 0..100 (control service already clamps) |
| V6 Cryptography       | yes               | `QCryptographicHash::Sha256` (never hand-roll); store OBS password via `QSettings` (note: plaintext at rest — see threat T-21-04)                     |

### Known Threat Patterns for Qt6 / built-in actions

| Pattern                                                                       | STRIDE                             | Standard Mitigation                                                                                                                                           |
| ----------------------------------------------------------------------------- | ---------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Always-on global keyboard hook (silent keylogger surface)                     | Information Disclosure / Elevation | Opt-in toggle, default OFF; capture only while the toggle is set (LOCKED anti-feature)                                                                        |
| Auto-connect to unauthenticated OBS (credential/control exposure)             | Spoofing / Tampering               | Auth default-on; refuse Identify without a configured password (LOCKED)                                                                                       |
| Profile-authored `system.hotkey`/`plain.text` synthesizing arbitrary OS input | Elevation                          | Same trust model as a keyboard macro; profile is local/user-authored (Phase 15 threat model T-15-03 carried forward); synthesis output is intentional         |
| `settingsJson` malformed/oversized reaching a handler                         | Tampering / DoS                    | App-tier JSON parse with bounded extraction; unknown UUID → no-op; missing fields → defaults                                                                  |
| OBS password at rest in `QSettings` (plaintext)                               | Information Disclosure             | Document as a known limitation; consider OS keychain in a later phase. Tagged for the threat model — do NOT claim encryption that isn't there (honesty rule). |
| Command/text injection via `plain.text`                                       | Tampering                          | `typeText` is literal-string synthesis, not a shell — no injection vector (unlike `runCommand`, which uses argv lists per T-15-02)                            |

## Sources

### Primary (HIGH confidence)

- `src/core/include/ajazz/core/action_engine.hpp` (lines 76-166) — `ActionExecutors` (the `plugin`/`keyPress`/`openUrl` callbacks), `ActionEngine::run`/`pushPage`/`popPage`/`currentPageId`. [VERIFIED: read]
- `src/core/include/ajazz/core/macro_recorder.hpp` + `src/core/src/macro_recorder.cpp` — the per-OS-backend interface + stub + `AJAZZ_FEATURE_*` build-gate pattern to clone for `IInputSynthesizer`. [VERIFIED: read]
- `src/core/include/ajazz/core/profile.hpp` (Action/ActionKind/ProfilePage/Profile) — the chain + page model. [VERIFIED: read]
- `tests/unit/test_action_engine.cpp` — the spy-executor + Catch2 test pattern. [VERIFIED: read]
- `tests/unit/test_sd_plugin_server.cpp` (lines 100-130) — the loopback `QWebSocketServer` + `QWebSocket` client + `QSignalSpy` mock-server pattern to clone for the mock OBS server. [VERIFIED: read]
- `src/app/CMakeLists.txt:85-92` + `CMakeLists.txt:129-143` — `AJAZZ_HAVE_WEBSOCKETS` build-gate the OBS client must reuse. [VERIFIED: read]
- `src/devices/streamdeck/src/akp05.cpp:615` + `akp_common_protocol.hpp:30` (`CmdLight` "LIG") — the `setBrightness` wire path `device.brightness` reuses. [VERIFIED: grep]
- `.planning/REQUIREMENTS.md:28,79` — PLUGIN-12 + the anti-feature inventory (opt-in hook; OBS auth-on). [VERIFIED: grep]
- `.planning/phases/15-...15-01-PLAN.md` + `16-...16-03-PLAN.md` — the planned `keyPress`/`plugin` stubs, `pageNavRequested`, and the page carousel this phase builds on. [VERIFIED: read]
- `git log` + `ls src/app/src/` — Phases 15/16 are planned-not-executed (no `stream_dock_input_service.*`, no SUMMARY files). [VERIFIED: tool]

### Secondary (MEDIUM confidence)

- obs-websocket v5 protocol — auth algorithm `Base64(SHA256(Base64(SHA256(pw+salt))+challenge))`, Hello(0)/Identify(1)/Request(6)/RequestResponse(7) message shapes, rpcVersion 1, request types `SetCurrentProgramScene`/`SetCurrentPreviewScene`/`ToggleRecord`/`ToggleStream`. [CITED: github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md]

### Tertiary (LOW confidence)

- Default OBS port 4455 (well-known v5 default; user-configurable). [ASSUMED — not stated in the fetched protocol.md excerpt]
- Windows `SendInput` / macOS `CGEvent` as the synthesis backends, and X11 `XTest` as a Linux fallback. [ASSUMED — training knowledge; the interface + stub are the only code that must compile/test now]

## Metadata

**Confidence breakdown:**

- Standard stack: HIGH — all in-tree; no new packages; verified by grep/read.
- Architecture (registry + interface + reuse map): HIGH — the `plugin` executor seam, `pushPage`/`popPage`, control-service `setBrightness`, and `macro_recorder` backend pattern are all verified in-tree.
- OBS protocol: MEDIUM — cited to the official protocol.md; the exact auth steps were extracted, but port/some request payloads are ASSUMED.
- OS synthesis backends (Win/mac): LOW — ASSUMED from training; only the interface + Linux uinput stub must land/test now (the rest are CI-compile + Phase-25 witness).
- Pitfalls: HIGH — derived from verified CLAUDE.md hard rules + the verified phase-sequencing gap.

**Research date:** 2026-05-23
**Valid until:** 2026-06-22 (stable in-tree architecture; obs-websocket v5 is mature/stable).
