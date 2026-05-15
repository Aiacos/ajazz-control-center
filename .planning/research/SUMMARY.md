# v1.2 Research Summary — Connected-Device Capability Parity

**Project:** AJAZZ Control Center
**Milestone:** v1.2 — Connected-Device Capability Parity
**Domain:** USB HID reverse-engineering + capability impl across 4 physically-connected devices (`0300:3004`, `0c45:8009`, `3151:5007`, `0c45:7016`)
**Researched:** 2026-05-15
**Confidence:** HIGH on architecture / stack / privacy posture; MEDIUM on per-device wire-format specifics until first-party captures land in Phase 9.

## Executive Summary

The four parallel researches converge on a single load-bearing picture: **v1.2 is a capture-driven "swap stub for real wire" milestone with zero new architecture**. Every interface needed for the seven advertised capabilities (`display`, `encoder`, `clock`, `rgb`, `macros`, `layers`, `dpi`) already exists in `src/core/include/ajazz/core/capabilities.hpp` (ARCHITECTURE §"Capability mix-in inventory"). No new `I*Capable` headers, no new `nlohmann::json` link surface, no new `libpcap`/`libusb` deps — the entire stack delta is dev-time tooling (Wireshark + `usbmon` + `usbrply`) plus pre-decoded hex fixtures (STACK §"Core: Test-replay infrastructure"). COD-031 remains untouched. The work is replacing `NotImplemented` bodies in three existing `.cpp` files (`akp03.cpp`, `proprietary_keyboard.cpp`, `aj_series.cpp`) with byte-encoders driven by captured ground truth, then materialising a Qt-side image pipeline for AKP03 (ARCH-04).

The single most surprising and load-bearing finding, stated independently by all four researchers, is that **the `clock` capability advertised on three of the four devices is unreal** — no AJAZZ/Mirabox/Microdia/SONiX firmware in any captured corpus exposes a host-settable RTC, and the marketing "clock display" widgets on the AKP03 keyfaces and the AK980 PRO 1.14" TFT are host-rendered images delivered through the `display` capability, not firmware time (FEATURES §1, §2; PITFALLS Pitfall 19; STACK §"AKP03 wire-format facts"). The honest path — explicitly endorsed by FEATURES, PITFALLS, and ARCHITECTURE — is to keep `setTime` returning `NotImplemented`, demote `hasClock` to `false` per device unless Phase 9 captures contradict (ARCH-05), and reroute the user-visible "clock" affordance to a host-rendered image widget tracked as a v1.3+ differentiator (TftClockWidget). This preserves the v1.1 D-02 honesty contract — the single biggest UX regression risk in this milestone is shipping a fake `Ok` on `setTime`.

The principal risks are **capture privacy** (raw `usbmon` `.pcap` files recover keystrokes deterministically — PITFALLS Pitfall 17, MUST-FIX-FIRST), **AK980 PRO wireless dongle bandwidth contention** during RGB burst writes (PITFALLS Pitfall 24), **NVM wear** on macros/layers if the app auto-flashes (PITFALLS Pitfall 25), and **8K mouse wire-format ambiguity** between the AJ199 V1.0 (current backend shape) and AJ199 Max forks (FEATURES §3, ARCHITECTURE §"AjSeriesMouse"). All four mitigations are captures-driven, gated on Phase 9 deliverables before any implementation phase can promote a device past `partial`.

## Key Findings

### Recommended Stack

**No new C++ link-time dependencies.** Every stack addition is dev-time tooling or in-tree code (STACK §"Recommended Stack — additions only", §"Confidence Assessment"). Qt6 already covers image preprocessing (`QImage::scaled(SmoothTransformation)` + `QImageWriter` JPEG) for the AKP03 LCD pipeline — no `stb_image`, no `OpenCV`, no `libjpeg-turbo` direct link. The capture toolchain is documented in CONTRIBUTING/dev-prereqs only.

**Core tooling additions:**

- **Wireshark 4.4.x + tshark + dumpcap + `usbmon` kernel module** — Linux USB capture. Verified missing on dev box; documented as dev-prereq, not a project dep. Recipe verified against Wireshark Wiki + kernel docs + `liquidctl/docs/developer/capturing-usb-traffic.md` (STACK §"Linux — wireshark").
- **`usbrply` 2.1.1 (Python, dev-time)** — pcap → JSON extractor; feeds `scripts/hex-to-cpparray.py` (~30 LoC helper) that emits `tests/integration/fixtures/<codename>_*.h` `std::array` literals. Pattern matches existing `tests/integration/fixtures/akp153/` precedent. **No `PcapPlusPlus`/`libpcap` link** (STACK §"Test-replay infrastructure").
- **Reference corpora (READ-ONLY clean-room references):** `4ndv/mirajazz` v0.12.1 (primary for AKP03 family — opcodes `DIS`/`LIG`/`BAT`/`CLE`/`STP`/`HAN`/`SETLB`/`LBLIG`, 1024-B framing for proto v2/v3); `4ndv/opendeck-akp03` (PID coverage matrix — confirms 0x3004 is NOT covered); `TaxMachine/ajazz-keyboard-software-linux` (clean-room AK820 Pro at same VID:PID 0c45:8009 — RGB modes cmd 0x13, sleep-timer cmd 0x17, partial image upload cmd 0x72); `libratbag/libratbag` (architectural reference only — does NOT cover SONiX 3151) (STACK §"Reverse-engineering corpora").

**New in-tree code (NO third-party library):**

- `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` — PRIVATE-linked to `ajazz_devices_streamdeck`; reuses existing `Qt6::Gui`.
- `tests/unit/fixtures/mock_transport.hpp` (NEW ~80 LoC) — pre-condition for every capability test (ARCHITECTURE §"Test strategy / New test infrastructure").
- Per-device fixture directories under `tests/integration/fixtures/{akp03_variant_3004,ak980pro,ajazz_24g_8k,0c45_7016}/`.
- `docs/protocols/CAPTURING.md` — runbook companion to existing `REVERSE_ENGINEERING.md`.

**COD-031 verified preserved:** `grep -rn nlohmann src/core/include/` = 0 hits at HEAD; every recommended addition lives in `ajazz_devices_*` (PRIVATE) or `tests/` (header-only fixtures) or `scripts/` (not installed).

### Expected Features (per device)

**`akp03_variant_3004` (0300:3004) — caps `[display, encoder, clock]` — LOW risk:**
Current state: scaffolded with `512-byte` packet framing (legacy AKP153 v1). v1.2 delivers: AKP03 v2 framing migration (PacketSize `512 → 1024` in `akp03_protocol.hpp` — **one-line load-bearing fix that simultaneously unblocks 13 sibling SKUs** per FEATURES §1 / Dependencies); per-key 60×60 `Rot0` JPEG upload via `BAT` opcode (encoded host-side, AKP03R rev. 2 lineage uses 64×64 `Rot90` — descriptor-parameterised); brightness via `LIG`; encoder events via `parseInputReport()` already in tree; `EncoderReleased` proper core event (replaces the `value=0` half-step workaround at akp03.cpp:289-293); `clock` demoted to `hasClock=false` (no RTC opcode in mirajazz/opendeck-akp03/ajazz-sdk corpora). **Expected maturity: `functional` for display + encoder; `clock` row removed honestly.**

**`ak980pro` (0c45:8009) — caps `[rgb, macros, layers, clock]` — MEDIUM-HIGH risk:**
Current state: scaffolded via `ProprietaryKeyboard` shared backend. TaxMachine's AK820 Pro project captured a partial protocol at the same VID:PID using Report ID 0x04, three-stage `START` (0x18) → cmd → `FINISH` (0xf0), 64-byte feature reports. Captured: 20 RGB lighting modes via cmd `0x13` (brightness 0..5 not 0..100, speed 0..5, direction 0..3); sleep-timer via cmd `0x17`; partial TFT image upload via cmd `0x72` (chunked-send loop is TODO in TaxMachine, must capture chunk size). NOT captured anywhere OSS: per-key custom RGB, macro upload, layer-switch opcode, battery readback, RTC. v1.2 delivers: RGB mode + brightness + sleep-timer (LOW complexity — promotes to `partial`). v1.2.x: TFT image upload (enables future TftClockWidget). v1.3+: per-key RGB, macros, layers, battery (all `[SPECULATIVE — capture needed]`). **Expected maturity: `partial`. `clock` demoted (no RTC opcode captured).**

**`ajazz_24g_8k` (3151:5007) — caps `[dpi, rgb]` — HIGHEST risk:**
Current state: scaffolded; routes through `aj_series.cpp` backend assuming AJ199 V1.0 wire format. **Zero 3rd-party OSS corpus exists for this PID**; libratbag does not cover it. The 8KHz polling fork between AJ199 V1.0 and AJ199 Max (per `docs/research/vendor-protocol-notes.md` Finding 11.A/B) means the current backend may emit wire that the device silently ignores. v1.2 delivers: probe-and-confirm session (capture vendor app + device descriptor read + replay against `MockTransport` to verify cmd 0x21 DPI / cmd 0x22 PollRate / cmd 0x23 LOD / cmd 0x24 Button / cmd 0x30 RGB / cmd 0x40 Battery / cmd 0x50 Commit). Possible factory split (`makeAjSeries` → `makeAjazz24g8k`) if AJ199 Max envelope is materially different. **Expected maturity: `partial` (some capabilities confirmed) or `functional` (all confirmed) depending on what captures reveal.**

**`microdia_dongle_7016` (0c45:7016) — uncataloged — LOW priority:**
Current state: NOT in `devices.yaml`, NOT in any `register.cpp`. **Cross-research convergence corrects the milestone_context hypothesis**: live `lsusb -v` + topology analysis (USB hub `1-13/1-13.1/1-13.1.2` vs AK980 PRO's direct `1-10`; Full-Speed 12 Mbps, not High-Speed; two boot-keyboard interfaces with 8-byte EPs; iProduct = "USB DEVICE" / iManufacturer = "SONiX") is the canonical signature of a **separate wireless 2.4GHz Microdia/SONiX receiver dongle**, NOT a composite interface of AK980 PRO (FEATURES §1 topology table + §4; PITFALLS Pitfall 20). **This resolves ARCH-06 negatively — composite-HID dedup is NOT required.** v1.2 delivers: descriptor read, paired-input identification attempt (user-driven), `devices.yaml` row at `probed` tier with empty `capabilities: []`. **Expected maturity: `probed`.**

**Anti-features confirmed (DO NOT ship):**

- Synthesising a fake `setSystemTimeOn` wire format on bytes that "look like time" (PITFALLS 19, FEATURES §1).
- Clock-via-image-render disguised as RTC (image upload → time digits is acceptable as a separate `TftClockWidget` differentiator, NOT routed through `IClockCapable::setTime`).
- Running vendor binaries via wine/innoextract; firmware extraction; air-side RF capture (CLAUDE.md hard rule; PITFALLS 21).
- Auto-flashing macros/layers to keyboard NVM on every UI change (PITFALLS 25 — silent brick in ~3-8 hours of editing).
- Committing raw `.pcap` to repo (PITFALLS 17 — keystroke recovery is deterministic).
- Treating 0c45:7016 as a secondary interface of AK980 PRO (PITFALLS 20 + FEATURES §4 topology evidence).
- Auto-flashing SonixQMK firmware to AK980 PRO (STACK §"What NOT to Use").
- Per-key RGB writes at 60 keys without batching/rate-limiting on wireless link (PITFALLS 24 — keystroke loss during RGB transition).

### Architecture Approach

**Existing architecture holds verbatim** (ARCHITECTURE §"Existing architecture — verified at HEAD"). The v1.1 capability-mix-in single-source-of-truth + `shared_ptr<IDevice>` flyweight registry + ARCH-02 HidEnumerator seam + ARCH-03 lifecycle + TimeSyncService canonical mix-in consumer pattern (`dynamic_cast<IClockCapable*>` + null-check within 3 lines + WARN-once + sentinel-not-throw) is the template every new capability follows.

**Major components touched:**

1. **`Akp03Device`** (`src/devices/streamdeck/src/akp03.cpp:274-521`) — single class hosts every AKP03 sibling including 0x3004; descriptor-parameterised. Replaces stubbed `setKeyImage` with real resize+JPEG+chunked `BAT` send; flips `setTime` decision per ARCH-05.
1. **`ProprietaryKeyboard`** (`src/devices/keyboard/src/proprietary_keyboard.cpp:204-437`) — single shared keyboard impl. AK980 PRO captures may force Pattern A (per-codename branches) or Pattern B (promote to abstract base + `makeAk980Pro` sibling factory).
1. **`AjSeriesMouse`** (`src/devices/mouse/src/aj_series.cpp:97-264`) — single shared mouse impl. Probe-and-confirm 64-byte envelope + checksum; possible factory split.
1. **`StreamDeckImagePipeline`** (NEW, PRIVATE-linked).
1. **`MockTransport`** (NEW, `tests/unit/fixtures/mock_transport.hpp`).

**ARCH candidates explicit (Phase 9 ratification):**

- **ARCH-04 — Image-encoding pipeline location.** Recommend Option C: Qt6 `QImage::save(QBuffer*, "JPEG", quality)` host-side (smallest blast radius, no new core dep). Roadmap note that Option B (new `ajazz_imaging` static lib) is the long-term shape if AKP815's 800×480 strip image upload lands.
- **ARCH-05 — `setTime` wire-format per device, binary outcome.** Captures-driven. Default verdict: **NO RTC opcode exists anywhere; `hasClock=false` per device; `setTime` stays `NotImplemented`.** PROJECT.md "Out of Scope" row remains valid. The "three-witness rule" (capture + observable state change + negative test) structurally cannot be satisfied for `clock` on AKP03 — no firmware-rendered LCD clock widget to read back from.
- **ARCH-06 — Composite-HID dedup.** **Recommended NOT to fire.** Topology proves 0c45:7016 is a separate dongle (`usb1/1-13/1-13.1` vs AK980 PRO's `usb1/1-10`). Treat as a separate `probed`-tier device. (If Phase 9 captures contradict — unplugging AK980 PRO causes 0c45:7016 to disappear simultaneously — ARCH-06 fires; LOW probability.)

### Critical Pitfalls

1. **Pitfall 17 — USB capture corpus contains user keystrokes / passwords (MUST-FIX-FIRST).** `.pcap` files recover keystrokes deterministically. **Phase 9 gating deliverable**: `.planning/research/captures/` directory gitignored; pre-commit hook rejects `*.pcap`; captures referenced by SHA-256 + metadata blob; replay tests use sanitised `.hex` fixtures with boot-protocol-keyboard reports stripped. Must land before any researcher does a first capture.
1. **Pitfall 19 — `clock` capability false-positive lie.** Three-witness rule mandatory for any capability promotion. For `clock` on AKP03, round-trip witness structurally unavailable — **expected outcome: stays `NotImplemented`/`partial`**, document as expected.
1. **Pitfall 18 — AKP03 chunked image upload wrong chunk size hangs device until power-cycle.** Always 1024-byte chunks, Transfer-is-Done flag = 0x01 on last chunk, signature-enforce with `std::span<const uint8_t, 1024>`.
1. **Pitfall 24 — AK980 PRO wireless dongle queue overflow on RGB burst.** Phase 9 capture witness determines batched-vs-per-key; if per-key, rate-limit ≤10 writes/sec when `isWireless=true`.
1. **Pitfall 25 — NVM wear from auto-flashing macros/layers.** Separate "host-disk save" (instant) from "device-flash commit" (deliberate, ≤1/min). Never auto-flash on profile-switch or tab-switch.
1. **Pitfall 22 — OSS corpus wire-format drift.** 0x3004 is `HOTSPOTEKUSB HID DEMO` — likely pre-production firmware. Descriptor-parameterise everything; per-variant table-driven test row.

(Full taxonomy: PITFALLS Pitfalls 17-35 with prevention, warning signs, recovery, phase mapping.)

## Implications for Roadmap

**Phase numbering continues from v1.1 (ended at Phase 8).** v1.2 begins at Phase 9.

### Phase 9: Research — Captures, Hygiene, Diff

**Rationale:** All 4 researches scheduled this independently. Every implementation phase depends on first-party captures + privacy-hygiene policy.

**Deliverables (concrete):**

1. **`docs/policies/capture-data-hygiene.md`** + `.planning/research/captures/.gitignore` + pre-commit hook rejecting `*.pcap`/`*.pcapng`. MUST land before first capture (Pitfall 17).
1. **`docs/protocols/CAPTURING.md`** — Wireshark + `usbmon` runbook, capture-time filtering, USB-side only (Pitfall 21), per-device commands.
1. **Per-device captures (SHA-256-indexed, sanitised):**
   - **AKP03 0x3004:** image upload (first + last chunk), `CLE`, `LIG`, encoder rotate per encoder, encoder/side-button press, `HAN`, negative test on hypothetical `TIM` opcode.
   - **AK980 PRO:** 20 RGB modes (cmd 0x13), sleep-timer (cmd 0x17), TFT chunked send (close TaxMachine TODO), per-key RGB / macro / layer / battery attempts (SPECULATIVE), vendor-control interface ID via `udevadm`.
   - **8K mouse:** DPI cycle (operator-driven event window, NOT sustained movement), DPI stage set, polling-rate dropdown, LOD, button bind, RGB per zone, battery, flash commit. Probe AJ199 V1.0 vs Max.
   - **0c45:7016:** `lsusb -v`, `udevadm info -a /dev/hidraw{5,6}`, report descriptor dump, paired-input identification (user-driven `evtest`).
1. **Per-device wire-format diff doc** (`docs/protocols/<family>/<device>.md` extensions) — byte-for-byte deltas vs OSS corpus (Pitfall 22).
1. **`MockTransport` fixture** (`tests/unit/fixtures/mock_transport.hpp`, ~80 LoC).
1. **`scripts/hex-to-cpparray.py`** — pcap → `std::array` literal generator.
1. **ARCH-05 ratification** — binary clock outcome per device. Default verdict: NO RTC; `hasClock=false`; `setTime` stays `NotImplemented`.
1. **ASCII codename check** for `microdia_dongle_7016` (Pitfall 32).

**Avoids:** Pitfalls 17, 19, 21, 22, 32.

### Phase 10: AKP03 variant_3004 promotion (FIRST — lowest risk)

**Rationale:** Best OSS corpus (three-way agreement); one-line PacketSize fix unblocks 13 sibling SKUs; establishes canonical "device-promotion phase" template for Phases 11-12.

**Delivers:** PacketSize 512→1024 migration; `StreamDeckImagePipeline` (ARCH-04 Option C); real `setKeyImage`/`setKeyColor`/`setBrightness`/`flush`; `EncoderReleased` proper core event; `clock` demotion to `hasClock=false`; per-codename + family-coverage tests (Pitfall 30); maturity `scaffolded → functional`; real-hardware 100-image power-cycle smoke (Pitfall 18 promotion gate).

**Avoids:** Pitfalls 18, 22, 23 (encoder signal storm — coalesce at model layer 16ms QTimer), 27 (mix-in QObject diamond — `static_assert(!is_base_of_v<QObject, IDisplayCapable>)`), 29 (lying maturity tier), 30 (cross-family regression).

### Phase 11: AJAZZ 2.4G 8K mouse promotion (SECOND — highest single-device risk, simplest scope)

**Rationale:** Zero OSS corpus but smallest capability surface (`[dpi, rgb]` only). Fail-fast on the highest-uncertainty device; failure does not block Phase 12.

**Delivers:** Envelope reconciliation per Finding 11.A; DPI (cmd 0x21) + per-stage color + cycle order (Pitfall 28 — vendor cycle order captured, not +1); polling-rate (cmd 0x22) + honest USB 2.0 warning; LOD/button/RGB/battery/flash; possible `makeAjazz24g8k` factory split; DPI persistence across power-cycle; maturity → `partial` or `functional`.

**Avoids:** Pitfalls 21 (USB-side only; event-window captures), 28 (cycle order, persistence), 29.

### Phase 12: AK980 PRO promotion (THIRD — biggest scope, biggest risk)

**Rationale:** Largest capability surface; needs infrastructure (rate-limiter, host-save-vs-device-flash UX) established here; reuses patterns from Phase 10.

**Delivers:** RGB 20-mode + brightness 0..5 + speed + direction (cmd 0x13); sleep-timer (cmd 0x17); `clock` demotion; `isWireless=true` field + RGB rate-limiter (Pitfall 24); UI "Save profile" vs "Push to device" separation; flash rate-limit ≤1/min; AK980-specific zone names + 0..5 brightness scale (not 0..100); possible Pattern B promotion to abstract base + `makeAk980Pro`; maturity → `partial` (RGB+sleep functional; macros/layers/per-key RGB stay `feature_summary.pending:` per Pitfall 29).

**Defers v1.2.x:** TFT image upload via cmd 0x72.
**Defers v1.3+:** per-key custom RGB, macros, layers, battery.
**Avoids:** Pitfalls 24, 25, 26 (layer-RGB indicator race — atomic-if-supported OR layer-first-then-RGB + `IndicatorOutOfSync`), 27, 29.

### Phase 13: `microdia_dongle_7016` catalogue + UI verifies back-fill

**Rationale:** ARCH-06 negative; small phase combining catalogue entry with v1.1-deferred real-hardware UI verifications (Sync-button visibility, Settings auto-sync persistence, auto-sync glyph-only-no-toast, MaturityRole tooltip) now unblocked by physical access.

**Delivers:** `microdia_dongle_7016` at `probed` tier; `capabilities: []`; stub `docs/protocols/keyboard/microdia_dongle.md`; v1.1 UI verifications closed.
**Conditional:** If Phase 9 captures contradict the dongle hypothesis, ARCH-06 fires and dedup infrastructure lands before Phase 12 — re-sequence.

### Phase Ordering Rationale

- **Device-clustered, NOT capability-clustered.** Capability-clustered forces synchronous capture-availability across all 4 devices per capability — Gantt-of-captures instead of fan-out. Device-clustered scales per-researcher; each phase has a binary maturity acceptance gate.
- **Order by decreasing capture-availability confidence:** AKP03 first (three-way OSS agreement), 8K mouse second (fail-fast on highest uncertainty), AK980 PRO third (largest scope, needs Phase 10 infrastructure), 0c45:7016 fourth (probably zero protocol work).
- **Phase 10 establishes patterns Phases 11-12 reuse:** `MockTransport` consumption, `static_assert` mix-in lock, `EncoderReleased` event, descriptor-parameterisation, three-witness rule.
- **No cross-device implementation dependencies.** Backends in different libraries; factories independent; could fan out subject to CLAUDE.md 2-agent concurrent cap.

### Research Flags

Phases likely needing deeper research during planning:

- **Phase 9 itself:** captures ARE the research. By definition.
- **Phase 12 (AK980 PRO):** if captures reveal TFT chunk / per-key RGB / macro / layer divergence from TaxMachine baseline, mid-phase `/gsd-research-phase` may be warranted. Microdia chipset family confirmation is MEDIUM-LOW confidence pending HID descriptor capture.
- **Phase 11 (8K mouse):** if AJ199 V1.0 vs Max diverges materially, re-research on SONiX 3151 chipset family.

Phases with standard patterns (skip research):

- **Phase 10 (AKP03):** mirajazz + opendeck-akp03 + ajazz-sdk three-way agreement; image pipeline proven by AKP153.
- **Phase 13:** standard YAML + v1.1 closure work.

## Confidence Assessment

| Area         | Confidence                       | Notes                                                                                                                                                                                                                           |
| ------------ | -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Stack        | **HIGH**                         | Recipes verified against Wireshark Wiki + kernel docs + liquidctl reference + dev-box `lsusb`; COD-031 preserved; one open question (pcap-replay determinism on fast encoder events) closes in Phase 9.                         |
| Features     | **MEDIUM-HIGH for 3 catalogued** | OSS corpora cover ≥80% of AKP03 family + AK980 PRO RGB/sleep; 8K mouse has zero 3rd-party corpus (probe-and-confirm gates promotion); 0c45:7016 methodology concrete but paired-device unknown.                                 |
| Architecture | **HIGH**                         | Every claim verified against HEAD; v1.1 mix-in single-source-of-truth held; no new interface headers required; existing COD-026 DI ctors enable `MockTransport` without architectural change.                                   |
| Pitfalls     | **HIGH on repo-anchored**        | All `file:line` references verified; v1.1 pitfalls 1-16 carry forward unchanged; new Pitfalls 17-35 anchored on HEAD / OSS byte-level findings / live `lsusb`. MEDIUM on Microdia/SONiX wireless-stall claims (flagged inline). |

**Overall confidence:** **HIGH** — the milestone is captures-bound, not architecture-bound. Phase 9 closes remaining MEDIUM/LOW gaps; if it does not (e.g. inconclusive 8K mouse captures), the affected device stays at `partial` honestly per Pitfall 29.

### Gaps to Address

- **AKP03 0x3004 specific wire format vs canonical AKP03 (0x1001)**: 0x3004 is "HOTSPOTEKUSB HID DEMO" likely pre-production. Resolution: Phase 9 first-party capture; descriptor-parameterise deltas (Pitfall 22, 30).
- **AK980 PRO chipset family confirmation**: Microdia 0c45 fronts multiple families; cannot assume Sonix SN32. Resolution: Phase 9 `lsusb -v -d 0c45:8009` HID descriptor capture; SonixQMK relevance conditional.
- **AJAZZ 2.4G 8K wire-format envelope (V1.0 vs Max)**: Zero 3rd-party OSS corpus. Resolution: Phase 11 probe-and-confirm; possible factory split; promotion gated on capture coverage.
- **0c45:7016 paired downstream device**: Topology proves separate dongle. Paired wireless input unknown. Resolution: Phase 9 user-driven `evtest`/`hexdump < /dev/hidraw5`. If no input ever arrives, stays at `probed`-tier indefinitely.
- **AK980 PRO TFT image-upload chunk size**: TaxMachine has packet shape but chunked-send loop is `//TODO`. Resolution: Phase 9 capture; defer to v1.2.x impl if cleanly captured.
- **`clock` final disposition per device**: Default verdict NO RTC anywhere; `hasClock=false`; `setTime` stays `NotImplemented`. ARCH-05 ratified in Phase 9. Extremely unlikely any capture contradicts given converged OSS evidence.

## Key Takeaway (one line)

**v1.2 has no new architecture — it has a one-line PacketSize fix for AKP03 (unblocking 13 sibling SKUs), a captures-driven probe-and-confirm for two more devices, an honest demotion of `clock` everywhere because no AJAZZ firmware exposes a settable RTC, and a strict privacy-first capture-hygiene policy that must land before any researcher does their first capture.**

## Sources

### Primary (HIGH confidence)

**Live hardware (read 2026-05-15 from user's machine):** `lsusb -d 0300:3004 -v` / `0c45:8009 -v` / `3151:5007 -v` / `0c45:7016 -v`; `/sys/bus/usb/devices/{1-13.3.4,1-10,1-13.2,1-13.1.2}/`; `udevadm info -a -n /dev/hidraw{5,6,11..19}`; `which wireshark tshark dumpcap` (NOT installed).

**OSS reverse-engineering corpora (clean-room, no code vendored):** [`4ndv/mirajazz`](https://github.com/4ndv/mirajazz) v0.12.1; [`4ndv/opendeck-akp03`](https://github.com/4ndv/opendeck-akp03); [`mishamyrt/ajazz-sdk`](https://github.com/mishamyrt/ajazz-sdk); [`TaxMachine/ajazz-keyboard-software-linux`](https://github.com/TaxMachine/ajazz-keyboard-software-linux); [`SonixQMK/qmk_firmware`](https://github.com/SonixQMK/qmk_firmware); [`libratbag/libratbag`](https://github.com/libratbag/libratbag).

**Tooling:** [Wireshark Wiki: CaptureSetup/USB](https://wiki.wireshark.org/CaptureSetup/USB), [Linux kernel docs: usbmon](https://docs.kernel.org/usb/usbmon.html), [liquidctl capturing-usb-traffic](https://github.com/liquidctl/liquidctl/blob/main/docs/developer/capturing-usb-traffic.md), [`JohnDMcMaster/usbrply`](https://github.com/JohnDMcMaster/usbrply) v2.1.1, [USBPcap 1.5.4](https://github.com/desowin/usbpcap/releases).

### Secondary (MEDIUM confidence)

Vendor product pages (input-layer only): [AJAZZ AK980](https://ajazzbrand.com/products/ajazz-ak980-keyboard), [MechLands AK980 V2 (Amazon)](https://www.amazon.com/MechLands-AK980-Mechanical-Keyboard-Swappable/dp/B0DF7STD5F), [AJAZZ AJ159 APEX](https://ajazzbrand.com/products/ajazz-aj159-apex-mouse). USB-ID DBs: [usb-ids.gowdy.us Microdia 0c45](https://usb-ids.gowdy.us/read/UD/0c45), [DeviceHunt 0C45](https://devicehunt.com/view/type/usb/vendor/0C45). [Sagacious 2013 0c45:7000 Microdia receiver](https://himself.wordpress.com/2013/05/14/0c457000-microdia-receiver-for-ipazzPort-commander-bluetooth-kp-810-18br/). [Elgato Stream Deck HID API](https://docs.elgato.com/streamdeck/hid/general/). [HackTricks USB Keystrokes](https://hacktricks.wiki/en/generic-methodologies-and-resources/basic-forensic-methodology/pcap-inspection/usb-keystrokes.html), [USB-Keyboard-Parser](https://github.com/bolisettynihith/USB-Keyboard-Parser).

### Tertiary (LOW confidence)

AK980 PRO chipset family ID; AJ199 V1.0 vs Max envelope structural difference; Microdia/SONiX 2.4G dongle TX queue saturation; AK980 PRO firmware NVM wear-cycle ceiling.

### In-repo (load-bearing)

`CLAUDE.md`; `.planning/PROJECT.md`; `.planning/RETROSPECTIVE.md`; `.planning/milestones/v1.1-research/{STACK,ARCHITECTURE,PITFALLS}.md`; `docs/_data/devices.yaml:264-378`; `docs/protocols/streamdeck/akp03.md`; `docs/protocols/keyboard/proprietary.md`; `docs/protocols/mouse/aj_series.md`; `docs/protocols/REVERSE_ENGINEERING.md`; `docs/research/vendor-protocol-notes.md`; `src/core/include/ajazz/core/capabilities.hpp:31-601`; `src/devices/streamdeck/src/akp03.cpp:274-521` + `akp03_protocol.hpp:53-58`; `src/devices/keyboard/src/proprietary_keyboard.cpp:204-437`; `src/devices/mouse/src/aj_series.cpp:97-264`; `src/app/src/time_sync_service.{hpp,cpp}`; `resources/linux/99-ajazz.rules`.

______________________________________________________________________

*Research synthesised: 2026-05-15*
*Ready for roadmap: yes*
*Phase 9 gates Phase 10+; capture-data-hygiene policy is the milestone-blocking deliverable*
