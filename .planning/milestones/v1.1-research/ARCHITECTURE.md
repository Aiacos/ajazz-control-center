# Architecture Research — v1.1 Integration

**Milestone:** v1.1 — Device lifecycle hardening + scaffolding-to-functional
**Researched:** 2026-05-13
**Confidence:** HIGH (all claims verified against working-tree source)

## Executive summary

v1.1 lands five workstreams on top of an established three-layer architecture
(core library → device backends → Qt/QML app + plugin host). Four of the five
workstreams are **additive** (extend existing seams cleanly); only WR-01 forces
an architectural decision (parser choice).

The biggest correction relative to the design docs:

- **`IDevice::capabilities()` does not exist in the working tree.** The design
  spec and time-sync plan both reference it, but the actual capability dispatch
  in `src/` today is `dynamic_cast<ICapable*>(IDevice*)` paired with static
  `DeviceDescriptor.hasRgb / hasTouchStrip` hints for UI gating. The
  `enum class Capability` exists in the header but is **not surfaced** through
  any virtual method on `IDevice`. Time-sync should follow the existing
  `hasRgb / hasTouchStrip` static-flag pattern, not invent a new
  `capabilities()` method.

## Existing architecture — verified layout

```
┌──────────────────────────────────────────────────────────────────────┐
│  src/app/  (Qt 6 Widgets + QML — libajazz-app)                       │
│                                                                      │
│  Application (QObject, owns everything via unique_ptr)               │
│   ├── DeviceRegistry m_deviceRegistry          (audit A1, NOT singleton)
│   ├── unique_ptr<BrandingService>      ─┐                            │
│   ├── unique_ptr<ThemeService>          │  QML singletons,           │
│   ├── unique_ptr<AutostartService>      │  registered via            │
│   ├── unique_ptr<DeviceModel>           │  Service::registerInstance │
│   ├── unique_ptr<ProfileController>     │  + QML_NAMED_ELEMENT       │
│   ├── unique_ptr<TrayController>        │  (the post-`d7f932f`       │
│   ├── unique_ptr<PluginCatalogModel>    │   shadow-trap-aware        │
│   ├── unique_ptr<LoadedPluginsModel>    │   pattern)                 │
│   ├── unique_ptr<PropertyInspector>    ─┘                            │
│   ├── unique_ptr<HotplugMonitor>        m_hotplug    ◄── extend      │
│   └── unique_ptr<IPluginHost>           m_pluginHost (gated on       │
│                                                       AJAZZ_PYTHON_HOST)
└──────────────────────────────────────────────────────────────────────┘
                ▲                                  │
                │ registerAll()                    │ refresh()  +
                │ + factory invocations            │ onHotplug(ev)
                ▼                                  ▼
┌──────────────────────────────────────────────────────────────────────┐
│  src/devices/  (libajazz-streamdeck / -keyboard / -mouse)            │
│                                                                      │
│  registerAll(DeviceRegistry&) — 8 streamdeck + keyboard + 7 mouse    │
│                                  (mouse adds AKP variants too)       │
│                                                                      │
│  Backends inherit IDevice + zero-or-more capability mix-ins:         │
│    Akp153Device  : IDevice, IDisplayCapable, IFirmwareCapable        │
│    Akp03Device   : IDevice, IDisplayCapable, IEncoderCapable         │
│    Akp05Device   : IDevice, IDisplayCapable, IEncoderCapable         │
│    ProprietaryKeyboard : IDevice (no display/RGB mix-ins yet)        │
│    AjSeriesMouse : IDevice (IMouseCapable, but see register.cpp      │
│                              wire-format @warning)                   │
└──────────────────────────────────────────────────────────────────────┘
                                  ▲
                                  │ #include
                                  ▼
┌──────────────────────────────────────────────────────────────────────┐
│  src/core/  (libajazz-core — pure C++20, no Qt dep)                  │
│                                                                      │
│  IDevice  (open/close/poll/onEvent/descriptor/id/firmwareVersion)    │
│  DeviceDescriptor  (POD struct: vid/pid/family/codename/keyCount/    │
│                     gridColumns/encoderCount/dpiStageCount/          │
│                     hasRgb/hasTouchStrip ← static UI hints)          │
│  IDisplayCapable / IRgbCapable / IEncoderCapable / IKeyRemappable /  │
│  IMouseCapable / IFirmwareCapable  (mix-ins; dynamic_cast'd)         │
│  enum class Capability  (declared but UNUSED at runtime — see below) │
│                                                                      │
│  DeviceRegistry  (factory map, constructor-injected per audit A1)    │
│  HotplugMonitor  (PIMPL, per-OS impl in same .cpp via #ifdef)        │
│  HidTransport / EventBus / Logger / ProfileIO / etc.                 │
└──────────────────────────────────────────────────────────────────────┘
                                  ▲
                                  │ subprocess (signed manifests + IPC)
                                  ▼
┌──────────────────────────────────────────────────────────────────────┐
│  src/plugins/  (libajazz-plugins — child Python process per plugin)  │
│                                                                      │
│  OutOfProcessPluginHost  (POSIX + Win32 splits — *_win32.cpp)        │
│  manifest_signer  (POSIX + Win32 splits — Ed25519 via python child)  │
│  wire_protocol.hpp  (zero-dep mini-JSON helpers, incl.               │
│                      `wire::findStringField`)                        │
│  Sandbox: bwrap (Linux) / sandbox-exec (macOS) / AppContainer (Win32)│
└──────────────────────────────────────────────────────────────────────┘
```

### Anchor files

| Concern                                                  | File                                                       | Lines                 |
| -------------------------------------------------------- | ---------------------------------------------------------- | --------------------- |
| `IDevice` + `DeviceDescriptor`                           | `src/core/include/ajazz/core/device.hpp`                   | 1-183                 |
| Capability mix-ins (`IDisplayCapable`, `IRgbCapable`, …) | `src/core/include/ajazz/core/capabilities.hpp`             | 1-545                 |
| `enum class Capability` (unused at runtime today)        | `src/core/include/ajazz/core/capabilities.hpp`             | 31-48                 |
| `HotplugMonitor` public API                              | `src/core/include/ajazz/core/hotplug_monitor.hpp`          | 1-111                 |
| `HotplugMonitor` per-OS impl (single TU, `#ifdef`)       | `src/core/src/hotplug_monitor.cpp`                         | 1-477                 |
| `Application` (owns hotplug + services)                  | `src/app/src/application.{hpp,cpp}`                        | hpp 1-137, cpp 53-195 |
| QML singleton registration                               | `src/app/src/application.cpp`                              | 144-159               |
| `DeviceModel` roles (`HasRgbRole`, `HasTouchStripRole`)  | `src/app/src/device_model.{hpp,cpp}`                       | hpp 71, cpp 79-101    |
| OOP host Win32 env (CR-01)                               | `src/plugins/src/out_of_process_plugin_host_win32.cpp`     | 451-468, 540-560      |
| Trust-roots parser POSIX (WR-01)                         | `src/plugins/src/manifest_signer.cpp`                      | 102-153               |
| Trust-roots parser Win32 (WR-01, mirror)                 | `src/plugins/src/manifest_signer_win32.cpp`                | 115-158               |
| Existing device registrations                            | `src/devices/{streamdeck,keyboard,mouse}/src/register.cpp` | full files            |

## Q1 — Where does `IClockCapable` slot in?

**Answer:** As a peer mix-in next to `IDisplayCapable`, `IRgbCapable`,
`IEncoderCapable`, `IFirmwareCapable` in `src/core/include/ajazz/core/capabilities.hpp`.
There is no "RGB subsystem" to coexist with — RGB is itself a flat mix-in.

**The Capability enum is decorative today.** It is declared (lines 31-48 of
`capabilities.hpp`) but no `IDevice` subclass exposes it via a virtual
`capabilities()` method, and no UI / service reads `device.capabilities() & Capability::PerKeyRgb`. The actual runtime checks I found are:

1. **Static UI gating** — `DeviceDescriptor.hasRgb` and
   `.hasTouchStrip` read by `DeviceModel::data()` and surfaced as QML roles
   `hasRgb` / `hasTouchStrip`. `ProfileEditor.qml:48` does
   `capabilities && capabilities.hasRgb`.
1. **Runtime method dispatch** — `dynamic_cast<IDisplayCapable*>(deviceptr)`
   (the design-doc rationale is correct; only the plumbing it claims to extend
   is fictitious).

**Recommended slot for time-sync:**

- Add `class IClockCapable` to `capabilities.hpp` next to `IFirmwareCapable`
  (the closest semantic neighbour — both are device-state queries, not display
  surfaces).
- Add `bool hasClock{false}` to `DeviceDescriptor` next to `hasTouchStrip`,
  mirroring the existing pattern.
- Do **not** invent `IDevice::capabilities()` in this milestone. The plan's
  Task 1 currently says "find the existing `enum class Capability` block"
  and "add `Clock = 1u << 15`" — that's fine cosmetically, but UI gating must
  come from `DeviceDescriptor.hasClock`, not from a runtime `capabilities()`
  call (which doesn't exist). The plan text "the `dynamic_cast` + bit-flag pair
  lets the UI decide …" should be read as "`dynamic_cast` for the service +
  `hasClock` flag for the UI", not as "bit-flag bitmask query".

**Confidence:** HIGH — verified by grepping for `capabilities()` and
`Capability::` across all of `src/` (zero non-header hits in app or devices).

## Q2 — `HotplugMonitor` abstraction for a mock backend

**Current shape (`hotplug_monitor.hpp`):**

- Concrete class with PIMPL (`struct Impl;` forward-declared, owned via
  `unique_ptr`).
- Public surface: `setCallback(Callback)`, `start() → bool`, `stop()`,
  `isRunning()`, destructor joins thread.
- **No virtual functions, no protected hooks, no enumerator injection point.**
- The OS event source is selected via `#ifdef __linux__ / _WIN32 / __APPLE__`
  inside `hotplug_monitor.cpp` and runs on a `std::thread` the class owns.

**Implications for a mock backend:**

There are three viable approaches; pick one in a roadmap phase before writing
the multi-device tests:

| Option                                            | Effort  | Touch surface                                                                                                                                                                               | Trade-off                                                                                                                                                                          |
| ------------------------------------------------- | ------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **A. Extract `IHotplugSource` interface**         | Medium  | Adds new header; `HotplugMonitor` becomes thin facade that delegates to an injected `unique_ptr<IHotplugSource>`. Default factory returns the per-OS one. Mock implements `IHotplugSource`. | Cleanest. Lets tests drive `HotplugEvent` injection without OS coupling. Breaks ABI of `HotplugMonitor` (acceptable — internal lib, no plugin SDK exposure).                       |
| **B. Subclass via `protected virtual`**           | Low     | Promote the per-OS `runImpl` pattern to a `protected virtual void runOnce(stop_token)` on `HotplugMonitor`; test fixture subclasses and overrides.                                          | Smaller diff but ties the public class to its test-mock subclass. Conflicts with the `final`-by-default house style implied by every existing device class being `final`.          |
| **C. Test-only `injectEvent(HotplugEvent)` shim** | Trivial | Adds a `#ifdef AJAZZ_TESTING` (or just public, documented as "test only") method on `HotplugMonitor` that the test calls to feed `snapshotCallback()` directly.                             | Pragmatic; matches what `PluginCatalogModel::mockFixture()` does elsewhere. Doesn't let you test the thread/wake plumbing — but that's already covered by the platform code paths. |

**Recommendation:** Option **C** for v1.1 (cheapest, sufficient for the
"disconnect-during-use, reconnect, multi-device baseline" tests called out in
PROJECT.md), with a roadmap note that Option A is the right long-term shape if
the hot-plug surface grows.

The thread-safety contract already documents that
`snapshotCallback()` is taken under `cbMu` and the callback is invoked from the
monitor thread — a test-only `injectEvent` that calls the callback on the
calling thread is consistent with the existing "callback runs on a background
thread; marshal to GUI thread" pattern at `application.cpp:178-184`.

**Confidence:** HIGH — `hotplug_monitor.hpp` has no virtuals; the only
extension seams today are `setCallback` (already public) and the PIMPL
boundary.

## Q3 — Does `dynamic_cast<IClockCapable*>` match an existing pattern?

**Yes.** Confirmed:

- The capability-mix-in pattern is the codebase's house style. Every
  `IDevice` subclass lists zero or more `I*Capable` mix-ins in its inheritance
  list, e.g.:
  - `Akp153Device : IDevice, IDisplayCapable, IFirmwareCapable`
    (`akp153.cpp:214`)
  - `Akp03Device : IDevice, IDisplayCapable, IEncoderCapable`
    (`akp03.cpp:205`)
  - `Akp05Device : IDevice, IDisplayCapable, IEncoderCapable`
    (`akp05.cpp:275`)
- The runtime dispatch idiom is `dynamic_cast<IDisplayCapable*>(device.get())`
  — though I could not find an *active* call site for this in the working tree
  (`capabilities()` and `dynamic_cast<I.*Capable` greps came up empty in
  `src/app/` and `src/devices/`). That means **the time-sync slice will be the
  first feature to actually exercise the dynamic_cast dispatch pattern that
  the headers were designed for.** This is a green-field affordance, not a
  retrofit.

**`hasRgb` lookup pattern (the static-flag UI gate):**

```cpp
// device_model.cpp:79-82  (the existing pattern to mirror)
case HasRgbRole:
    return d.hasRgb;
case HasTouchStripRole:
    return d.hasTouchStrip;
```

```qml
// ProfileEditor.qml:48 (the existing QML pattern)
readonly property bool _hasRgb: capabilities && capabilities.hasRgb ? capabilities.hasRgb : false
```

**Pattern fit for time-sync:**

- `DeviceDescriptor.hasClock` → `DeviceModel::HasClockRole` → QML
  `hasClock` role gates `DeviceRow.qml`'s "Sync now" button visibility.
  (Pure mirror of `hasTouchStrip`.)
- `TimeSyncService::setSystemTimeOn(codename)` opens the device via the
  registry, `dynamic_cast<IClockCapable*>(devicePtr)`, calls `setTime()`,
  emits Qt signal with the `TimeSyncResult`. (First active user of the
  dynamic_cast pattern.)

The plan at `docs/superpowers/plans/2026-05-13-time-sync.md` already
specifies exactly this split and uses the right idioms.

**Confidence:** HIGH — verified inheritance lists in all four backend `.cpp`
files; verified the `hasRgb` lookup chain end-to-end (header → model → QML).

## Q4 — CR-01 Win32 env block: blast radius

**Verified — touches only one file.**

```cpp
// out_of_process_plugin_host_win32.cpp:463-467
if (!pythonPath.empty()) {
    _putenv_s("PYTHONPATH", pythonPath.c_str());
}
_putenv_s("PYTHONDONTWRITEBYTECODE", "1");
_putenv_s("PYTHONUNBUFFERED", "1");
```

These three `_putenv_s` calls leak into the **parent (Qt UI) process**
environment every time a plugin spawns, and are also the only env mutation
required for the child Python process. The fix is the standard
`CREATE_UNICODE_ENVIRONMENT` recipe: snapshot `GetEnvironmentStringsW`, append
the three `KEY=VALUE\0` UTF-16 records + trailing `\0`, pass as `lpEnvironment`
to `CreateProcessW` / `CreateProcessAsUserW` (lines 542-560 of the same file).

**Cross-platform-CI dependency:** This is Windows-only code (the file is
under `#ifdef _WIN32` exclusion via build-system file selection). It cannot
be exercised on the Linux dev box — phase planning **must** schedule a CI
matrix entry that runs the OOP plugin host test on Windows (or at minimum a
Windows-compile-only smoke). The codebase already has CI for Win32
(`test_windows_app_container_sandbox.cpp` lives in `tests/unit/`), so the
hooks exist; the question is whether the existing CI matrix actually exercises
the OOP host child spawn or just compiles it.

**Does it touch any other code?**

- `manifest_signer_win32.cpp` — verified, **no env mutation**. It uses
  `_spawnvp` and inherits the parent env naturally (lines 87-104). Does not
  need the CR-01 fix.
- Plugin sandbox (`windows_app_container_sandbox.cpp`) — no env mutation
  (verified by greps for `_putenv_s` and `CREATE_UNICODE_ENVIRONMENT` — both
  return only the OOP-host file).

**Net:** CR-01 is a single-file, ~20-line change at
`out_of_process_plugin_host_win32.cpp:451-560` (delete the three `_putenv_s`
calls, add an env-block builder, pass `lpEnvironment` and OR in
`CREATE_UNICODE_ENVIRONMENT` to `creationFlags`). The Win32 build branch is
already separated; the rest of the host code path is untouched.

**Confidence:** HIGH — verified by full grep for `_putenv_s|GetEnvironmentStrings|lpEnvironment|CREATE_UNICODE_ENVIRONMENT` across the repo (one file).

## Q5 — WR-01: trust-roots parser dependency chain

**Call-site map (verified):**

```
loadTrustRoots(jsonPath)
  └─ manifest_signer.cpp:102 (POSIX)  +  manifest_signer_win32.cpp:115 (Win32)
       └─ verifyManifest()  (both files, ~line 187)
            └─ OutOfProcessPluginHost  (via ManifestSignerConfig handed in)
                 └─ Application::initPluginHost()  (application.cpp:94-141,
                    gated on `AJAZZ_PYTHON_HOST` build flag)
                      └─ LoadedPluginsModel  (consumes verified manifests +
                         publisher names for the "trusted vs self-signed"
                         badge — loaded_plugins_model.cpp:136 references the
                         trust_roots.json semantics)

Test surface:
  tests/unit/test_manifest_signer.cpp  (lines 226, 266, 276, 281, 305)
    - Verifier-flow tests use the bundled JSON.
    - `loadTrustRoots: parses the bundled JSON`
    - `loadTrustRoots: missing file returns empty list`
    - `loadTrustRoots: malformed entry never cross-pairs`  ◄── WR-01 regression
    - `loadTrustRoots: name-before-key entry resolves to a row`  ◄── REVIEW WR-01 fix landed
```

**Current implementation (both backends, mirrored verbatim):**

The two parsers are character-by-character identical (per the
`manifest_signer_win32.cpp:122` comment: "Mirroring is intentional"). They use
`wire::findStringField` (a 5-line mini-grep at
`src/plugins/src/wire_protocol.hpp:182`) with a manual brace-bounded window to
prevent malformed-entry cross-pairing. Two pre-existing tests exercise the
exact corner cases the WR-01 audit flagged.

**What constrains the parser choice:**

1. **No new third-party deps in `libajazz-core` or `libajazz-plugins`
   transitively.** The plugin host has a deliberately tight dep surface
   (hidapi, OpenSSL via libsodium, libudev/IOKit/Win32). Pulling
   `nlohmann/json` would bring ~25k LoC into the security-critical TU.
1. **Trust-roots JSON is shipped as a config file** at
   `AJAZZ_PLUGIN_TRUST_ROOTS` (an install-time path baked into the build).
   It is small (one object with a `"publishers"` array of a handful of
   entries) and infrequently parsed (once per plugin verify).
1. **Same logic must run on both POSIX and Win32** without divergence — the
   bug class WR-01 fixed (reverse key order) was a drift-prone hand-rolled
   parser. Any replacement needs to be either (a) a real JSON parser shared
   between the two TUs, or (b) a hardened scanner with property-based tests
   over the entry shape.
1. **No existing JSON parser in the codebase.** `wire_protocol.hpp` provides
   only `findStringField` / `findIntField` (line-oriented helpers for the
   stdin/stdout IPC wire format), which are explicitly documented as
   non-nesting-aware.

**Three live options for the architectural decision:**

| Option                                                                     | New dep                          | Risk                                                 | When this wins                                                                                                                                                    |
| -------------------------------------------------------------------------- | -------------------------------- | ---------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **A. Bundle `nlohmann/json` (single-header)**                              | +1 third-party header (~25k LoC) | Compile-time hit; security review of one new dep     | If the plugin manifest format itself drifts toward needing nested JSON elsewhere (manifest body, plugin store catalog, …) and we'd want a real parser there too.  |
| **B. Custom hardened scanner with state machine**                          | none                             | Hand-rolled = bug-prone; needs fuzz tests            | If we accept that `trust_roots.json` is the only JSON we ever parse in `libajazz-plugins` and the existing mini-grep + brace-window approach is "almost enough".  |
| **C. Accept COD-031 break** (read schema as line-delimited or YAML-subset) | none                             | Schema break; requires a migration note in CHANGELOG | If we control all `trust_roots.json` producers (we do — it's a CI-generated file from the project's signing keys) and the COD audit explicitly contemplates this. |

**Recommendation:** Option **A** (`nlohmann/json` single-header,
header-only, MIT) — the cost is one `#include` and ~150ms of compile time on
the two TUs that use it; the benefit is eliminating a class of parser bugs
that already burned us once (REVIEW WR-01) and gives us a real JSON parser
ready for the next time we need one. PROJECT.md "Key Decisions" already
contemplates this trade-off ("WR-01 needs an architectural decision …").

The decision is gated only by whether we want a new dep in the
security-critical TU; there's no technical blocker beyond that.

**Confidence:** HIGH for the call-site map (grep-verified). MEDIUM for the
parser-choice recommendation (it's a judgement call, not a fact).

## Q6 — Suggested build order

### Phase dependencies

```
                          v1.1 milestone
                                │
                                ▼
            ┌───────────────────────────────────────┐
            │  P0: Architectural decisions (gating) │
            │   • WR-01 parser choice (A/B/C)       │
            │   • Hotplug mock approach (A/B/C)     │
            └───────────────┬───────────────────────┘
                            │
       ┌────────────────────┼────────────────────┐
       │                    │                    │
       ▼                    ▼                    ▼
┌──────────────┐   ┌─────────────────┐   ┌─────────────────┐
│ P1: Time-sync│   │ P2: Hot-plug    │   │ P3: WR-01       │
│ slice 1/8    │   │ hardening       │   │ implementation  │
│ (capability  │   │ (mock backend + │   │                 │
│ + interface  │   │ disconnect +    │   │ Once parser     │
│ + descriptor │   │ multi-device    │   │ chosen, single  │
│ flag)        │   │ tests)          │   │ POSIX+Win32 swap│
│              │   │                 │   │ + mirror tests  │
│ → P1a: stubs │   │                 │   │                 │
│   in all 4   │   │ Depends on:     │   │ Depends on:     │
│   backends   │   │ P0 (mock approach)  │ P0 (parser dep) │
└──────┬───────┘   └────────┬────────┘   └─────────────────┘
       │                    │
       ▼                    ▼
┌──────────────┐   ┌─────────────────┐
│ P1b: Service │   │ Optional: feeds │
│  + DeviceModel  │   into P1's      │
│  + QML UI    │←──│ auto-sync-on-   │
│  + docs      │   │ arrival hook    │
└──────────────┘   └─────────────────┘

           Parallel-independent (gate on Windows CI):
           ┌──────────────────────────────────────┐
           │  P4: CR-01  (Win32 env block)        │
           │  Single-file, ~20-line change.       │
           │  Blocked only by Windows CI runner   │
           │  exercising the OOP host child spawn │
           └──────────────────────────────────────┘

           Independent feature track:
           ┌──────────────────────────────────────┐
           │  P5: Scaffolded-device wiring        │
           │  (pick N of 7 during phase planning) │
           │  • AKB980 PRO HID protocol           │
           │  • AKP03 / AKP05 wire-format reconc. │
           │  • AJ-series mouse wire-format       │
           │  Each is its own protocol-RE task    │
           │  with no v1.1 cross-deps             │
           └──────────────────────────────────────┘
```

### Recommended phase ordering with rationale

| #   | Phase                                                         | Why this order                                                                                                                                                                                                                                                                          |
| --- | ------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | **Architectural decisions** (WR-01 parser; hotplug mock seam) | Both block downstream phases. Cheap to do as a single decision-doc phase before any code lands.                                                                                                                                                                                         |
| 2   | **Time-sync slice** (the 8-task plan, taken whole)            | Independent of CR-01/WR-01. Adds the `IClockCapable` mix-in and exercises the dynamic_cast pattern for the first time. Lands the `hasClock` descriptor flag that future capability-checked features (e.g. clock widgets) will read. The plan is already authored and atomic.            |
| 3   | **Hot-plug hardening** (mock backend + multi-device tests)    | Builds on the architectural-decision phase's mock-seam choice. Can land in parallel with the time-sync slice if the team splits, but the auto-sync-on-arrival hook in time-sync Task 4 benefits from the mock once it exists — schedule the mock-seam decision before time-sync Task 4. |
| 4   | **WR-01 parser hardening**                                    | Architecturally cheap once option chosen. Touches two files in lockstep. Drop-in for existing tests.                                                                                                                                                                                    |
| 5   | **CR-01 Win32 env block**                                     | Single-file change; depends on a Windows CI runner exercising the OOP host (or willingness to land "best-effort" with a manual test). Gate on the CI capability check, not on code complexity.                                                                                          |
| 6   | **Scaffolded-device wiring**                                  | Independent feature track. Each device is its own protocol-RE task. Pick N during phase planning based on signal-capture availability (vendor-app inventory `d5616ef` is the limiting input for most of them).                                                                          |

### Why time-sync goes before hot-plug hardening

Two reasons:

1. **The plan is already written** (`docs/superpowers/plans/2026-05-13-time-sync.md`,
   8 atomic tasks, ready for `/gsd-execute-plan`). Hot-plug hardening
   currently has no plan; ordering plan-ready work first keeps the milestone
   on a tight time-to-first-commit.
1. **Time-sync's auto-sync hook (Task 4 in the plan) consumes
   `HotplugMonitor::deviceArrived` events** but doesn't need the mock — it
   needs the *real* monitor wired up, which already exists at
   `application.cpp:180`. The mock is a *test infrastructure* improvement, not
   a feature dependency.

### Why CR-01 is independent

Touches one file, no inter-phase deps. The only constraint is environmental
(Windows CI). Schedule it whenever a Windows-aware developer-day is available;
don't gate the rest of the milestone on it.

**Confidence:** HIGH for the dependency graph (derived from verified call-site
maps). MEDIUM for "time-sync before hot-plug hardening" (it's a sequencing
preference based on plan-readiness, not a hard dependency).

## Architectural anti-patterns to avoid in v1.1

### Anti-pattern 1: Inventing a runtime `IDevice::capabilities()` method

**What people would do:** Read the time-sync design doc, see "advertised in
`IDevice::capabilities()`", and add a `virtual uint32_t capabilities() const`
method to `IDevice` (lines 112-167 of `device.hpp`).

**Why it's wrong:** No existing backend overrides such a method. Adding it
forces a multi-file refactor (4 backend classes minimum) for zero behaviour
change in this slice, *and* it creates a redundant capability-query path
alongside the working `DeviceDescriptor.hasRgb` / `dynamic_cast` pair.

**Do this instead:** Add `DeviceDescriptor.hasClock` and rely on
`dynamic_cast<IClockCapable*>(device.get())`. Defer the
"unified runtime capabilities()" refactor to a future milestone if the need
ever surfaces.

### Anti-pattern 2: Subclassing `HotplugMonitor` for the mock

**What people would do:** Make `HotplugMonitor::start` / `stop` virtual and
subclass for the test mock.

**Why it's wrong:** Every existing backend class is `final`; the codebase
prefers composition over inheritance. Worse, it conflicts with the PIMPL
boundary — the per-OS implementation lives behind `struct Impl;` precisely so
the public class doesn't need vtable overhead.

**Do this instead:** Either inject an `IHotplugSource` (clean, ABI-breaking,
correct long-term) or add a documented test-only `injectEvent` shim (cheap,
sufficient for v1.1). See Q2 recommendation.

### Anti-pattern 3: Letting CR-01 and WR-01 drift between POSIX/Win32

**What people would do:** Fix the parser only in `manifest_signer.cpp` because
it's the file the dev box compiles; assume Win32 follows.

**Why it's wrong:** The two parsers are intentionally identical (Win32 file
line 121-125 comment: "Mirroring is intentional"). Drift would re-introduce
the very bug class that WR-01 is fixing.

**Do this instead:** Treat the two files as a single logical unit; any change
to one must land simultaneously in the other and exercise both via the
existing parallel test cases at `test_manifest_signer.cpp:226-326`.

## Data-flow changes in v1.1

### New: time-sync request flow

```
[User clicks "Sync now" on DeviceRow.qml]
  ↓ QML signal `syncTimeRequested(codename)` → Main.qml router
TimeSyncService::setSystemTimeOn(codename)        (new — src/app/src/)
  ↓
DeviceRegistry::open(deviceId)                    (existing core)
  ↓
dynamic_cast<IClockCapable*>(devicePtr)           (FIRST live use of pattern)
  ↓
clk->setTime(system_clock::now())                 (backend stubs)
  ↓ STUB returns TimeSyncResult::NotImplemented + AJAZZ_LOG_WARN
TimeSyncService emits syncFailed(codename, msg)   (Qt::QueuedConnection to QML)
  ↓
DeviceRow.qml button glyph: exclamation + tooltip
```

### New: time-sync auto-sync-on-arrival flow

```
USB plug event (OS)
  ↓
HotplugMonitor worker thread → Callback           (existing)
  ↓
Application::onHotplug(ev) [GUI thread via QueuedConnection]   (existing)
  ↓
m_deviceModel->refresh()                          (existing — unchanged)
  +
m_timeSync->onDeviceArrived(deviceId)             (NEW connection, app.cpp:180)
  ↓ if (autoSync && descriptor.hasClock)
QTimer::singleShot(300ms, [setSystemTimeOn])      (debounce HID-stack settle)
  ↓
[same stack as manual sync]
```

The only **modified** data flow is `Application::onHotplug` — currently a
single-line `m_deviceModel->refresh()` (line 193); time-sync adds a second
fan-out branch. No semantic change to existing behaviour.

### Changed: Win32 child-process env (CR-01)

```
Before:                                  After:
parent process env                       parent process env (unchanged)
  ├ _putenv_s PYTHONPATH ←leak              │
  ├ _putenv_s PYTHONDONTWRITEBYTECODE       │
  └ _putenv_s PYTHONUNBUFFERED              │
       ↓                                    │
CreateProcessW(lpEnv=nullptr)            CreateProcessW(lpEnv=customBlock,
   → child inherits parent env             CREATE_UNICODE_ENVIRONMENT)
                                            → child gets parent + 3 overrides;
                                              parent stays clean
```

## Integration points summary

| Integration                 | New file                              | Modified file                                                                                                | Direction                                                                                |
| --------------------------- | ------------------------------------- | ------------------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------- |
| `IClockCapable` interface   | —                                     | `core/include/ajazz/core/capabilities.hpp`                                                                   | additive (new class)                                                                     |
| `hasClock` descriptor flag  | —                                     | `core/include/ajazz/core/device.hpp`                                                                         | additive (new field)                                                                     |
| Backend stubs               | —                                     | `devices/streamdeck/src/akp{03,05,153}.cpp` + `devices/keyboard/src/proprietary_keyboard.cpp`                | additive (new mix-in + method)                                                           |
| Device registrations        | —                                     | `devices/streamdeck/src/register.cpp` + `devices/keyboard/src/register.cpp`                                  | additive (set `.hasClock = true`)                                                        |
| `TimeSyncService`           | `app/src/time_sync_service.{hpp,cpp}` | —                                                                                                            | new component                                                                            |
| Service registration        | —                                     | `app/src/application.{hpp,cpp}`                                                                              | additive (new unique_ptr + 1 line in `exposeToQml`, 1 line in `startBackgroundServices`) |
| `DeviceModel::HasClockRole` | —                                     | `app/src/device_model.{hpp,cpp}`                                                                             | additive (new role)                                                                      |
| QML UI                      | —                                     | `app/qml/components/DeviceRow.qml`, `app/qml/SettingsPage.qml`, `app/qml/DeviceList.qml`, `app/qml/Main.qml` | additive                                                                                 |
| Hot-plug mock seam          | TBD (Option A/B/C from Q2)            | `core/include/ajazz/core/hotplug_monitor.hpp` + `core/src/hotplug_monitor.cpp`                               | abstraction change                                                                       |
| CR-01 fix                   | —                                     | `plugins/src/out_of_process_plugin_host_win32.cpp`                                                           | replace 3 `_putenv_s` calls with env-block construction (~20 LoC)                        |
| WR-01 fix                   | depends on Q5 option                  | `plugins/src/manifest_signer.cpp` + `plugins/src/manifest_signer_win32.cpp`                                  | replace mini-grep with chosen parser (mirror in both)                                    |

## Sources

All claims verified against the working tree at HEAD (commit `7201758` on `main`):

- `src/core/include/ajazz/core/capabilities.hpp` (lines 31-48, 130-145, 220-262, 510-543)
- `src/core/include/ajazz/core/device.hpp` (lines 52-70, 112-170)
- `src/core/include/ajazz/core/hotplug_monitor.hpp` (full file)
- `src/core/src/hotplug_monitor.cpp` (lines 45-77, 213-291, 389-477)
- `src/devices/streamdeck/src/{akp153,akp03,akp05}.cpp` (class declarations)
- `src/devices/keyboard/src/proprietary_keyboard.cpp` (class declaration)
- `src/devices/{streamdeck,keyboard,mouse}/src/register.cpp` (full files)
- `src/app/src/application.{hpp,cpp}` (full files)
- `src/app/src/device_model.{hpp,cpp}` (relevant role declarations)
- `src/plugins/src/out_of_process_plugin_host_win32.cpp` (lines 430-560)
- `src/plugins/src/manifest_signer.cpp` (lines 1-200)
- `src/plugins/src/manifest_signer_win32.cpp` (lines 100-200)
- `src/plugins/src/wire_protocol.hpp` (line 182)
- `tests/unit/test_manifest_signer.cpp` (existing WR-01 regression tests at lines 226, 266, 276, 281, 305)
- `.planning/PROJECT.md` (Key Decisions and v1.1 carry-over candidates)
- `docs/superpowers/specs/2026-05-13-time-sync-design.md` (design intent)
- `docs/superpowers/plans/2026-05-13-time-sync.md` (8-task plan)

______________________________________________________________________

*Architecture research for: AJAZZ Control Center v1.1 — device lifecycle hardening + scaffolding-to-functional*
*Researched: 2026-05-13*
