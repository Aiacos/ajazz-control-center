# Roadmap: AJAZZ Control Center

## Milestones

- ✅ **v1.0 milestone** — Phases 1-2, retro-fit catalogue (shipped 2026-05-13). See [milestones/v1.0-ROADMAP.md](milestones/v1.0-ROADMAP.md).
- ✅ **v1.1 milestone** — Phases 3-8, device lifecycle hardening + scaffolding-to-functional (shipped 2026-05-14). See [milestones/v1.1-ROADMAP.md](milestones/v1.1-ROADMAP.md).
- 🚧 **v1.2 milestone** — Phases 9-13, Connected-Device Capability Parity (active, bootstrapped 2026-05-15).
- 🚧 **v1.3 milestone** — Phases 14-25, Stream Dock End-to-End / Elgato-compatible Plugin SDK (active; replanned from scratch 2026-05-23).

## Phases

<details>
<summary>✅ v1.0 milestone (Phases 1-2) — SHIPPED 2026-05-13</summary>

- [x] Phase 1: SEC-003 Plugin Host — completed 2026-05-03
- [x] Phase 2: QML Singleton Sweep — completed 2026-05-04

Audit: `tech_debt` — 7/7 success criteria PASSED; CR-01 (Win32 env pollution) and WR-01 (trust-roots parser) deferred to v1.1.

</details>

<details>
<summary>✅ v1.1 milestone (Phases 3-8) — SHIPPED 2026-05-14</summary>

- [x] Phase 3: Architectural Decisions (1/1 plan) — completed 2026-05-14
- [x] Phase 4: Hot-plug Hardening (7/7 plans) — completed 2026-05-14
- [x] Phase 5: Time-Sync Scaffolding (8/8 plans) — completed 2026-05-14
- [x] Phase 6: CR-01 Win32 OOP Env Pollution Fix (3/3 plans) — completed 2026-05-14
- [x] Phase 7: WR-01 Trust-Roots Parser Hardening (3/3 plans) — completed 2026-05-14
- [x] Phase 8: Scaffolded-Device Wiring (4/4 plans) — completed 2026-05-14

Audit: `tech_debt` — 28/28 requirements satisfied, 178/178 tests pass; deferred items (real-hardware UI verifies, Windows CI back-fill, AKP815 / Mirabox N3 maturity promotion blocked on real-device captures, libFuzzer Fedora packaging) carried to v1.1.x / v1.2 backlog.

</details>

### 🚧 v1.2 milestone (Phases 9-13)

**Milestone Goal:** Promote the 4 currently-connected scaffolded devices (3 catalogued + 1 unknown PID) to full advertised-capability parity with the native AJAZZ control software, driven by real-hardware USB protocol captures. Phase 9 is captures-driven research (no further `/gsd-research-phase` needed for it — it IS the research); Phase 10 establishes the device-promotion template; Phases 11-12 reuse the template at increasing risk/scope; Phase 13 closes the catalogue and back-fills v1.1 real-hardware UI verifies.

- [ ] **Phase 9: Research, Captures, Hygiene** *(PARTIAL-SCOPE: 7/7 non-capture plans shipped + ARCH-04/05/06 default verdicts ratified; CAPTURE-05/06 sanitised fixtures + ARCH finalization gated on operator Phase 9.x capture run — see STATE.md)* — Capture-data-hygiene policy + Wireshark/`usbmon` runbook + per-device sanitised wire-format fixtures + ARCH-04/05/06 ratification. Gates every implementation phase.
- [ ] **Phase 10: AKP05E (0x3004) Promotion** *(PARTIAL — reconciled 2026-05-27: image pipeline + 1024-byte packets + EncoderReleased + `hasClock=false` shipped; `akp05e` at `partial`. Open: 16ms encoder_coalescer not built; 10-03 LIVE-HW render smoke deferred → Phase 25; AKP05 wire bytes still Ghidra-derived + Linux render bug unconfirmed)* — real `setKeyImage`/encoder/brightness wired to the 0x3004 LCD (10 LCD keys / 4 endless encoders / LCD touch strip) + `clock` honest demotion.
- [ ] **Phase 11: AJAZZ 2.4G 8K Mouse Probe-and-Confirm** *(SUBSTANTIALLY SHIPPED — reconciled 2026-05-27: `ajazz_24g_8k` at `functional`; DPI 0x54 / poll 0x04 / RGB 0x07 / clock 0x28 / battery 0x05 shipped & tested. Open: USB-2.0 SOF-cap UI warning unbound. Opcodes corrected vs stale plan)* — DPI cycle / per-stage / polling-rate / LOD / per-zone RGB.
- [ ] **Phase 12: AK980 PRO Promotion** *(PARTIAL — reconciled 2026-05-27: `ak980pro` at `functional`; clock 0x28 + battery 0x20/01 + 20-mode RGB shipped & hardware-confirmed. Open (HW-free): wireless RGB rate-limiter (Pitfall 24), RGB direction plumbing, dedicated 0x17 sleep-timer, Save-vs-Push UX)* — RGB 20-mode + brightness/speed/direction + sleep-timer + `isWireless` rate-limiter + `clock` per ARCH-05.1.
- [x] **Phase 13: Catalogue + v1.1 UI Verifies Back-Fill** — `microdia_dongle_7016` entered at `probed` with topology evidence + ARCH-06 negative ratified + four real-hardware visual verifies from v1.1 (Sync button visibility, Settings auto-sync persistence, glyph-only-no-toast, MaturityRole tooltip). (completed 2026-05-28)

**Milestone constraints (load-bearing — do not lose these):**

- **Cap concurrent execute agents at 2** in autonomous runs (v1.1 retrospective lesson; three concurrent agents split a Phase 7 atomic commit and forced a Phase 5 planner `--no-verify`).
- **CAPTURE-01 is MUST-FIX-FIRST inside Phase 9** — `.pcap`/`.pcapng` gitignore + pre-commit reject hook + policy doc MUST land before any researcher does their first capture (Pitfall 17 — keystroke recovery from raw captures is deterministic via `tshark` / `USB-Keyboard-Parser`).
- **COD-031 boundary preserved** — no `nlohmann::json` in `ajazz_core` or any installed public header. Capture-extraction tooling is dev-time Python (`scripts/hex-to-cpparray.py`); runtime code reads `std::array<uint8_t>` literals, not parsed JSON.
- **Direct-to-`main` workflow + atomic Conventional Commits.** Pre-commit hooks must pass; `--no-verify` only acceptable when the hook itself is broken and the content verified independently.
- **ASCII-only test names** (Win32 CMD codepage mangling).

### 🚧 v1.3 milestone (Phases 14-25) — **replanned from scratch 2026-05-23**

**Milestone Goal:** Recreate the AJAZZ "Stream Dock" control system **1:1** — wire the
already-built, capture-verified Stream Dock device backends into the app **and** complete the
partially-built **Elgato Stream Deck v6-compatible plugin SDK** so the AKP05E (10 LCD keys / 4
endless encoders / touch strip) behaves like the original app. v1.2 shipped the device-side
wire layer (`BAT`/`LIG`/`CLE`/`ENC`/`MAI`/`DRA`/`ULEND`, byte-tested) but no app code calls it,
and the existing `SdPluginServer` (a loopback-bound `QWebSocketServer`) implements only the 13
standard Elgato messages — no plugin spawn, auth, Property Inspector, or device bridge. v1.3
builds the **app→device integration layer** (Phases 14-16) and the **full plugin SDK** (Phases
17-22) — the 26 AJAZZ messages, manifest + node/native/HTML spawn, the device↔plugin bridge,
Property Inspector, built-in actions, and a host-owned plugin store — converging at Phase 19
(setImage end-to-end + input→plugin). Phases 23-25 cover the hardware-gated auxiliary surfaces,
family generalisation, and end-to-end verification (including running a real `.sdPlugin`).
Promotes the Stream Dock family `scaffolded` → `functional`/`verified`.

**Reuse-first** (already built — see PROJECT/STATE): `SdPluginServer` (LocalHost-only, the RE's
#1 security footgun already fixed + test-pinned), `ActionEngine` (folder nav + chained
multi-actions), `Profile`/`ProfilePage` schema, the zip-slip-guarded `.sdPlugin` extractor,
`image_pipeline` (ARCH-04). **Decisions locked 2026-05-23:** Full Elgato SDK 1:1 · WebSocket
runtime (the Python OOP host stays as-is for SEC-003) · replan v1.3 from scratch.

- [x] **Phase 14: Stream Dock Control Service** — Persistent held-open control service: brightness ON at `open()`, live key-image push (`BAT`→chunks→`ULEND`, ~1s, no manual flush), repaint-on-load, `VER` firmware probe cached, and the `akp05e` `hasClock=false` honesty fix. The first app→device call that reaches the panel. (DISPLAY-06/07/08, DOCK-01/02, DEVICES-11) (completed 2026-05-24)
- [x] **Phase 15: Stream Dock Input Routing** — Poll loop drives each connected Stream Deck; key press/release, **4-encoder rotate (CW/CCW) + press (synthesised release)**, and touch-strip tap-zone/swipe route to bound actions via the core `ActionEngine` (instantiated in the app for the first time); 16 ms rotation coalescer. (INPUT-03/04/05) (completed 2026-05-24)
- [x] **Phase 16: Device Controls + Binding Persistence + Pages** — Brightness slider + "clear all" drive the device live; key/encoder/touch bindings persist to the profile and survive restart; multi-page/folder profiles drive host-side page navigation (swipe + page actions). (DISPLAY-09, PROFILE-01/02) (completed 2026-05-24)
- [x] **Phase 17: Plugin Protocol Completion** — On the existing `SdPluginServer`: `passHello`+salt/challenge auth, the **26 AJAZZ-only actions**, and all host→plugin events incl. encoder `dialRotate`/`dialDown`/`dialUp`. (PLUGIN-01/02/03/04/05) (completed 2026-05-24)
- [x] **Phase 18: Plugin Manifest + Discovery + Lifecycle + Spawn** — Manifest schema (Elgato v6 + AJAZZ ext incl. `Controllers:["Knob"]`), discovery + extraction, spawn for **system node ≥20 / native exe / HTML (WebEngine)**, crash/restart/`exitApp`, Mirabox compat shim. (PLUGIN-06/07/08/11) (completed 2026-05-24)
- [x] **Phase 19: Device ↔ Plugin Bridge (setImage e2e)** — `actionReceived`(setImage/setTitle/setState/setBG…) → control service → physical key; device input (keyDown/dialRotate/touchTap) → plugin; willAppear/deviceDidConnect lifecycle. **The convergence phase — first demoable plugin↔device round-trip.** (PLUGIN-10) (completed 2026-05-24)
- [x] **Phase 20: Property Inspector + Settings** — Per-action Property Inspector in `QWebEngineView`+`QWebChannel` (cefQuery polyfill, `sdpi.css` served); `get/set(+Global)Settings` persistence. (PLUGIN-09/13) (completed 2026-05-24)
- [x] **Phase 21: Built-in In-Process Actions** — page/profile nav, `device.brightness`, `system.hotkey` (opt-in global hook), multimedia, volume, plain text, browser/openUrl, multiactions, OBS (auth default-on). (PLUGIN-12) (completed 2026-05-24)
- [x] **Phase 22: Plugin Store / Local Install** — Install from local `.sdPlugin`/`.zip` via a host-owned catalog (**no phone-home**), behind a signature/manifest-verification gate. (PLUGIN-14) (completed 2026-05-24)
- [x] **Phase 23: Auxiliary Display Surfaces** — Per-encoder overlays (touch-strip zones; reconcile ENC-LCD vs DRA-zone model), main LCD strip, touch strip, and `DRA` rect-addressable partial upload. HARDWARE-GATED. (DISPLAY-10) (completed 2026-05-26)
- [x] **Phase 24: Family Coverage AKP03/153/815** — Same assign-image-and-press flow via the capability-generic service (per-family init + image format honored; AKP03 has 3 encoders). (DEVICES-10) (completed 2026-05-24)
- [x] **Phase 25: Hardware Verification + Real Plugin** *(PARTIAL — reconciled 2026-05-28: 25-01 shipped (UAT runbook); 25-02 PARTIAL after operator walkthrough — 3 PASS (brightness, clear, hasClock honesty), 1 FAIL (Test 1 image-upload 3-layer regression: L1+L2 fixed in commit `24651a3`, L3 routes to Phase 26), 1 NO_AFFORDANCE (Test 6 — no touch-strip drop target), 6 BLOCKED (demo unit 0x3004 input-streaming gap, awaits retail AKP05E/Mirabox N4/Frida path), 5 NOT_WALKED (gated on Phase 26 + real sdPlugin). Two new gaps GAP-25A/B routed to Phase 26.)* — AKP05E (`0300:3004`, fw `V3.AKP05E.01.007`) verified end-to-end (image, key press, encoder rotate+press, touch tap/swipe, brightness, clear); provisional §5 wire items reconciled; `hasClock=false` confirmed; **a real third-party `.sdPlugin` runs live**. HARDWARE-GATED. (VERIFY-05/06) (completed 2026-05-28)
- [ ] **Phase 26: OpenDeck-shaped Device Editor** *(NEW 2026-05-28; closes GAP-25A + GAP-25B from Phase 25 partial)* — Replace the generic `KeyDesigner.qml` NxN tile grid with an Elgato Stream Deck / OpenDeck-pattern device-shaped editor: per-SKU geometry (AKP05E = 5×2 key grid + 4 encoder dials + 1 touch strip; AKP153 = 3×5; AKP03 = 2×3; AK980 = keyboard view; AJ-mouse = DPI/RGB-only panel), drag-drop action library, per-key + per-encoder-LCD + per-touch-strip-zone drop targets, **auto-wire `StreamDockControlService::setActiveDevice()` on sidebar selection-changed (closes GAP-25A)**. Research deliverable: comprehensive analysis of `nekename/OpenDeck` (Tauri/SvelteKit), `naerschhersch/opendeck-akp05` (Mirajazz plugin), and `4ndv/mirajazz` (Rust reference) — covering ALL AJAZZ / Mirabox SKUs OpenDeck supports, not just AKP05E. Delivers VERIFY-05 affordances + unblocks Phase 25 UAT resume. (UI-01, DISPLAY-11)
- [ ] **Phase 27: Plugin Install/Trust/Persistence Hardening** *(NEW 2026-05-31; reconciles the 2026-05-30 plugin install/run epic — see STATE.md "2026-05-31 — Plugin install/run epic reconciliation")* — Close the gap between "plugins register over the WS" (shipped 2026-05-30, commits `02bed37`/`8e985f7`/`5725cb0`) and "plugins are fully installable, concurrent, and persistent **from the GUI**": **rediscover-after-install** (spawn a freshly installed `.sdPlugin` with NO app restart — no `rediscover()` exists today; `02bed37` only discovers once at launch), **GUI unsigned-install-with-consent** (split `verifyStagedPlugin`'s `Refused` verdict into unsigned-no-signature vs tampered-bad-signature so tampered ALWAYS refuses but unsigned installs with explicit consent — CR-01, named as the follow-up in `90d97e2`'s body), **in-app trust UX** (settings toggle + per-plugin "allow" action replacing the `AJAZZ_ALLOW_UNTRUSTED_PLUGINS` env var), **persisted per-plugin enable/disable** (today `discover()` spawns every plugin in the dir unconditionally; a user-disabled plugin must stay disabled across restart), and a **concurrency regression guard** (N-plugin `.sdPlugin`-dir-name keying + one-crash-doesn't-disable-siblings, guarding the `5725cb0` fix). HW-free (optional debug-channel install→spawn smoke). The plugin→device `setImage` round-trip via a bound profile action on **real hardware** stays **Phase 25 live-debt**, NOT this phase. (PLUGIN-15/16/17)
- [x] **Phase 28: AKP05 Plugin Action Completeness + Drag-to-Bind on Keys & Dials** *(NEW 2026-05-31; closes the plugin-usability gap found after Phase 27 — installed plugins don't surface all their tools, and tools can't be dropped on dials. See STATE.md reconciliation_2026_05_31_pm + the 3-agent investigation captured in 28-CONTEXT.md)* — Make installed AKP05 plugin actions fully **visible and bindable**: (1) **action-library completeness** — every `VisibleInActionsList != false` action a plugin declares appears, correctly grouped/labelled; malformed/skipped actions (empty UUID/Name at `plugin_catalog_model.cpp:382`, whole-plugin manifest-parse-skip at `:375`) surface as a visible diagnostic instead of vanishing silently; root cause pinned by **live-diagnosing System Monitor + Weather** via the `AJAZZ_DEBUG_CONTROL` channel (`installedActions()` count vs manifest visible-action count). (2) **Drag-to-bind on keys AND dials AND touch zones** — fix the confirmed `actionId`-drop regression at `EncoderDial.qml:179` + `TouchStripLane.qml:271` (both call `commit*Binding` with 5 args, silently losing the 6th plugin `actionId`, so a plugin dropped on a dial persists un-routable) so plugin actions bind routably on encoders + touch-strip zones, not just keys. (3) **Controller-affordance gating (STRICT reject)** — a normalizer treating Elgato `Encoder` **and** AJAZZ `Knob` as dial-capable, `SecondaryScreen` as touch-zone-capable, `Keypad`/absent as key-capable; mismatched drops visually rejected (`drag.accepted=false`) — a Knob-only action only on a dial, a Keypad-only action only on a key. (4) **Full action model** — parse `VisibleInActionsList`, the `Encoder` block (`TriggerDescription` rotate/push/touch + layout), multi-state (`States` count + state index + `DisableAutomaticStates`), and default `Settings` seeding; unit-tested over fixture manifests. (5) **ActionContext registration on drop / profile-load** so the device↔plugin bridge actually invokes the plugin — verified **LIVE** via the debug channel: drop a plugin action on a dial → synthetic `input.encoder` → plugin receives `dialRotate`/`dialDown` with `controller:"Encoder"`. UI + plugin-app layer only; **no protocol/wire changes** (the `plugin_device_bridge` controller/coordinates mapping is already correct). (PLUGIN-18/19/20) (completed 2026-05-31)

**Plans:** 5 plans (5 waves; ≤1 plan per wave — strict dependency chain)

- [x] 28-01-PLAN.md — Full PluginAction model (VisibleInActionsList/Encoder block/multi-state/Settings) + affordanceMask normalizer + Wave-0 fixtures + manifest tests (PLUGIN-18/20) (wave 1)
- [x] 28-02-PLAN.md — installedActions() visibility filter + hidden/error diagnostic counters + plugin.installedActions debug RPC + controllers/affordanceMask into the drag MIME payload + catalog tests (PLUGIN-18/20) (wave 2)
- [x] 28-03-PLAN.md — Fix 6-arg drops on EncoderDial:179 + TouchStripLane:271 + STRICT affordance gating on all three drop targets + C++ persistence round-trip test (PLUGIN-19/20) (wave 3)
- [x] 28-04-PLAN.md — Wire profileChanged -> bridge populateContextsForActivePage (activeDeviceId accessor + guarded lambda) + enumerate touchZones.onTap under controller=Encoder + bridge tests (PLUGIN-19) (wave 4)
- [x] 28-05-PLAN.md — LIVE debug-channel verification: installedActions count vs manifest visible count (System Monitor + Weather) + dial round-trip (synthetic input.encoder -> dialRotate/dialDown controller=Encoder) (PLUGIN-18/19/20) (wave 5)

**Milestone constraints (load-bearing — do not lose these):**

- **Built on branch `feat/streamdock`** (off `develop`). COD-031 boundary preserved — no `nlohmann::json` in `ajazz_core` or any installed public header; PRIVATE-linked to the app/plugin targets only. Core `Profile` keeps its hand-rolled JSON writer.
- **Plugin SDK = Elgato Stream Deck v6, 1:1** with the AJAZZ extensions, per `docs/protocols/streamdeck/akp_plugin_sdk.md`. Reuse `SdPluginServer`; do NOT create a new `src/host/plugin-host/` module (the RE doc's speculative path — the real server lives in `src/app/src/`). UI/CEF replaced by `QWebEngineView` + `QWebChannel` (CLAUDE.md: never QCefView; `Qt6::WebChannelQuick`).
- **Anti-features explicitly NOT replicated:** bind to `QHostAddress::Any`; unsigned-plugin trust; phone-home auto-update / store fetch (`cdn1.key123.vip` / Aliyun OSS); plaintext OBS WebSocket; always-on global keyboard hook (opt-in only); bundling `node20.exe` (detect system node ≥20).
- **The RE is the source of truth** (CLAUDE.md hard rule). The capture-verified BAT header (JPEG size BE16@10-11, key index 1-based@12) MATCHES real capture `43 52 54 00 00 42 41 54 00 00 08 7C 0D` — do NOT change it. Provisional §5 wire items (DRA / encoder-overlay framing / touch zone+swipe map) are hypotheses to verify against the physical device; where hardware contradicts a provisional RE value, the hardware wins and the RE doc is updated. Sources: `docs/protocols/streamdeck/**` + `~/MEGAsync/ajazz-reverse-engineering/dossier/{akp-streamdeck,capture-evidence}.md`.
- **HARDWARE-GATED phases**: Phase 23 (auxiliary surface layouts) and Phase 25 (full end-to-end verify + real plugin) require the AKP05E physically connected with a working `uaccess` ACL. If `/dev/hidraw*` is root-only after a re-enumeration (systemd ≥258 regression), physically replug or `setfacl` per CLAUDE.md.
- **Cap concurrent execute agents at 2** in autonomous runs (v1.1 retrospective lesson).
- **Direct-to-`main`-style workflow + atomic Conventional Commits.** Pre-commit must pass; `--no-verify` only when the hook itself is broken and verified independently. ASCII-only test names (Win32 CMD codepage mangling). Land the plugin host in reviewable slices (transport/auth · manifest/spawn · actions/bridge · PI · store).

## Phase Details

### Phase 9: Research, Captures, Hygiene

**Goal**: Capture-data-hygiene policy + Wireshark/`usbmon` runbook are in place; sanitised per-device wire-format fixtures + diff documents for all 4 connected devices are committed; ARCH-04 (image-pipeline location), ARCH-05 (per-device `setTime` outcome), and ARCH-06 (composite-HID dedup) are ratified in writing as Phase 9 artefacts.
**Depends on**: Nothing (first v1.2 phase; v1.1 audit closed independently).
**Requirements**: ARCH-04, ARCH-05, ARCH-06, CAPTURE-01, CAPTURE-02, CAPTURE-03, CAPTURE-04, CAPTURE-05, CAPTURE-06
**Success Criteria** (what must be TRUE):

1. **MUST-FIX-FIRST inside the phase**: raw `.pcap` / `.pcapng` files are rejected at commit time — `docs/policies/capture-data-hygiene.md` documents the policy, `.planning/research/captures/.gitignore` excludes both extensions, and a pre-commit hook rejects any commit attempting to add raw capture files (CAPTURE-01; Pitfall 17 closed before any capture is taken).
1. A developer reading `docs/protocols/CAPTURING.md` can install Wireshark + `usbmon` on the dev box, mount the kernel module, set the per-device USB capture filter (`usb.idVendor == 0xVVVV && usb.idProduct == 0xPPPP`), capture a USB-side-only event window, and convert the result to `std::array<uint8_t, N>` C++ literals via `scripts/hex-to-cpparray.py` without writing any libpcap / PcapPlusPlus link-time code (CAPTURE-02 + CAPTURE-03).
1. `tests/unit/fixtures/mock_transport.hpp` exists as a header-only ~80 LoC `MockTransport` exposing `write(span<const uint8_t>) → vector<vector<uint8_t>> writes()`; the three existing device backends (`Akp03Device`, `ProprietaryKeyboard`, `AjSeriesMouse`) accept it via the existing COD-026 DI constructors without architectural change (CAPTURE-04).
1. Sanitised capture fixtures exist for all 4 connected devices (`akp05e`, `ak980pro`, `ajazz_24g_8k`, `0c45_7016`), SHA-256-indexed in `.planning/research/captures/INDEX.md`; raw `.pcap` files stay out-of-tree per CAPTURE-01 (CAPTURE-05). Per-device wire-format diff documents extend `docs/protocols/streamdeck/akp03.md` and add `docs/protocols/keyboard/ak980pro.md` + `docs/protocols/mouse/ajazz_24g_8k.md` where findings diverge from the existing OSS-corpus baseline (CAPTURE-06).
1. Three written architectural decision artefacts land under `.planning/phases/09-research-captures-hygiene/`: ARCH-04 records the AKP03 image-encoding pipeline location (recommended Option C — Qt6 `QImage::scaled(SmoothTransformation)` + `QImageWriter` JPEG host-side in `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`, PRIVATE-linked); ARCH-05 records the per-device `setTime` outcome (default verdict: `hasClock=false` on `akp05e` and `ak980pro`, `setTime` stays `NotImplemented`; PROJECT.md Out-of-Scope row preserved); ARCH-06 records the composite-HID dedup decision (default verdict: NOT firing — topology proves `0c45:7016` is a separate dongle on a different bus branch).

**Plans**: 7 plans (partial-scope execution — non-capture deliverables only; CAPTURE-05/06 + ARCH-04/05/06 finalization deferred to a follow-up Phase 9.x run after the user produces captures)

- [x] 09-01-PLAN.md — CAPTURE-01 hygiene policy + gitignore + pre-commit hook (MUST-FIRST per D-01)
- [x] 09-02-PLAN.md — CAPTURE-02 CAPTURING.md Wireshark + usbmon + dumpcap runbook
- [x] 09-03-PLAN.md — CAPTURE-03 hex-to-cpparray.py + pytest smoke test
- [x] 09-04-PLAN.md — CAPTURE-04 MockTransport header-only fixture + AjSeriesMouse smoke test
- [x] 09-05-PLAN.md — ARCH-04 default-verdict ratification (Qt6 host-side image pipeline, Option C)
- [x] 09-06-PLAN.md — ARCH-05 default-verdict ratification (no RTC opcode; hasClock=false per device)
- [x] 09-07-PLAN.md — ARCH-06 default-verdict ratification (dongle is separate; NOT firing dedup)

**Phase notes**:

- This phase IS the research — no further `/gsd-research-phase` invocation needed for Phase 9.
- ARCH decisions are captures-driven; default verdicts above are the expected outcome but Phase 9 captures can flip them. If ARCH-06 captures contradict (e.g. unplugging `ak980pro` causes `0c45:7016` to disappear simultaneously), dedup infrastructure lands before Phase 12 and Phase 13 re-sequences.
- CAPTURE-01 is the milestone-blocking deliverable. Land it in the first plan of this phase before any capture is taken.
- **PARTIAL EXECUTION (2026-05-15):** the partial-scope plan set above closes 4 of 5 Phase 9 success criteria (CAPTURE-01 hygiene; CAPTURE-02/03 runbook + script; CAPTURE-04 MockTransport; ARCH-04/05/06 default-verdict ratification). Success criterion #4 (sanitised fixtures for all 4 devices + diff docs — CAPTURE-05/06) stays pending until a follow-up Phase 9.x run, which requires the user to install Wireshark, load usbmon, and physically interact with the 4 connected devices (out of agent scope per CLAUDE.md hard rules).

### Phase 10: AKP05E (0x3004) Promotion

**Goal**: A user with a `0300:3004` AKP05E Stream Dock Plus (10 LCD keys / 4 endless encoders / LCD touch strip) plugged in can push `QImage`s to any LCD key, see real encoder rotate/press/release events, set per-key colour, set global brightness, clear the device, and flush pending writes — and the `devices.yaml` row no longer falsely advertises `clock` capability. Maturity `scaffolded` → `functional`.
**Depends on**: Phase 9 (ARCH-04 image-pipeline location decided; ARCH-05 `clock` verdict for `akp05e` decided; AKP05E 0x3004 captures + diff doc committed; `MockTransport` available).
**Requirements**: DISPLAY-01, DISPLAY-02, DISPLAY-03, DISPLAY-04, INPUT-01, INPUT-02, DEVICES-05
**Success Criteria** (what must be TRUE):

1. `src/devices/streamdeck/include/.../akp03_protocol.hpp` PacketSize migrates from 512 → 1024 in one load-bearing commit that simultaneously unblocks the 13 Stream Dock sibling SKUs (verified by per-codename + family-coverage tests for both 0x3004 and at least one canonical 0x1001 sibling — Pitfall 30 cross-family regression closed) (DISPLAY-01).
1. A user can push a `QImage` to any LCD key on `akp05e` via `IDisplayCapable::setKeyImage(int keyIndex, QImage)`; image preprocessing lives in `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` per ARCH-04 (60×60 JPEG `Rot0` for 0x3004; AKP03R rev. 2 lineage 64×64 `Rot90` from descriptor table); chunked uploads use 1024-byte packets via `BAT` opcode with last-chunk Transfer-Done flag = 0x01, signature-enforced by `std::span<const uint8_t, 1024>` (Pitfall 18 prevention) (DISPLAY-02).
1. A user can clear keys (`CLE`), set per-key solid colour via short-circuit (`setKeyColor`), set global LCD brightness via `LIG`, and explicitly flush pending writes (`flush`); all four methods are backed by byte-level wire-format assertions against `MockTransport` (DISPLAY-03).
1. **Promotion gate**: the real-hardware 100-image power-cycle smoke test in `tests/integration/` (manual, gated behind `AJAZZ_REAL_HARDWARE` ctest filter) passes — chunked uploads do not hang the device across 100 cycles (DISPLAY-04; Pitfall 18 promotion gate).
1. A user spinning an encoder sees rotation events delivered with a proper `EncoderReleased(int encoderIndex)` event (replacing the `value=0` half-step workaround at `src/devices/streamdeck/src/akp03.cpp:289-293`); rotation events are coalesced at the QML observer layer with a 16 ms `QTimer` so fast spin produces one signal per repaint frame (total delta accumulated and emitted once — Pitfall 23 signal-storm closed) (INPUT-01 + INPUT-02).
1. `docs/_data/devices.yaml` row for `akp05e` removes `clock` from `capabilities:` with a `notes:` line citing ARCH-05 ("no RTC opcode in mirajazz/opendeck-akp03/ajazz-sdk; LCD clock widgets are host-rendered images via the `display` capability, not firmware time"); maturity promotes `scaffolded` → `functional` (DEVICES-05).

**Plans**: 3 plans (3 waves)

- [x] 10-01-PLAN.md — DISPLAY-01/02/03: 1024-byte AKP05 packets + setKeyImage/setKeyColor via image_pipeline + MockTransport wire tests (wave 1) *(shipped ad-hoc; reconciled 2026-05-27, see 10-01-SUMMARY.md — work landed in akp05*, not akp03; no 512→1024 migration since AKP05 was authored at 1024)\*
- [ ] 10-02-PLAN.md — INPUT-01/02 + DEVICES-05: genuine EncoderReleased + 16ms encoder coalescer + devices.yaml clock demotion (wave 2) *(PARTIAL — EncoderReleased + clock demotion shipped; **16ms encoder_coalescer NOT built** (file absent); maturity stayed `partial` not `functional`)*
- [ ] 10-03-PLAN.md — DISPLAY-04: real-hardware 100-image power-cycle smoke test, AJAZZ_REAL_HARDWARE-gated + operator runbook (wave 3) *(DEFERRED → Phase 25; LIVE-HW, never shipped)*

**Phase notes**:

- This phase establishes the canonical "device-promotion phase" template (`MockTransport` consumption, `static_assert(!is_base_of_v<QObject, IDisplayCapable>)` mix-in lock, descriptor-parameterisation, three-witness rule for any capability promotion) that Phases 11-12 reuse verbatim.
- Standard research-during-planning patterns; no mid-phase `/gsd-research-phase` expected.

**UI hint**: yes

### Phase 11: AJAZZ 2.4G 8K Mouse Probe-and-Confirm

**Goal**: A user with a `3151:5007` 8K mouse plugged in can cycle through 8 DPI stages (field-determined count — `devices.yaml dpi_stages: 8`, corrected 2026-05-20 from the earlier 6 assumption; cycle ORDER vendor-captured, NOT naive `+1`), set per-stage DPI / colour / LOD independently, set polling rate up to 8000 Hz (with an honest USB 2.0 cap warning), and set per-zone RGB — and the `devices.yaml` row reflects the captured AJ199 V1.0-vs-Max envelope outcome. Maturity `scaffolded` → `partial` or `functional` per capture coverage.
**Depends on**: Phase 9 (AJ199 V1.0-vs-Max envelope reconciliation captured; 8K mouse cmd 0x21/0x22/0x23/0x24/0x30/0x40/0x50 captures + diff doc committed; `MockTransport` available).
**Requirements**: MOUSE-01, MOUSE-02, MOUSE-03, MOUSE-04, MOUSE-05, DEVICES-07

> **Opcode supersession (reconciled 2026-05-27):** the `0x21/0x22/0x23/0x30` DPI/poll/LOD/RGB map in this section is a **pre-corpus guess**. The 2026-05-21 RE corpus (`~/MEGAsync/ajazz-reverse-engineering/dossier/aj-series-mouse.md`) corrected it — shipped code uses DPI table `0x54` (atomic 8-stage), omnibus settings/LOD `0x53`, poll rate `0x04` via `_RateToNum`, LED `0x07`. `0x21/0x22/0x24` are OLED picture/GIF opcodes, NOT DPI/poll. RE/hardware wins (CLAUDE.md). Code is correct; the success criteria below quote the stale opcodes.

**Success Criteria** (what must be TRUE):

1. `docs/protocols/mouse/aj_series.md` is extended with the first-party-captured AJ199 V1.0-vs-Max envelope reconciliation outcome for `3151:5007`; if the device uses the Max envelope materially divergent from the current `aj_series.cpp` V1.0 assumption, `makeAjSeries` factory splits per Pattern B and a new `makeAjazz24g8k` factory entry is added; if V1.0 holds, the existing factory shape is preserved with the reconciliation documented (MOUSE-01).
1. A user can cycle through 8 DPI stages on `ajazz_24g_8k` in the firmware-captured cycle order (NOT a naive +1 — Pitfall 28 closed); stage state persists across power-cycle by living in device NVM, NOT host-side cache (verified by power-cycling between cycle commands in a real-hardware test) (MOUSE-02).
1. A user can set per-stage DPI value via `IDpiCapable::setStageDpi(int stage, int dpi)` (cmd 0x21), per-stage colour indicator, and Lift-Off-Distance (cmd 0x23) independently for each of the 8 stages; all wire-format assertions against `MockTransport` (MOUSE-03).
1. A user can set polling rate to 1000 / 2000 / 4000 / 8000 Hz via `IPollingRateCapable::setRate` (cmd 0x22); when 8000 Hz is selected and the host port reports USB 2.0, the UI surfaces an honest USB 2.0 warning that effective polling is capped by the USB SOF rate (no lying about throughput — D-02 honesty contract carried from v1.1) (MOUSE-04).
1. A user can set per-zone RGB on `ajazz_24g_8k` (cmd 0x30); zone count + names are derived from the device capability descriptor, NOT hardcoded (Pitfall 22 mitigation) (MOUSE-05).
1. `docs/_data/devices.yaml` row for `ajazz_24g_8k` updates `notes:` with the first-party-captured wire-format reconciliation result; maturity promotes `scaffolded` → `partial` if any advertised capability is uncaptured, `scaffolded` → `functional` only if all `[dpi, rgb]` capabilities pass the three-witness rule (capture + observable state change + negative test) (DEVICES-07; Pitfall 29 honesty contract).

**Plans**: 4 plans (shipped ad-hoc ahead of bookkeeping; reconciled 2026-05-27 — see 11-0N-SUMMARY.md)

- [x] 11-01-PLAN.md — MOUSE-01 wire-format footing: control collection usage 0x02, BIT7 checksum, AJ159 APEX naming *(shipped)*
- [x] 11-02-PLAN.md — MOUSE-02/03 DPI table (0x54 atomic 8-stage) + LOD (0x53 omnibus) *(shipped; opcodes corrected vs plan)*
- [x] 11-03-PLAN.md — MOUSE-04/05 poll rate (0x04/\_RateToNum) + RGB (0x07, single-zone) *(PARTIAL — wire shipped; **USB-2.0 SOF-cap warning in MousePanel.qml NOT bound** (ComboBox unwired))*
- [x] 11-04-PLAN.md — DEVICES-07 devices.yaml row: `dpi_stages: 8`, `maturity: functional`, battery + APEX name *(shipped; maturity functional vs plan's "partial" — justified by hardware-confirmed clock/battery/control, config opcodes are decompile-confidence)*

Beyond plan: OLED clock `0x28` + battery telemetry (`0x05` byte-3 + `0xF7` poll) shipped (hardware-confirmed 2026-05-21).

**Phase notes**:

- Highest single-device risk in v1.2 (zero 3rd-party OSS corpus exists for this PID). Failure here does not block Phase 12 — phase ordering is fail-fast on highest uncertainty.
- **Mid-phase research flag**: if Phase 9 captures reveal AJ199 V1.0 vs Max diverges materially (or if cmd 0x21..0x50 envelopes don't replay cleanly against `MockTransport`), invoke `/gsd-research-phase` on the SONiX 3151 chipset family before committing to a factory split. Capture the trigger and the research output in the Phase 11 plan artefact.
- Reuses Phase 10's `MockTransport` + descriptor-parameterisation + three-witness-rule template verbatim.

**UI hint**: yes

### Phase 12: AK980 PRO Promotion

**Goal**: A user with an `0c45:8009` AK980 PRO keyboard plugged in (via its 2.4G wireless dongle) can select one of 20 RGB lighting modes, set RGB brightness / speed / direction (raw 0..5 scale shielded by UI scale-mapping), set a discrete sleep-timer, save / push profile changes deliberately (host-save instant; device-flash ≤1/min), and the wireless link does not stall keystrokes during RGB transitions. `devices.yaml` row honestly advertises the **real firmware `clock`** (RTC via opcode `0x28`, per ARCH-05.1). Maturity `scaffolded` → `partial`.
**Depends on**: Phase 9 (`ak980pro` Report ID 0x04 / cmd 0x13 / cmd 0x17 captures + diff doc committed; ARCH-05.1 `clock` verdict for `ak980pro` decided — RTC FLIPPED: ARCH-05.1 (2026-05-17, FINAL) supersedes the ARCH-05 default verdict for this device after locating the real `0x28` firmware RTC; `isWireless=true` topology evidence captured). Phase 10 (canonical device-promotion template established).
**Requirements**: KEYBOARD-01, KEYBOARD-02, KEYBOARD-03, KEYBOARD-04, DEVICES-06
**Success Criteria** (what must be TRUE):

1. A user can select one of 20 AK980 PRO RGB lighting modes via `IRgbCapable::setMode` on `ak980pro`; implementation uses cmd 0x13 with 64-byte Report ID 0x04 three-stage `START` (0x18) → cmd → `FINISH` (0xf0) per the TaxMachine AK820 Pro clean-room corpus; the mode list is table-driven from Phase 9 capture, NOT hardcoded (KEYBOARD-01).
1. A user can set RGB brightness (0..5), speed (0..5), and direction (0..3) via `IRgbCapable` extensions; the QML UI surfaces scale-mapped sliders, not the raw 0..5 values, and the range is derived from the device-reported capability descriptor (Pitfall 22 mitigation — never hardcode the range) (KEYBOARD-02).
1. A user can set the keyboard sleep-timer (idle-minutes-to-OLED-off) via cmd 0x17; the UI exposes a discrete picker (1 / 5 / 10 / 30 min), and the selection persists across power-cycle (verified by real-hardware test) (KEYBOARD-03).
1. **Honesty-critical promotion gate**: `ak980pro` device record carries `isWireless = true`; `ProprietaryKeyboard::writeRgb` enforces a `≤10 writes/sec` rate-limit when `isWireless` is true, and a real-hardware RGB-transition smoke test confirms that keystrokes are NOT stalled during a 60-second RGB sweep (Pitfall 24 closed — wireless dongle queue overflow does not bleed into keystroke loss). Rate-limiter is opt-in per-device, NOT a global throttle. The UI also separates "Save profile" (instant, host-disk) from "Push to device" (deliberate, NVM-flash, ≤1/min) to prevent NVM wear (Pitfall 25 closed) (KEYBOARD-04).
1. `docs/_data/devices.yaml` row for `ak980pro` **retains** `clock` in `capabilities:` — ARCH-05.1 (2026-05-17, FINAL) flipped the ARCH-05 default verdict for this device after locating the real firmware RTC (4-packet `0x18`/`0x28`/data/`0x02` HID Feature Report envelope, corroborated by gohv + KyleBoyer + vendor-binary disassembly). `IClockCapable::setTime` is implemented end-to-end (returns `Ok`/`IoError`, never a lying no-op `Ok`); the `notes:` line cites ARCH-05.1. Maturity promotes `scaffolded` → `partial`; clock promotion `partial` → `functional` gates on the Phase 9.x physical round-trip witness (TFT shows the time we sent). RGB + sleep-timer functional; macros / layers / per-key RGB / battery stay `feature_summary.pending:` per Pitfall 29 honesty contract (DEVICES-06). **NOTE:** this is the OPPOSITE of DEVICES-05 (`akp05e`), where ARCH-05 stands and `clock` IS removed — the Stream Dock family has no firmware RTC.

**Plans**: 4 plans (core shipped ad-hoc; reconciled 2026-05-27 — see 12-0N-SUMMARY.md). **PARTIAL: several plan deliverables never shipped.**

- [ ] 12-01-PLAN.md — KEYBOARD-04 wireless RGB rate-limiter (`isWireless` + `writeRgb` funnel, Pitfall 24) *(**NOT SHIPPED** — no rate-limit funnel; a code comment delegates rate-limit to the OS write queue. Pitfall-24 keystroke-stall threat unmitigated. HW-free, still open.)*
- [ ] 12-02-PLAN.md — KEYBOARD-01/02 firmware RGB: 20-mode table (0x13, 5-packet) + direction *(PARTIAL — 20-mode picker + brightness/speed shipped; **`direction` NOT plumbed** — builder accepts it but caller hardcodes 0. HW-free, still open.)*
- [ ] 12-03-PLAN.md — KEYBOARD-03 dedicated 0x17 sleep-timer + discrete picker *(**NOT SHIPPED** — sleep still rides the 0x07/0x10 settings batch; no dedicated 0x17 path. HW-free, still open.)*
- [x] 12-04-PLAN.md — DEVICES-06 honesty + clock (ARCH-05.1) + battery *(clock 4-packet 0xFF13 + 30ms handshake + battery 0x20/0x01 shipped & hardware-confirmed; **Save-vs-Push UX (Pitfall 25) NOT built**; `ak980pro.md` doc not created)*

Maturity is `functional` in devices.yaml — rests on hardware-confirmed clock/battery/0xFF13 control; RGB (no live witness) + TFT `0x7F` (PROVISIONAL, no capture) + the unshipped items above remain open.

**Phase notes**:

- Largest capability surface + biggest risk in v1.2. Establishes the rate-limiter pattern + host-save-vs-device-flash UX separation that future wireless / NVM-heavy backends will reuse.
- **Mid-phase research flag**: if Phase 9 captures reveal TFT chunked-send (cmd 0x72) chunk size, per-key custom RGB cmd, macro upload cmd, or layer-switch cmd materially divergent from the TaxMachine baseline, invoke `/gsd-research-phase` on the Microdia 0c45 chipset family. Capture the trigger and research output in the Phase 12 plan artefact. AK980 PRO TFT image upload (DISPLAY-05) is explicitly deferred to v1.2.x even if captured cleanly here.
- Possible Pattern B promotion to abstract base + `makeAk980Pro` sibling factory, mirroring the Phase 11 mouse-factory decision; gated on captured envelope divergence from `ProprietaryKeyboard`'s shared shape.

**UI hint**: yes

### Phase 13: Catalogue + v1.1 UI Verifies Back-Fill

**Goal**: `microdia_dongle_7016` (`0c45:7016`) enters the catalogue at `probed` tier with the live USB-topology evidence supporting ARCH-06's negative verdict (separate dongle, NOT a composite interface of AK980 PRO); the four v1.1-deferred real-hardware visual UI verifications are closed now that the user has physical access to 4 connected devices.
**Depends on**: Phase 9 (`microdia_dongle_7016` topology + HID descriptor captured; ASCII codename verified). Phase 10 (`akp05e` is `functional` with `hasClock=false` so Phase 5 Sync-button visibility flips honestly on that row). Phase 12 (`ak980pro` is `partial` with `hasClock=true` per ARCH-05.1 — the real `0x28` firmware RTC — so the Sync-button stays VISIBLE on that row, honestly).
**Requirements**: DEVICES-08, DEVICES-09, VERIFY-01, VERIFY-02, VERIFY-03, VERIFY-04
**Success Criteria** (what must be TRUE):

1. A new `docs/_data/devices.yaml` row for `microdia_dongle_7016` (`0c45:7016`) at `probed` tier with `capabilities: []` and `family: dongle` (or `unknown`) is added; `notes:` documents the live-`lsusb` topology evidence (separate bus branch from `ak980pro`, two boot-keyboard interfaces, Full-Speed 12 Mbps, iManufacturer = "SONiX" / iProduct = "USB DEVICE") and the unknown paired-input downstream device; codename verified as ASCII-only (Pitfall 32 closed) (DEVICES-08).
1. `docs/protocols/keyboard/microdia_dongle.md` (NEW, stub) documents the dongle's HID descriptor + topology + identification methodology so a future SKU recognising the same dongle can be added without re-doing the topology forensics (DEVICES-09).
1. **VERIFY-01**: A user looking at the sidebar sees the Sync-button on rows whose `hasClock=true` and NOT on rows whose `hasClock=false`. Verified visually on `akp05e` (post-DEVICES-05 demotion: button hidden), `ak980pro` (`hasClock=true` per ARCH-05.1 — the real `0x28` RTC: button VISIBLE), and any v1.1 catalogue device with `hasClock` still set (button visible).
1. **VERIFY-02**: A user toggling the Settings "auto-sync time on device connect" switch sees the toggle state survive an app restart (QSettings persistence verified), and a real device arrival triggers the auto-sync 300 ms-after-arrival firing path (verified via log inspection — capability re-validated at firing time per v1.1 D-02).
1. **VERIFY-03**: A user looking at a device row whose backend returns `NotImplemented` from `IClockCapable::setTime` sees an exclamation glyph + tooltip — never a "Time synced" success toast (Pitfall 19 honesty contract carried from v1.1).
1. **VERIFY-04**: A user hovering on a sidebar row sees the MaturityRole tooltip; the tooltip content matches the `devices.yaml` `notes:` field for that row; all 5 tier values (`scaffolded` / `probed` / `partial` / `functional` / `verified`) render legibly with the v1.0 styling vocabulary.

**Plans**: TBD
**Phase notes**:

- ARCH-06 negative ratification is the implicit deliverable: by entering `microdia_dongle_7016` as a separate `probed`-tier device with no dedup logic anywhere in `DeviceRegistry`, the topology-driven decision is encoded in the catalogue itself.
- **Conditional re-sequencing**: if Phase 9 captures contradict the dongle hypothesis (e.g. unplugging `ak980pro` causes `0c45:7016` to disappear simultaneously, or vendor-control interface ID resolution shows shared addressing), ARCH-06 fires and composite-HID dedup infrastructure lands in a new Phase 12.5 BEFORE this phase. Probability: LOW per Phase 9 default verdicts.
- Smallest phase in v1.2 by scope; no further research flags.

**UI hint**: yes

### Phase 14: Stream Dock Control Service

**Goal**: A user with an AKP05E plugged in sees the panel light up when the app selects it; assigning an image to a key makes it appear on the physical key within ~1s with no manual flush; loading a profile repaints every key. The first app→device call that reaches the panel — closes the Phase-10 UAT gap.
**Depends on**: v1.2 device-side wire layer (`BAT`/`LIG`/`CLE`/`ULEND`, capture-verified) + ARCH-04 `image_pipeline`.
**Requirements**: DISPLAY-06, DISPLAY-07, DISPLAY-08, DOCK-01, DOCK-02, DEVICES-11
**Success Criteria**:

1. Making a Stream Deck the active device lights the panel — the control service holds it open and issues a brightness-ON `LIG` at `open()` (DISPLAY-06); single held-open HID handle, ARCH-03 weak_ptr flyweight preserved.
1. Assigning a key image pushes it within ~1s through encode → `BAT` → 1024-byte chunks → `ULEND`, no manual flush (DISPLAY-07); an `ULEND` commit follows each burst (DOCK-02).
1. Loading/switching a profile repaints all keys from saved bindings (DISPLAY-08).
1. `VER` firmware probe runs at open and is cached/surfaced (DOCK-01); the `akp05e` descriptor advertises `hasClock=false` (DEVICES-11).

**Plans**: 2 plans · **Phase notes**: Load-bearing foundation — land first. BAT header capture-verified; do NOT alter the wire format (app-side wiring only). **UI hint**: yes

Plans:

- [x] 14-01-PLAN.md — akp05e `hasClock=false` descriptor honesty fix (DEVICES-11) + regression test
- [x] 14-02-PLAN.md — StreamDockControlService: held-open handle, brightness-ON at open, coalesced BAT->chunk->ULEND key push, repaint-on-load, firmware surface (DISPLAY-06/07/08, DOCK-01/02)

### Phase 15: Stream Dock Input Routing

**Goal**: Pressing a key, turning/pressing one of the 4 encoders, or tapping/swiping the touch strip fires the bound action via the core `ActionEngine` (instantiated in the app for the first time).
**Depends on**: Phase 14 (held-open handle the poll loop drives).
**Requirements**: INPUT-03, INPUT-04, INPUT-05
**Success Criteria**:

1. A poll loop drives each connected Stream Deck; a key press/release fires its bound action via `ActionEngine` (INPUT-03).
1. Each of the **4 encoders** fires bound actions on rotate CW vs CCW and on press; because the device emits a press only, the host **synthesises the release** and delivers a genuine `EncoderReleased` (replaces the `value=0` half-step workaround); rotation coalesced with a 16 ms `QTimer` (INPUT-04).
1. A touch-strip **tap on one of the 4 zones** fires the action bound to the encoder under that zone, and **swipe left/right** changes pages (INPUT-05 — PROVISIONAL zone/coordinate map, verified in Phase 25).

**Plans**: 2 plans (2 waves) · **Phase notes**: First `ActionEngine` instantiation. Touch framing provisional; hardware wins (Phase 25). **UI hint**: yes

Plans:

- [x] 15-01-PLAN.md — StreamDockInputService: QTimer poll-pump on the Phase-14 held handle, first ActionEngine instantiation, DeviceEvent->Binding dispatch, 16 ms rotation coalescer, encoder press->synthetic-release, provisional touch X->zone map + swipe page-nav intent; MockTransport-fed dispatch test (INPUT-03/04/05) (wave 1)
- [x] 15-02-PLAN.md — Application wiring: own one QtExecutor-backed ActionEngine (keyPress stub / runCommand QProcess / openUrl QDesktopServices / plugin stub), share the Phase-14 held handle on Stream Deck arrival, wire the ProfileController::activeProfile() accessor + page-nav intent sink (INPUT-03/04/05) (wave 2)

### Phase 16: Device Controls + Binding Persistence + Pages

**Goal**: Brightness slider + "clear all" drive the device live; key/encoder/touch bindings persist and survive restart; multi-page/folder profiles drive host-side page navigation.
**Depends on**: Phase 14 (control service is the live driver + repaint surface) **and** Phase 15 (the `pageNavRequested(±1)` swipe intent + the `ActionEngine` page-state authority this phase reuses). Both 14 and 15 must be EXECUTED first (HARD dependency; `depends_on: [14, 15]`).
**Requirements**: DISPLAY-09, PROFILE-01, PROFILE-02
**Success Criteria**:

1. A brightness slider drives panel brightness live (`LIG`); a "clear all keys" control blanks the device (`CLE`) (DISPLAY-09).
1. Key/encoder/touch bindings (image, label, action chain) round-trip through the profile schema (`deviceCodename` ⇄ `"device"`) and repaint on reload after restart — no longer session-only (PROFILE-01).
1. Multi-page/folder profiles switch pages host-side (page prev/next/goto + touch swipe) via `ActionEngine` `OpenFolder`/`BackToParent` and repaint; no device page opcode is invented (PROFILE-02).

**Plans**: 3 plans (2 waves) · **Phase notes**: Schema doc is the source of truth for JSON wire keys. Reuses Phase 14 paint path. **UI hint**: yes

Plans:

- [x] 16-01-PLAN.md — DISPLAY-09: Q_INVOKABLE setBrightness/clearAll on the control service (LIG/CLE) + QML-expose it + debounced brightness Slider + Clear-all button in the Keys tab (wave 1)
- [x] 16-02-PLAN.md — PROFILE-01: default profile path (AppDataLocation/profiles/<id>.json) + KeyDesigner→Profile commit + Main Apply/Revert real save/load + fresh-controller round-trip (keys/encoders/touch/pages) + repaint-on-load (wave 1)
- [x] 16-03-PLAN.md — PROFILE-02: page-scoped repaintPage(pageId) + pageNavRequested(±1) carousel over top-level pages + repaint via the reused ActionEngine page state; no device page opcode (wave 2, depends 16-01+16-02)

### Phase 17: Plugin Protocol Completion

**Goal**: The existing `SdPluginServer` gains the AJAZZ message surface + auth so any Elgato/Mirabox plugin's full protocol is honored.
**Depends on**: Reuses `src/app/src/sd_plugin_server.{hpp,cpp}` (LocalHost-bound, 13 standard messages already done).
**Requirements**: PLUGIN-01, PLUGIN-02, PLUGIN-03, PLUGIN-04, PLUGIN-05
**Success Criteria**:

1. The WS server stays `LocalHost`-only on a random free port (loopback invariant test-pinned); JSON envelope round-trips every message type (PLUGIN-01/02).
1. The **26 AJAZZ-only actions** are implemented (setBG, sendToDevice, touchbar menu, screensaver reg, setText, setFeedback, lock/unlock, getScreenshot, …) on top of the 13 standard (PLUGIN-03).
1. All host→plugin events are wired incl. encoder `dialDown`/`dialUp`/`dialRotate` (+ legacy `keyDownCord`), `touchTap`, `willAppear`, `deviceDidConnect`, `titleParametersDidChange`, `didReceiveSettings` (PLUGIN-04).
1. `passHello` + salt/challenge auth (`sha256(password+salt)`, reject after N); no TLS by design (PLUGIN-05).

**Plans**: 3 plans (3 waves) · **Phase notes**: Can build early against a MockDevice (no hardware). Land transport/auth and actions as separate slices. **UI hint**: no

Plans:

- [x] 17-01-PLAN.md — PLUGIN-01/02/03: route all 39 plugin→host actions (13 standard + 26 AJAZZ) via actionReceived, re-pin LocalHost-only bind + envelope round-trip, invert the setBG→unhandled test (wave 1)
- [x] 17-02-PLAN.md — PLUGIN-04: sendEvent host→plugin writer + uuid→socket lookup covering §4.4 events incl. dialRotate ticks/pressed/controller (wave 2)
- [x] 17-03-PLAN.md — PLUGIN-05: passHello + random per-connection nested salt/challenge auth (sha256(password+salt)), reject-after-5 socket close, test-only password setter (wave 3)

### Phase 18: Plugin Manifest + Discovery + Lifecycle + Spawn

**Goal**: Plugins are discovered, validated, extracted, and spawned across all three runtimes, with the lifecycle (crash/restart/exitApp) the original app has.
**Depends on**: Phase 17 (protocol the spawned plugins speak).
**Requirements**: PLUGIN-06, PLUGIN-07, PLUGIN-08, PLUGIN-11
**Success Criteria**:

1. Manifest parser accepts Elgato v6 + AJAZZ extensions (`IsK1Pro`, `RunAsAdministrator`, `FSize`/`FFamily`, `Nodejs.Version`, `PUUID`, `Controllers` incl. `"Knob"`/`"SecondaryScreen"`); rejects `OS`/`Software.MinimumVersion` mismatch (PLUGIN-06).
1. Discovery (`defaultPlugins/` + `installedPlugins/`) + `.sdPlugin` extraction (reuse the zip-slip-guarded extractor) + lifecycle: spawn, crash-3×→disable+notify, restart, `exitApp` shutdown (PLUGIN-07).
1. Spawn for **system node ≥20** (detected, not bundled), native exe via `QProcess` (optional elevation), and HTML via `QWebEngineView`+`QWebChannel` (PLUGIN-08).
1. `connectMiraBoxSDSocket` aliased to `connectElgatoStreamDeckSocket` — existing packages load unmodified (PLUGIN-11).

**Plans**: 4 plans (4 waves) · **Phase notes**: No QCefView. Node is system-detected, never bundled. Plans land in reviewable slices (manifest -> node-runner -> mirabox-shim -> manager); the four serialize because they share `tests/unit/CMakeLists.txt` + `src/app/CMakeLists.txt`. **UI hint**: yes

Plans:

- [x] 18-01-PLAN.md - PluginManifest struct + parsePluginManifest + OS/MinimumVersion gate (Linux OS-accept rule); Controllers incl. Knob/SecondaryScreen (PLUGIN-06) (wave 1)
- [x] 18-02-PLAN.md - NodeRunner: exact node argv builder + injectable node>=20 detection, asserted without launching node (PLUGIN-08 node runtime) (wave 2)
- [x] 18-03-PLAN.md - Mirabox compat shim: connectMiraBoxSDSocket -> connectElgatoStreamDeckSocket QWebEngineScript at DocumentCreation (PLUGIN-11) (wave 3)
- [x] 18-04-PLAN.md - PluginCrashTracker (3-in-30s) + PluginManager: discovery + reused zip-slip extraction + spawn dispatch (node/native/HTML) + crash/restart/disable + exitApp shutdown via SdPluginServer::sendEvent (PLUGIN-07/08) (wave 4)

### Phase 19: Device ↔ Plugin Bridge (setImage end-to-end)

**Goal**: A plugin paints a key and a physical press/turn reaches the plugin — the convergence of the device slice and the SDK. First demoable plugin↔device round-trip.
**Depends on**: Phases 14 (control service), 15 (input), 17 (protocol), 18 (spawn).
**Requirements**: PLUGIN-10
**Success Criteria**:

1. `setImage` works end-to-end: strip `data:` URI → `QImage::loadFromData` → scale to per-key dims → JPEG q85 → control-service write queue → physical key; placeholder on decode failure (PLUGIN-10).
1. `actionReceived` (setImage/setTitle/setState/setBG/setFeedback/setText) routes to the device via the control service.
1. Device input (keyDown/keyUp, dialRotate/dialDown/dialUp, touchTap) routes to the registered plugin, with willAppear/deviceDidConnect lifecycle.

**Plans**: 3 plans · **Phase notes**: The integration spine; everything upstream proves out here. **UI hint**: no

Plans:

- [x] 19-01-PLAN.md — STOP-gate dep SUMMARY files + bridge shell, ContextRegistry, pure helpers (data-URI decode, keyIndex↔coords, owner-prefix resolution)
- [x] 19-02-PLAN.md — inbound action routing: setImage decode→Phase-14 paint, context-ownership denial, placeholder; Application wiring + control-spy e2e
- [x] 19-03-PLAN.md — outbound DeviceEvent→§4.4 sendEvent (keyDown/dialRotate/touchTap) + willAppear/deviceDidConnect lifecycle; loopback receives-event e2e

### Phase 20: Property Inspector + Settings

**Goal**: Each action has its settings UI (Property Inspector) and settings persist.
**Depends on**: Phase 18 (HTML/WebEngine spawn surface).
**Requirements**: PLUGIN-09, PLUGIN-13
**Success Criteria**:

1. The per-action Property Inspector renders in `QWebEngineView`+`QWebChannel`; a `cefQuery` polyfill delegates to the bridge; `sendToPlugin`/`sendToPropertyInspector` relay; Elgato `sdpi.css` served from a built-in URL (PLUGIN-09).
1. `get/setSettings` (per-context) and `get/setGlobalSettings` (plugin-wide) persist and survive restart (PLUGIN-13).

**Plans**: 3 plans (3 waves) · **Phase notes**: Reuse-first — the PI stack is pre-built; this phase closes 4 gaps (cefQuery polyfill, sdpi.css, load handshake, restart round-trip test) and reuses the 18-03 QWebEngineScript injection mechanism. **UI hint**: yes

Plans:

- [x] 20-01-PLAN.md — PLUGIN-13: settings + global restart round-trip test + path-traversal refusal; link pi_bridge.cpp into the test binary (wave 1)
- [x] 20-02-PLAN.md — PLUGIN-09: cefQuery polyfill (pi_cef_shim + PIBridge::invoke §8 dispatcher) + bundled sdpi.css served via the interceptor redirect (wave 2)
- [x] 20-03-PLAN.md — PLUGIN-09: registerPropertyInspector load handshake (action-select -> loadInspector -> didReceiveSettings) + cefQuery shim insertion + Inspector.qml trigger; relay endpoints test-pinned, live route deferred to 17/19/25 per STOP gate (wave 3)

### Phase 21: Built-in In-Process Actions

**Goal**: The ~50 in-process built-in UUIDs work without an external plugin.
**Depends on**: Phases 16 (pages/profiles), 19 (bridge).
**Requirements**: PLUGIN-12
**Success Criteria**:

1. Page nav (previous/next/goto/indicator/change), profile nav (openchild/backtoparent/rotate), and `device.brightness` work in-process (PLUGIN-12).
1. `system.hotkey` (**opt-in** global hook — never always-on), `system.multimedia`, `system.volume`, `plain.text`, `browser`/`openUrl`, `multiactions` (+ carousel) work.
1. `obsstudio` integration ships with WebSocket auth **default-on**.

**Plans**: 3 plans (2 waves) · **Phase notes**: Reuses `ActionEngine` kinds; hotkey hook is opt-in (anti-feature avoided); OBS auth default-on. First task = STOP-if-SUMMARY-absent gate for 15/16/19. **UI hint**: yes

Plans:

- [x] 21-01-PLAN.md — STOP-gate 15/16/19 SUMMARY files + pure-core `IInputSynthesizer` (Linux uinput real, Win/mac stubbed-compiling) behind `AJAZZ_FEATURE_INPUT_SYNTH` + opt-in capture gate OFF + fake-backend test (wave 1)
- [x] 21-02-PLAN.md — `ObsClient` obs-websocket v5 (auth default-on, refuse unauthenticated) + the four-request subset + mock-OBS `QWebSocketServer` auth test, gated `AJAZZ_HAVE_WEBSOCKETS` (wave 1)
- [x] 21-03-PLAN.md — pure-core `BuiltinActionRegistry` + app `BuiltinActionsService` (nav/profile/brightness/synthesis/OBS/multiactions+LunBo/browser) + plugin-executor short-circuit + spy/fake e2e test (wave 2, depends 21-01+21-02)

### Phase 22: Plugin Store / Local Install

**Goal**: Users install plugins from local packages through a host-owned catalog, with a signature gate — no phone-home.
**Depends on**: Phase 18 (manifest/discovery).
**Requirements**: PLUGIN-14
**Success Criteria**:

1. A local `.sdPlugin`/`.zip` installs into `installedPlugins/` via the host-owned catalog (`PluginStore.qml` + catalog models), with **no network call** to Mirabox/Aliyun (PLUGIN-14).
1. A signature/manifest-verification gate (reuse `ManifestSignerConfig`) must pass before a plugin is trusted/loaded.

**Plans**: 2 plans (2 waves) · **Phase notes**: Closes the vendor's no-signature gap. ~80% wiring + the honesty gate; verify covers ALL install paths (local + network + launch sweep), phone-home default-OFF + fail-closed are tested mitigations. First task = STOP-if-SUMMARY-absent gate for 18. **UI hint**: yes

Plans:

- [x] 22-01-PLAN.md — STOP-gate 18; un-gate the manifest_signer link/verifier defs (or fail-closed); reusable `verifyStagedPlugin` gate + apply it to the existing network `install()` and launch-sweep paths (wave 1)
- [x] 22-02-PLAN.md — `installFromFile` staging->verify->promote into Phase-18 `installedPlugins/`; kill the launch phone-home (opt-in default-off) + no-network test; PluginStore.qml FileDialog + opt-in online toggle (wave 2)

### Phase 23: Auxiliary Display Surfaces

**Goal**: The encoder overlays, main LCD strip, and touch strip accept assigned images, with DRA partial-zone upload — framing confirmed on hardware.
**Depends on**: Phases 14 and 16 (both extend `StreamDockControlService`; 16 provides the editor assignment UX). HARDWARE-GATED.
**Requirements**: DISPLAY-10
**Success Criteria**:

1. The **per-encoder overlays** (the 4 touch-strip zones — reconcile the in-code `ENC` per-encoder-LCD model against akp05.md's "no separate encoder LCD"), the **main LCD strip**, and the **touch strip** accept assigned images (DISPLAY-10).
1. The `DRA` rect-addressable opcode uploads a single zone without re-encoding the whole 800×480 strip (vendor §10 P0).
1. Per-surface framing matches what the device accepts; where the provisional §5 RE contradicts the hardware, the RE doc is updated (DISPLAY-10).

**Plans**: 2 plans (2 waves) · **Phase notes**: **HARDWARE-GATED** (AKP05E `0300:3004`, fw `V3.AKP05E.01.007`, working `uaccess` ACL; replug/`setfacl` if root-only). App-wiring only — the backend aux methods are byte-tested; do NOT edit `akp05.cpp`. Encoder-overlay framing (ENC-vs-zone) is the key provisional item; both paths wired, neither deleted, reconciled live in Phase 25. **UI hint**: yes

Plans:

- [x] 23-01-PLAN.md — STOP-gate Phase 14 + add assignMainImage/assignEncoderImage/assignTouchStripZone to the control service with MockTransport ENC/MAI/DRA wire tests (DISPLAY-10) (completed 2026-05-24)
- [x] 23-02-PLAN.md — repaintEncodersFromProfile (DRA zone default, ENC fallback) wired into the existing profileChanged path (DISPLAY-10)

### Phase 24: Family Coverage AKP03/153/815

**Goal**: The same assign-image-and-press flow works across the AKP03/153/815 families via the capability-generic control service.
**Depends on**: Phases 14-19 (the AKP05E vertical slice is the template).
**Requirements**: DEVICES-10
**Success Criteria**:

1. On AKP03 (6 keys + 3 encoders), AKP153, and AKP815, assigning a key image shows it and pressing fires the bound action via the same capability-generic service (DEVICES-10).
1. Per-family differences (init sequence, image resolution/rotation/format, encoder count) are table-driven from the descriptor, not hardcoded.

**Plans**: 2 plans · **Phase notes**: Generalisation, not new protocol. AKP153/815 may be bench-gated behind descriptor + `MockTransport` tests if not physically connected. **UI hint**: yes

Plans:

- [x] 24-01-PLAN.md — STOP-if-14/15-SUMMARY-absent gate + add makeAkp03/153/815WithTransport DI overloads (test seam; no wire edits)
- [x] 24-02-PLAN.md — make control + input services descriptor-driven (no AKP05 geometry) + family MockTransport byte test (AKP03 6keys+3enc, AKP153/815 15keys)

### Phase 25: Hardware Verification + Real Plugin

**Goal**: The whole milestone is verified on the physical AKP05E, provisional RE is reconciled, and a real third-party `.sdPlugin` runs live.
**Depends on**: Phases 14-23. HARDWARE-GATED. Gates the milestone close.
**Requirements**: VERIFY-05, VERIFY-06
**Success Criteria**:

1. On the connected AKP05E: an assigned image appears on the key, a key press fires its action, **each encoder fires on rotate (CW/CCW) and press**, a touch-zone tap fires the under-encoder action, swipe changes pages, the brightness slider works, and clear blanks the panel — verified by a human (VERIFY-05).
1. Every provisional §5 wire item (DRA / encoder-overlay framing / touch zone+swipe map) is reconciled and the RE doc updated where hardware contradicts; `hasClock=false` confirmed (no Sync button on the AKP05E row) (VERIFY-05).
1. A real third-party Elgato/Mirabox `.sdPlugin` registers over the loopback WebSocket, paints a key via `setImage`, and receives `keyDown`/`dialRotate` from a physical press/turn with its observable effect (VERIFY-06).

**Plans**: 2 plans · **Phase notes**: **HARDWARE-GATED** (replug/`setfacl` if root-only — systemd ≥258). Verification gate + RE reconciliation; promotes the family `functional`/`verified` honestly. **UI hint**: yes

Plans:

- [x] 25-01-PLAN.md — Author the 25-UAT.md operator runbook + correct akp05e hasClock=false (autonomous)
- [ ] 25-02-PLAN.md — Operator walks VERIFY-05/06 on the AKP05E + reconciles provisional §5 (hardware wins; operator-gated)

### Phase 26: OpenDeck-shaped Device Editor

**Goal**: Replace the generic `KeyDesigner.qml` NxN tile grid with a device-shaped editor that renders the physical geometry of each LCD-key AJAZZ / Mirabox SKU (key grid + optional encoder-dial row + optional touch-strip-zone row), drives drag-drop from an action library onto every drop target, and wires `StreamDockControlService::setActiveDevice` on sidebar selection — closing GAP-25A + GAP-25B from the Phase 25 partial walkthrough so Phase 25 25-UAT.md Tests 1 + 6 convert FAIL/NO_AFFORDANCE → PASS on the live AKP05E.
**Depends on**: Phase 14 (StreamDockControlService — `setActiveDevice`, `assignMainImage`, `assignEncoderImage`, `assignTouchStripZone` wire layer already shipped + tested); Phase 23 (touch-strip zone wire); Phase 25 (UAT runbook + identified GAP-25A/B). UI-only — no protocol or wire-format changes.
**Requirements**: REQ-26-A, REQ-26-B, REQ-26-C, REQ-26-D, REQ-26-E (locked in `26-SPEC.md`; no parent `.planning/REQUIREMENTS.md` IDs — Phase 26 is a follow-up that closes Phase 25 gaps).
**Success Criteria**:

1. `Main.qml:onDeviceSelected` invokes `StreamDockControlService.setActiveDevice(codename)` before assigning `editor.codename`; `m_activeDevice` is non-null after sidebar selection; the next binding edit produces wire writes (BAT header + chunks + ULEND) on hidraw (closes GAP-25A — REQ-26-A).
1. A new `DeviceView.qml` (replacing `KeyDesigner.qml`) renders three stacked rows discriminated by `DeviceDescriptor` geometry: an LCD-key grid (`keyRows × keyColumns`), an optional encoder-dial row (`encoderCount`), and an optional touch-strip-zone row (`touchZoneCount`). AKP05E → 5×2 + 4 + 4; AKP153 → 3×5 + 0 + 0; AKP03 → 2×3 + 3 + 0. `KeyDesigner.qml` deleted atomically with `DeviceView.qml`'s first render (REQ-26-B).
1. `DeviceDescriptor` extended with 4 additive zero-default geometry fields (`keyRows`, `touchZoneCount`, `mainScreenWidthPx`, `mainScreenHeightPx`); every LCD-key SKU descriptor row in `src/devices/streamdeck/src/register.cpp` populated (AKP05/05E family with `touchZoneCount=4`; AKP153 family incl. AKP153R; AKP03 family; Mirabox N3/N3E/N4). AKP815 row stays at `keyRows=0` as a deferred sentinel (REQ-26-C).
1. A Catch2 regression test (`tests/unit/test_streamdeck_register_geometry.cpp` or equiv) iterates every `DeviceDescriptor` with `keyCount > 0` and asserts `keyRows > 0` for LCD-key SKUs minus an explicit AKP815 allow-list; visible in `ctest -N -R Geometry`; passes (REQ-26-D).
1. On the live AKP05E demo unit (`0300:3004`, fw `V3.AKP05E.01.007`), after Phase 26 lands the operator re-walks 25-UAT.md Tests 1 + 6 and records PASS; `25-UAT.md` Summary `passed:` ≥ 5 (REQ-26-E). Drag-drop wire from action library tile → key / encoder / touch-strip-zone drop target → `ProfileController.commitKeyBinding` / `commitEncoderBinding` / `commitTouchZoneBinding` works in an offscreen QML test with a mock `ProfileController`.
1. `ctest --preset linux-release -E qml` ≥ 645 passed, 0 failed (no regressions in the existing suite). AK820/AK980 keyboard and AJ-series mouse SKUs continue routing to their existing specialised panels (`MousePanel`, `RgbPicker`, `SettingsRow`, `FirmwarePanel`) untouched.

**Plans**: 7 plans (5 waves; max 2 concurrent per CLAUDE.md cap). Wave 1: GAP-25A one-liner. Wave 2: descriptor extension + geometry test (parallel) || profile schema v2 + commitTouchZoneBinding. Wave 3: DeviceView + companion components + atomic KeyDesigner deletion. Wave 4: QML offscreen tests (parallel) || per-SKU layout JSONs + photo attribution. Wave 5: operator UAT re-walk (HARDWARE-GATED, autonomous=false). · **Phase notes**: UI-only follow-up to Phase 25 PARTIAL; no protocol changes; hard-replacement atomic commits per CLAUDE.md; layout JSONs at `resources/device-layouts/<codename>.json` (D-06); per PATTERNS.md, photos REUSE existing `resources/devices/products/product-<codename>.png` (not a new `resources/device-photos/` directory) with outline-frame fallback (D-07); `Profile::touchZones` map + schema v2 with auto-migrate (D-11, D-12); AKP815 deferral sentinel `keyRows=0` with allow-list (D-13). **UI hint**: yes (UI-SPEC approved 2026-05-28).

Plans:

**Wave 1**

- [x] 26-01-PLAN.md — REQ-26-A one-liner setActiveDevice wire in Main.qml onDeviceSelected (closes GAP-25A) (wave 1)

**Wave 2** *(blocked on Wave 1 completion)*

- [x] 26-02-PLAN.md — REQ-26-C/D DeviceDescriptor extension (keyRows, touchZoneCount, mainScreenWidth/Height) + populate ~15 LCD-key descriptor rows (AKP815 stays sentinel) + Catch2 geometry regression test (wave 2)
- [x] 26-03-PLAN.md — D-11/D-12 Profile::touchZones map + schema v2 auto-migrate (hand-rolled JSON, COD-031 preserved) + ProfileController::commitTouchZoneBinding Q_INVOKABLE (wave 2)

**Wave 3** *(blocked on Wave 2 completion)*

- [x] 26-04-PLAN.md — REQ-26-B DeviceView.qml + EncoderDial + TouchStripLane + ActionLibraryPane + KeyCell DropArea/Drag extension + ProfileEditor LCD-key branch flip + ATOMIC KeyDesigner.qml deletion (wave 3)

**Wave 4** *(blocked on Wave 3 completion)*

- [x] 26-05-PLAN.md — REQ-26-B offscreen QML tests (geometry 3 SKU classes + drag-drop wire to each commit method); resolves tests/qml link issue (wave 4)
- [x] 26-06-PLAN.md — D-04/05/06/07 per-SKU layout JSONs (AKP05E first; then AKP05/N4, AKP153 family, AKP03 family, Mirabox N3 variants) + attribution README (fair-use) (wave 4)

**Wave 5** *(blocked on Wave 4 completion)*

- [x] 26-07-PLAN.md — REQ-26-E operator-gated re-walk of 25-UAT.md Tests 1 + 6 on live AKP05E demo unit; HARDWARE-GATED, autonomous=false (wave 5)

### Phase 27: Plugin Install/Trust/Persistence Hardening

**Goal**: Make the Stream Dock (`.sdPlugin`) plugin system fully usable, concurrent, and persistent **from the GUI** — closing the five gaps that remain after the 2026-05-30 install/run epic (commits `02bed37` PluginManager wired, `8e985f7` HTML in-process run-path, `0f6b949`/`90d97e2`/`5725cb0` id-keying + strict install). The runtime is already concurrent (Node/native = separate `QProcess`; HTML = per-plugin `QWebEnginePage`; `SdPluginServer` holds `std::vector<PluginConnection> m_connections` — 4 plugins proven concurrent). What is missing is the *install→run→persist* loop from the UI.

**Depends on**: the 2026-05-30 plugin landing (PluginManager live at `application.cpp:799`; HTML path at `plugin_manager.cpp:399-442`; install-from-file at `plugin_catalog_model.cpp:668`; verifier at `plugin_verify_gate.cpp:36-82`; `PluginCrashTracker`). Reuses all of these. **No protocol or wire-format changes** (COD-031 + RE hard rules untouched).

**Requirements**: PLUGIN-15 (rediscover + restart-survival), PLUGIN-16 (verifier unsigned-vs-tampered split + GUI consent + trust UX), PLUGIN-17 (concurrency regression guard). No parent `.planning/REQUIREMENTS.md` IDs — Phase 27 is a reconciliation follow-up that closes the install epic's named TODOs.

**Success Criteria**:

1. `PluginManager::rediscover()` exists and is connected to `PluginCatalogModel::installFinished` (or an equivalent signal) so installing a `.sdPlugin` from the GUI **spawns it live with NO app restart** — `connectedPluginCount()` increments. Verified via the debug channel (`plugin.installFromFile` + `plugin.rediscover` RPC, or installFromFile triggering rediscover directly). Re-scan spawns ONLY newly added plugins; already-live plugins are untouched (PLUGIN-15).
1. `verifyStagedPlugin` splits `VerifyVerdict::Refused` into **Unsigned** (no signature block in manifest) vs **Tampered** (signature present but Ed25519-invalid). Tampered ALWAYS quarantines — even with `userConfirmedUnsigned==true`. Unsigned promotes when `userConfirmedUnsigned==true`. A Catch2 test builds a hostile tampered `.sdPlugin` zip (QZipWriter) and asserts it is refused *even with consent*, while an unsigned zip installs *with consent* (closes CR-01 — PLUGIN-16).
1. An in-app trust UX replaces the env var: a settings toggle ("Allow unsigned plugins") + a per-plugin "Allow this plugin" action in `LoadedPluginsPage.qml` (which today shows only read-only trust chips). The trust decision persists (QSettings). `AJAZZ_ALLOW_UNTRUSTED_PLUGINS` survives only as a CI/dev override (PLUGIN-16).
1. Per-plugin **enabled/disabled** state persists across restart: a user-disabled plugin is NOT spawned by `discover()` on the next launch (today `discover()` spawns every `*.sdPlugin/` unconditionally; `m_disabled` is session-only). Round-trip proven by a hermetic test with `QStandardPaths::setTestModeEnabled` (PLUGIN-15).
1. Concurrency regression test: ≥3 plugins keyed by distinct `.sdPlugin` dir names register concurrently; crashing one (3-in-30s) disables ONLY that plugin and leaves siblings' sockets/processes intact — guards the `5725cb0` shared-key collision and the `WR-02` HTML-no-respawn guard (PLUGIN-17).
1. `ctest --preset linux-release -E qml` ≥ current count passed, 0 failed (no regressions). The Python OOP host (`src/plugins/`, SEC-003) is untouched — it remains the separate AJAZZ-Python ecosystem, not the Stream Dock runtime.

**Plans**: 5 plans (3 waves; max 2 concurrent per CLAUDE.md cap). Mostly HW-free; the only hardware-adjacent item (plugin→device `setImage` via a bound profile action on the live AKP05E) is **explicitly out of scope** — it is Phase 25 live-verification debt (the unmet VERIFY-05/06 witness on the input-blocked `0x3004` demo unit). **Phase notes**: reconciliation-born phase; the install/run path already works for unsigned plugins via the env-var launch-sweep, so this phase is about *GUI parity + persistence + a regression net*, not net-new runtime capability.

Plans:

**Wave 1**

- [x] 27-01-PLAN.md — PLUGIN-16 verifier Unsigned/Tampered split (SignatureState None/Valid/Invalid + VerifyVerdict::Unsigned) + installFromFile CR-01 gate (tampered ALWAYS refuses even with consent) + hostile-zip Catch2 tests (wave 1)

**Wave 2** *(parallel; zero file overlap)*

- [x] 27-02-PLAN.md — PLUGIN-15 PluginManager::rediscover() (idempotent, spawns only new) + installFinished->rediscover wiring + hermetic idempotency test (wave 2)
- [x] 27-05-PLAN.md — PLUGIN-17 concurrency regression test (new tests/unit/test_plugin_concurrency.cpp): one crash disables only itself across 3 distinctly-keyed plugins + WR-02 HTML-no-respawn guard (wave 2)

**Wave 3** *(parallel; zero file overlap; 03 depends 02, 04 depends 01)*

- [x] 27-03-PLAN.md — PLUGIN-15 persisted per-plugin enable/disable (QSettings plugins/disabled/<id> consulted by discover/spawn+rediscover) + setPluginEnabled + restart-survival test (wave 3)
- [x] 27-04-PLAN.md — PLUGIN-16 in-app trust UX: allowUnsignedPlugins setting + per-plugin allowPlugin() (tampered never consentable) + LoadedPluginsPage.qml toggle/allow action + Catch2 gate (wave 3)

### Phase 28: AKP05 Plugin Action Completeness + Drag-to-Bind on Keys & Dials

**Goal**: Installed AKP05 `.sdPlugin` plugins are fully usable from the device editor — **every tool a plugin declares shows in the action library** (correctly grouped, with hidden/internal actions honestly hidden and malformed actions diagnosed rather than silently dropped), and **a tool can be dragged onto a key, an encoder dial, or a touch-strip zone** and actually drives the plugin on the device. Closes the two user-reported defects ("plugins install but don't show all their tools" + "can't drag tools onto the dials"), confirmed against the live System Monitor + Weather plugins via the `AJAZZ_DEBUG_CONTROL` channel.

**Depends on**: Phase 18 (manifest parser + `Controllers:["Knob"]` extension — extended here, not created); Phase 19 (`plugin_device_bridge` controller/coordinates wire mapping — already correct, reused); Phase 26 (DeviceView/DeviceCanvas + KeyCell/EncoderDial/TouchStripLane drop targets + `ProfileController::commit{Key,Encoder,TouchZone}Binding` 6-arg signatures — the drop wiring fixed here); Phase 27 (install/rediscover so a freshly installed plugin's actions are enumerable). UI + plugin-app layer only — no protocol/wire-format changes.

**Requirements**: PLUGIN-18 (action-library completeness + diagnostic), PLUGIN-19 (drag-to-bind plugin actions on keys + dials + touch zones, routably persisted), PLUGIN-20 (controller-affordance normalization + strict gating + full action model). No parent `.planning/REQUIREMENTS.md` IDs — Phase 28 is a usability-completion follow-up closing the Phase 25/26/27 plugin slice.

**Success Criteria**:

1. **Action-library completeness (PLUGIN-18).** For each installed `.sdPlugin`, the action library shows exactly the actions with `VisibleInActionsList != false` from the manifest's `Actions[]` (no fewer, no internal/hidden ones leaking in). `PluginAction`/`plugin_manifest.cpp` parses `VisibleInActionsList` (default true); `installedActions()` filters on it. Actions skipped for empty `UUID`/`Name` (`plugin_catalog_model.cpp:382`) OR whole plugins skipped on manifest-parse failure (`:375`) emit a **visible, countable diagnostic** (log line + a surfaced "N actions skipped" signal), never a silent vanish. Proven LIVE: `scripts/ajazz-debug` shows `installedActions()` count == the manifest's visible-action count for **System Monitor + Weather**.
1. **Drag-to-bind on keys, dials, and touch zones (PLUGIN-19).** Dropping a plugin tile onto a `KeyCell` (already working), an `EncoderDial`, OR a `TouchStripLane` zone persists the plugin `actionId` into the binding. The confirmed regression — `EncoderDial.qml:179` + `TouchStripLane.qml:271` calling `commit*Binding` with 5 args and discarding the 6th `actionId` — is fixed (both forward `ap.actionId`). `core::Action.id` is non-empty for plugin bindings on all three controller types after a drop; persists across restart.
1. **Controller-affordance normalization + STRICT gating (PLUGIN-20).** A single helper maps controller tokens → affordances: `Keypad`/absent → key-capable; `Encoder` (Elgato) **and** `Knob` (AJAZZ) → dial-capable; `SecondaryScreen` → touch-zone-capable. The affordance set rides the drag payload. Drop targets **strictly reject** mismatches (`drag.accepted=false` + reject visual): a `["Knob"]`-only action cannot land on a key; a `["Keypad"]`-only action cannot land on a dial. Unit-tested across Elgato-token, AJAZZ-token, and both-token fixtures.
1. **Full action model (PLUGIN-20).** `PluginAction` additionally parses the `Encoder` block (`TriggerDescription.{Rotate,Push,Touch,LongTouch}`, `layout`, `Icon`), multi-state (`States` count + per-state `Name`/`Title`/`ShowTitle`, the `state` index, `DisableAutomaticStates`), and per-action default `Settings` (seeded into a new binding's `settingsJson`). Covered by Catch2 tests over a fixture manifest exercising single-state Keypad, multi-state toggle, and Knob+Encoder-block actions.
1. **ActionContext registration → real invocation (PLUGIN-19).** On a binding drop (and on profile load) the app registers an `ActionContext` (`pluginUuid`, action `UUID`, controller, row/column) in the bridge's `ContextRegistry` so a device event reaches the plugin — not only via a plugin's own `willAppear` round-trip. Verified **LIVE** via the debug channel: bind a plugin action to a dial, fire synthetic `input.encoder` (CW/press), and confirm the plugin receives `dialRotate`/`dialDown` with `controller:"Encoder"` + correct coordinates (per memory `project_akp05_autonomous_input_test` — no working input hardware needed).
1. **No regressions / boundaries (PLUGIN-18/19/20).** `ctest --preset linux-release -E qml` ≥ current baseline (714), 0 failed. COD-031 clean (`grep -rn nlohmann src/core/include/` = 0). No opcode/`BAT`/`LIG`/hidraw/protocol changes (the device↔plugin bridge is unchanged). Built-in in-process actions (Phase 21) and the Python OOP host (SEC-003) untouched.

**Plans**: TBD (authored by `/gsd:plan-phase 28`). Expected shape: Wave 1 = the two-line `actionId`-drop fix (EncoderDial + TouchStripLane) + a regression test (fastest path to dial binding) ‖ live-diagnose System Monitor + Weather to pin the "missing tools" data drop; Wave 2 = `PluginAction` model extension (VisibleInActionsList + Encoder block + multi-state + Settings) + affordance normalizer + parser unit tests; Wave 3 = `installedActions()` completeness filter + skipped-action diagnostic + library grouping; Wave 4 = strict affordance gating on the three drop targets + ActionContext registration on drop/load; Wave 5 = LIVE debug-channel verification (installedActions count match; synthetic-encoder dialRotate round-trip). **Phase notes**: every new interactive control sets `objectName` (debug-addressability is definition-of-done per CLAUDE.md); verify EVERY change live through the debug channel before claiming done (the Phase-27 "Allow button wired to the wrong list" lesson); known harness gap — `qml.invoke toggle`/`click` doesn't reproduce `onToggled`, so expose `Q_INVOKABLE`s or drive the C++ setter for any new switch.

## Progress

**Execution Order:**
Phases execute in numeric order: 9 → 10 → 11 → 12 → 13. Phases 10, 11, 12 are device-clustered and could in principle fan out subject to the 2-agent concurrent cap, but Phase 10 establishes the template Phases 11/12 reuse — landing Phase 10 first remains the recommended sequencing.

v1.3 phases execute: 14 → 15 → 16 → 17 → 18 → 19 → 20 → 21 → 22 → 23 → 24 → 25 (replanned 2026-05-23). Phase 14 is the load-bearing foundation (persistent open + brightness + the first capability call reaching the device) and MUST land first. The device slice (15, 16) and the plugin SDK transport/protocol/spawn (17, 18) can proceed in parallel against a MockDevice subject to the 2-agent cap; **Phase 19 is the convergence point** (setImage end-to-end + input→plugin) and depends on 14/15/17/18. Phases 20-22 (Property Inspector, built-in actions, store) build on the SDK. Phase 24 (family coverage) lands after the 14-19 AKP05E slice proves the template. **Phases 23 and 25 are HARDWARE-GATED** (AKP05E connected, fw `V3.AKP05E.01.007`); Phase 25 (incl. running a real `.sdPlugin`) gates the milestone close.

| Phase                                               | Milestone | Plans Complete | Status                  | Completed  |
| --------------------------------------------------- | --------- | -------------- | ----------------------- | ---------- |
| 1. SEC-003 Plugin Host                              | v1.0      | 1/1            | Complete (retro)        | 2026-05-03 |
| 2. QML Singleton Sweep                              | v1.0      | 1/1            | Complete (retro)        | 2026-05-04 |
| 3. Architectural Decisions                          | v1.1      | 1/1            | Complete                | 2026-05-14 |
| 4. Hot-plug Hardening                               | v1.1      | 7/7            | Complete                | 2026-05-14 |
| 5. Time-Sync Scaffolding                            | v1.1      | 8/8            | Complete                | 2026-05-14 |
| 6. CR-01 Win32 Env Fix                              | v1.1      | 3/3            | Complete                | 2026-05-14 |
| 7. WR-01 Trust-Roots Parser                         | v1.1      | 3/3            | Complete                | 2026-05-14 |
| 8. Scaffolded-Device Wiring                         | v1.1      | 4/4            | Complete                | 2026-05-14 |
| 9. Research, Captures, Hygiene                      | v1.2      | 6/7            | In Progress             |            |
| 10. AKP05E (0x3004) Promotion                       | v1.2      | 0/?            | Not started             | —          |
| 11. AJAZZ 2.4G 8K Mouse Probe-and-Confirm           | v1.2      | 0/?            | Not started             | —          |
| 12. AK980 PRO Promotion                             | v1.2      | 0/?            | Not started             | —          |
| 13. Catalogue + v1.1 UI Verifies Back-Fill          | v1.2      | 2/2            | Complete                | 2026-05-28 |
| 14. Stream Dock Control Service                     | v1.3      | 2/2            | Complete                | 2026-05-24 |
| 15. Stream Dock Input Routing                       | v1.3      | 2/2            | Complete                | 2026-05-24 |
| 16. Device Controls + Persistence + Pages           | v1.3      | 3/3            | Complete                | 2026-05-24 |
| 17. Plugin Protocol Completion                      | v1.3      | 3/3            | Complete                | 2026-05-24 |
| 18. Plugin Manifest + Spawn + Lifecycle             | v1.3      | 4/4            | Complete                | 2026-05-24 |
| 19. Device ↔ Plugin Bridge (setImage e2e)           | v1.3      | 3/3            | Complete                | 2026-05-24 |
| 20. Property Inspector + Settings                   | v1.3      | 3/3            | Complete                | 2026-05-24 |
| 21. Built-in In-Process Actions                     | v1.3      | 3/3            | Complete                | 2026-05-24 |
| 22. Plugin Store / Local Install                    | v1.3      | 2/2            | Complete                | 2026-05-24 |
| 23. Auxiliary Display Surfaces (HW)                 | v1.3      | 2/2            | Complete                | 2026-05-26 |
| 24. Family Coverage AKP03/153/815                   | v1.3      | 2/2            | Complete                | 2026-05-24 |
| 25. Hardware Verification + Real Plugin(HW)         | v1.3      | 1/2            | In Progress             |            |
| 26. OpenDeck-shaped Device Editor                   | v1.3      | 7/7            | Complete                | 2026-05-28 |
| 27. Plugin Install/Trust/Persistence Hardening      | v1.3      | 5/5            | Verified (human_needed) | 2026-05-31 |
| 28. AKP05 Plugin Action Completeness + Drag-to-Bind | v1.3      | 5/5            | Complete                | 2026-05-31 |
