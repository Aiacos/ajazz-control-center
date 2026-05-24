---
gsd_state_version: 1.0
milestone: v1.3
milestone_name: Stream Dock End-to-End / Elgato-compatible Plugin SDK
status: executing
stopped_at: 'P3.6 AK980 CMD_FINISH 0xF0 landed (issue #58 closed); audit-3 user-visible feature stack complete on `main`; STATE/HANDOFF/README updated; PR #56 (dependabot rebase) requested; Phase 9.x captures + remaining P3.x patches still pending'
last_updated: '2026-05-24T12:19:41.211Z'
last_activity: 2026-05-24
progress:
  total_phases: 17
  completed_phases: 5
  total_plans: 51
  completed_plans: 20
  percent: 29
---

# Project State

## Project Reference

See: .planning/PROJECT.md (updated 2026-05-15)

**Core value:** Honest, capability-driven control of AJAZZ hardware with a sandboxed plugin system — never lying about what a device can do, never crashing when a device is yanked, never silently leaking host state into plugin children.
**Current focus:** Phase 18 — plugin-manifest-discovery-lifecycle-spawn

## Current Position

Phase: 18 (plugin-manifest-discovery-lifecycle-spawn) — EXECUTING
Plan: 4 of 4
Plan counts: 14(2) 15(2) 16(3) 17(3) 18(4) 19(3) 20(3) 21(3) 22(2) 23(2) 24(2) 25(2) = 31 plans across 12 phases. Each has CONTEXT+RESEARCH+VALIDATION+PLAN committed.
Plan-checker: ran on Phases 14-22 (all PASS; 17-01 revised once for a 39-vs-41 routed-action BLOCKER, then PASS). Phases 23-25 plans authored + self-audited but the standalone plan-checker was deferred (budget) — recommend `/gsd-plan-phase 23 --reviews`-style check or a checker pass before executing those three.
Status: Ready to execute
Branch: feat/streamdock (off develop) — ~40 planning commits unpushed (operator pushes per workflow)
Last activity: 2026-05-24

### Execution dependency map (for the operator)

- 14 (foundation) → 15, 16, 23 build on it. 17 (independent, MockDevice) → 18. **19 = convergence** (depends 14,15,17,18). 20 (PI) ← 18. 21 (built-ins) ← 15,16,19. 22 (store) ← 18. 24 (family) ← 14,15,16,19. 25 (HW verify) ← whole slice.
- Every dependent phase's FIRST task is a STOP-if-SUMMARY-absent gate, so out-of-order execution halts safely.
- HARDWARE-GATED (need the physical AKP05E + uaccess): Phase 23 (aux-surface live framing) + Phase 25 (full UAT + real `.sdPlugin`). Everything else lands hardware-free.

**Phase 14 execution prerequisites (for the operator):** the build needs Qt6 private headers (`qt6-qtbase-private-devel` on Fedora — Phase-10 review flagged CorePrivate missing). System-package install is the operator's action (project hard rule: no system-level mutations from tooling). Verification is hardware-free (MockTransport + `makeAkp05WithTransport`); the live power-cycle smoke is deferred to Phase 25.

### v1.3 replan decisions (locked 2026-05-23)

- **Scope = Full Elgato SDK, 1:1** — WebSocket plugin server, manifest schema, Property Inspector, 13 Elgato + 26 AJAZZ messages, node/native/HTML plugin spawn, plugin store. Source of truth: `docs/protocols/streamdeck/akp_plugin_sdk.md` + `akp05_vendor.md` + `akp05_init_sequence.md` + `akp05.md`.
- **Runtime = Elgato-compatible WebSocket** — extend the existing `SdPluginServer` (`src/app/src/`, already LocalHost-bound + 13 standard messages). The Python OOP host (`src/plugins/`) stays as-is (SEC-003); it is NOT the Stream Dock plugin runtime. Do NOT create a `src/host/plugin-host/` module.
- **Structure = replan v1.3 from scratch** — Phases 14-25 supersede the prior Phases 14-19. Reuse-first: `SdPluginServer`, `ActionEngine` (folder nav), `Profile`/`ProfilePage`, the `.sdPlugin` extractor, `image_pipeline`.
- **Device facts (verified):** AKP05E (`0300:3004`, fw `V3.AKP05E.01.007`) = 10 LCD keys (2×5) + **4 endless pressable rotary encoders** + touch strip; encoders emit press-only → synthesise release; per-encoder graphics are touch-strip zones (provisional vs in-code ENC-LCD model — Phase 25). `register.cpp` currently advertises `hasClock=true` for `akp05e` → must be `false` (DEVICES-11, Phase 14).
- **Anti-features NOT replicated:** bind `Any`, unsigned plugins, phone-home, plaintext OBS, always-on global hook, bundled node.
- Convergence at **Phase 19** (setImage e2e + input→plugin). HARDWARE-GATED: Phases 23, 25.

## Performance Metrics

**Velocity (v1.0 + v1.1 carried forward):**

- Total plans completed: 37 (1 retro + 26 forward-planned in v1.1)
- v1.1 calendar duration: ~2 days end-to-end, ~80 commits

**v1.2 baseline:** Counters reset to 0/5 phases, 0/? plans (plan counts TBD per phase).

**Recent Trend:**

- v1.1 sustained ≥6 plans/day with 178/178 ctest pass.
- Cap concurrent execute agents at 2 in autonomous runs (v1.1 retrospective lesson).

*Updated after each plan completion.*

## Accumulated Context

### Decisions

See PROJECT.md Key Decisions table for the full log (v1.0 + v1.1 entries with outcomes).

Phase 9 will ratify three new written ADRs:

- **ARCH-04**: AKP03 image-encoding pipeline location (recommended: Qt6 `QImage::scaled(SmoothTransformation)` + `QImageWriter` JPEG host-side in `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`, PRIVATE-linked).
- **ARCH-05**: per-device `setTime` outcome (default verdict: NO RTC opcode in any AJAZZ corpus → `hasClock=false` on `akp05e` and `ak980pro`; `setTime` stays `NotImplemented`).
- **ARCH-06**: composite-HID dedup (default verdict: NOT firing — topology proves `0c45:7016` is a separate dongle on a different bus branch).
- \[Phase 9\]: ARCH-04 default verdict ratified at `.planning/phases/09-research-captures-hygiene/ARCH-04.md` — AKP03 image-pipeline at `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` (Option C), PRIVATE-linked to `ajazz_devices_streamdeck`; Option B (new `ajazz_imaging` static lib) deferred to v1.3+; D-05 honesty contract preserved (status: DEFAULT VERDICT — PENDING CAPTURE CONFIRMATION) — Phase 10 gates on Phase 9.x captures-confirmation run (Pitfall 22). [commit: 60f3140]
- \[Phase 9\]: Phase 10/11/12 reuse pattern: makeAjSeriesWithTransport public factory overload exposes anonymous-namespace COD-026 DI ctor across TU boundaries (CAPTURE-04)
- \[Phase 9\]: MockTransport is header-only under tests/unit/fixtures/ in the ajazz::tests:: namespace; static_asserts lock rule-of-five contract inherited from ITransport (CAPTURE-04)
- \[Phase 9\]: ARCH-05 default verdict ratified at .planning/phases/09-research-captures-hygiene/ARCH-05.md - per-device IClockCapable::setTime outcome: hasClock=false on akp05e and ak980pro; setTime stays NotImplemented; PROJECT.md Out-of-Scope row preserved; Pitfall 19 three-witness rule STRUCTURALLY unsatisfiable for clock on AKP03 + ak980pro; anti-feature forbidden: synthesizing fake setSystemTimeOn from bytes that look like time; acceptable alternative: host-rendered TftClockWidget via display capability (DISPLAY-05, v1.2.x); D-05 honesty contract preserved (status: DEFAULT VERDICT - PENDING CAPTURE CONFIRMATION); Phase 10 DEVICES-05 + Phase 12 DEVICES-06 + Phase 13 VERIFY-01/03 bind to this ADR; gate on Phase 9.x finalization run. [commit: 5410c2a] — Four-corpus convergence (mirajazz + opendeck-akp03 + ajazz-sdk + TaxMachine AK820 Pro) shows NO RTC opcode in any AJAZZ reference corpus. Pitfall 19 three-witness rule applied: round-trip witness STRUCTURALLY unavailable on AKP03 (no firmware-rendered LCD clock widget per docs/protocols/streamdeck/akp03.md:113-114) and on ak980pro (TFT clock is host-pushed image via cmd 0x72, not firmware time). Two of three witnesses unavailable; even positive capture witness alone cannot satisfy promotion. v1.1 D-02 honesty contract reinforced - no lying success UX on setTime returning Ok when device cannot.
- \[Phase 9\]: ARCH-06 default verdict ratified at .planning/phases/09-research-captures-hygiene/ARCH-06.md — composite-HID dedup NOT firing in DeviceRegistry::enumerate (topology evidence from live lsusb 2026-05-15 refutes the composite hypothesis at the USB devicefs layer); 0c45:7016 enters Phase 13 DEVICES-08 as separate microdia_dongle_7016 at probed tier; v1.1 ARCH-02 (vid, pid, serial) keying preserved unchanged. D-05 honesty contract preserved (status: DEFAULT VERDICT — PENDING CAPTURE CONFIRMATION). Captures-confirmation trigger is a 2-minute physical unplug test (no capture tooling required). CONDITIONAL: if test contradicts, new Phase 12.5 lands dedup BEFORE Phase 12 and Phase 13 re-sequences (LOW probability). [commit: 4619bb8]
- \[Phase ?\]: DEVICES-11 scope: akp05e only; akp05/mirabox_n4 hasClock flip deferred to a later honesty sweep
- \[Phase ?\]: ARCH-03 flyweight handle share: DeviceRegistry::open(same devId) post-setActiveDevice returns same shared_ptr for input service
- \[Phase ?\]: QtExecutor non-owning shared_ptr wrapper pattern: unique_ptr owned by Application, non-owning shared_ptr alias passed to ActionEngine (no-op deleter, lifetime guaranteed by Application)
- \[Phase ?\]: StreamDockInputService owns ActionEngine: Application constructs then moves the engine into the service; m_actionEngine null after ctor; Phase 19 accesses engine via m_streamDockInput
- \[Phase ?\]: Departure handler added to onHotplug for StreamDeck: setActiveDevice(nullptr) stops input poll pump on device removal (T-15-05 UAF mitigation)
- \[Phase ?\]: Linux OS-accept locked (18-01)
- \[Phase ?\]: argv contract: codePath is argv[0], node binary NOT in list (PLUGIN-08/Pitfall3)
- \[Phase ?\]: NodeProbe struct defaults uninitialised; makeDefaultNodeProbe() wires QStandardPaths+QProcess; tests inject fakes directly

### Pending Todos

**Phase 9.x follow-up (user-driven, gates Phase 10 start):**

1. Install Wireshark + usbmon prereqs on dev box per `docs/protocols/CAPTURING.md`:
   - `sudo dnf install wireshark tshark` (or `sudo apt install wireshark tshark dumpcap`)
   - `sudo modprobe usbmon`
   - Optional: add user to `wireshark` group for unprivileged capture (`sudo usermod -aG wireshark $USER`)
1. Produce sanitised captures for all 4 connected devices following the CAPTURING.md runbook (CAPTURE-05):
   - `akp05e` (`0300:3004`, AKP05E Stream Dock Plus — 10 LCD keys / 4 endless encoders / LCD touch strip / protocol_version 3 / firmware "V3.AKP05E.01.007", routed via makeAkp05): image upload (first + last chunk), `CLE`, `LIG`, encoder rotate/press, `HAN`, negative-test for hypothetical `TIM` opcode
   - `ak980pro` (`0c45:8009`): 20 RGB modes (cmd 0x13), sleep-timer (cmd 0x17), TFT chunked send (close TaxMachine TODO), `lsusb -v -d 0c45:8009` HID descriptor dump
   - `ajazz_24g_8k` (`3151:5007`): DPI cycle, polling-rate dropdown, LOD, button bind, per-zone RGB, battery, flash commit; AJ199 V1.0 vs Max probe
   - `microdia_dongle_7016` (`0c45:7016`): `lsusb -v`, `udevadm info -a /dev/hidraw{5,6}`, paired-input identification via `evtest`
1. Run `scripts/hex-to-cpparray.py` per device to produce `tests/integration/fixtures/<codename>_*.h` headers + SHA-256 metadata in `.planning/research/captures/INDEX.md` (CAPTURE-06).
1. Produce per-device wire-format diff docs:
   - Extend `docs/protocols/streamdeck/akp03.md` with `0300:3004` first-party findings
   - Create `docs/protocols/keyboard/ak980pro.md` if diverges from `proprietary.md`
   - Create `docs/protocols/mouse/ajazz_24g_8k.md` if diverges from `aj_series.md`
1. Run the 2-minute physical unplug test for ARCH-06 finalization: unplug `ak980pro`, observe whether `0c45:7016` disappears simultaneously. Default verdict expects NO simultaneous disappearance (separate dongle confirmed).
1. Finalize ARCH-04 / ARCH-05 / ARCH-06 — flip from "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" to "FINAL" if captures confirm, or amend if they contradict. Update ADR Status section + PROJECT.md Key Decisions outcome column.

After all 6 items land, re-run `/gsd-plan-phase 9` or invoke a `Phase 9.x` plan-only run to close the deferred plans (CAPTURE-05, CAPTURE-06, ARCH-04/05/06 finalization). Then Phase 10 can start.

**v1.1 carry-overs (inherited):** VERIFY-01..04 (Phase 13) closes the real-hardware UI back-fills (Phase 5 Sync button visibility, Settings auto-sync persistence, glyph behavior; Phase 8 MaturityRole tooltip).

### Blockers/Concerns

- **CAPTURE-01 is MUST-FIX-FIRST inside Phase 9**: capture-data-hygiene policy + `.pcap`/`.pcapng` gitignore + pre-commit reject hook MUST land before any researcher does their first capture (Pitfall 17 — keystroke recovery from raw `.pcap` is deterministic via `tshark` / `USB-Keyboard-Parser`).
- **Phase 11 (8K mouse) mid-phase research flag**: zero 3rd-party OSS corpus exists for `3151:5007`; if Phase 9 captures reveal AJ199 V1.0 vs Max envelope diverges materially, invoke `/gsd-research-phase` on the SONiX 3151 chipset family before committing to a factory split.
- **Phase 12 (AK980 PRO) mid-phase research flag**: if Phase 9 captures reveal TFT cmd 0x72 / per-key RGB / macros / layers materially divergent from the TaxMachine baseline, invoke `/gsd-research-phase` on the Microdia 0c45 chipset family.
- **Concurrent agent cap**: 2 max in autonomous runs. Three concurrent agents created a git race during v1.1 (split Phase 7 atomic commit + forced Phase 5 `--no-verify`).
- **COD-031 boundary**: no `nlohmann::json` in `ajazz_core` or any installed public header. Capture-extraction tooling is dev-time Python; runtime code reads `std::array<uint8_t>` literals.

## Deferred Items

| Category                                               | Item                                                                          | Status                            | Deferred At              |
| ------------------------------------------------------ | ----------------------------------------------------------------------------- | --------------------------------- | ------------------------ |
| v1.3+ (KEYBOARD)                                       | AK980 PRO per-key custom RGB / macros / layers / battery (KEYBOARD-05..08)    | Pending captures                  | v1.2 milestone-bootstrap |
| v1.2.x (DISPLAY)                                       | AK980 PRO 1.14" TFT chunked image upload (DISPLAY-05; cmd 0x72)               | Capture in Phase 9; impl deferred | v1.2 milestone-bootstrap |
| v1.2.x / v1.3                                          | AKP815 + Mirabox N3 promotion (devices not physically connected)              | Blocked on captures               | v1.1 close               |
| v1.2.x                                                 | Explicit `Toast.qml` cap=1 implementation (A-05)                              | Carried                           | v1.1 close               |
| v1.2.x                                                 | TimeSyncService Pitfall-13 contextual INFO message                            | Carried                           | v1.1 close               |
| v1.2.x                                                 | Codename→maturity map → Qt resource + runtime YAML parse (if catalogue grows) | Carried                           | v1.1 close               |
| v1.2.x                                                 | libFuzzer Fedora packaging once `libclang_rt.fuzzer.a` lands                  | Upstream                          | v1.1 close               |
| Phase 9 P03                                            | 6min                                                                          | 2 tasks                           | 2 files                  |
| Phase 9 P04                                            | 4min                                                                          | 3 tasks                           | 5 files                  |
| Phase 9 P05                                            | 3min                                                                          | 1 tasks                           | 2 files                  |
| Phase 9 P06                                            | 3min                                                                          | 1 tasks                           | 2 files                  |
| Phase 9 P07                                            | 7min                                                                          | 1 tasks                           | 6 files                  |
| Phase 14 P14-01                                        | 8                                                                             | 2 tasks                           | 6 files                  |
| Phase 14-stream-dock-control-service P02               | 90                                                                            | 3 tasks                           | 9 files                  |
| Phase 15-stream-dock-input-routing P02                 | 8                                                                             | 1 tasks                           | 3 files                  |
| Phase 17-plugin-protocol-completion P01                | 5                                                                             | 2 tasks                           | 3 files                  |
| Phase 17 P03                                           | 5                                                                             | 3 tasks                           | 3 files                  |
| Phase 18 P01                                           | 8                                                                             | 2 tasks                           | 9 files                  |
| Phase 18-plugin-manifest-discovery-lifecycle-spawn P02 | 4                                                                             | 2 tasks                           | 5 files                  |

## Session Continuity

Last session: 2026-05-24T12:19:41.203Z
Stopped at: P3.6 AK980 CMD_FINISH 0xF0 landed (issue #58 closed); audit-3 user-visible feature stack complete on `main`; STATE/HANDOFF/README updated; PR #56 (dependabot rebase) requested; Phase 9.x captures + remaining P3.x patches still pending
Resume file: None

## 2026-05-17 mid-milestone amendment update

Two atomic commits landed ahead of Phase 10 schedule (autonomous research + execute run, 2026-05-17):

- **acc239e** `feat(streamdeck): implement ARCH-04 host-side image pipeline for AKP05` — Qt6 QImage + QImageWriter pipeline at `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` (Option C from ARCH-04 default verdict). AKP05 backend now does RGBA8 → resize → JPEG host-side per IDisplayCapable contract; setKeyColor no longer falls back to clearKey. 10 new unit tests (32 assertions). Phase 10 DEVICES-05 prerequisite ARCH-04 implementation now in place; per-byte JPEG quality tuning still pending Phase 9.x AKP05E 0x3004 capture confirmation (Pitfall 22).

- **9787962** `feat(keyboard): implement AK980 PRO firmware RTC setTime (ARCH-05.1)` — partial flip of ARCH-05 default verdict. Two independent corpora (gohv/EPOMAKER-Ajazz-AK820-Pro + KyleBoyer/TFTTimeSync-node, both targeting Sonix SN32F299 family at VID:PID 0x0c45:0x8009) document a host-settable firmware RTC at opcode 0x28. ProprietaryKeyboard::setTime() now writes the 3-packet (preamble + data + save) envelope. 6 new [clock]-tagged unit tests (203 assertions) pin byte-precise layout. ak980pro.maturity promoted scaffolded → partial. Phase 9.x physical round-trip witness (TFT clock widget shows the time we sent) gates partial → functional promotion. ARCH-05 stands for Stream Dock family (AKP03/AKP05/Mirabox N3/N4) — Companion streamdock.ts audit confirms zero time opcodes there; clock widget on AKP05 main LCD strip is a v1.2.x deferred host-rendered TftClockWidget via the new image_pipeline.

ARCH-05.1 ADR: `.planning/phases/09-research-captures-hygiene/ARCH-05.1.md`.

## Operator Next Steps

**Phase 9 PARTIAL-SCOPE COMPLETE (2026-05-15).** Phase 9.x follow-up gates Phase 10:

1. Read `### Pending Todos` above for the 6-step Phase 9.x follow-up checklist.
1. Install Wireshark + usbmon prereqs (`docs/protocols/CAPTURING.md`).
1. Produce captures for the 4 connected devices + run `scripts/hex-to-cpparray.py`.
1. Run the ARCH-06 2-minute physical unplug test.
1. Re-run `/gsd-plan-phase 9` (or invoke a focused Phase 9.x run) to land CAPTURE-05, CAPTURE-06, and ARCH-04/05/06 finalization.
1. Once Phase 9.x ships, `/gsd-plan-phase 10` to start AKP05E 0x3004 promotion (canonical device-promotion template; one-line `PacketSize 512 → 1024` fix unblocks 13 sibling SKUs).
