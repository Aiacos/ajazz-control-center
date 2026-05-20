---
phase: 9
phase_slug: research-captures-hygiene
gathered: 2026-05-15
status: Ready for planning (PARTIAL SCOPE — non-capture deliverables only)
mode: orchestrator-curated
---

# Phase 9: Research, Captures, Hygiene — Context

**Gathered:** 2026-05-15 (orchestrator-curated, no `/gsd-discuss-phase` interactive run)
**Source for recommendations:** `.planning/research/SUMMARY.md` §"Implications for Roadmap" + `.planning/research/PITFALLS.md` Pitfalls 17-22 + `.planning/research/ARCHITECTURE.md` §"Test strategy" + `.planning/REQUIREMENTS.md` §"Capture Infrastructure" + §"Architectural Decisions" + ROADMAP Phase 9 success criteria.

## Scope Decision (load-bearing)

This Phase 9 execution is **partially scoped to non-capture deliverables only**. The capture activity (CAPTURE-05/06) and ARCH-04/05/06 final-verdict ratification require the user (orchestrator) to:

1. Install system packages on the dev box (`sudo apt/dnf install wireshark tshark dumpcap`) — CLAUDE.md hard rule forbids the agent from making system-level mutations.
1. Load the `usbmon` kernel module (`sudo modprobe usbmon`) — same reason.
1. Physically interact with the 4 connected AJAZZ devices while running the official vendor apps, so USB packets can be captured.

None of those steps can be automated by the agent. Splitting Phase 9 into two executions decouples the **infrastructure** (this run) from the **captures** (follow-up run after the user produces them).

**In-scope for this run (4 capability requirements + 3 ARCH default-verdict ratifications):**

> (NOTE 2026-05-20: this device — USB 0x3004 — was later firmware-confirmed to be an AKP05E, codename akp05e; see STATE.md.)

| REQ        | Deliverable                                                                                                                                                                                                                                                                                                                    | Why this run                                                                                                                                                                                                                                                                     |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CAPTURE-01 | `docs/policies/capture-data-hygiene.md` + `.planning/research/captures/.gitignore` + pre-commit hook rejecting `*.pcap`/`*.pcapng`                                                                                                                                                                                             | **MUST land BEFORE the user takes a first capture** (Pitfall 17 — keystroke recovery from raw `.pcap` is deterministic via tshark / USB-Keyboard-Parser). Gates the entire capture activity.                                                                                     |
| CAPTURE-02 | `docs/protocols/CAPTURING.md` runbook (Wireshark + usbmon + dumpcap install, kernel-module mount, per-device filter, USB-side-only event windowing)                                                                                                                                                                            | Doc-only; gives the user a deterministic recipe for their capture sessions. No hardware interaction needed to write it.                                                                                                                                                          |
| CAPTURE-03 | `scripts/hex-to-cpparray.py` (~30 LoC dev-time helper, usbrply JSON → `std::array<uint8_t>` C++ header) + a smoke test against synthetic input                                                                                                                                                                                 | Pure dev-tooling. The captures the user produces later will feed this pipeline; if the pipeline doesn't exist when captures arrive, time is wasted.                                                                                                                              |
| CAPTURE-04 | `tests/unit/fixtures/mock_transport.hpp` (~80 LoC header-only) + a one-backend smoke test exercising the existing COD-026 DI ctor                                                                                                                                                                                              | Architectural pre-condition for every Phase 10-12 wire-format test. Designable purely from existing `IDevice` + `Transport` interfaces — no captures needed.                                                                                                                     |
| ARCH-04    | `.planning/phases/09-research-captures-hygiene/ARCH-04.md` — **default verdict** "Qt6 `QImage::scaled(SmoothTransformation)` + `QImageWriter` JPEG host-side in `src/devices/streamdeck/src/image_pipeline.{hpp,cpp}` (Option C); Option B (`ajazz_imaging` static lib) deferred to v1.3+ AKP815 strip-image needs"            | Image-pipeline location can be decided architecturally without captures (the *content* of the JPEG depends on captures; the *location* does not). Phase 10 binds to this decision.                                                                                               |
| ARCH-05    | `.planning/phases/09-research-captures-hygiene/ARCH-05.md` — **default verdict** "No RTC opcode in any AJAZZ corpus (mirajazz, opendeck-akp03, ajazz-sdk, TaxMachine AK820 Pro); `hasClock=false` on `akp03_variant_3004` and `ak980pro`; `setTime` stays `NotImplemented`; PROJECT.md Out-of-Scope row for setTime preserved" | Strong cross-corpus evidence (three-way + clean-room AK820 Pro) makes the default verdict high-confidence. Captures could in principle flip it but no current evidence suggests they will. Document the verdict + the captures-confirmation trigger that would flip it.          |
| ARCH-06    | `.planning/phases/09-research-captures-hygiene/ARCH-06.md` — **default verdict** "Topology proves `0c45:7016` is a separate dongle on a different USB bus branch from `ak980pro`; NOT firing composite-HID dedup; new `microdia_dongle_7016` row enters catalogue at `probed` tier in Phase 13"                                | Topology evidence (live `lsusb` 2026-05-15) is unambiguous: different bus branch, Full-Speed-only, separate iManufacturer/iProduct strings. Captures could in principle flip it (e.g. unplug-correlation) but evidence is so strong that NOT firing is the load-bearing default. |

**Deferred to a follow-up Phase 9.x run (after the user produces captures):**

| REQ                  | Deliverable                                                                                                                                                                                                                 | What triggers the follow-up run                                                                                 |
| -------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| CAPTURE-05           | Sanitised capture fixtures for all 4 devices, SHA-256-indexed in `.planning/research/captures/INDEX.md`; per-device hex headers under `tests/integration/fixtures/<codename>_*.h`                                           | User produces raw `.pcap` captures (out-of-tree), runs `scripts/hex-to-cpparray.py`, the fixtures land in tree. |
| CAPTURE-06           | Per-device wire-format diff docs (extend `docs/protocols/streamdeck/akp03.md` for `0300:3004`; create `docs/protocols/keyboard/ak980pro.md` + `docs/protocols/mouse/ajazz_24g_8k.md` if findings diverge from OSS baseline) | Requires CAPTURE-05 fixtures to compare against the OSS corpora referenced in research.                         |
| ARCH-04 finalization | Confirm Option C against actual AKP03 0x3004 image upload byte sequence                                                                                                                                                     | Requires AKP03 image-upload capture. Default verdict expected to hold.                                          |
| ARCH-05 finalization | Confirm absence of RTC opcode against actual captures from all 4 devices                                                                                                                                                    | Requires all 4 device captures. Default verdict expected to hold.                                               |
| ARCH-06 finalization | Confirm dongle is independent (unplugging `ak980pro` does NOT take `0c45:7016` offline)                                                                                                                                     | Requires a 2-minute physical test from the user. Default verdict expected to hold.                              |

## Phase Boundary (this execution)

A developer has, after this run:

- A repo that **rejects raw capture files at commit time**, with a documented policy explaining the privacy risk and the sanitised-fixture workflow.
- A reproducible Wireshark + usbmon recipe in `docs/protocols/CAPTURING.md` they can follow to produce their own captures of the 4 connected devices.
- A working pcap → C++ header pipeline (`scripts/hex-to-cpparray.py`) that turns sanitised capture data into committed `std::array<uint8_t>` fixtures.
- A `MockTransport` fixture (`tests/unit/fixtures/mock_transport.hpp`) consumed by the existing COD-026 DI constructors of all three device backends, ready to write byte-level wire-format tests against.
- Three ratified ARCH decisions (ARCH-04, ARCH-05, ARCH-06) at **default verdict**, each documenting the captures-confirmation trigger that would flip it.

A developer has, after the follow-up Phase 9.x run:

- Sanitised SHA-256-indexed capture fixtures for all 4 devices.
- Wire-format diff docs anchoring v1.2 implementation work.
- ARCH-04/05/06 finalized (verdicts confirmed or flipped by capture evidence).

Maps to requirements (this execution): **CAPTURE-01, CAPTURE-02, CAPTURE-03, CAPTURE-04, ARCH-04 (default), ARCH-05 (default), ARCH-06 (default)** = 7 of 9 Phase 9 requirements.

## Requirements (locked for this execution)

- CAPTURE-01, CAPTURE-02, CAPTURE-03, CAPTURE-04 — full text in `.planning/REQUIREMENTS.md`.
- ARCH-04, ARCH-05, ARCH-06 — full text in `.planning/REQUIREMENTS.md`; this execution ratifies at **default verdict** only (the captures-confirmation finalization is part of Phase 9.x follow-up).

## Implementation Decisions (locked)

### D-01 — CAPTURE-01 lands in plan 09-01, MUST-FIX-FIRST

The hygiene policy + gitignore + pre-commit hook lands as plan **09-01** before anything else. Rationale: Pitfall 17 says raw `.pcap` files recover keystrokes deterministically; if the user does any capture before the gitignore is in place, an accidental `git add .` ships passwords to the public repo. The hygiene policy is structurally the first thing in this execution and the first thing in any follow-up.

### D-02 — Pre-commit hook implementation: per-extension blocklist + size guardrail

The pre-commit hook is a small shell script registered in `.pre-commit-config.yaml`. It checks staged files for `*.pcap` and `*.pcapng` extensions (case-insensitive) and any binary file over 10 KB under `.planning/research/captures/`. Rejection message points the user to `scripts/hex-to-cpparray.py` + `docs/policies/capture-data-hygiene.md`.

### D-03 — `scripts/hex-to-cpparray.py` consumes `usbrply` JSON, not raw pcap

The script is post-`usbrply`. The user's flow is: `tshark` → pcap → `usbrply -j pcap.pcap > out.json` → `scripts/hex-to-cpparray.py out.json --device akp03_variant_3004 --capture image-upload-first-chunk > tests/integration/fixtures/akp03_variant_3004/image_upload_first_chunk.h`. This keeps `libpcap` out of the agent codebase (Pitfall 17 + Pitfall 22 + STACK §"Test-replay infrastructure"). Smoke test against a 3-line synthetic JSON.

### D-04 — `MockTransport` is header-only + matches the existing `Transport` interface

`tests/unit/fixtures/mock_transport.hpp` is a single header (no `.cpp`). It implements whatever interface the three backends' COD-026 DI ctors expect (read existing ctors to confirm signature). API minimum: `void write(std::span<const uint8_t>)`, `std::vector<std::vector<uint8_t>> writes() const`. Smoke test wires one backend (the easiest of the three — likely `AjSeriesMouse` per `aj_series.cpp:97-264`) and asserts the byte sequence of a no-op call.

### D-05 — ARCH default verdicts are PRO-FORMA — finalization gates promotion

ARCH-04/05/06 documents this run produces are explicitly labeled **DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)**. They unblock Phase 10 design discussion but Phase 10 plans referencing ARCH-04 (image pipeline location) get gated on the Phase 9.x finalization run, which is captured in STATE.md `pending_todos`. This is the honesty contract — we don't lie about being "done" with ARCH ratification when one capture could flip it.

### D-06 — Phase 9 ROADMAP status reflects partial completion

After this execution, ROADMAP Phase 9 shows **4/8 plans complete** (or whatever the planner decides — likely 5 if the planner splits the 4 CAPTURE-NN + 3 ARCH-NN deliverables differently). Phase 10 cannot start until the remaining captures land + ARCH finalization is committed.

## Open Questions (none for this execution)

All open questions about CAPTURE-NN + ARCH default verdicts are answered by `.planning/research/SUMMARY.md` and the existing OSS-corpora evidence. The user has explicitly chosen partial scope; no scope ambiguity remains.

Open questions that **will** need answers when the follow-up Phase 9.x runs (and Phase 10 starts):

- Does AKP03 0x3004 use 1024-byte chunks with last-chunk `0x01` flag (mirajazz baseline) or does HOTSPOTEKUSB pre-production firmware diverge? — Pitfall 22.
- AK980 PRO chipset family — is it really Sonix SN32 (TaxMachine inference) or a different Microdia 0c45 chipset? — `lsusb -v -d 0c45:8009` HID descriptor capture answers.
- AJ199 V1.0 vs Max envelope on the 8K mouse — does the existing `aj_series.cpp` backend assumption hold? — Phase 11 probe-and-confirm answers.
- 0c45:7016 dongle paired-input — what wireless device feeds it? — user `evtest /dev/hidraw5` answers.

These don't block this execution; they block the follow-up Phase 9.x and Phase 10+.
