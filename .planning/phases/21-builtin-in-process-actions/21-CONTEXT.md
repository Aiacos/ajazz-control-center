# Phase 21: Built-in In-Process Actions - Context

**Gathered:** 2026-05-23
**Status:** Ready for planning
**Source:** v1.3 replan locked decisions + akp_plugin_sdk.md §1 + existing-code survey

<domain>
## Phase Boundary

Phase 21 implements the **built-in (in-process) actions** the original app handles without a
separate plugin process (akp_plugin_sdk.md §1 lists ~24 `com.hotspot.streamdock.*` UUIDs). These
are dispatched natively when a profile binding's `Action.id` matches a built-in UUID — no plugin
spawn. It reuses the core `ActionEngine` kinds + the Phase-14/16 control service + the Phase-16
page model.

**Delivers (PLUGIN-12) — the roadmap core set:**

- **Page nav:** `page.previous`/`page.next`/`page.goto`/`page.indicator`/`page.change` (Knob).
- **Profile nav:** `profile.openchild`/`profile.backtoparent`/`profile.rotate`.
- **`device.brightness`** → the Phase-14/16 control service `setBrightness` (`LIG`).
- **`system.hotkey`** → OS key-combo synthesis (**opt-in** global hook — never always-on).
- **`system.multimedia`** / **`system.volume`** → OS media/volume key synthesis.
- **`plain.text`** → type a literal string (key synthesis).
- **`browser`** → open a URL (reuse `ActionEngine` `OpenUrl`).
- **`multiactions`** (+ `multiactions.LunBo` carousel) → run an ordered `ActionChain` / cycle.
- **`obsstudio`** → obs-websocket client action (auth **default-on**).

**Deferred (NOT in the success criteria; v1.3+ backlog):** `vmix`, `youtube`, `network`,
`mouse.event`, `device.k1proLED+-`, `pageindicatororgoto`, `pagebackorforword`.

**Out of scope:** plugin store (Phase 22); auxiliary surfaces (Phase 23); hardware verify (Phase 25).
</domain>

<decisions>
## Implementation Decisions (LOCKED)

### Dispatch

- A **`BuiltinActionRegistry`** maps each `com.hotspot.streamdock.*` UUID → a native handler,
  invoked when `ActionEngine`'s `plugin` executor (the stub from Phase 15) sees a built-in UUID —
  **in-process, no plugin spawn**. Reuse `ActionEngine` kinds where they already map: `browser` →
  `OpenUrl`; page/profile nav → `OpenFolder`/`BackToParent` + the Phase-16 page model + the
  Phase-15 `pageNavRequested` path; `device.brightness` → the control service; `multiactions` →
  the existing `ActionChain` walk.

### OS input synthesis (system.hotkey / plain.text / multimedia / volume)

- Behind an **`IInputSynthesizer` interface** with per-OS backends (Linux `uinput`/`XTest`,
  Windows `SendInput`, macOS `CGEvent`). This makes the Phase-15 `keyPress` executor stub real.
- **`system.hotkey` global hook is OPT-IN** — gated by a settings toggle, never always-on
  (anti-feature: always-on `WH_KEYBOARD_LL`/global hook). Output synthesis (typing/hotkeys) is the
  primary path; *capturing* a global hotkey to trigger an action is the opt-in surface.

### OBS (obsstudio)

- A `QWebSocket` obs-websocket client; **auth default-on** — require the user's OBS password,
  never connect to an unauthenticated OBS by default (anti-feature: plaintext OBS).

### Honesty / anti-features (NOT replicated)

- No always-on global keyboard hook; no auto-connect to unauthenticated OBS.

### Claude's Discretion (planner/executor)

- Registry shape (UUID → `std::function` / handler objects) and where it lives (app-layer).
- Whether OS synthesis lands all four backends now or Linux-first with the interface + the other
  backends stubbed (cross-platform parity is a CI concern — the interface must compile on all 3).
- OBS action subset (scene switch / source toggle / record-stream) for the first cut.
  </decisions>

\<canonical_refs>

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

- `docs/protocols/streamdeck/akp_plugin_sdk.md` §1 (the built-in UUID list).
- `src/core/include/ajazz/core/action_engine.hpp` — `ActionEngine`, `ActionExecutors` (the `plugin`/`keyPress`/`openUrl`/`runCommand` callbacks), `OpenFolder`/`BackToParent`, `pushPage`/`popPage`.
- `.planning/phases/15-stream-dock-input-routing/15-01-PLAN.md` — the `keyPress`/`plugin` executor STUBS this phase makes real; `pageNavRequested`.
- `.planning/phases/16-...pages/16-03-PLAN.md` — the page model (`repaintPage`, carousel) page nav drives.
- The Phase-14/16 control service — `setBrightness` for `device.brightness`.
- CLAUDE.md — cross-platform build strictness (MSVC `_s` variants, `-Werror`, ASCII test names); COD-031; never skip pre-commit. Anti-feature inventory (opt-in hook; OBS auth-on) in `.planning/REQUIREMENTS.md` Out-of-Scope.
  \</canonical_refs>

<specifics>
## Specific Ideas

- Verification is hardware-free: unit-test the `BuiltinActionRegistry` dispatch (each UUID →
  the right handler called with the binding's settings); OS synthesis behind `IInputSynthesizer`
  with an **injected fake backend** asserting the synthesized key/combo/text without touching the
  real OS; `device.brightness` → control-service spy (`LIG`); page/profile nav → page-model
  assertions; `multiactions` → ordered chain ran; OBS client against a **mock WebSocket server**
  (auth-required handshake asserted; refuses unauthenticated). The opt-in hook gate: assert the
  global-hook capture is OFF unless the toggle is set. ASCII test names; `ctest --preset linux-release`.

</specifics>

<deferred>
## Deferred Ideas

- `vmix`/`youtube`/`network`/`mouse.event`/`k1proLED`/`pageindicatororgoto`/`pagebackorforword` → v1.3+ backlog (not in the success criteria).
- Plugin store → Phase 22. Auxiliary surfaces → Phase 23. Live witness → Phase 25.

</deferred>

______________________________________________________________________

*Phase: 21-builtin-in-process-actions*
*Context gathered: 2026-05-23 (v1.3 replan locked decisions; akp_plugin_sdk.md §1 is the spec)*
