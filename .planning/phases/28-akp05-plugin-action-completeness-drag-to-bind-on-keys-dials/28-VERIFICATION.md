---
phase: 28-akp05-plugin-action-completeness-drag-to-bind-on-keys-dials
verified: 2026-05-31T23:30:00Z
status: passed
score: 6/6 must-haves verified
verification_mode: live debug-channel (CLAUDE.md mandatory) + unit suite
suite: ctest --preset linux-release -E qml = 739/739, 0 failed (baseline 714)
---

# Phase 28: AKP05 Plugin Action Completeness + Drag-to-Bind — Verification Report

**Phase Goal:** Installed AKP05 `.sdPlugin` plugins are fully usable — every declared tool shows
in the action library (hidden ones honestly hidden, OS/version-rejected ones diagnosed), and a
tool can be dragged onto a key / encoder dial / touch-strip zone and actually drives the plugin.

**Verified:** 2026-05-31 — implemented across 5 waves (28-01..28-05) + a live-driven gap-closure
(GAP-28-ABC) that fixed three real integration bugs unit tests missed. Live-proven through the
`AJAZZ_DEBUG_CONTROL` channel on the running app with a real connected AKP05E.

______________________________________________________________________

## Goal Achievement (6 ROADMAP success criteria)

| #   | Criterion                                                                                                                         | Status   | Evidence                                                                                                                                                                                                                                                                                                                                                                                    |
| --- | --------------------------------------------------------------------------------------------------------------------------------- | -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Action-library completeness: `VisibleInActionsList != false` actions shown; skipped/malformed surfaced as diagnostic; proven live | VERIFIED | LIVE: `plugin.installedActions` count=2 visible, **hiddenByVisibility=1** (hidden action filtered + counted), affordanceMask dial=3/key=1. PLUS gap-closure added **skippedOsVersion** so OS/version-rejected vendor plugins (System Monitor/Weather on Linux) are no longer silently invisible (28-LIVE-VERIFICATION.md "Orchestrator live re-verification" + GAP-28A, commit a25e46d)     |
| 2   | Drag-to-bind persists actionId for keys + dials + touch zones (fix EncoderDial.qml:179 + TouchStripLane.qml:271)                  | VERIFIED | 28-03 fixed both 5-arg→6-arg drops; C++ ProfilePersistence round-trip tests prove Action.id persists for encoder + touch-zone plugin bindings (28-03-SUMMARY)                                                                                                                                                                                                                               |
| 3   | Controller-affordance normalizer + STRICT reject of mismatched drops + unit-tested                                                | VERIFIED | `affordanceMask()` (Keypad=1, Knob/Encoder=2, SecondaryScreen=4, Information=0); strict gating on all three drop targets; LIVE affordanceMask dial=3/key=1; fixture tests (28-01/28-02/28-03)                                                                                                                                                                                               |
| 4   | Full action model: VisibleInActionsList + Encoder block + multi-state + default Settings                                          | VERIFIED | 28-01 parser extension + Catch2 fixtures (manifest_visibility.json / manifest_affordances.json)                                                                                                                                                                                                                                                                                             |
| 5   | ActionContext registered on drop/load → bridge invokes the plugin; LIVE dial round-trip                                           | VERIFIED | **LIVE END-TO-END**: bind plugin action to encoder 0 → plugin receives `willAppear`, then `dialDown` + `dialRotate` with `controller:"Encoder"` on synthetic `input.encoder`/`input.encoderPress`. Gap-closure GAP-28B fixed the real blocker (active-device id never propagated to input service + bridge for a device present at startup) via a `deviceActivated` signal (commit 05f99a6) |
| 6   | `ctest -E qml` ≥ 714, 0 failed; COD-031 clean; no protocol/wire changes; OOP host + built-ins untouched                           | VERIFIED | **739/739, 0 failed**; COD-031 clean each wave; no opcode/BAT/byCoord changes; Python OOP host untouched                                                                                                                                                                                                                                                                                    |

**Score:** 6/6 verified.

## Requirements Coverage

| Req                                                                        | Status    | Evidence                                                                                                                                                      |
| -------------------------------------------------------------------------- | --------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| PLUGIN-18 (completeness + diagnostic)                                      | SATISFIED | VisibleInActionsList filter + hiddenByVisibility/skippedUuidName/skippedParseFailure + **skippedOsVersion** + `plugin.installedActions` RPC; live count match |
| PLUGIN-19 (drag-to-bind keys+dials+zones, routable + context registration) | SATISFIED | 6-arg actionId fix; profileChanged→populateContextsForActivePage; touchZones.onTap enumeration; **deviceActivated propagation (GAP-28B)** makes it fire live  |
| PLUGIN-20 (affordance normalization + strict gating + full model)          | SATISFIED | affordanceMask normalizer + strict reject + Encoder/multi-state/Settings parse                                                                                |

## Real bugs found ONLY by live verification (CLAUDE.md mandate)

- **GAP-28B (critical):** device present at app start → udev emits no `Arrived` → `m_activeDeviceId`
  empty in input service + bridge → `byCoord("",…)`=nullopt (no key/dial events) AND profileChanged
  guard false (no willAppear). On a real machine with the AKP05E plugged in before launch, EVERY
  plugin binding was silently dead. Fixed: single `deviceActivated(codename)` propagation path.
- **GAP-28A:** OS/version-rejected plugins returned 0 actions with no diagnostic → user sees no tools,
  no explanation. Fixed: `skippedOsVersion` counter.
- **GAP-28C:** app passed `-pluginUUID=<dir>.sdPlugin` (dir name); `ownerForActionUuid` needs the
  manifest UUID. Fixed: parse top-level `UUID` as `puuid` fallback.

All three passed 713 unit tests before they were caught live — the exact Phase-27 lesson.

## Human verification (optional, low residual)

- Real-GUI drag-drop (mouse) of a plugin tile onto a dial in the actual window (vs the debug-RPC
  commitEncoderBinding path used live) — the QML drop handlers are unit-covered + the C++ commit
  path is the same; a manual GUI drag is the only unexercised surface.
- A retail AKP05E / Mirabox N4 to confirm the plugin event reaches the physical encoder render
  (the 0x3004 demo unit's input streaming is hardware-limited; synthetic input fully exercised the
  software path).

**Status: passed.** The phase goal — show all a plugin's tools, and drag them onto keys/dials to
actually use them — is met and live-proven end-to-end.
