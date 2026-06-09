# Phase 32 — Code Review Disposition

**Date:** 2026-06-08
**Reviewer report:** `32-REVIEW.md` (2 BLOCKER, 4 WARNING, 3 INFO)
**Disposition by:** orchestrator (autonomous run), evidence-based, after live debug-channel verification.

## BLOCKERS — both REFUTED (false positives)

### CR-01 / CR-02 — "Toggle index-base mismatch (renderToggleState `index + 1` vs 1-based ev.index)"

**Verdict: NOT A BUG.** The reviewer's premise — that `ev.index` is 1-based — is incorrect.

Evidence (verified against the live tree + a running app):

1. **`ev.index` is 0-based.** It is used directly in `prof.keys.find(ev.index)` (stream_dock_input_service.cpp:250) and `prof.encoders.find(ev.index)` (:282). `Profile::keys` is documented 0-based (plugin_device_bridge.cpp:1124-1145: "Profile::keys use 0-based uint16_t index; device uses 1-based"). A 1-based `ev.index` would miss every binding.
1. **`renderToggleState`'s `index + 1` matches the WORKING reference path.** `populateContextsForActivePage` (the path that already delivers live willAppear/setTitle) computes `keyIdx0 + 1` then `coordsForKeyIndex(keyIdx1)` (1145). `coordsForKeyIndex` is declared `coordsForKeyIndex(uint8_t oneBasedKeyIndex, ...)` (:198). `renderToggleState` does the identical `coordsForKeyIndex(index + 1)` (:1080). Same convention.
1. **Live confirmation.** With the AKP05E connected, `scripts/ajazz-debug input.key index=8` resolved to the CPU-bound key whose registered context is `akp05e#root#Keypad#1#3`. `coordsForKeyIndex(8 + 1 = 9, cols=5) → row1,col3` — exactly matches. The System Monitor plugin's `setTitle` flows on that same context id, proving the 0-based→1-based+coords pipeline is correct end to end.
1. **Touch-zone `"Encoder"` controller is the existing convention** (zones are stored in `prof.encoders` and register under the Encoder controller per the in-code comment at :343 and prior phases), not a Phase-32 mix-up.

**Legitimate residual the reviewer surfaced:** `test_toggle_dispatch.cpp` stubs the render hook, so the *real* `renderToggleState` coordinate conversion has no automated regression guard. Tracked as a follow-up (add a `renderToggleState` coordinate test in `test_plugin_device_bridge.cpp`, and the live 3-state Toggle walk below).

## WARNINGS — triaged

- **Nested Multi Action "double-dispatch" in `flatten`:** Re-examined. The flatten emits a *container* child (one that itself has children) as a Plugin Action step AND recurses its leaves. The container step carries the built-in multiaction id, which `ActionKind::Plugin` forwards to the plugin host — the host does not own a built-in id → **no-op**. Leaves still run exactly once. Harmless spurious step on an uncommon (nested-Multi) path. **Deferred** (cheap future cleanup: skip emitting a step for container children, recurse only).
- **DeviceCanvas `ListModel.get()` live-render mirror:** Live screenshot showed the CPU key face rendering/updating correctly on the akp05e — the mirror works in practice. No action.
- **LunBo double-decode:** pre-existing, not Phase-32 code. Out of scope.
- **Silent willAppear no-op when `m_activeDeviceId` empty:** this is the intentional T-28-09 startup guard (do not register contexts before a real device connect). By design.

## Remaining live verification (human-verify follow-ups)

The deterministic layer is fully green (744/744 + 17/17 qml) and the dispatch pipeline + EDIT-01 are live-confirmed. Not yet walked live (require authoring a Multi/Toggle ActionInstance binding, for which a debug-channel instance-commit RPC is not yet wired):

- **Multi Action live walk (BIND-04):** bind a 2-child Multi to a key → `input.key` → `plugin.protocolLog` shows both children in order with the inter-step delay.
- **Toggle live walk (BIND-05/07):** bind a 3-state Toggle → `input.key` ×3 → `screenshot` shows the key face cycling 0→1→2→0; relaunch confirms persisted `currentState`.

These are recorded in each plan SUMMARY's "Pending live verification" section.
