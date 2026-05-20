# Roadmap: AJAZZ Control Center

## Milestones

- ✅ **v1.0 milestone** — Phases 1-2, retro-fit catalogue (shipped 2026-05-13). See [milestones/v1.0-ROADMAP.md](milestones/v1.0-ROADMAP.md).
- ✅ **v1.1 milestone** — Phases 3-8, device lifecycle hardening + scaffolding-to-functional (shipped 2026-05-14). See [milestones/v1.1-ROADMAP.md](milestones/v1.1-ROADMAP.md).
- 🚧 **v1.2 milestone** — Phases 9-13, Connected-Device Capability Parity (active, bootstrapped 2026-05-15).

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

- [ ] **Phase 9: Research, Captures, Hygiene** — Capture-data-hygiene policy + Wireshark/`usbmon` runbook + per-device sanitised wire-format fixtures + ARCH-04/05/06 ratification. Gates every implementation phase.
- [ ] **Phase 10: AKP05E (0x3004) Promotion** — One-line PacketSize 512→1024 fix (unblocks 13 Stream Dock sibling SKUs) + real `setKeyImage`/encoder/brightness wired to the 0x3004 LCD (10 LCD keys / 4 endless encoders / LCD touch strip) + `clock` honest demotion. `scaffolded` → `functional`.
- [ ] **Phase 11: AJAZZ 2.4G 8K Mouse Probe-and-Confirm** — Zero-OSS-corpus probe-and-confirm session on `3151:5007`; DPI cycle / per-stage / polling-rate / LOD / per-zone RGB; possible factory split if AJ199 Max-fork. `scaffolded` → `partial` or `functional` per capture coverage.
- [ ] **Phase 12: AK980 PRO Promotion** — RGB 20-mode + brightness/speed/direction + sleep-timer + `isWireless` rate-limiter + host-save-vs-device-flash UX separation + `clock` honest demotion. `scaffolded` → `partial`.
- [ ] **Phase 13: Catalogue + v1.1 UI Verifies Back-Fill** — `microdia_dongle_7016` entered at `probed` with topology evidence + ARCH-06 negative ratified + four real-hardware visual verifies from v1.1 (Sync button visibility, Settings auto-sync persistence, glyph-only-no-toast, MaturityRole tooltip).

**Milestone constraints (load-bearing — do not lose these):**

- **Cap concurrent execute agents at 2** in autonomous runs (v1.1 retrospective lesson; three concurrent agents split a Phase 7 atomic commit and forced a Phase 5 planner `--no-verify`).
- **CAPTURE-01 is MUST-FIX-FIRST inside Phase 9** — `.pcap`/`.pcapng` gitignore + pre-commit reject hook + policy doc MUST land before any researcher does their first capture (Pitfall 17 — keystroke recovery from raw captures is deterministic via `tshark` / `USB-Keyboard-Parser`).
- **COD-031 boundary preserved** — no `nlohmann::json` in `ajazz_core` or any installed public header. Capture-extraction tooling is dev-time Python (`scripts/hex-to-cpparray.py`); runtime code reads `std::array<uint8_t>` literals, not parsed JSON.
- **Direct-to-`main` workflow + atomic Conventional Commits.** Pre-commit hooks must pass; `--no-verify` only acceptable when the hook itself is broken and the content verified independently.
- **ASCII-only test names** (Win32 CMD codepage mangling).

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

- [ ] 09-01-PLAN.md — CAPTURE-01 hygiene policy + gitignore + pre-commit hook (MUST-FIRST per D-01)
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

- [ ] 10-01-PLAN.md — DISPLAY-01/02/03: PacketSize 512→1024 + setKeyImage/setKeyColor via image_pipeline + MockTransport wire tests (wave 1)
- [ ] 10-02-PLAN.md — INPUT-01/02 + DEVICES-05: genuine EncoderReleased + 16ms encoder coalescer + devices.yaml clock demotion to functional (wave 2)
- [ ] 10-03-PLAN.md — DISPLAY-04: real-hardware 100-image power-cycle smoke test, AJAZZ_REAL_HARDWARE-gated + operator runbook (wave 3)

**Phase notes**:

- This phase establishes the canonical "device-promotion phase" template (`MockTransport` consumption, `static_assert(!is_base_of_v<QObject, IDisplayCapable>)` mix-in lock, descriptor-parameterisation, three-witness rule for any capability promotion) that Phases 11-12 reuse verbatim.
- Standard research-during-planning patterns; no mid-phase `/gsd-research-phase` expected.

**UI hint**: yes

### Phase 11: AJAZZ 2.4G 8K Mouse Probe-and-Confirm

**Goal**: A user with a `3151:5007` 8K mouse plugged in can cycle through 8 DPI stages (field-determined count — `devices.yaml dpi_stages: 8`, corrected 2026-05-20 from the earlier 6 assumption; cycle ORDER vendor-captured, NOT naive `+1`), set per-stage DPI / colour / LOD independently, set polling rate up to 8000 Hz (with an honest USB 2.0 cap warning), and set per-zone RGB — and the `devices.yaml` row reflects the captured AJ199 V1.0-vs-Max envelope outcome. Maturity `scaffolded` → `partial` or `functional` per capture coverage.
**Depends on**: Phase 9 (AJ199 V1.0-vs-Max envelope reconciliation captured; 8K mouse cmd 0x21/0x22/0x23/0x24/0x30/0x40/0x50 captures + diff doc committed; `MockTransport` available).
**Requirements**: MOUSE-01, MOUSE-02, MOUSE-03, MOUSE-04, MOUSE-05, DEVICES-07
**Success Criteria** (what must be TRUE):

1. `docs/protocols/mouse/aj_series.md` is extended with the first-party-captured AJ199 V1.0-vs-Max envelope reconciliation outcome for `3151:5007`; if the device uses the Max envelope materially divergent from the current `aj_series.cpp` V1.0 assumption, `makeAjSeries` factory splits per Pattern B and a new `makeAjazz24g8k` factory entry is added; if V1.0 holds, the existing factory shape is preserved with the reconciliation documented (MOUSE-01).
1. A user can cycle through 8 DPI stages on `ajazz_24g_8k` in the firmware-captured cycle order (NOT a naive +1 — Pitfall 28 closed); stage state persists across power-cycle by living in device NVM, NOT host-side cache (verified by power-cycling between cycle commands in a real-hardware test) (MOUSE-02).
1. A user can set per-stage DPI value via `IDpiCapable::setStageDpi(int stage, int dpi)` (cmd 0x21), per-stage colour indicator, and Lift-Off-Distance (cmd 0x23) independently for each of the 8 stages; all wire-format assertions against `MockTransport` (MOUSE-03).
1. A user can set polling rate to 1000 / 2000 / 4000 / 8000 Hz via `IPollingRateCapable::setRate` (cmd 0x22); when 8000 Hz is selected and the host port reports USB 2.0, the UI surfaces an honest USB 2.0 warning that effective polling is capped by the USB SOF rate (no lying about throughput — D-02 honesty contract carried from v1.1) (MOUSE-04).
1. A user can set per-zone RGB on `ajazz_24g_8k` (cmd 0x30); zone count + names are derived from the device capability descriptor, NOT hardcoded (Pitfall 22 mitigation) (MOUSE-05).
1. `docs/_data/devices.yaml` row for `ajazz_24g_8k` updates `notes:` with the first-party-captured wire-format reconciliation result; maturity promotes `scaffolded` → `partial` if any advertised capability is uncaptured, `scaffolded` → `functional` only if all `[dpi, rgb]` capabilities pass the three-witness rule (capture + observable state change + negative test) (DEVICES-07; Pitfall 29 honesty contract).

**Plans**: TBD
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

**Plans**: TBD
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

## Progress

**Execution Order:**
Phases execute in numeric order: 9 → 10 → 11 → 12 → 13. Phases 10, 11, 12 are device-clustered and could in principle fan out subject to the 2-agent concurrent cap, but Phase 10 establishes the template Phases 11/12 reuse — landing Phase 10 first remains the recommended sequencing.

| Phase                                      | Milestone | Plans Complete | Status           | Completed  |
| ------------------------------------------ | --------- | -------------- | ---------------- | ---------- |
| 1. SEC-003 Plugin Host                     | v1.0      | 1/1            | Complete (retro) | 2026-05-03 |
| 2. QML Singleton Sweep                     | v1.0      | 1/1            | Complete (retro) | 2026-05-04 |
| 3. Architectural Decisions                 | v1.1      | 1/1            | Complete         | 2026-05-14 |
| 4. Hot-plug Hardening                      | v1.1      | 7/7            | Complete         | 2026-05-14 |
| 5. Time-Sync Scaffolding                   | v1.1      | 8/8            | Complete         | 2026-05-14 |
| 6. CR-01 Win32 Env Fix                     | v1.1      | 3/3            | Complete         | 2026-05-14 |
| 7. WR-01 Trust-Roots Parser                | v1.1      | 3/3            | Complete         | 2026-05-14 |
| 8. Scaffolded-Device Wiring                | v1.1      | 4/4            | Complete         | 2026-05-14 |
| 9. Research, Captures, Hygiene             | v1.2      | 6/7            | In Progress      |            |
| 10. AKP05E (0x3004) Promotion              | v1.2      | 0/?            | Not started      | —          |
| 11. AJAZZ 2.4G 8K Mouse Probe-and-Confirm  | v1.2      | 0/?            | Not started      | —          |
| 12. AK980 PRO Promotion                    | v1.2      | 0/?            | Not started      | —          |
| 13. Catalogue + v1.1 UI Verifies Back-Fill | v1.2      | 0/?            | Not started      | —          |
