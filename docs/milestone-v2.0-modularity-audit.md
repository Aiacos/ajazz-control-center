<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Milestone v2.0 Modularity + Security Audit (VERIF-02)

> **Living verification deliverable.** This document is the source-of-truth
> modularity / security boundary map for the v2.0 milestone (Phases 30-35). It
> records the four scripted VERIF-02 grep-gate results, the objectName-coverage
> scan over the new v2.0 QML surfaces, and an HONEST per-phase reconciliation of
> automated live evidence vs deferred human-UAT walks. Keep it current: when a
> gate, a control's objectName, or a phase's evidence status changes, update the
> matching row in the same PR. ASCII-only (CLAUDE.md: survive the Win32 CMD
> codepage). Mirrors the table style of `docs/plugin-event-parity.md`.

**Authored 2026-06-08** (Phase 35, Plan 35-02). The security model audited here
was **already shipped** across Phases 27-35; this plan VERIFIES + TEST-LOCKS +
DOCUMENTS it — it does not reimplement it.

## Legend

| Status     | Meaning                                                                                           |
| ---------- | ------------------------------------------------------------------------------------------------- |
| `PASS`     | Gate clean on the current tree (0 live-code hits; comment-only hits are excluded by design).      |
| `FAIL`     | Gate tripped (a live-code occurrence exists) — a release blocker until fixed.                     |
| `partial`  | Code path exists but is incomplete (e.g. chip-only, Wine launch deferred; human-verify deferred). |
| `done`     | Implemented + test-locked + (where applicable) live-confirmed via the debug channel.              |
| `deferred` | A live / hardware / windowed-session walk that cannot run headless on the dev machine.            |

______________________________________________________________________

## 1. VERIF-02 grep gates (scripted + comment-aware + CI-wired)

Run locally with `bash scripts/verif-milestone-gates.sh` (exit 0 iff all four
PASS). The same script is invoked by `.github/workflows/ci.yml` as a Linux-only
step ("Enforce milestone modularity gates (VERIF-02)"), placed immediately after
— and extending, not replacing — the Phase-30 `QHostAddress::Any`/SIGPIPE gate.

Each gate is **comment-aware**: a naive textual hit that lives only in a comment
(`//` or a block-comment continuation `*`) is excluded by the filter
`grep -vP ':[0-9]+:\s*(//|[*])'` (the identical idiom used by the Phase-30 CI
gate). A live-code occurrence WOULD trip the gate; the filter only suppresses
documentation prose. (Verified: a synthetic `m->listen(QHostAddress::Any, p);`
line trips; the `// QHostAddress::Any ...` comment does not.)

| Gate                    | Command (producing grep)                                                                          | Expect | Result on current tree | Naive (pre-filter) hits — all comment-only                                                                                                                     |
| ----------------------- | ------------------------------------------------------------------------------------------------- | ------ | ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `mirajazz-coupling`     | `grep -rn mirajazz src/core/ src/app/src/plugin_manager.cpp src/app/src/plugin_device_bridge.cpp` | 0      | **PASS** (count=0)     | 0 — no naive hits at all                                                                                                                                       |
| `nlohmann-core-include` | `grep -rn '#include.*nlohmann' src/core/include/`                                                 | 0      | **PASS** (count=0)     | 0 — COD-031 boundary clean (nlohmann is PRIVATE to ajazz_plugins)                                                                                              |
| `akp-wire-symbols`      | `grep -rnE 'makeAkp05\|makeAkp03\|makeAkp153' src/`                                               | 0      | **PASS** (count=0)     | 1 comment: `sidecar_stream_dock_device.hpp:16` (` * (Slice 4 flips the AKP05 VID/PID from makeAkp05 to this).`)                                                |
| `qhostaddress-any`      | `grep -rn 'QHostAddress::Any' src/`                                                               | 0      | **PASS** (count=0)     | 2 comments: `sd_plugin_server.hpp:14` (` * **Security delta from vendor**...`) + `sd_plugin_server.cpp:90` (`// QHostAddress::Any (0.0.0.0) which exposes...`) |

**Boundary notes.**

- **mirajazz-coupling:** the Stream Dock wire layer lives only in the
  out-of-process Rust sidecar (`streamdock-host/`); `ajazz_core` and the plugin
  bridge/manager never reference it. (The AKP815 keeps a custom in-tree C++
  backend but exposes no `mirajazz` symbol; it is not a gate target.)
- **nlohmann-core-include:** COD-031 release-blocker boundary. `nlohmann::json`
  is PRIVATE-linked to `ajazz_plugins` only and must never appear in an
  installed/public core header.
- **akp-wire-symbols:** `makeAkp03/05/153` were removed in the sidecar migration
  (`experiment/mirajazz` Slice D). The single naive hit is a doc comment
  describing the removal; the gate asserts on the function symbols (not the
  `akp05.cpp` filename, which survives in many comments).
- **qhostaddress-any:** the plugin WebSocket server binds `QHostAddress::LocalHost`
  only (`sd_plugin_server.cpp:93`). The two naive hits are the security-delta
  documentation comments. This gate duplicates the Phase-30 CI gate by design
  (defense in depth) and is also locked at the code level by
  `test_sd_plugin_server.cpp:59` + `:331` (`bindAddress() == LocalHost`, `!= Any`).

______________________________________________________________________

## 2. objectName coverage scan over new v2.0 QML (FINALIZED — Plan 35-03)

> **FINALIZED by Plan 35-03 (2026-06-08).** Plan 35-03 added the
> `LoadedPluginsPage` Windows status chip (`platformStatusChip`), the
> unsigned-consent chip (`unsignedConsentChip`), and an `objectName` +
> readable-label property on the pre-existing `trustChip` (which had none —
> `grep -c objectName LoadedPluginsPage.qml` was 0 before this plan). All three
> chips now expose both an `objectName` and a headless-readable label property
> (`statusLabel` / `consentLabel` / `trustLabel`), mirroring the
> `SettingsPage.qml waylandCapabilityWarningChip` idiom.

The debug channel addresses controls by `objectName` (`findByName`); an un-named
interactive control is invisible to `qml.get/set/invoke/click` (CLAUDE.md
definition-of-done item). This scan reports coverage over the QML surfaces
touched in v2.0; it reports rather than hard-fails on pre-v2.0 files.

| QML surface                   | Interactive controls                                             | objectName decls                                             | Status                                                                                                                   |
| ----------------------------- | ---------------------------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------ |
| `LoadedPluginsPage.qml`       | 3 delegate-internal display chips (no Switch — harness gap)      | 3 (`trustChip`, `platformStatusChip`, `unsignedConsentChip`) | `done (source)` — 1:1 objectName + readable-label coverage; live-render BLOCKED in this build (see live-gate note below) |
| `Inspector.qml`               | 3                                                                | 3                                                            | `done` — 1:1 objectName coverage on interactive controls                                                                 |
| `components/DeviceCanvas.qml` | 0 (layout/canvas; KeyCells lack objectNames — known harness gap) | 0                                                            | `partial` — KeyCell-selection harness gap is tracked (33-HUMAN-UAT.md item 1)                                            |

**Live-gate note (Plan 35-03, HONEST — not a fabricated walk).** The chips are
correctly authored and `objectName`-addressable in source, and their bindings
(`row.platformStatus` / `row.trustLevel`) are unit-locked via
`test_loaded_plugins_model.cpp` (the `platformStatus` derive) and the QML smoke
build. However, the live `qml.get` of the chips against an isolated offscreen
instance could **not** be completed this phase, for two independent,
honestly-recorded reasons (see `35-HUMAN-UAT.md`):

1. **(RESOLVED in code — live render still pending a GUI walk.)** This audit
   originally found `LoadedPluginsModel` was fed ONLY by the Python
   `OutOfProcessPluginHost` (`application.cpp:956-957`), with the
   `UnifiedPluginHost` aggregator (`m_pluginHost2`) constructed but **never wired
   to the model** — so `.sdPlugin` plugins (the only ones carrying a meaningful
   `winClass`) never reached the model and the `platformStatusChip` never
   instantiated (the code-review CR-01 / Phase-27 no-op-chip class of bug,
   contradicting 35-PATTERNS.md A1's "DECISIVE" claim). **This was FIXED** in
   commit `12acd2f`: `application.cpp:1120-1122` now calls
   `m_loadedPlugins->setPluginHost2(m_pluginHost2.get())` +
   `setPlugins(m_pluginHost2->plugins())` after the unified host is constructed,
   and `loaded_plugins_model.cpp:122-137` adds `setPluginHost2`/`m_host2` with
   `refresh()` preferring the merged inventory. The merged-population path is now
   unit-covered (WR-02 `FakeMergedHost2` test). The remaining gap is purely the
   **live chip-render walk** (a real win-only `.sdPlugin` installed → the chip
   shows the correct label in the running GUI), which the headless harness cannot
   drive (modal-Drawer gap, below) — tracked in `35-HUMAN-UAT.md`, not an
   unwired-data-source defect.
1. **The `loadedPluginsDrawer` (a modal `Drawer`/`Popup`) cannot be opened
   headlessly.** Opening it requires the `navLoaded` ToolButton's `onClicked` →
   `loadedPluginsRequested()` path, and the documented CLAUDE.md harness gap is
   that `qml.invoke clicked` / `qml.invoke open` do not fire for ToolButton /
   Popup. So even with rows present, the delegate chips are not realized until a
   real windowed click opens the drawer.

Loopback (PLGSEC-03) and the consent-persistence/tamper-refusal invariants
(PLGSEC-01/02) WERE confirmed live + test-locked this phase (see §3).

**Known harness gap (carried from Phase 33).** `DeviceCanvas`/KeyCell delegates
lack per-cell `objectName`s, so a key cannot be selected headlessly via
`qml.invoke`/`qml.click` — the Property-Inspector render/lifecycle walks are
therefore deferred to a windowed session (see 33-HUMAN-UAT.md). Plan 35-03's
live gate is the place to close the new-chip coverage; the pre-existing KeyCell
gap is a separate, lower-priority follow-up.

______________________________________________________________________

## 3. Security invariants (PLGSEC-01/02/03) — confirmed green, test-locked

All three are **already shipped, CR-fixed, and unit-tested**. Plan 35-02 confirms
each at its cited file:line and that the locking test stays green; **no security
source file was modified** (`git diff --stat` shows zero change to
`plugin_catalog_model.cpp`, `plugin_verify_gate.hpp`, `sd_plugin_server.cpp`).

| Req       | Invariant                                                                                                 | Implementing site (file:line)                                                             | Locking test (still green)                                                                                 |
| --------- | --------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- |
| PLGSEC-01 | 4-way verdict `{ Trusted, SelfSigned, Unsigned, Refused }`; `tampered != unsigned`                        | `plugin_verify_gate.hpp:41-46`; `verdictToTrustLevel` :89                                 | `test_plugin_verify_gate.cpp:265` tampered->Refused; `:305` unsigned->Unsigned; `:179-195` trust-level map |
| PLGSEC-01 | CR-01: a tampered plugin is **Refused even with `userConfirmedUnsigned=true`**                            | `plugin_catalog_model.cpp:907-927` (Refused branch quarantines unconditionally)           | `test_plugin_install_from_file.cpp:581` ("...refused even with consent") + `:854` (CR-01) + `:1004`        |
| PLGSEC-02 | per-plugin unsigned consent persists in `QSettings plugins/allowed/<uuid>` and survives the launch-sweep  | FIX-CONSENT write `plugin_catalog_model.cpp:1063-1076`; launch-sweep read `:173`          | `test_plugin_install_from_file.cpp:942` ("per-plugin allow survives launch-sweep with global toggle OFF")  |
| PLGSEC-03 | WS server binds `QHostAddress::LocalHost`; no setter widens it                                            | `sd_plugin_server.cpp:93` (`listen(QHostAddress::LocalHost, port)`); `bindAddress()` :145 | `test_sd_plugin_server.cpp:59` + `:331` (`== LocalHost`, `!= Any/AnyIPv4/AnyIPv6`)                         |
| PLGSEC-03 | catalog phone-home is opt-in (`onlineCatalogEnabled` + "disabled" sentinel, no outbound request when off) | `plugin_catalog_model.cpp:521-535`                                                        | covered by the catalog fetch fixtures; documented here as **gated, not URL-absent** (RESEARCH A4)          |

**Verdict note (PLGSEC-01).** A tampered package (signature block present,
Ed25519-invalid -> `Refused`) is treated as an ATTACK and quarantined
unconditionally. Consent (`userConfirmedUnsigned`) gates ONLY the `Unsigned`
(no-signature-block, developer sideload) branch and must NEVER leak into the
`Refused` branch — locked by `test_plugin_install_from_file.cpp:581`/`:854`.

**Verify run (current tree):** `ctest --preset linux-release -R "PluginVerifyGate|PluginInstallFromFile|PluginCatalog|SdPluginServer"` ->
48/48 PASS (39 verify/install/catalog + 9 SdPluginServer).

______________________________________________________________________

## 4. Per-phase live-evidence vs deferred HUMAN-UAT reconciliation (HONEST)

For each v2.0 phase, this table states whether the requirement work has
**automated** live evidence (headless debug-channel + screenshot, or unit/grep
gates) vs a **deferred** human-UAT walk that could not run headless on the dev
machine. No screenshot or walk is claimed that was not actually performed
(Pitfall 5: never fabricate evidence).

| Phase | Subsystem                                   | Automated evidence (headless / unit / gate)                                                                                                                                             | Deferred human-UAT walk                                                                                                                                                                                | Status    |
| ----- | ------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------- |
| 30    | Plugin-host modular foundation (HOST-0x)    | RED tests + CI loopback/SIGPIPE/hid_open gates + ADR; `SdPluginServer` unit-locked                                                                                                      | none required (boundary/scaffold phase)                                                                                                                                                                | `done`    |
| 31    | ActionInstance core model + schema v2       | unit tests for the ActionInstance model + profile schema v2; CR-01 recursion-DoS fixed (caught by review, not by 700+ tests)                                                            | none required (model-layer phase)                                                                                                                                                                      | `done`    |
| 32    | Binding layer + Multi/Toggle + EDIT-01      | `test_multiaction_dispatch.cpp` (logic); input->dispatch pipeline live-confirmed; EDIT-01 invisible-canvas live-fixed via screenshot                                                    | **deferred** — Multi Action + Toggle Action live sequencing/cycle walks (need a binding-authoring path) — see `32-HUMAN-UAT.md` (status: partial)                                                      | `partial` |
| 33    | Property Inspector E2E (PI-0x)              | PI-01/02/04 done + unit-locked (728 + 17 qml); WR-01 PI reload-churn live-fixed                                                                                                         | **deferred** — PI HTML render / `propertyInspectorDidAppear` on open + `$SD.setSettings` round-trip + restart — see `33-HUMAN-UAT.md` (status: partial; blocked headless by the KeyCell-selection gap) | `partial` |
| 34    | Per-app profiles + event-parity audit       | `docs/plugin-event-parity.md` audit; CR-01 X11 `XSetErrorHandler` BLOCKER fixed; WR-01..05 fixed; 785 ctest                                                                             | **deferred** — live Wayland (niri) + X11/XWayland focus-walk; all 7 items live/hardware-gated — see `34-HUMAN-UAT.md` (status: partial)                                                                | `partial` |
| 35    | Windows plugin support + security hardening | WINPLG-01/02 classifier + native-run gate + ADR (798 ctest, +13 `[win-plugin-classification]`); PLGSEC-01/02/03 confirmed + test-locked (this plan); VERIF-02 gates scripted + CI-wired | **deferred** — Windows / Wine launch of a VendorDll plugin (no Wine launcher this milestone); PI human-verify                                                                                          | `partial` |

### Requirement partials (explicit)

- **WINPLG-03 — `partial` (chip-only).** The Windows-plugin status surface is the
  `LoadedPluginsPage` chip consuming `PluginInfo.winClass` (Plan 35-01 + finalized
  in 35-03). **The actual Wine/Windows launch of a VendorDll plugin is DEFERRED** —
  no Wine launcher ships this milestone (the `supportsCurrentPlatform`
  VendorDll-Windows-only-with-Wine path is intentionally deferred per the WINPLG-01
  ADR). The classifier and the chip are done; the run path is not.
- **PI-03 — `partial` (human-verify deferred).** The Property-Inspector render +
  lifecycle-on-open and the `$SD.setSettings` round-trip-survives-restart criterion
  are deferred to a windowed session (blocked headless by the KeyCell-selection
  harness gap). PI-01/02/04 are done + unit-locked. See `33-HUMAN-UAT.md`.

______________________________________________________________________

## How to re-run this audit

```bash
# 1. Modularity gates (4 comment-aware grep gates; exit 0 == all PASS)
bash scripts/verif-milestone-gates.sh

# 2. Security invariants (PLGSEC-01/02/03) — all must stay green
ctest --preset linux-release -R "PluginVerifyGate|PluginInstallFromFile|PluginCatalog|SdPluginServer"

# 3. Confirm no security source drift (must show zero change)
git diff --stat -- src/app/src/plugin_catalog_model.cpp \
                   src/app/src/plugin_verify_gate.hpp \
                   src/app/src/sd_plugin_server.cpp
```

The deferred items in Section 4 are tracked in their respective
`.planning/phases/3{2,3,4}-*/3{2,3,4}-HUMAN-UAT.md` files and are to be walked
in a windowed / hardware-equipped session; they are NOT regressions.
