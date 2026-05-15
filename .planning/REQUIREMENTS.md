# Requirements: AJAZZ Control Center

**Defined:** 2026-05-15
**Milestone:** v1.2 — Connected-Device Capability Parity
**Core Value:** Honest, capability-driven control of AJAZZ hardware with a sandboxed plugin system — never lying about what a device can do, never crashing when a device is yanked, never silently leaking host state into plugin children.

## v1.2 Requirements

Requirements for this milestone. Each maps to one roadmap phase (Phases 9-13). Numbering continues category-by-category from v1.0+v1.1 (ARCH-04 follows ARCH-03, DEVICES-05 follows DEVICES-04); new categories (CAPTURE, DISPLAY, INPUT, KEYBOARD, MOUSE, VERIFY) start at `-01`.

### Architectural Decisions

Phase 9 deliverables — written rationale gating Phases 10-12. Captures-driven; final wording set in Phase 9 once first-party captures land.

- [x] **ARCH-04**: Project records the AKP03 image-encoding pipeline location (recommended: Qt6 `QImage::scaled(SmoothTransformation)` + `QImageWriter` JPEG host-side in `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}`, PRIVATE-linked to `ajazz_devices_streamdeck`; alternative: new `ajazz_imaging` static lib deferred to v1.3+ AKP815). Decision binds Phase 10.
- [x] **ARCH-05**: Project records the `IClockCapable::setTime` wire-format outcome **per connected device**, based on Phase 9 captures. Default verdict (NO RTC opcode in any AJAZZ corpus): `hasClock=false` on `akp03_variant_3004` and `ak980pro`; `setTime` remains `NotImplemented`; PROJECT.md "Out of Scope" row preserved. Decision is binary per device; captures determine outcome.
- [x] **ARCH-06**: Project records the composite-HID dedup decision for `DeviceRegistry`. Default verdict (USB topology proves `0c45:7016` is a separate dongle on a different bus branch): **NOT firing** — `microdia_dongle_7016` enters catalogue as a separate `probed`-tier device with no dedup logic added. If Phase 9 captures contradict, decision flips and dedup infrastructure lands before Phase 12.

### Capture Infrastructure

Phase 9 deliverables — gates every subsequent implementation phase. Privacy-first capture-hygiene policy is the milestone-blocking deliverable.

- [ ] **CAPTURE-01**: Repo enforces a capture-data-hygiene policy — `docs/policies/capture-data-hygiene.md` defines the policy, `.planning/research/captures/.gitignore` excludes `*.pcap` / `*.pcapng`, and a pre-commit hook rejects any commit attempting to add raw capture files. Must land before any researcher does a first capture (Pitfall 17 — keystroke recovery from raw `.pcap` is deterministic).
- [ ] **CAPTURE-02**: `docs/protocols/CAPTURING.md` documents the Wireshark + `usbmon` + `dumpcap` runbook (Linux dev box prereq install, `usbmon` kernel-module mount, per-device capture filter `usb.idVendor == 0xVVVV && usb.idProduct == 0xPPPP`, USB-side-only event windowing).
- [x] **CAPTURE-03**: `scripts/hex-to-cpparray.py` (≤30 LoC dev-time helper) extracts sanitized HID payload bytes from `usbrply`-decoded captures into `std::array<uint8_t, N>` C++ literals under `tests/integration/fixtures/<codename>_*.h`. No `libpcap` / `PcapPlusPlus` link-time dependency added.
- [x] **CAPTURE-04**: `tests/unit/fixtures/mock_transport.hpp` (NEW, ~80 LoC, header-only) implements a `MockTransport` exposing `write(span<const uint8_t>) → vector<vector<uint8_t>> writes()` for byte-level wire-format assertion. Backends accept it via the existing COD-026 DI constructor — no architectural change.
- [ ] **CAPTURE-05**: Phase 9 produces sanitized capture fixtures for all 4 connected devices, SHA-256-indexed in a metadata blob committed to `.planning/research/captures/INDEX.md`; raw `.pcap` files stay out-of-tree per CAPTURE-01.
- [ ] **CAPTURE-06**: Phase 9 produces per-device wire-format diff documents extending `docs/protocols/streamdeck/akp03.md` (for `0300:3004`) and creating `docs/protocols/keyboard/ak980pro.md` + `docs/protocols/mouse/ajazz_24g_8k.md` if findings diverge materially from the existing AKP03 / proprietary / aj_series protocol docs.

### AKP03 Display Capability

Phase 10 deliverables — `akp03_variant_3004` (`0300:3004`) `scaffolded` → `functional` for `display` capability.

- [ ] **DISPLAY-01**: `src/devices/streamdeck/include/.../akp03_protocol.hpp` PacketSize migrates from `512` → `1024` for AKP03 protocol v2/v3 framing. One-line load-bearing fix; simultaneously unblocks 13 sibling SKUs across the AKP03 family (verified via three-way OSS corpus agreement: mirajazz + opendeck-akp03 + ajazz-sdk).
- [ ] **DISPLAY-02**: User can push a `QImage` to any LCD key on `akp03_variant_3004` via `IDisplayCapable::setKeyImage(int keyIndex, QImage)`. Implementation chunked-uploads 1024-byte packets via `BAT` opcode with last-chunk Transfer-Done flag `= 0x01`; image preprocessing (60×60 JPEG `Rot0`) lives in `image_pipeline.{hpp,cpp}` per ARCH-04. Per-variant rotation/resolution table-driven (AKP03R rev. 2 lineage uses 64×64 `Rot90`).
- [ ] **DISPLAY-03**: User can clear (`CLE` opcode), set per-key color via short-circuit when image is a solid fill (`setKeyColor`), set global LCD brightness via `LIG` opcode, and explicitly flush pending writes (`flush`). All four methods backed by `MockTransport` byte-level wire-format tests.
- [ ] **DISPLAY-04**: Real-hardware 100-image power-cycle smoke test exists in `tests/integration/` (manual, gated behind `AJAZZ_REAL_HARDWARE` ctest filter): chunked uploads do not hang the device across 100 cycles (Pitfall 18 promotion gate).

### AKP03 Input Capability

Phase 10 deliverables — encoder event correctness.

- [ ] **INPUT-01**: `IDevice` core event API gains `EncoderReleased(int encoderIndex)` proper event, replacing the `value = 0` half-step workaround at `src/devices/streamdeck/src/akp03.cpp:289-293`. Existing `EncoderRotated(int encoderIndex, int delta)` semantics preserved; delta is post-quadrature-decode (not raw quadrature pairs — Pitfall 23).
- [ ] **INPUT-02**: Encoder rotation events are coalesced at the QML observer layer with a 16ms `QTimer` to avoid signal-storm repaints during fast spin (single signal per repaint frame; total delta accumulated and emitted once).

### AK980 PRO Keyboard Configuration

Phase 12 deliverables — `ak980pro` (`0c45:8009`) `scaffolded` → `partial`. Per-key RGB / macros / layers / battery deferred to v1.3+ pending captures.

- [ ] **KEYBOARD-01**: User can select one of 20 AK980 PRO RGB lighting modes via `IRgbCapable::setMode` on `ak980pro`; implementation uses cmd `0x13` with 64-byte Report ID `0x04` three-stage `START` (`0x18`) → cmd → `FINISH` (`0xf0`) per the TaxMachine AK820 Pro clean-room corpus. Mode list matches device firmware (table-driven from capture, NOT hardcoded).
- [ ] **KEYBOARD-02**: User can set RGB brightness (0..5 scale — NOT 0..100, vendor-specific), speed (0..5), and direction (0..3) via `IRgbCapable` extensions. UI scale-mapping shields users from the raw 0..5 range; QML range derived from device-reported capability descriptor (NOT hardcoded — Pitfall 22 mitigation).
- [ ] **KEYBOARD-03**: User can set keyboard sleep-timer (idle-minutes-to-OLED-off) via cmd `0x17`; UI exposes a discrete picker (1 / 5 / 10 / 30 min); persistence verified across power-cycle.
- [ ] **KEYBOARD-04**: `ak980pro` device record carries `isWireless = true`; `ProprietaryKeyboard::writeRgb` enforces a `≤10 writes/sec` rate-limit when `isWireless` is true (Pitfall 24 — wireless dongle queue overflow can stall keystrokes during RGB transitions). Rate-limiter is opt-in per-device, NOT a global throttle.

### AJAZZ 2.4G 8K Mouse

Phase 11 deliverables — `ajazz_24g_8k` (`3151:5007`) `scaffolded` → `partial` or `functional` (gated on Phase 9 capture coverage; promotion-to-`functional` requires three-witness rule on every capability).

- [ ] **MOUSE-01**: Phase 9 captures confirm whether `ajazz_24g_8k` uses the AJ199 V1.0 (current `aj_series.cpp` backend assumption) or AJ199 Max envelope; outcome documented in `docs/protocols/mouse/aj_series.md` extension. If Max-fork: `makeAjSeries` factory splits into `makeAjazz24g8k` (Pattern B per ARCHITECTURE research).
- [ ] **MOUSE-02**: User can cycle through 6 DPI stages on `ajazz_24g_8k`; stage sequence ORDER matches device firmware capture (NOT a naive `+1` — Pitfall 28). Stage state persists across power-cycle (NVM-stored, NOT host-side cache).
- [ ] **MOUSE-03**: User can set DPI value for each of the 6 stages independently via `IDpiCapable::setStageDpi(int stage, int dpi)` (cmd `0x21`). Per-stage color indicator and Lift-Off-Distance setting (cmd `0x23`) similarly addressable.
- [ ] **MOUSE-04**: User can configure polling rate (1000Hz / 2000Hz / 4000Hz / 8000Hz) via `IPollingRateCapable::setRate` (cmd `0x22`). UI surfaces an honest USB 2.0 warning when 8000Hz is selected and the host port reports USB 2.0 (effective polling capped by USB SOF rate).
- [ ] **MOUSE-05**: User can set per-zone RGB on `ajazz_24g_8k` (cmd `0x30`); zone count + names derived from device capability descriptor, not hardcoded.

### Device Catalogue + Maturity Updates

Phase 10, 12, 13 deliverables — `devices.yaml` honesty enforcement + new entries.

- [ ] **DEVICES-05**: `devices.yaml` row for `akp03_variant_3004` removes `clock` from `capabilities:` list with a `notes:` line citing ARCH-05 ("no RTC opcode in mirajazz/opendeck-akp03/ajazz-sdk; LCD clock widgets are host-rendered images via the `display` capability, not firmware time"). Maturity promotes `scaffolded` → `functional`.
- [ ] **DEVICES-06**: `devices.yaml` row for `ak980pro` removes `clock` from `capabilities:` list with the same ARCH-05 reasoning. Maturity promotes `scaffolded` → `partial` (RGB + sleep-timer functional; macros / layers / per-key RGB / battery stay `feature_summary.pending:` per Pitfall 29 honesty contract).
- [ ] **DEVICES-07**: `devices.yaml` row for `ajazz_24g_8k` updates `notes:` with first-party-captured wire-format reconciliation result (V1.0 vs Max fork); maturity promotes `scaffolded` → `partial` or `functional` based on capture coverage of all advertised capabilities.
- [ ] **DEVICES-08**: New `devices.yaml` row for `microdia_dongle_7016` (`0c45:7016`) added at `probed` tier with `capabilities: []`, `family: dongle` (or `unknown`), `notes:` documenting the live-`lsusb` topology evidence (separate bus branch from `ak980pro`, two boot-keyboard interfaces, Full-Speed) and the unknown paired-input downstream device. ASCII codename verified per Pitfall 32.
- [ ] **DEVICES-09**: `docs/protocols/keyboard/microdia_dongle.md` (NEW, stub) documents the dongle's HID descriptor + topology + identification methodology for future SKU recognition.

### v1.1 UI Verifies Back-Fill

Phase 13 deliverables — close v1.1 deferred items now that the user has 4 devices physically connected with `uaccess` udev tags.

- [ ] **VERIFY-01**: Real-hardware visual verification of Phase 5 Sync-button visibility — button shows on rows advertising `hasClock=true`, hidden on rows where `hasClock=false`. Verified on `akp03_variant_3004` (post-DEVICES-05 demotion: hidden) and any v1.1 catalogue device with `hasClock` still set.
- [ ] **VERIFY-02**: Real-hardware visual verification of Phase 5 Settings auto-sync persistence — toggle state survives app restart (QSettings) AND the auto-sync 300ms-after-arrival firing path triggers (verified via log inspection).
- [ ] **VERIFY-03**: Real-hardware visual verification of Phase 5 per-row glyph behavior — exclamation glyph + tooltip on `NotImplemented`, no success toast (Pitfall 19 honesty contract).
- [ ] **VERIFY-04**: Real-hardware visual verification of Phase 8 MaturityRole tooltip — tooltip appears on hover, content matches `devices.yaml` `notes:` field, all 5 tier values render legibly.

## Deferred — v1.2.x / v1.3+

Acknowledged but out of scope for v1.2. Promoted from carry-over backlog + research-surfaced deferred items.

### AK980 PRO Differentiator Capabilities (v1.3+)

- **KEYBOARD-05**: AK980 PRO per-key custom RGB array (cmd not in TaxMachine capture; needs Phase 12+ research).
- **KEYBOARD-06**: AK980 PRO macro recording + flash to NVM (cmd unknown; NVM wear-rate-limit per Pitfall 25).
- **KEYBOARD-07**: AK980 PRO layer switch + indicator LED (cmd unknown).
- **KEYBOARD-08**: AK980 PRO battery readback (cmd unknown).

### AK980 PRO TFT Display (v1.2.x)

- **DISPLAY-05**: AK980 PRO 1.14" TFT image upload via cmd `0x72` (chunked-send loop is `// TODO` in TaxMachine corpus; capture chunk size + pixel format). Enables future `TftClockWidget` host-rendered differentiator (NOT routed through `IClockCapable::setTime`).

### AKP815 / Mirabox N3 Promotion (v1.2.x or v1.3)

Carry-over from v1.1 deferred. Blocked on real-device captures of those specific devices (the user does NOT have them physically connected for v1.2).

### Other Carry-Over (v1.2.x)

- Explicit `Toast.qml` cap=1 implementation (A-05 mitigation from v1.1).
- TimeSyncService Pitfall-13 contextual INFO message.
- Codename→maturity map → Qt resource + runtime YAML parse (if catalogue grows beyond ~30 codenames).
- libFuzzer Fedora packaging once `libclang_rt.fuzzer.a` lands.

## Out of Scope

Explicitly excluded from v1.2. Documented to prevent scope creep — sourced from research anti-feature inventory + PROJECT.md historical Out of Scope.

| Feature                                                                            | Reason                                                                                                                                                                                                                                       |
| ---------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Synthesizing a fake `setSystemTimeOn` wire format from bytes that "look like time" | Anti-feature — lying success UX. Three-witness rule (capture + observable state change + negative test) structurally cannot be satisfied for `clock` on AKP03; default verdict is `setTime` stays `NotImplemented` per ARCH-05 (Pitfall 19). |
| Clock-via-image-render routed through `IClockCapable::setTime`                     | Anti-feature — conflates `display` with `clock`. Host-rendered clock widgets to TFT (AK980 PRO) are acceptable as a separate v1.3+ `TftClockWidget` differentiator, NOT a setTime impl.                                                      |
| Running vendor binaries via wine / innoextract / Delphi installers                 | CLAUDE.md hard rule — clean-room reverse engineering only. All wire-format knowledge from live captures + OSS corpora (Pitfall 21).                                                                                                          |
| Firmware extraction / air-side RF capture                                          | Out of clean-room scope. USB-side captures only; over-the-air 2.4GHz dongle traffic is opaque and not needed.                                                                                                                                |
| Committing raw `.pcap` / `.pcapng` files to the repo                               | Capture corpora contain user keystrokes (passwords, secrets) recoverable deterministically via `tshark` or `USB-Keyboard-Parser`. CAPTURE-01 enforces gitignore + pre-commit rejection (Pitfall 17).                                         |
| Auto-flashing macros / layers to keyboard NVM on every UI change                   | Anti-feature — NVM wear silently bricks the device in ~3-8 hours of editing. Separate "host-disk save" (instant) from "device-flash commit" (deliberate, ≤1/min) UX (Pitfall 25).                                                            |
| Per-key RGB writes at 60+ keys without rate-limiting on wireless link              | Anti-feature — wireless dongle queue overflow stalls keystrokes during RGB transitions. `isWireless=true` + `≤10 writes/sec` rate-limiter per KEYBOARD-04 (Pitfall 24).                                                                      |
| Auto-flashing SonixQMK firmware to AK980 PRO                                       | Anti-feature — destructive, irreversible, outside the control-center's mandate. Project remains read/write-config-only; firmware modification is the user's responsibility.                                                                  |
| Treating `0c45:7016` as a composite/secondary interface of AK980 PRO               | Topology evidence refutes this hypothesis (different USB bus branch, Full-Speed-only, separate iManufacturer/iProduct strings). ARCH-06 default verdict is NOT firing — `microdia_dongle_7016` is a separate dongle (Pitfall 20).            |
| Composite-HID dedup logic in `DeviceRegistry`                                      | Default ARCH-06 verdict is NOT firing; topology proves no dedup needed. Re-evaluated only if Phase 9 captures contradict.                                                                                                                    |
| Modal "must capture USB first" dialog                                              | Anti-feature — captures are a developer activity, not a user activity. UX assumes captures are pre-baked into fixture headers by the build pipeline.                                                                                         |
| Reusing v1.1 `nlohmann::json` link surface for capture parsing                     | Anti-feature — would cross the COD-031 boundary. Capture extraction is dev-time Python; runtime code reads `std::array<uint8_t>` literals, not parsed JSON.                                                                                  |
| AKB980 PRO promotion                                                               | Vendor driver is Delphi installer requiring wine / innoextract; not in dev env. Deferred indefinitely (carried from v1.1).                                                                                                                   |
| Telemetry / device usage metrics                                                   | Anti-feature — privacy + scope creep (carried from v1.0).                                                                                                                                                                                    |
| `nlohmann::json` in `ajazz_core` or any installed public header                    | COD-031 boundary. PRIVATE-linked to `ajazz_plugins` only (carried from v1.1).                                                                                                                                                                |

## Traceability

Empty initially. Populated by the gsd-roadmapper agent during ROADMAP.md generation.

| Requirement | Phase    | Status   |
| ----------- | -------- | -------- |
| ARCH-04     | Phase 9  | Complete |
| ARCH-05     | Phase 9  | Complete |
| ARCH-06     | Phase 9  | Complete |
| CAPTURE-01  | Phase 9  | Pending  |
| CAPTURE-02  | Phase 9  | Pending  |
| CAPTURE-03  | Phase 9  | Complete |
| CAPTURE-04  | Phase 9  | Complete |
| CAPTURE-05  | Phase 9  | Pending  |
| CAPTURE-06  | Phase 9  | Pending  |
| DISPLAY-01  | Phase 10 | Pending  |
| DISPLAY-02  | Phase 10 | Pending  |
| DISPLAY-03  | Phase 10 | Pending  |
| DISPLAY-04  | Phase 10 | Pending  |
| INPUT-01    | Phase 10 | Pending  |
| INPUT-02    | Phase 10 | Pending  |
| KEYBOARD-01 | Phase 12 | Pending  |
| KEYBOARD-02 | Phase 12 | Pending  |
| KEYBOARD-03 | Phase 12 | Pending  |
| KEYBOARD-04 | Phase 12 | Pending  |
| MOUSE-01    | Phase 11 | Pending  |
| MOUSE-02    | Phase 11 | Pending  |
| MOUSE-03    | Phase 11 | Pending  |
| MOUSE-04    | Phase 11 | Pending  |
| MOUSE-05    | Phase 11 | Pending  |
| DEVICES-05  | Phase 10 | Pending  |
| DEVICES-06  | Phase 12 | Pending  |
| DEVICES-07  | Phase 11 | Pending  |
| DEVICES-08  | Phase 13 | Pending  |
| DEVICES-09  | Phase 13 | Pending  |
| VERIFY-01   | Phase 13 | Pending  |
| VERIFY-02   | Phase 13 | Pending  |
| VERIFY-03   | Phase 13 | Pending  |
| VERIFY-04   | Phase 13 | Pending  |

**Coverage:**

- v1.2 requirements: 33 total
- Mapped to phases: 33 (validated by gsd-roadmapper 2026-05-15; no orphans, no duplicates)
- Unmapped: 0

______________________________________________________________________

*Requirements defined: 2026-05-15*
*Last updated: 2026-05-15 after v1.2 milestone research synthesis*
*Traceability validated: 2026-05-15 by gsd-roadmapper (ROADMAP.md generated; 33/33 reqs mapped to Phases 9-13)*
