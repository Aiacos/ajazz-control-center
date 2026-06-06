---
status: human_needed
phase: 21-builtin-in-process-actions
score: 8/8
verified: '2026-05-24T19:00:00Z'
reverification: false
requirement_ids: [PLUGIN-12]
human_verification:
  - test: 'Live key synthesis: bind system.hotkey/system.multimedia/system.volume to a key and confirm the keystroke reaches the OS (X11/Wayland/Windows/macOS). On Wayland the uinput path may be restricted — confirm the honest stub/no-op fallback rather than a silent failure.'
    expected: On a platform that permits synthesis, the bound hotkey/media/volume key actuates the OS; where the platform restricts it, the action degrades gracefully (logged, no crash).
    why_human: Synthesis is gated behind AJAZZ_FEATURE_INPUT_SYNTH and exercised hardware-free via a FakeSynth; real OS delivery (esp. Wayland uinput permissions) needs a live desktop session — Phase 25 integration.
  - test: 'Live OBS action: with OBS + obs-websocket v5 (auth ON) running, bind obsstudio scene-switch/record/stream-toggle and confirm it drives OBS; with a wrong password, confirm authFailed and no plaintext/unauth connection + no per-press retry storm.'
    expected: Correct password → OBS responds; wrong/missing password → authFailed, connection refused, retries suppressed until settings change.
    why_human: Proven hardware-free with a mock OBS loopback (auth vector + refuse-without-auth + non-object-auth bypass regression, 597/597); a live OBS instance confirms the end-to-end.
---

# Phase 21: Built-in In-Process Actions — Verification Report

**Phase Goal:** The ~50 in-process built-in UUIDs work without an external plugin.

**Status:** human_needed (8/8 automated must-haves verified; 2 live-environment integration items deferred to Phase 25)
**Verified by:** orchestrator inline (verifier subagent conserved — weekly usage limit). Evidence by code inspection + the full app+qml+tests build/test gate.

## Must-Haves Verified (8/8)

1. **PLUGIN-12 (built-in actions in-process):** `BuiltinActionRegistry` (pure core) + `BuiltinActionsService` register and dispatch the built-in UUID families — page nav (`page.previous/next/goto/indicator/change`), profile nav (`profile.openchild/backtoparent/rotate`), `device.brightness`, `system.hotkey`, `system.multimedia`, `system.volume`, `plain.text`, `browser`/`openUrl`, `multiactions` (+ carousel), `obsstudio`. REQUIREMENTS.md PLUGIN-12 = Complete. Wired into Application; short-circuit before plugin fallback.
1. **Registry double-gate:** prefix check + map lookup — unknown/misnamed UUIDs forward to the plugin fallback (no silent swallow).
1. **Anti-feature — input synth opt-in:** `captureHotkeys` is OFF by default (`m_captureEnabled{false}`); no always-on global hook. Backends gated by `AJAZZ_FEATURE_INPUT_SYNTH` (default OFF).
1. **Anti-feature — OBS auth default-on:** ObsClient performs obs-websocket v5 SHA256 auth; a present-but-malformed `authentication` field (null/true/string) now emits `authFailed` and refuses to send Identify (CR-01 fix + 3 regression tests). No plaintext/unauth path.
1. **OBS retry guard (WR-01):** `m_obsAuthFailed` suppresses per-press reconnect storms after an auth failure; cleared on connect.
1. **URL scheme allowlist:** `browser`/`openUrl` validates http/https only (double-validated in service + Application executor) — Phase-20 WR-01 lesson applied.
1. **LunBo cursor reset (WR-02):** `ProfileController::profileChanged` wired to `resetLunBoCursors`.
1. **COD-031 + reuse:** `builtin_action_registry.hpp` + `input_synthesizer.hpp` are Qt/nlohmann-free (verified — only boundary-doc comments mention nlohmann); single `ActionEngine` instance; reuses control service / ObsClient / IInputSynthesizer. OBS password never logged. Build app+qml+unit clean under -Werror; ctest 597/597.

## Human Verification Required (2 — Phase 25)

See `human_verification` frontmatter: live OS key synthesis + live OBS action.
