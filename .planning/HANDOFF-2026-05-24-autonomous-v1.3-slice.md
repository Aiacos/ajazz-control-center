# Handoff - v1.3 hardware-free slice (autonomous run, 2026-05-24)

**Run:** `/gsd-autonomous` from Phase 14, stopping before the hardware-gated phases (per operator decision at start).
**Result:** Phases **14, 15, 16, 17, 18, 19, 20, 21, 22, 24** executed -> verified -> complete. 195 commits on `feat/streamdock`, **unpushed** (operator pushes per workflow). Full suite **632/632** green; app + qml + unit targets build clean under `-Werror`.

## What landed (10 phases)

| Phase | What                                                            | Req IDs                                  | Verify              |
| ----- | --------------------------------------------------------------- | ---------------------------------------- | ------------------- |
| 14    | StreamDockControlService - first app->device paint path         | DISPLAY-06/07/08, DOCK-01/02, DEVICES-11 | 9/9 auto            |
| 15    | StreamDockInputService - first ActionEngine instantiation       | INPUT-03/04/05                           | 12/12 auto          |
| 16    | Live device controls + binding persistence + pages              | DISPLAY-09, PROFILE-01/02                | 8/8 auto            |
| 17    | Plugin protocol completion (41-action surface + passHello auth) | PLUGIN-01..05                            | 14/14 auto (passed) |
| 18    | Plugin manifest + discovery + lifecycle + spawn                 | PLUGIN-06/07/08/11                       | 5/5 auto            |
| 19    | Device\<->Plugin bridge (setImage e2e) - **convergence**        | PLUGIN-10                                | 10/10 auto          |
| 20    | Property Inspector + settings (WebEngine PI)                    | PLUGIN-09/13                             | 9/9 auto            |
| 21    | ~50 built-in in-process actions                                 | PLUGIN-12                                | 8/8 auto            |
| 22    | Plugin store / local install (signature gate, no phone-home)    | PLUGIN-14                                | 8/8 auto            |
| 24    | Family coverage AKP03/153/815 (descriptor-driven services)      | DEVICES-10                               | 9/9 auto            |

Every phase ran the full gate: execute -> post-merge build+test -> code review -> fix -> verify. **Code review caught and fixed real issues every phase**, notably:

- **17 (deep security review):** 3 auth-bypass BLOCKERs - routed actions bypassing the auth gate, `pluginRegistered` firing pre-auth, no UUID-collision guard. All fixed + regression-tested.
- **18:** an **app-only build break** (the unit-test ctest was green while `ajazz-control-center` failed under `-DAJAZZ_HAVE_WEBENGINE`) + 3 plugin-sandbox criticals (host-env leakage into children, double-crash-fire, codePathWin/Mac traversal bypass).
- **19:** image-OOM DoS (unbounded decode) + cross-device coordKey collision.
- **20:** qml-test MOC `Q_DECLARE_OPAQUE_POINTER` break + PI object-payload drop + nav/size hardening.
- **21:** OBS auth-bypass via non-object `authentication` field.
- **22:** verify-gate bypass on extraction failure + cross-fs TOCTOU + phone-home-on-Refresh + an MSVC C4996 build break.
- **24:** akp03 sending image bursts to 3 non-LCD side buttons (guarded on total count, not `DisplayKeyCount`); AKP05 geometry hardcodes generalized to descriptor-driven.

Anti-features held throughout: LocalHost-only plugin server, plugin children get an allowlist env (no host-state leak), OBS auth default-on, input synthesis opt-in (no always-on hook), plugin signature gate + no phone-home, http/https-only URL actions, COD-031 boundary intact (core headers Qt/nlohmann-free).

## (!) Remaining work - NOT executed (hardware/capture-gated)

The milestone lifecycle (audit -> complete -> cleanup) was **NOT run** because this is a partial run. Do not archive v1.3 until the below close.

### Capture-gated (need Phase 9.x physical captures first - see STATE.md "Pending Todos")

- **Phase 10** AKP05E (0x3004) promotion - **Phase 11** 2.4G 8K mouse - **Phase 12** AK980 PRO - **Phase 13** catalogue + v1.1 UI back-fill.
  These were skipped at run start: they depend on Wireshark/usbmon captures for the 4 connected devices (a system-install + physical-capture task only the operator can do). Their plans already exist.

### Hardware-gated (need the physical AKP05E + a real `.sdPlugin`)

- **Phase 23** Auxiliary Display Surfaces (encoder overlays, main LCD strip, touch strip, DRA partial-zone upload - framing must be confirmed on hardware). Plans exist (2).
- **Phase 25** Hardware Verification + Real Plugin - the milestone-closing UAT: power-cycle smoke, the live plugin\<->device round-trip, a real third-party `.sdPlugin` running, and reconciliation of the RE-provisional values. Plans exist (2). **Gates milestone close.**

### Accumulated HUMAN-UAT (physical confirmations deferred to Phase 25)

Each completed phase persisted a `*-HUMAN-UAT.md` with `deferred_to_phase: 25`. Roll-up of what a human must confirm on hardware:

1. (14) panel lights on select; key image appears ~1s no flush; profile repaints.
1. (15) key/encoder/touch fire bound actions; swipe -> page-nav intent.
1. (16) brightness slider dims panel; clear-all blanks; bindings survive real restart; swipe navigates pages.
1. (19) live plugin\<->device round-trip (the convergence demo).
1. (20) a real plugin's Property Inspector renders (sdpi.css) + live settings round-trip.
1. (21) live OS key synthesis (incl. Wayland graceful-degrade) + live OBS action with auth.
1. (22) live local install of a real signed `.sdPlugin` + Refused-package rejection + online-off silence.
1. (24) live AKP03/153/815 assign-image-and-press; RE-provisional values (AKP815 strip 800x480; akp153 release-byte encoding) confirmed on hardware.

## Recommended operator follow-ups (not blockers)

- **`/gsd:secure-phase 17`** - Phase 17 has a real threat surface (T-17-PREAUTH/REPLAY/CRYPTO/BRUTE/UAF). It was covered by a deep adversarial code review + regression tests, but the formal `SECURITY.md` artifact was not generated. Worth running for the auth phase.
- **Qt private-header watch-item:** Phase 18/20 use `QtWebEngineQuick/private/qquickwebenginescriptcollection_p.h` (+ `Qt6::WebEngineQuickPrivate`) because there's no public API for `QQuickWebEngineProfile::userScripts()`'s collection type. A `static_assert(QT_VERSION ...)` guards it, but a Qt bump may need attention.
- **Advisory UI (Phase 16, 21/24):** UI-review ran for 16 (21/24, no blockers - suggested a brightness value readout, a "Restore defaults" confirm dialog, and KeyDesigner grid reseed-on-restart). UI-review was skipped for 20/22 to conserve usage budget - re-run `/gsd:ui-review 20` / `22` if desired.
- **Verifier ran inline for phases 19-24:** the per-phase `gsd-verifier` subagent hit a weekly usage limit mid-run, so verification for 19/20/21/22/24 was performed inline by the orchestrator (evidence in each `*-VERIFICATION.md`). Re-running `/gsd:verify-work` on those is optional belt-and-suspenders.

## Next steps

1. `git push` the 195 commits (fetch+rebase first per workflow).
1. Do the Phase 9.x captures (STATE.md Pending Todos) to unblock 10-13, OR proceed straight to the hardware checkpoints with the AKP05E connected.
1. Connect the AKP05E and run **`/gsd-autonomous --from 23`** (or `/gsd:execute-phase 23` then 25) to close the hardware-gated phases.
1. After 23 + 25 pass, run the milestone lifecycle: `/gsd:audit-milestone` -> `/gsd:complete-milestone v1.3` -> `/gsd:cleanup`.
