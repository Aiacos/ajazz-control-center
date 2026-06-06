# Phase 16: Device Controls + Binding Persistence + Pages - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + existing-code survey

<domain>
## Phase Boundary

Phase 16 gives the user live device controls and makes their work stick: a brightness slider
and a "clear all" control drive the panel live, key/encoder/touch bindings persist across
restart and repaint on load, and multi-page/folder profiles navigate host-side. It closes the
Phase-10 gap where `KeyDesigner` bindings were session-only.

**Delivers (DISPLAY-09, PROFILE-01/02):**

- A **brightness slider** and a **"clear all keys"** control in the device panel drive the
  device live (via the Phase-14 control service: `LIG` / `CLE`) (DISPLAY-09).
- Key, **encoder**, and touch bindings (image, label, action chain) persist to the profile and
  survive an app restart, repainting the device on load (PROFILE-01).
- **Multi-page / folder profiles** drive host-side page navigation — page prev/next/goto and the
  Phase-15 touch-swipe intent switch pages and repaint, via `ActionEngine` `OpenFolder`/
  `BackToParent` (no device page opcode is invented; `STP` page-magic is legacy-only) (PROFILE-02).

**Out of scope:** the plugin SDK (Phases 17-22); auxiliary surface rendering (Phase 23);
hardware verification (Phase 25). Live-device behavior is verified via MockTransport here; the
physical witness is Phase 25.
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Controls (DISPLAY-09)

- The brightness slider and clear-all route to the **Phase-14 `StreamDockControlService`**
  (`setBrightness` → `LIG`; clear-all → `CLE`). Do **NOT** route through `LightingService`
  (that is keyboard firmware-RGB, a different capability/device class).

### Persistence (PROFILE-01) — machinery already exists; this is wiring

- Reuse `ProfileController::loadProfile`/`saveProfile` (profile_controller.cpp:47/60) over core
  `profile_io` (`writeProfileToDisk` — atomic, fsync-safe) + `profileToJson`/`profileFromJson`.
  Do NOT add a second serialization path.
- The gap is the bridge: editor assignments (KeyDesigner) must **mutate the `Profile`** (keys /
  encoders / touch `Binding`/`EncoderBinding`/`KeyState`) and trigger a save; on load, the
  device **repaints from the profile** via the Phase-14 control-service paint path.
- **Schema doc is the source of truth** for JSON wire keys (`Profile::deviceCodename` ⇄
  `"device"`) — never align a writer to a C++ field name without checking the schema (CLAUDE.md).

### Pages (PROFILE-02) — machinery already exists; this is wiring

- Reuse `ActionEngine::pushPage`/`popPage` + `OpenFolder`/`BackToParent` + `Profile::pages`
  (`ProfilePage`). A page switch **repaints** from the new page's bindings via the control
  service. Consume the **`pageNavRequested(±1)`** intent emitted by the Phase-15 input service
  (swipe / page-nav action) — Phase 16 owns turning that intent into an actual page change +
  repaint.

### Claude's Discretion (planner/executor; surface trade-offs)

- Where the brightness slider / clear-all live in QML (the device panel) and how they bind to
  the control service (Q_INVOKABLE vs property). If a service is QML-exposed as a singleton, use
  `qmlRegisterSingletonInstance` + the `static_assert(!is_default_constructible)` lock (CLAUDE.md
  QML_SINGLETON gotcha), as `LightingService` does.
- How KeyDesigner's current session-only binding model bridges to `Profile` (extend the existing
  ListModel-backed editor vs a controller method that writes the Profile then saves).
- Debounce/throttle of the live brightness slider so dragging doesn't flood `LIG` writes.
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Persistence / profile (reuse)

- `src/app/src/profile_controller.{hpp,cpp}` — `loadProfile`/`saveProfile`/`loadProfileById`, `activeProfile()` (added in Phase 14).
- `src/core/src/profile_io.cpp` + `ajazz/core/profile_io.hpp` — atomic fsync-safe `writeProfileToDisk`/`readProfileFromDisk`.
- `src/core/include/ajazz/core/profile.hpp` — `Profile`/`ProfilePage`/`Binding`/`EncoderBinding`/`KeyState`; `profileToJson`/`profileFromJson`; `deviceCodename` ⇄ `"device"`.
- `docs/protocols/PROFILE_SCHEMA.md` — the JSON wire-key source of truth.

### Controls / pages (reuse)

- The Phase-14 `src/app/src/stream_dock_control_service.{hpp,cpp}` — `setBrightness`, clear-all, the repaint-from-profile paint path.
- `src/core/include/ajazz/core/action_engine.hpp` — `pushPage`/`popPage` (148/151), `OpenFolder`/`BackToParent`, `NavigationContext`.
- The Phase-15 `src/app/src/stream_dock_input_service.{hpp,cpp}` — `pageNavRequested(±1)` intent to consume.

### UI (reuse / extend)

- `src/app/qml/KeyDesigner.qml` + `src/app/qml/KeyCell.qml` — the key editor (bindings session-only today — bridge to `Profile`).
- `src/app/src/lighting_service.cpp:47/54` — the `QML_SINGLETON` + `registerInstance` + `static_assert` pattern to mirror if a control service is QML-exposed.

### Decisions

- v1.1 ARCH-03 (single held-open handle — controls drive the same handle).
- CLAUDE.md: schema-doc-is-truth for wire keys; `qmlRegisterSingletonInstance` (not bare `QML_SINGLETON`).
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free: brightness slider → assert a `LIG` write with the dragged value
  (via MockTransport / control-service spy); clear-all → assert a `CLE` write; persistence →
  assign bindings, `saveProfile`, reload into a fresh `ProfileController`, assert round-trip
  equality AND that the control service repaints the saved keys (`BAT`+`ULEND` per bound key);
  pages → push a child page, assert the device repaints from the child page's bindings, pop and
  assert it repaints the parent.
- ASCII-only test names; `ctest --preset linux-release`.

</specifics>

<deferred>
## Deferred Ideas

- Plugin SDK (server, manifest, spawn, bridge, Property Inspector, store) → Phases 17-22.
- Auxiliary surfaces (encoder overlays / main strip / touch strip) → Phase 23 (HW-gated).
- Physical-device witness of live brightness/clear/persistence/pages → Phase 25.

</deferred>

______________________________________________________________________

*Phase: 16-device-controls-binding-persistence-pages*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; design context from replan research)*
