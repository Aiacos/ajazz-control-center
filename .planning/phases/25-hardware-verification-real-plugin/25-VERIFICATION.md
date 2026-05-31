---
status: human_needed
phase: 25-hardware-verification-real-plugin
score: 7/7 on-unit output criteria PASS (input + plugin items hardware/scope-gated)
verified: '2026-05-31T13:00:00Z'
reverification: true
requirement_ids: [VERIFY-05, VERIFY-06]
human_verification:
  - test: 'Input round-trip (Tests 2,3,4,5,15,16): encoder rotate/press, touch tap/swipe, plugin keyDown/dialRotate fire actions.'
    expected: Physical input drives bound actions + reaches plugins.
    why_human: BLOCKED on this unit — the 0x0300:0x3004 white-label demo SKU does not stream input (5-method proof incl. mirajazz; CLAUDE.md AKP05E glossary §7.1). Needs a retail AKP05E / Mirabox N4 or Frida-on-Windows. NOT a code gap.
  - test: 'Real .sdPlugin end-to-end (Tests 13,14): plugin handshake + setImage paints a key + receives events.'
    expected: A third-party .sdPlugin registers, paints a key, and receives input.
    why_human: NOT_WALKED — needs a real plugin sample + the Phase 26 editor to bind it. The image wire path is now reachable (Test 1 PASS), so Test 14 (plugin setImage) is unblocked on the output side.
  - test: Touch-strip zone UI drop target (Test 6 affordance).
    expected: The device editor exposes a strip-zone drop target so a user can assign an image to a zone.
    why_human: NO_AFFORDANCE in the current KeyDesigner — deferred to Phase 26 (OpenDeck-shaped editor, GAP-25B). The underlying capability works (zones render via BAT wire 1..4, confirmed below).
  - test: Sustained-session robustness (idle keep-alive + replug re-open) — RESOLVED 2026-05-31.
    expected: Panel stays lit during idle; survives a device power-cycle without an app restart.
    why_human: Both gaps now FIXED + hardware-validated — VERIFY-OP-1 keep-alive (commit 5722ead, soak-tested) and VERIFY-OP-2 cache-evict-on-replug (commit decc85c, replug-tested). No longer pending.
---

# Phase 25: Hardware Verification + Real Plugin — Verification Report (re-walk 2026-05-31)

**Phase Goal:** Prove the assign-image-and-press flow + a real plugin on the physical AKP05E. Gates the milestone close.

**Status:** human_needed — all **output** criteria verifiable on the connected demo unit now PASS on the fixed build; the **input** criteria are hardware-BLOCKED on this demo SKU (documented limitation, not a code gap); the **plugin** criteria are NOT_WALKED (need a sample + Phase 26).

This supersedes the PARTIAL 25-02 walkthrough (2026-05-28). Re-walked on the live
`0x0300:0x3004` (fw `V3.AKP05E.01.007`) against the build with commits `cd48ea3`
(key Rot180), `cb00677` (strip zones via BAT wire 1..4), `ac24a33`/`73d59da`
(`device.renderTest`).

## Re-walk results

| #             | Test                             | Prior (05-28) | Now (05-31)           | Evidence                                                                                            |
| ------------- | -------------------------------- | ------------- | --------------------- | --------------------------------------------------------------------------------------------------- |
| 1             | Push image to an LCD key         | FAIL          | **PASS**              | Keys 1-10 render upright (Rot180); operator-confirmed                                               |
| 6             | Push image to a touch-strip zone | NO_AFFORDANCE | **PASS (capability)** | Zones E1-E4 render via BAT wire 1..4; operator-confirmed. UI drop target → Phase 26                 |
| 7             | Global brightness slider         | PASS          | **PASS**              | Brightness sweep drove the panel                                                                    |
| 8             | Clear all blanks the panel       | PASS          | **PASS**              | clearAll blanks the panel                                                                           |
| 9             | hasClock=false honesty           | PASS          | **PASS**              | unchanged                                                                                           |
| 11            | DRA rect header layout           | BLOCKED       | **RESOLVED**          | DRA renders blank on this firmware; the strip uses BAT wire 1..4. The DRA-layout question is mooted |
| 12            | ENC vs MAI vs BAT mapping        | BLOCKED       | **RESOLVED**          | BAT-for-all-surfaces, hardware-confirmed. ENC/MAI/DRA blank. See akp05.md                           |
| 10            | Touch-strip zone+gesture map     | BLOCKED       | **PARTIAL**           | Output zone map confirmed (wire 1..4 = 4 zones aligned to dials); gesture/input half BLOCKED        |
| 2,3,4,5,15,16 | Input round-trips                | BLOCKED       | **BLOCKED**           | Demo-unit 0x3004 input-streaming gap; needs retail unit/Frida                                       |
| 13,14         | Real plugin handshake/setImage   | NOT_WALKED    | **NOT_WALKED**        | Needs a real .sdPlugin + Phase 26 editor; Test 14 output wire now reachable                         |

## Operational robustness gaps — found AND FIXED during this re-walk

Both NEW findings are now fixed and hardware-validated (2026-05-31):

- **VERIFY-OP-1 — idle-wedge — FIXED (commit `5722ead`).** The display
  controller wedged (backlit-but-black) when idle because the app sent no
  keep-alive. Added a ~1 s `CRT CONNECT` keep-alive timer in
  `StreamDockControlService` (akp05 `keepAlive()` override). Soak-tested: the
  panel stays lit after multi-minute idle.
- **VERIFY-OP-2 — stale handle on replug — FIXED (commit `decc85c`).** A
  power-cycle re-enumerated onto a new hidraw node but the registry flyweight
  cache returned the stale backend bound to the dead node. Added
  `DeviceRegistry::invalidateOpenDevice()` called on hot-plug Removed so the
  Arrived re-resolve rebuilds a fresh backend on the live node. Validated: after
  an unplug/replug with the app running, a re-render paints — no app restart.

## Close-gate status

Output verification on the demo unit is **complete and passing**. The milestone
close gate still requires: (1) Phase 26 (editor) finished, (2) a real `.sdPlugin`
walk (Tests 13/14), (3) the input round-trips — which need a **retail AKP05E /
Mirabox N4** (the connected `0x3004` demo SKU cannot stream input). The two
operational gaps above should be fixed before declaring the AKP05E "stable".
