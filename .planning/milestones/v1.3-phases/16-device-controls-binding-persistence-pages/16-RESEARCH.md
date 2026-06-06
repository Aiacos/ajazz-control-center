# Phase 16: Device Controls + Binding Persistence + Pages - Research

**Researched:** 2026-05-23
**Domain:** Qt6/QML C++20 app-layer wiring — QML-to-Profile binding bridge, live device controls (brightness/clear), and host-side multi-page navigation, all over already-built core machinery
**Confidence:** HIGH (every reuse surface read at file:line in-tree; no new wire format, no new serializer)

## Summary

Phase 16 is **pure wiring of machinery that already exists**, with one structural extension. The
core `Profile` schema, the hand-rolled `profileToJson`/`profileFromJson` (round-trips keys,
encoders, mouseButtons, **pages**, and full `KeyState` already), the atomic `writeProfileToDisk`/
`readProfileFromDisk` I/O, the `ActionEngine` with working `pushPage`/`popPage`/`OpenFolder`/
`BackToParent`, and `ProfileController::loadProfile`/`saveProfile` are all present and tested. The
brightness/clear capability (`IDisplayCapable::setBrightness`/`clearKey(0xFF)`) is byte-tested in
the AKP05 backend. The four gaps Phase 16 closes are: (1) **no QML control binds to brightness/clear**
— there is no Slider/clear-button in `ProfileEditor.qml` and no service method QML can call; (2)
**KeyDesigner is session-only** — its `bindings` ListModel never mutates the C++ `Profile`, so edits
evaporate on restart (explicitly documented at `KeyDesigner.qml:18-23`); (3) **the repaint path is
root-page-only** — Phase 14's planned `repaintFromProfile()` iterates `activeProfile().keys` with no
notion of the active page, so it cannot repaint a child folder; (4) **nobody consumes
`pageNavRequested`** and nothing maps a page switch back to a repaint.

The single structural change worth flagging early: **the Phase-14 control service must be extended to
repaint from a specific page's binding set, not just `Profile::keys`** (PROFILE-02 requires repainting
the *new page's* bindings). This is a small, additive change — add a `repaintPage(pageId)` overload (or
parameterize `repaintFromProfile`) that resolves `pageId` to either `Profile::keys` (for `"root"`) or
`Profile::pages[pageId].keys`, then reuses the existing coalesced `assignKeyImage` burst per bound key.

**A blocking prerequisite must be confirmed first:** Phase 14 and Phase 15 are *planned but not yet
executed* (no `*-SUMMARY.md` exists; `src/app/src/stream_dock_*.cpp` do not exist on disk). Phase 16's
reuse surfaces — `StreamDockControlService` (setBrightness, clear, repaint path, `activeProfile()`
getter) and `StreamDockInputService` (`pageNavRequested`) — are produced by those phases. Phase 16
**cannot execute until 14 and 15 land.** The planner must encode this as a hard dependency, not an
assumption.

**Primary recommendation:** Bridge KeyDesigner → `Profile` via a new app-layer controller method that
mutates the active `Profile` and calls `saveProfile` (do NOT add a second serializer); add a brightness
`Slider` (debounced ~50–100 ms) + a "Clear all" button to the Keys tab bound to new `Q_INVOKABLE`s on
the (QML-exposed) control service; extend the control service's repaint path to take a page id; and add
a thin page-navigation owner that consumes `pageNavRequested(±1)` + drives `ActionEngine` page state +
triggers a page repaint. Verify everything hardware-free with MockTransport wire-byte spies and a
fresh-controller profile round-trip.

## Architectural Responsibility Map

| Capability                       | Primary Tier                                           | Secondary Tier               | Rationale                                                                                                                        |
| -------------------------------- | ------------------------------------------------------ | ---------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| Brightness slider live-write     | App service (`StreamDockControlService`)               | QML (Slider + debounce)      | `LIG` is a device write; QML only emits the value. Routes to control service, NOT LightingService (keyboard RGB).                |
| Clear-all                        | App service (control service `clearKey(0xFF)` → `CLE`) | QML (button)                 | Device write; QML triggers.                                                                                                      |
| Binding edit → persist           | App controller (mutate `Profile` + `saveProfile`)      | QML (KeyDesigner editor)     | The `Profile` is C++-owned; QML edits must cross into C++ to be serialized. Core `profile_io` owns the disk write.               |
| Repaint on load                  | App service (control service repaint path)             | Core (`Profile` bindings)    | Device write driven by saved bindings; iterates the active page's `keys`.                                                        |
| Page navigation (prev/next/goto) | App page owner + Core `ActionEngine` (nav stack)       | QML / input service (intent) | `ActionEngine` owns the page stack; the app maps the intent to push/pop + repaint. No device page opcode (`STP` is legacy-only). |
| Profile JSON serialize           | Core (`profileToJson`/`profileFromJson`)               | —                            | Single serializer, COD-031-safe, schema-doc-driven. Do NOT add a second.                                                         |
| Atomic disk write                | Core (`writeProfileToDisk`)                            | —                            | fsync + rename; already correct.                                                                                                 |

## User Constraints (from CONTEXT.md)

### Locked Decisions

**Controls (DISPLAY-09):**

- The brightness slider and clear-all route to the **Phase-14 `StreamDockControlService`**
  (`setBrightness` → `LIG`; clear-all → `CLE`). Do **NOT** route through `LightingService` (that is
  keyboard firmware-RGB, a different capability/device class).

**Persistence (PROFILE-01) — machinery already exists; this is wiring:**

- Reuse `ProfileController::loadProfile`/`saveProfile` (profile_controller.cpp:47/60) over core
  `profile_io` (`writeProfileToDisk` — atomic, fsync-safe) + `profileToJson`/`profileFromJson`.
  Do NOT add a second serialization path.
- The gap is the bridge: editor assignments (KeyDesigner) must **mutate the `Profile`** (keys /
  encoders / touch `Binding`/`EncoderBinding`/`KeyState`) and trigger a save; on load, the device
  **repaints from the profile** via the Phase-14 control-service paint path.
- **Schema doc is the source of truth** for JSON wire keys (`Profile::deviceCodename` ⇄ `"device"`) —
  never align a writer to a C++ field name without checking the schema (CLAUDE.md).

**Pages (PROFILE-02) — machinery already exists; this is wiring:**

- Reuse `ActionEngine::pushPage`/`popPage` + `OpenFolder`/`BackToParent` + `Profile::pages`
  (`ProfilePage`). A page switch **repaints** from the new page's bindings via the control service.
  Consume the **`pageNavRequested(±1)`** intent emitted by the Phase-15 input service — Phase 16 owns
  turning that intent into an actual page change + repaint.

### Claude's Discretion (planner/executor; surface trade-offs)

- Where the brightness slider / clear-all live in QML (the device panel) and how they bind to the
  control service (Q_INVOKABLE vs property). If a service is QML-exposed as a singleton, use
  `qmlRegisterSingletonInstance` + the `static_assert(!is_default_constructible)` lock (CLAUDE.md
  QML_SINGLETON gotcha), as `LightingService` does.
- How KeyDesigner's current session-only binding model bridges to `Profile` (extend the existing
  ListModel-backed editor vs a controller method that writes the Profile then saves).
- Debounce/throttle of the live brightness slider so dragging doesn't flood `LIG` writes.

### Deferred Ideas (OUT OF SCOPE)

- Plugin SDK (server, manifest, spawn, bridge, Property Inspector, store) → Phases 17-22.
- Auxiliary surfaces (encoder overlays / main strip / touch strip) → Phase 23 (HW-gated).
- Physical-device witness of live brightness/clear/persistence/pages → Phase 25.

## Phase Requirements

| ID         | Description                                                                                                   | Research Support                                                                                                                                                                                                                                                                              |
| ---------- | ------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| DISPLAY-09 | A brightness slider and a "clear all keys" control in the device panel drive the device live (`LIG` / `CLE`). | `IDisplayCapable::setBrightness(percent)` (capabilities.hpp:124 region) + `clearKey(0xFF)` clears all; backend byte-tested (DISPLAY-03). Add `Q_INVOKABLE setBrightness/clearAll` to the QML-exposed control service; add Slider+button to `ProfileEditor.qml` Keys tab; debounce the slider. |
| PROFILE-01 | Key, encoder, touch bindings persist + survive restart, repaint on load.                                      | `profileToJson`/`profileFromJson` already round-trip `keys`/`encoders`/`KeyState` (profile.cpp:220-320, 778-853). Gap = KeyDesigner ListModel → `Profile` mutation + `saveProfile`. Repaint via control service.                                                                              |
| PROFILE-02 | Multi-page/folder profiles drive host-side page nav; prev/next/goto + swipe; `OpenFolder`/`BackToParent`.     | `ActionEngine` page stack works (action_engine.cpp:91-129, test_action_engine.cpp:91-111). `Profile::pages` serialized (profile.cpp:274-307). Gap = consume `pageNavRequested`, map to push/pop, and repaint the active page (control-service repaint path must learn page id).               |

## Standard Stack

No new third-party packages. Phase 16 is in-tree C++20/Qt6 + QML only.

### Core (all in-tree, reuse)

| Component                                                  | Location                                                                           | Purpose                                                                          | Why Standard                                                                       |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| `ajazz::core::Profile` + `profileToJson`/`profileFromJson` | `src/core/src/profile.cpp`, `profile.hpp`                                          | The single serializer; round-trips keys/encoders/mouseButtons/pages/KeyState     | COD-031-safe hand-rolled writer; schema-doc-driven. [VERIFIED in-tree]             |
| `writeProfileToDisk`/`readProfileFromDisk`                 | `src/core/src/profile_io.cpp`, `profile_io.hpp`                                    | Atomic fsync+rename profile persistence                                          | Crash-safe by design (SEC-012, COD-017). [VERIFIED in-tree]                        |
| `ajazz::core::ActionEngine`                                | `src/core/src/action_engine.cpp`, `action_engine.hpp`                              | Page navigation stack (`pushPage`/`popPage`/`currentPageId`) + chain interpreter | Already test-covered for page push/pop. [VERIFIED in-tree]                         |
| `ProfileController`                                        | `src/app/src/profile_controller.{hpp,cpp}`                                         | QML `loadProfile`/`saveProfile`; QML_SINGLETON                                   | Existing QML bridge to `profile_io`. [VERIFIED in-tree]                            |
| `StreamDockControlService`                                 | `src/app/src/stream_dock_control_service.{hpp,cpp}` (**Phase 14 — not yet built**) | `setBrightness`, clear, `repaintFromProfile`, `activeProfile()` consumer         | The DISPLAY-09 + repaint reuse surface. [CITED: 14-02-PLAN.md — pending execution] |
| `StreamDockInputService`                                   | `src/app/src/stream_dock_input_service.{hpp,cpp}` (**Phase 15 — not yet built**)   | Emits `pageNavRequested(int direction)`                                          | The PROFILE-02 swipe intent source. [CITED: 15-01-PLAN.md — pending execution]     |

### Supporting (in-tree patterns to mirror)

| Component                               | Location                                       | Purpose                                                                                          | When to Use                                                                  |
| --------------------------------------- | ---------------------------------------------- | ------------------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------- |
| `LightingService` QML_SINGLETON pattern | `src/app/src/lighting_service.{hpp,cpp}:47/54` | `create()`/`registerInstance()`/`static_assert(!is_default_constructible)` + `Q_INVOKABLE` shape | If the control service is QML-exposed for the slider/clear-all (it must be). |
| `MockTransport`                         | `tests/unit/fixtures/mock_transport.hpp`       | Byte-level wire assertion (`writes()`, `enqueueRead`/`enqueueReadFeature`)                       | All hardware-free verification (LIG/CLE/BAT/ULEND bytes).                    |
| `makeAkp05WithTransport`                | `src/devices/streamdeck/src/akp05.cpp:928`     | Build a MockTransport-backed device for tests                                                    | Repaint/clear/brightness wire tests.                                         |

### Alternatives Considered

| Instead of                                             | Could Use                                              | Tradeoff                                                                                                                                                                                                                                       |
| ------------------------------------------------------ | ------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Mutate `Profile` via a controller method (recommended) | Build a full QML-exposed Profile model (roles per key) | A model gives live two-way QML binding but is much larger; the editor is already a session ListModel — a controller "commit" method is the smaller, in-scope bridge. Surface this trade-off to the planner.                                    |
| Control service owns page state                        | Page owner is a separate small QObject                 | `ActionEngine` already owns the nav stack; whether the page owner is the control service, the input service, or a new small coordinator is a Claude's-discretion seam — pick the one that keeps the repaint-on-page-change wire test simplest. |
| Debounce in QML (`Timer`)                              | Debounce in C++ (QTimer in the service)                | QML `Timer` is simplest for the slider drag; a C++ coalescer is reusable. Either satisfies "don't flood LIG". Recommend QML-side debounce on the Slider `moved` signal plus a final write on `released`.                                       |

**Installation:** None. `cmake --preset linux-release && cmake --build --preset linux-release`.

## Package Legitimacy Audit

**Not applicable.** Phase 16 installs no external packages — it is pure in-tree C++20/Qt6 + QML wiring.
No npm/PyPI/crates dependency is added. (Consistent with 14-02-PLAN.md threat row T-14b-SC and
15-01-PLAN.md T-15-SC: "No package installs — Package Legitimacy Gate N/A.")

## Architecture Patterns

### System Architecture Diagram

```
                          ┌─────────────────────────────────────────────┐
   USER (QML)             │              ProfileEditor.qml               │
                          │  Keys tab:                                   │
   drag slider ──────────▶│   • Brightness Slider (debounced ~50-100ms)  │
   click "Clear all" ────▶│   • Clear-all button                         │
   edit a key (icon/      │   • KeyDesigner (bindings ListModel)         │
     label/action) ──────▶│   • Apply button                             │
                          └───────┬───────────────┬──────────┬──────────┘
                                  │               │          │
              setBrightness(v)    │   clearAll()  │  commit  │
              (Q_INVOKABLE)       │  (Q_INVOKABLE)│  binding │
                                  ▼               ▼          ▼
              ┌───────────────────────────────────┐  ┌──────────────────────┐
              │   StreamDockControlService (Ph14) │  │ binding-commit method │
              │   • setBrightness → LIG           │  │ (mutate active Profile│
              │   • clearAll → clearKey(0xFF)→CLE │  │  keys/encoders/state) │
              │   • repaintPage(pageId) ──┐       │  └───────────┬──────────┘
              └──────────────┬────────────┼───────┘              │
                             │            │                      ▼
                  held shared_ptr<IDevice>│              ProfileController
                             │            │              .saveProfile(path)
                             ▼            │                      │
                    Akp05Device backend   │                      ▼
                    (BAT/ULEND/LIG/CLE,   │            core::writeProfileToDisk
                     byte-tested)         │            (atomic fsync+rename)
                                          │                      │
                                          │                      ▼
                                          │            <AppDataLocation>/profiles/<id>.json
                                          │
   ── PAGE NAV ──                         │
   touch swipe (device) ──▶ StreamDockInputService.pageNavRequested(±1)
                                          │
                                          ▼
                              ┌───────────────────────────┐
                              │ Page owner (control svc or │
                              │ small coordinator)         │
                              │  • resolve current page id │
                              │  • ActionEngine push/pop OR │
                              │    page index +/- in pages │
                              │  • call repaintPage(newId) ─┘ (back to control svc)
                              └───────────────────────────┘
   On profile load:
   ProfileController.profileChanged ──▶ control svc repaintPage("root") ──▶ device repaint
```

A reader can trace the three primary use cases: (1) drag slider → `LIG`; (2) edit key → commit →
`Profile` mutated → `saveProfile` → disk → (on next load) `profileChanged` → repaint; (3) swipe →
`pageNavRequested` → page owner → `repaintPage(newId)`.

### Recommended Project Structure (files touched)

```
src/app/
├── src/
│   ├── stream_dock_control_service.{hpp,cpp}   # EXTEND (Ph14): add repaintPage(pageId) + Q_INVOKABLE setBrightness/clearAll; QML-expose
│   ├── profile_controller.{hpp,cpp}            # EXTEND: add binding-commit / mutate-Profile method(s) + default-path resolution
│   └── application.{hpp,cpp}                    # wire pageNavRequested → page owner; register control service as QML singleton
└── qml/
    ├── ProfileEditor.qml                        # add brightness Slider + Clear-all button to the Keys-tab header/footer
    ├── KeyDesigner.qml                          # bridge bindings ListModel → ProfileController commit
    └── Inspector.qml / components/KeyCell.qml   # already render icon/label; feed from committed Profile on load
```

### Pattern 1: QML-exposed control service singleton (mirror LightingService)

**What:** Expose `StreamDockControlService` to QML as a `QML_SINGLETON` so the Slider/button can call it.
**When to use:** DISPLAY-09 slider + clear-all binding.
**Example:**

```cpp
// Source: src/app/src/lighting_service.hpp:46-66 (in-tree pattern to mirror)
class StreamDockControlService : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(StreamDockControlService)
    QML_SINGLETON
public:
    static StreamDockControlService* create(QQmlEngine*, QJSEngine*); // returns registered instance
    static void registerInstance(StreamDockControlService*) noexcept;
    explicit StreamDockControlService(DeviceLookup lookup, QObject* parent); // no default parent
    Q_INVOKABLE void setBrightness(QString const& codename, int percent); // → LIG (clamped 0..100)
    Q_INVOKABLE void clearAll(QString const& codename);                   // → clearKey(0xFF) → CLE
};
// Co-located build-break lock (CLAUDE.md QML_SINGLETON gotcha):
static_assert(!std::is_default_constructible_v<StreamDockControlService>);
```

Registration in Application: `qmlRegisterSingletonInstance(...)` / the `create()` factory path — NEVER
the bare `QML_SINGLETON` macro alone (CLAUDE.md: bare macro silently spawns a second instance per import).

### Pattern 2: Brightness debounce (don't flood LIG)

**What:** Coalesce slider-drag values so only the latest reaches the device.
**When to use:** DISPLAY-09 slider.
**Example (QML-side, recommended for simplicity):**

```qml
// Source: Qt6 Slider + Timer idiom
Slider {
    id: brightness; from: 0; to: 100; stepSize: 1
    onMoved: debounce.restart()
    onPressedChanged: if (!pressed) StreamDockControlService.setBrightness(root.codename, value) // final
}
Timer { id: debounce; interval: 80; repeat: false
        onTriggered: StreamDockControlService.setBrightness(root.codename, brightness.value) }
```

Either a QML `Timer` (above) or a C++ single-shot `QTimer` in the service satisfies the constraint. The
C++ control service already plans a single-shot coalescing drain for images (14-02-PLAN.md) — a sibling
`QTimer` for brightness mirrors that pattern if the planner prefers C++-side throttling.

### Pattern 3: Page repaint must be page-scoped (the structural extension)

**What:** Repaint the *active page's* bindings, not just `Profile::keys`.
**When to use:** PROFILE-02 page switch + PROFILE-01 load.
**Example:**

```cpp
// EXTENSION of Phase-14 repaintFromProfile (which iterates only activeProfile().keys)
void StreamDockControlService::repaintPage(std::string const& pageId) {
    auto const& prof = m_profileAccessor();                  // injected accessor (14-02 seam)
    auto const& page = (pageId == "root")
        ? prof.keys                                          // root page lives in Profile::keys
        : prof.pages.at(pageId).keys;                        // child folder page
    for (auto const& [idx, binding] : page) {
        // reuse the existing coalesced assignKeyImage burst (BAT -> chunks -> ULEND) per bound key
        assignKeyImage(idx /*map to device 1-based per 14-02 keyIndex note*/, renderFrom(binding.state));
    }
}
```

**Key index base:** 14-02-PLAN.md flags that the AKP05 backend is **1-based** while the
`IDisplayCapable` doxygen says 0-based; the backend wins. The profile key map base must be confirmed by
reading the committed profile (14-02 already requires documenting this mapping). Phase 16 inherits that
mapping decision — do not re-derive it; read the 14-02-SUMMARY once it exists.

### Pattern 4: Page navigation owner

**What:** Map `pageNavRequested(±1)` to a concrete page change + repaint.
**When to use:** PROFILE-02 swipe / prev/next.
**Notes on `ActionEngine` semantics (read action_engine.cpp:91-129):**

- `OpenFolder` (`pushPage(step.id)`) pushes a child page by id; `BackToParent` (`popPage`) pops one
  (never below `"root"`). `currentPageId()` returns the stack back, defaulting to `"root"`.
- **`ActionEngine` has no "next/prev sibling page" concept** — it is a *folder tree* (parent/child via
  `ProfilePage::children`), not a flat carousel. A swipe `+1`/`-1` is therefore NOT a direct
  `pushPage`/`popPage`. The planner must decide the prev/next semantics:
  - **Option A (carousel over siblings):** maintain an ordered list of sibling page ids (e.g. the
    current page's parent's `children`, or top-level pages) and advance an index ±1, then `repaintPage`.
  - **Option B (folder semantics only):** `+1`=enter the first child folder (`OpenFolder`), `-1`=`BackToParent`. Simpler, matches `ActionEngine` directly, but "swipe" then means in/out, not lateral.
    This is an **open question for discuss-phase** (see Open Questions). Whichever is chosen, after the
    page-stack mutation, call the control service's `repaintPage(currentPageId)`.

### Anti-Patterns to Avoid

- **Adding a second serializer / writing Profile JSON in the app layer.** COD-031 + the locked
  "do NOT add a second serialization path" decision. Always go through `profileToJson`/`writeProfileToDisk`.
- **Routing brightness through `LightingService`.** That is keyboard firmware-RGB (`IFirmwareLightingCapable`, opcode 0x13). Brightness here is the Stream Dock `LIG` via the control service.
- **Bare `QML_SINGLETON` macro for the control service.** Use the `create()`/`registerInstance()`/`static_assert` pattern (CLAUDE.md).
- **Inventing a device page opcode.** `STP` page-magic is legacy-only; page nav is host-side (CONTEXT + REQUIREMENTS PROFILE-02).
- **Repainting `Profile::keys` on a page switch.** That repaints the root, not the child page — the bug Pattern 3 fixes.
- **Blocking the GUI thread on a slider drag.** Debounce; never busy-write per pixel.

## Don't Hand-Roll

| Problem                        | Don't Build                       | Use Instead                                                 | Why                                                                         |
| ------------------------------ | --------------------------------- | ----------------------------------------------------------- | --------------------------------------------------------------------------- |
| Profile JSON serialize/parse   | A QJsonDocument writer in the app | `profileToJson`/`profileFromJson` (core)                    | Single source; COD-031; schema-doc-driven; round-trip-tested.               |
| Atomic save                    | tmpfile+rename in the app         | `writeProfileToDisk`                                        | Already fsync+rename, crash-safe.                                           |
| Page stack                     | A new nav stack in the service    | `ActionEngine::pushPage`/`popPage`/`currentPageId`          | Already implemented + tested (test_action_engine.cpp:91-111).               |
| Device brightness/clear wire   | New LIG/CLE builders              | `IDisplayCapable::setBrightness`/`clearKey(0xFF)`           | Byte-tested backend (DISPLAY-03); building new = duplicate + RE drift risk. |
| Image burst (BAT/chunks/ULEND) | A new upload loop                 | The control service's coalesced `assignKeyImage` (Phase 14) | DOCK-02 ULEND commit + last-write-wins coalescing already designed.         |

**Key insight:** Phase 16 adds essentially zero new algorithms. Its risk is *integration shape* — where
the QML→Profile bridge lives, how page-nav semantics are defined, and remembering to make the repaint
path page-aware — not novel logic.

## Runtime State Inventory

> Phase 16 is feature wiring, not a rename/refactor/migration. Most categories are N/A, but persistence
> introduces a new on-disk artifact whose location must be decided, so it is documented here.

| Category            | Items Found                                                                                                                                                                                                                                                                                                            | Action Required                                                                                                                                                                                                                                                                                                                                                                 |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Stored data         | **No default profile path exists today.** `ProfileController::loadProfile`/`saveProfile` take an explicit `path`; `Main.qml:138-145` documents the TODO that Apply/Revert currently toast "not implemented" because there is no default-path resolution. PROFILE-01 ("survive restart") requires a deterministic path. | **Decide + implement a default profile path** (recommended: `QStandardPaths::AppDataLocation/profiles/<profileId>.json`, mirroring `pi_bridge.cpp:100` `AppDataLocation/plugins/...` and `streamdock_catalog_fetcher` `CacheLocation`). Ensure the parent dir is created (`writeProfileToDisk` requires the parent to exist). This is a real PROFILE-01 sub-task, not optional. |
| Live service config | None — no external service stores Phase-16 state.                                                                                                                                                                                                                                                                      | None.                                                                                                                                                                                                                                                                                                                                                                           |
| OS-registered state | None.                                                                                                                                                                                                                                                                                                                  | None.                                                                                                                                                                                                                                                                                                                                                                           |
| Secrets/env vars    | None.                                                                                                                                                                                                                                                                                                                  | None.                                                                                                                                                                                                                                                                                                                                                                           |
| Build artifacts     | The two new app sources must be added to `src/app/CMakeLists.txt` in BOTH the library source block AND the AUTOMOC/header block (mirror `lighting_service`), and any new test registered in `tests/unit/CMakeLists.txt`.                                                                                               | Build-system wiring (standard for this repo).                                                                                                                                                                                                                                                                                                                                   |

**Existing-profile migration:** Not applicable — there are no shipped user profiles to migrate; this is
the first phase that writes them from the editor.

## Common Pitfalls

### Pitfall 1: Repainting the root page on a child-folder switch

**What goes wrong:** Calling Phase-14 `repaintFromProfile()` (iterates `activeProfile().keys`) after a
page change repaints the root keys, so the device shows the wrong page.
**Why it happens:** Phase 14's repaint path predates pages; it has no page id parameter.
**How to avoid:** Add the page-scoped `repaintPage(pageId)` (Pattern 3). Resolve `"root"` → `Profile::keys`, else `Profile::pages[pageId].keys`.
**Warning signs:** A test that switches pages but asserts on root-key images; device showing identical keys on every page.

### Pitfall 2: Bare QML_SINGLETON spawning a duplicate control service

**What goes wrong:** Two instances of the control service per QML import; the slider talks to a dead one (v1.0 light-theme bug class).
**Why it happens:** The bare `QML_SINGLETON` macro picks Constructor mode and bypasses the factory.
**How to avoid:** `create()`/`registerInstance()` + `static_assert(!is_default_constructible)` co-located, like `LightingService` (lighting_service.hpp:46-66) and `ProfileController` (profile_controller.hpp:149-151). Register via `qmlRegisterSingletonInstance`.
**Warning signs:** Slider has no effect; firmware version reads "unknown" in QML but not in C++.

### Pitfall 3: Slider drag floods LIG writes

**What goes wrong:** Every pixel of slider travel emits a HID write; device queue overflows / UI stutters.
**Why it happens:** Naive `onValueChanged → setBrightness`.
**How to avoid:** Debounce (Pattern 2): write on a short timer + a final write on release.
**Warning signs:** Visible stutter while dragging; many LIG packets per drag in the wire spy.

### Pitfall 4: KeyDesigner edits never reach the Profile

**What goes wrong:** Edits look applied (ListModel updates the preview) but vanish on restart.
**Why it happens:** `KeyDesigner.qml:48` `bindings` ListModel is session-only by design (documented at lines 18-23); nothing mutates the C++ `Profile`.
**How to avoid:** On Apply (or per-edit), call a controller method that maps each ListModel row → `Profile.keys[index]` `Binding`/`KeyState` and then `saveProfile`. The ListModel roles (`iconSource`, `label`, `actionKind`, `actionParams`) map to `KeyState.imagePath`, `KeyState.text`, and an `Action` (kind + settings). Confirm the role↔field mapping against `profile.hpp` `KeyState`/`Action` before writing the bridge.
**Warning signs:** Round-trip test passes for a hand-built `Profile` but the UI's edits don't appear after reload.

### Pitfall 5: Path with no parent directory

**What goes wrong:** `writeProfileToDisk` throws because the `AppDataLocation/profiles/` dir doesn't exist on first run.
**Why it happens:** `writeProfileToDisk` docs: "parent directory must exist."
**How to avoid:** `QDir().mkpath()` the profiles dir before the first save.
**Warning signs:** First-ever Apply emits `saveFailed`.

### Pitfall 6: ASCII-only test names / `--tests-regex`

**What goes wrong:** Em-dash/arrow in Catch2 `TEST_CASE` names mangle under Win32 CMD codepage and break `-R`; `--test-regex` is a typo.
**How to avoid:** ASCII-only titles; filter with `--tests-regex`/`-R` (CLAUDE.md, inherited from 14/15).

### Pitfall 7: Phase 14/15 not yet executed

**What goes wrong:** Phase 16 tasks reference `StreamDockControlService`/`StreamDockInputService` symbols that do not exist on disk yet.
**Why it happens:** 14 and 15 are planned (PLANs exist) but unexecuted (no SUMMARY files; `src/app/src/stream_dock_*.cpp` absent).
**How to avoid:** Planner must gate Phase 16 execution on 14 + 15 landing. Read 14-02-SUMMARY and 15-01/02-SUMMARY (once they exist) for the chosen seams (profile accessor lambda vs controller pointer, key-index mapping, whether the control service was QML-exposed, the page-nav intent consumer follow-up).
**Warning signs:** `grep StreamDockControlService src/app/` returns 0 hits at Phase-16 start.

## Code Examples

### Mutate the Profile from an editor edit, then persist

```cpp
// New ProfileController method (app layer; reuses core serializer + io).
// Source pattern: profile_controller.cpp:60 saveProfile + profile.hpp Binding/KeyState
void ProfileController::commitKeyBinding(int keyIndex, QString iconPath, QString label,
                                         int actionKind, QString settings) {
    auto& b = m_profile.keys[static_cast<std::uint16_t>(keyIndex)];
    b.state.imagePath = iconPath.isEmpty() ? std::nullopt
                                           : std::optional{iconPath.toStdString()};
    b.state.text      = label.isEmpty()    ? std::nullopt
                                           : std::optional{label.toStdString()};
    b.onPress = { ajazz::core::Action{ .kind = static_cast<ajazz::core::ActionKind>(actionKind),
                                       .settingsJson = settings.toStdString() } };
    emit profileChanged(); // control service repaints; UI refreshes
}
// Apply -> saveProfile(defaultProfilePath()) goes through writeProfileToDisk (atomic).
```

### Profile round-trip is already exact (the persistence proof shape)

```cpp
// Source: tests/unit/test_profile*.cpp pattern (in-tree) + profile.cpp round-trip contract
ajazz::core::Profile p;
p.id = "..."; p.deviceCodename = "akp05e";
p.keys[1].state.imagePath = "/tmp/icon.png";
p.encoders[0].onCw = { Action{.kind = ActionKind::OpenUrl, .id = "https://x"} };
p.pages["folderA"] = ProfilePage{ .id="folderA", .name="A",
                                  .keys = {{1, Binding{.state={.text="hi"}}}} };
auto json = ajazz::core::profileToJson(p);
auto back = ajazz::core::profileFromJson(json);
// REQUIRE field-by-field equality (keys/encoders/pages/KeyState all serialized).
```

### Page nav consumer wiring (Application)

```cpp
// Source pattern: 15-01-PLAN.md pageNavRequested signal + action_engine.cpp page stack
QObject::connect(m_streamDockInput.get(), &StreamDockInputService::pageNavRequested,
                 m_pageOwner.get(), &PageOwner::navigate); // navigate(+1/-1) -> push/pop or index +/- -> repaintPage
```

## State of the Art

| Old Approach                                        | Current Approach                                             | When Changed    | Impact                                      |
| --------------------------------------------------- | ------------------------------------------------------------ | --------------- | ------------------------------------------- |
| KeyDesigner bindings session-only (ListModel)       | Bridge ListModel → `Profile` + `saveProfile`                 | Phase 16 (this) | Edits survive restart (PROFILE-01).         |
| Apply/Revert toast "not implemented" (Main.qml:140) | Real `saveProfile`/`loadProfile` with default path           | Phase 16        | The editor footer becomes functional.       |
| Repaint = `Profile::keys` only (Phase 14)           | Page-scoped `repaintPage(pageId)`                            | Phase 16        | Folder pages render correctly (PROFILE-02). |
| No brightness/clear UI                              | Slider + Clear-all in Keys tab → control service `LIG`/`CLE` | Phase 16        | DISPLAY-09.                                 |

**Deprecated/outdated:**

- Device-side page opcode (`STP` page-magic): legacy-only; not used for N4/AKP05 (REQUIREMENTS PROFILE-02). Page nav is host-side.

## Assumptions Log

| #   | Claim                                                                                                                                                                                                                                                                                                                 | Section                            | Risk if Wrong                                                                                                                                                                                                                                                                                                                                                                                                     |
| --- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| A1  | Phase 14 ships `StreamDockControlService` with `setBrightness`, clear, a profile accessor, and a coalesced `assignKeyImage`; Phase 15 ships `pageNavRequested(int)`. These are PLANNED (PLANs read) but UNEXECUTED.                                                                                                   | Standard Stack, Patterns           | If 14/15 land with different seams (e.g. service not QML-exposed, different signal name), Phase 16 wiring shifts. Mitigation: read 14/15 SUMMARY files before planning tasks.                                                                                                                                                                                                                                     |
| A2  | Default profile path = `QStandardPaths::AppDataLocation/profiles/<id>.json`. Inferred from `pi_bridge.cpp:100` (`AppDataLocation/plugins/...`) and the `Main.qml:140` TODO suggesting `AppDataLocation/profile.json`. Not a locked decision.                                                                          | Runtime State Inventory, Pitfall 5 | Wrong path = profiles save somewhere unexpected / first-run dir-missing failure. Confirm with user in discuss-phase.                                                                                                                                                                                                                                                                                              |
| A3  | KeyDesigner ListModel roles map: `iconSource`→`KeyState.imagePath`, `label`→`KeyState.text`, `actionKind`/`actionParams`→a single `Action`. The editor currently surfaces only a single action per key; multi-action chains, encoder, and touch editing are NOT in the current QML (KeyDesigner comment lines 44-46). | Pitfall 4, Code Examples           | PROFILE-01 says "encoder and touch bindings persist" — but there is no encoder/touch editor UI today (EncoderPanel.qml exists but is RGB/label-oriented, not action-binding). Persistence machinery handles encoders; the *editor* may not yet author them. Surface to discuss-phase: does PROFILE-01 require new encoder/touch *authoring UI*, or just that the persistence layer round-trips them when present? |
| A4  | Swipe `±1` page semantics (carousel vs folder in/out) are undefined by the requirement.                                                                                                                                                                                                                               | Pattern 4, Open Questions          | Choosing the wrong model gives confusing UX. Needs a user decision.                                                                                                                                                                                                                                                                                                                                               |
| A5  | The control service is the natural home for `repaintPage`; the page-nav owner can be the control service, the input service, or a new coordinator (Claude's discretion).                                                                                                                                              | Architecture, Pattern 4            | Wrong placement complicates the repaint-on-page-change test but is not correctness-fatal.                                                                                                                                                                                                                                                                                                                         |

## Open Questions

1. **Swipe `±1` page-navigation semantics (carousel vs folder).**

   - What we know: `ActionEngine` is a folder *tree* (parent/child via `ProfilePage::children`), not a flat carousel; `pushPage`/`popPage` are in/out, not lateral.
   - What's unclear: Should swipe-left/right cycle *sibling* pages (carousel) or go *into the first child / back to parent* (folder)?
   - Recommendation: Take this to discuss-phase. If carousel, define the sibling ordering (top-level pages? parent's `children`?). Default suggestion: carousel over the ordered set of pages, wrapping disabled, `repaintPage` after each change.

1. **Does PROFILE-01 require new encoder/touch *authoring UI*, or only persistence round-trip?**

   - What we know: The serializer + `Profile` fully model encoder (`EncoderBinding`) and touch (`KeyState`) bindings; KeyDesigner authors only single-action key bindings today; `EncoderPanel.qml` is not an action-binding editor.
   - What's unclear: Whether the phase must add encoder/touch binding-authoring controls or merely guarantee that any encoder/touch bindings present in a `Profile` persist + repaint.
   - Recommendation: Confirm scope with the user. Persistence + repaint of encoder/touch state is in-scope and testable; net-new encoder/touch *editor UI* is a larger lift the requirement wording leaves ambiguous.

1. **Default profile path + multi-profile library.**

   - What we know: No default-path resolution exists; `loadProfileById` is a stub (only the active profile is cached; profile_controller.cpp:89-105).
   - What's unclear: Whether Phase 16 needs a profile *library* (list/switch) or just one round-trippable active profile per device.
   - Recommendation: For PROFILE-01's "survive restart," a single deterministic path per device/profile suffices. Defer a full library + `loadProfileById` index to a later phase unless the user wants it now.

1. **Where does the page-nav owner live, and does the control service hold the `ActionEngine`?**

   - What we know: Phase 15's input service constructs an `ActionEngine` (for input dispatch). The page stack lives in *that* engine. The control service (Phase 14) does the repaint.
   - What's unclear: Whether Phase 16 reuses the input service's `ActionEngine` page stack (and the control service queries `currentPageId()` from it) or introduces a shared page-state owner.
   - Recommendation: Reuse the input service's `ActionEngine` as the single page-state authority (it already mutates the stack via `OpenFolder`/`BackToParent` during dispatch); the page-nav owner reads `currentPageId()` and calls control-service `repaintPage`. Confirm the seam once 15-02-SUMMARY exists.

## Environment Availability

| Dependency                                 | Required By                | Available                 | Version   | Fallback                                                           |
| ------------------------------------------ | -------------------------- | ------------------------- | --------- | ------------------------------------------------------------------ |
| Qt 6 (Quick/Qml/Core)                      | All QML + service wiring   | ✓ (project baseline 6.7+) | per CMake | —                                                                  |
| CMake + Ninja + `linux-release` preset     | Build/test                 | ✓                         | —         | —                                                                  |
| `MockTransport` + `makeAkp05WithTransport` | Hardware-free verification | ✓ (CAPTURE-04 landed)     | in-tree   | —                                                                  |
| **`StreamDockControlService` (Phase 14)**  | DISPLAY-09 + repaint       | ✗ (planned, unexecuted)   | —         | **None — Phase 16 blocks on Phase 14.**                            |
| **`StreamDockInputService` (Phase 15)**    | PROFILE-02 swipe intent    | ✗ (planned, unexecuted)   | —         | **None — Phase 16 blocks on Phase 15.**                            |
| Physical AKP05E                            | Live witness               | ✗ (deferred)              | —         | MockTransport for all Phase-16 verification; hardware is Phase 25. |

**Missing dependencies with no fallback:**

- Phase 14 + Phase 15 must execute before Phase 16. This is a hard ordering dependency, not a soft one.

**Missing dependencies with fallback:**

- Physical device — fully substituted by MockTransport wire-byte assertions for this phase (Phase 25 is the hardware witness).

## Validation Architecture

### Test Framework

| Property           | Value                                                                           |
| ------------------ | ------------------------------------------------------------------------------- |
| Framework          | Catch2 (in-tree, `tests/unit/`), offscreen Qt for QObject services              |
| Config file        | `tests/unit/CMakeLists.txt` (per-test source-link blocks)                       |
| Quick run command  | `ctest --preset linux-release -R <TestName> --output-on-failure`                |
| Full suite command | `ctest --preset linux-release` (≈408 cases per CLAUDE.md; trust the live count) |

### Phase Requirements → Test Map

| Req ID     | Behavior                                                                                                                                          | Test Type                                             | Automated Command                                   | File Exists?                                                  |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------- | --------------------------------------------------- | ------------------------------------------------------------- |
| DISPLAY-09 | `setBrightness(v)` emits a `LIG` write with the dragged value (byte[5]=='L')                                                                      | unit (MockTransport spy)                              | `ctest --preset linux-release -R StreamDockControl` | ❌ Wave 0 (extend Ph14 test)                                  |
| DISPLAY-09 | `clearAll()` emits a `CLE` write (clearKey 0xFF)                                                                                                  | unit                                                  | same                                                | ❌ Wave 0                                                     |
| DISPLAY-09 | Slider drag is debounced (N drags → ≤ small bounded number of LIG writes)                                                                         | unit (QML or service timer)                           | same                                                | ❌ Wave 0                                                     |
| PROFILE-01 | Assign bindings → `saveProfile` → reload into a **fresh `ProfileController`** → field-by-field round-trip equality (keys/encoders/KeyState/pages) | unit (profile round-trip, no Qt needed for core part) | `ctest --preset linux-release -R Profile`           | ⚠️ extend existing profile tests + new controller-commit test |
| PROFILE-01 | After load, control service repaints saved keys (BAT+ULEND per bound key)                                                                         | unit (MockTransport)                                  | `ctest --preset linux-release -R StreamDockControl` | ❌ Wave 0                                                     |
| PROFILE-02 | Push a child page → device repaints from the **child page's** bindings; pop → repaints parent                                                     | unit (MockTransport repaintPage)                      | same                                                | ❌ Wave 0                                                     |
| PROFILE-02 | `pageNavRequested(±1)` triggers a page change + a repaint                                                                                         | unit (QSignalSpy + spy repaint)                       | `ctest --preset linux-release -R StreamDock`        | ❌ Wave 0                                                     |

### Sampling Rate

- **Per task commit:** `ctest --preset linux-release -R StreamDock` (+ `-R Profile` for persistence tasks)
- **Per wave merge:** full `ctest --preset linux-release`
- **Phase gate:** Full suite green before `/gsd:verify-work`

### Wave 0 Gaps

- [ ] `tests/unit/test_stream_dock_control_service.cpp` — EXTEND (Ph14) with brightness-value LIG, clearAll CLE, debounce-bound, and `repaintPage` page-scoped repaint cases.
- [ ] `tests/unit/test_profile_controller_commit.cpp` (NEW) — KeyDesigner-edit → `Profile` mutation → `saveProfile` → fresh-controller `loadProfile` round-trip equality.
- [ ] Page-nav consumer test (in the control/input service test or a new `test_stream_dock_pages.cpp`) — `pageNavRequested(±1)` → page change → repaint.
- [ ] No new framework install — Catch2 + offscreen Qt already in place.

## Security Domain

> `security_enforcement` not set to `false` → included.

### Applicable ASVS Categories

| ASVS Category         | Applies | Standard Control                                                                                                                                                                                                                               |
| --------------------- | ------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| V2 Authentication     | no      | No auth surface in this phase.                                                                                                                                                                                                                 |
| V3 Session Management | no      | —                                                                                                                                                                                                                                              |
| V4 Access Control     | no      | —                                                                                                                                                                                                                                              |
| V5 Input Validation   | yes     | Brightness clamped 0..100 (backend); key index range-checked 1..KeyCount (backend); profile `imagePath` loaded via Qt safe image decoders only — no raw arbitrary-path read; `delayMs` reject-leading-sign already enforced (profile.cpp:460). |
| V6 Cryptography       | no      | No crypto introduced. Profile JSON is plaintext local config (same trust model as a keyboard macro).                                                                                                                                           |

### Known Threat Patterns for {Qt6/QML + local profile JSON}

| Pattern                                                             | STRIDE                             | Standard Mitigation                                                                                                                                                                                                                                                                                                  |
| ------------------------------------------------------------------- | ---------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Untrusted `imagePath` in a profile → arbitrary file read on repaint | Information Disclosure / Tampering | Load via Qt's safe image decoders only; do not raw-read arbitrary paths. Phase-14 threat T-14b-03 was deferred "confirm in Phase 16" — Phase 16 should confirm the loader stays inside Qt's image pipeline and treat the profile as user-authored/local (deep untrusted-source handling is the plugin phases 19/22). |
| Profile-supplied action chain (`command`/`url`) side effects        | Elevation/Tampering                | Same as Phase 15: `runCommand` via `QProcess::startDetached(program, args)` with explicit argv (never `system()`); `openUrl` via `QDesktopServices::openUrl`. Phase 16 authors these into the profile but the executors are Phase 15/21 — Phase 16 must not introduce a shell-string path.                           |
| `writeProfileToDisk` symlink/path traversal on the save path        | Tampering                          | The save path is app-derived (`AppDataLocation/profiles/<sanitized id>.json`), not user-supplied raw; sanitize the profile id used in the filename (UUID-shaped). `writeProfileToDisk` is atomic (fsync+rename).                                                                                                     |
| COD-031 boundary                                                    | Tampering (build-time invariant)   | No `nlohmann::json` in core; the service is app-layer; the serializer is the hand-rolled core writer. `grep -rn nlohmann src/core/include/` must stay 0.                                                                                                                                                             |

## Project Constraints (from CLAUDE.md)

- **No `nlohmann::json` in `ajazz_core` or installed public headers (COD-031).** Profile serializer is the hand-rolled core writer; Phase 16 work is app-layer. Verify `grep -rn nlohmann src/core/include/` returns 0.
- **Schema doc is the source of truth for JSON wire keys.** `Profile::deviceCodename` ⇄ `"device"`; do not align the writer to a C++ field name. The writer already matches the schema (verified: profile.cpp:229-230 emits `"device"`; reader at :792-794).
- **`qmlRegisterSingletonInstance`, NOT bare `QML_SINGLETON`.** Co-locate `static_assert(!is_default_constructible)`. Applies to the QML-exposed control service.
- **ASCII-only test names; filter with `--tests-regex`/`-R`.**
- **No system-level mutations from project tooling.** Profile writes go to `QStandardPaths::AppDataLocation` (the app's own data dir) — not `/etc`, not user dotfiles.
- **Cross-platform build strictness** (GCC + Clang + Apple Clang `-Werror` + MSVC `/W4 /WX`): prefer `_s` variants on Windows; watch `-Wreorder` on new Application members (mirror 14-02-PLAN.md member-order guidance); guard moved-from `std::wstring`.
- **Direct-to-`main`, atomic commits, Conventional Commits, never skip pre-commit.** (Note: current work is on `feat/streamdock`; user instruction is to build/test on the active feature branch.)
- **Cap concurrent execute agents at 2.**
- **RE is source of truth for wire formats** — but Phase 16 touches no wire format (reuses byte-tested `LIG`/`CLE`/`BAT`/`ULEND`); no RE re-read required for this phase's scope.

## Sources

### Primary (HIGH confidence)

- `src/core/src/profile.cpp` — `profileToJson`/`profileFromJson`; confirmed keys/encoders/mouseButtons/pages/KeyState all serialize + round-trip; `"device"` wire key (lines 220-320, 778-853).
- `src/core/include/ajazz/core/profile.hpp` — `Profile`/`ProfilePage`/`Binding`/`EncoderBinding`/`KeyState`/`Action`/`ActionKind`.
- `src/core/src/action_engine.cpp` + `action_engine.hpp` — `pushPage`/`popPage`/`currentPageId`/`OpenFolder`/`BackToParent` semantics (lines 32-129); confirmed folder-tree (not carousel).
- `src/core/include/ajazz/core/profile_io.hpp` — atomic write contract; "parent directory must exist."
- `src/app/src/profile_controller.{hpp,cpp}` — `loadProfile`/`saveProfile`/`loadProfileById` (stub)/`knownProfileIds`; QML_SINGLETON; no default-path resolution.
- `src/app/qml/KeyDesigner.qml` — session-only `bindings` ListModel (lines 18-23, 48-91); no Profile bridge.
- `src/app/qml/ProfileEditor.qml` — Keys tab structure + Apply/Revert footer (the slider/clear-all/commit insertion points).
- `src/app/qml/Main.qml:125-160` — Apply/Revert toast "not implemented"; the default-path TODO.
- `src/app/src/lighting_service.hpp:46-66` — QML_SINGLETON + `Q_INVOKABLE` + DeviceLookup pattern to mirror.
- `docs/protocols/PROFILE_SCHEMA.md` — wire-key source of truth; confirms `pages`/`encoders`/`KeyState` schema.
- `tests/unit/test_action_engine.cpp:91-111` — page push/pop test precedent.
- `.planning/phases/14-stream-dock-control-service/14-02-PLAN.md` — control service shape, repaint path (root-only), key-index 1-based mapping note, profile accessor seam.
- `.planning/phases/15-stream-dock-input-routing/15-01-PLAN.md` + `15-RESEARCH.md` — `pageNavRequested(±1)` intent; "page model is Phase 16."

### Secondary (MEDIUM confidence)

- `src/app/src/pi_bridge.cpp:100` — `AppDataLocation/plugins/...` precedent informing the recommended profile path (A2).

### Tertiary (LOW confidence)

- None. All claims sourced in-tree.

## Metadata

**Confidence breakdown:**

- Standard stack / reuse surfaces: HIGH — every component read at file:line; serializer round-trip and page-stack semantics verified directly.
- Architecture (page-scoped repaint extension, QML-singleton control service): HIGH for the mechanism; MEDIUM for exact seam placement (depends on 14/15 SUMMARY files not yet written).
- Pitfalls: HIGH — derived from in-tree code + inherited 14/15/CLAUDE.md pitfalls.
- Scope ambiguities (encoder/touch authoring UI; swipe semantics; default path): flagged as Open Questions for discuss-phase, MEDIUM confidence.

**Research date:** 2026-05-23
**Valid until:** 2026-06-22 (stable in-tree domain) — but **re-read 14-02-SUMMARY and 15-01/02-SUMMARY before planning**, since they finalize the seams Phase 16 builds on.
