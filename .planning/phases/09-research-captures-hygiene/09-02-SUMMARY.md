---
phase: 09-research-captures-hygiene
plan: 02
subsystem: docs
tags: [capture, runbook, wireshark, usbmon, dumpcap, usbrply, pitfall-17, pitfall-21, capture-02]

requires:
  - phase: 09-research-captures-hygiene
    plan: 01
    provides: capture-data-hygiene policy + scripts/reject-raw-captures.sh hook (CAPTURE-01) — the rejection-at-commit-time boundary this runbook is built on top of.
provides:
  - docs/protocols/CAPTURING.md — per-device Wireshark + usbmon + dumpcap runbook
  - Per-device VID/PID capture-filter table for akp03_variant_3004 (0300:3004), ak980pro (0c45:8009), ajazz_24g_8k (3151:5007), microdia_dongle_7016 (0c45:7016)
  - Pitfall 17 (boot-keyboard interface filter) operational guidance for ak980pro + microdia_dongle_7016
  - Pitfall 21 (8K mouse capture-buffer sizing) operational guidance for ajazz_24g_8k
  - Display-filter vs. BPF-capture-filter distinction (the usbmon footgun)
  - Anti-features section codifying CLAUDE.md hard rules (no RF capture, no vendor binary inspection, no in-app sniffer, no telemetry upload)
  - Windows USBPcap appendix for cross-OS opcode confirmation
affects: [09-03, 09-04, 09-05, 09-06]

tech-stack:
  added: []  # doc-only; tools (wireshark, tshark, usbmon, usbrply) operator-installed
  patterns:
    - Operator-driven event windowing for USB-side-only capture (one device action per capture)
    - Per-bus capture (usbmonN, not usbmon0) + per-device display filter (Wireshark dialect, applied via tshark -Y)
    - usbrply JSON intermediate (no libpcap dependency in-tree; STACK D-03 enforced)

key-files:
  created:
    - docs/protocols/CAPTURING.md
  modified: []

key-decisions:
  - 'D-01 reinforcement: hygiene policy (09-01) is the structural prerequisite; this runbook references it from the header and cross-links scripts/reject-raw-captures.sh + the captures sink README.'
  - BPF capture-filter (-f) vs. display-filter (-Y) distinction documented explicitly — the usb.idVendor keyword is Wireshark display-filter syntax and produces a BPF syntax error if fed to -f. Common usbmon footgun that surprised every first-time user.
  - Step 5.3 forward-references scripts/hex-to-cpparray.py (lands in plan 09-03, next wave). The runbook is written so that when 09-03 ships, the end-to-end pipeline already documented here Just Works without further changes.
  - Windows USBPcap relegated to an appendix, not equal billing with Linux. Linux usbmon is the primary path for this project (4 connected v1.2 devices all confirmed working under Linux); USBPcap exists for the rare cross-firmware confirmation case.

patterns-established:
  - 'Doc-only deliverable shape for an OPERATOR procedure: agent writes the doc, agent does NOT execute the commands it documents (CLAUDE.md hard rule). The doc explicitly flags which steps are operator-side at the section level.'
  - Forward-reference to a not-yet-landed sibling plan (09-03 hex-to-cpparray.py) by exact path. This is acceptable when the forward-referenced artifact is in the same phase and the runbook is structurally complete without it.
  - Per-device filter table as the load-bearing centerpiece — gives the user a single grep-able row per device so they do not need to look up VID/PID values during a capture session.

requirements-completed: [CAPTURE-02]

duration: ~12min
completed: 2026-05-15
---

# Phase 9 Plan 02: Wireshark + usbmon + dumpcap Capture Runbook Summary

**`docs/protocols/CAPTURING.md` — a 486-line per-device USB-capture runbook for the 4 connected v1.2 devices, built on top of the CAPTURE-01 hygiene boundary and forward-referencing the CAPTURE-03 conversion pipeline. Doc-only deliverable, no system commands executed by the agent.**

## Performance

- **Duration:** ~12 min (read 5 context files → author single doc → mdformat reflow + re-stage → commit)
- **Tasks:** 1 (single atomic doc-authoring task)
- **Files created:** 1 (`docs/protocols/CAPTURING.md`, 486 lines)
- **Files modified:** 0

## Accomplishments

- 9-section runbook from frontmatter through cross-references, mdformat-clean, typos-clean.
- Per-device capture-filter table covers all 4 connected v1.2 devices with the exact Wireshark display-filter string each one requires:
  - `akp03_variant_3004` (0300:3004) — Pitfall 22 image-upload-first-chunk + brightness + encoder-rotate workflow.
  - `ak980pro` (0c45:8009) — Pitfall 17 boot-keyboard-protocol drop (`usb.bInterfaceProtocol != 1`).
  - `ajazz_24g_8k` (3151:5007) — Pitfall 21 8K capture-buffer cap at 50 MB + control-channel-event-only windowing.
  - `microdia_dongle_7016` (0c45:7016) — Pitfall 20 separate-dongle verification + same boot-keyboard drop as ak980pro.
- Display-filter vs. BPF-capture-filter footgun documented at step 4.2 (the `-f` flag does NOT understand `usb.idVendor`; this surprises every first-time usbmon user).
- Anti-features section enumerates 7 things the project explicitly does not do (RF capture, vendor binary inspection, firmware dump, in-app sniffer, telemetry upload, raw pcap commit, libpcap dependency).
- 7-row troubleshooting table covers the 7 most likely operator-side failure modes (permission denied, no `/sys/kernel/debug/usb/usbmon/`, empty `-Y` output, empty usbrply JSON, version-mismatch errors, runaway capture file size, suspicious boot-keyboard bytes in extracted header).
- Windows USBPcap appendix exists for cross-firmware confirmation but is clearly secondary to the Linux usbmon path.

## Document Table of Contents

1. Intro + RAW CAPTURE NEVER LEAVES MACHINE callout (cross-links policy + captures sink + REVERSE_ENGINEERING.md).
1. Prerequisites — Wireshark/tshark/dumpcap, usbmon module, wireshark group, usbrply.
1. Identify the device's USB bus and address — `lsusb` / `lsusb -v` / `lsusb -t`.
1. Per-device capture-filter table.
1. Capture session — display vs. BPF filter, capture, trigger, stop, extract.
1. Sanitise + convert to hex fixture — usbrply → eyeball → hex-to-cpparray.py → eyeball → commit → delete scratch.
1. What you CANNOT do (anti-features) — 7 explicit out-of-scope items with CLAUDE.md / Pitfall references.
1. Troubleshooting — 7-row symptom/cause/fix table.
1. Windows USBPcap appendix.
1. Cross-references — full inventory of related documents.

## Per-Device Filter Rows (load-bearing reproduction for grep)

| Codename               | VID:PID   | Display filter (Wireshark dialect)                                                 |
| ---------------------- | --------- | ---------------------------------------------------------------------------------- |
| `akp03_variant_3004`   | 0300:3004 | `usb.idVendor == 0x0300 && usb.idProduct == 0x3004`                                |
| `ak980pro`             | 0c45:8009 | `usb.idVendor == 0x0c45 && usb.idProduct == 0x8009 && usb.bInterfaceProtocol != 1` |
| `ajazz_24g_8k`         | 3151:5007 | `usb.idVendor == 0x3151 && usb.idProduct == 0x5007`                                |
| `microdia_dongle_7016` | 0c45:7016 | `usb.idVendor == 0x0c45 && usb.idProduct == 0x7016 && usb.bInterfaceProtocol != 1` |

## Cross-Link Inventory

| Direction | Target                                              | Purpose                                                               |
| --------- | --------------------------------------------------- | --------------------------------------------------------------------- |
| out       | `docs/policies/capture-data-hygiene.md`             | Policy authority + Pitfall 17 threat write-up                         |
| out       | `.planning/research/captures/README.md`             | Scratch-sink semantics                                                |
| out       | `docs/protocols/REVERSE_ENGINEERING.md`             | Broader clean-room methodology                                        |
| out       | `scripts/hex-to-cpparray.py` (will land in 09-03)   | Conversion pipeline (forward reference)                               |
| out       | `tests/unit/fixtures/mock_transport.hpp` (in 09-04) | Wire-format assertion seam (forward reference)                        |
| out       | `.planning/research/PITFALLS.md` 17, 20, 21, 22, 24 | Cross-cutting risk register entries                                   |
| out       | `.planning/research/STACK.md`                       | Upstream tooling reference (`NEW developer-prereqs` + test-replay)    |
| out       | `.planning/REQUIREMENTS.md` § CAPTURE-02            | Requirement satisfied                                                 |
| in        | `docs/policies/capture-data-hygiene.md`             | Already mentioned the runbook by path (no inbound-link change here)   |
| in        | `.planning/research/captures/README.md`             | Already references `docs/protocols/CAPTURING.md` (plan 09-02) by path |

## Task Commits

1. **Task 1: Author docs/protocols/CAPTURING.md** — `791e510` (docs(capture))

## Files Created/Modified

- `docs/protocols/CAPTURING.md` — 486 lines, mdformat-clean. 9 numbered sections + 7-row troubleshooting table + 4-row per-device filter table + Windows USBPcap appendix.

## Decisions Made

- **Forward-reference scripts/hex-to-cpparray.py by exact path** even though it has not landed yet (plan 09-03, next wave). Rationale: the runbook is structurally complete the moment 09-03 ships, and the alternative (waiting for 09-03 to land before this doc lands) would block the parallel-wave-2 plan layout.
- **Windows USBPcap as a one-page appendix, not equal billing.** Linux usbmon is the primary path for this project — all 4 connected v1.2 devices confirmed working under Linux, hidapi_hidraw is the chosen backend, CI's Linux runners are the canonical platform. USBPcap exists for the rare cross-firmware opcode confirmation case (e.g., two researchers confirming whether a vendor opcode is identical between Linux and Windows firmware paths).
- **Display-filter vs. capture-filter explicit explanation.** The plan called this out as "a common usbmon footgun" — verbatim documenting *why* the obvious `-f 'usb.idVendor == 0x...'` is wrong (BPF does not understand the `usb.idVendor` keyword) is worth the paragraph it costs.

## Deviations from Plan

None — plan executed exactly as written. The `mdformat` pre-commit hook reformatted the per-device capture-filter table and the troubleshooting table on first commit attempt (table column padding adjustment; no semantic change). Standard CLAUDE.md workflow: re-stage the formatted output and re-commit. No `--no-verify` invoked.

## Issues Encountered

- First commit attempt failed `mdformat` because the per-device filter table column padding did not match `mdformat`'s default reflow. Re-staged the reformatted file and the second commit attempt passed all hooks cleanly. This is the standard CLAUDE.md "mdformat will likely reformat tables once" interaction the plan explicitly anticipated.

## Next Phase Readiness

- **Plan 09-03 (`scripts/hex-to-cpparray.py`)** can now land in this same wave — the runbook's step 5.3 already documents the exact CLI shape the script must implement (`--device <codename> --capture <label>` from stdin/`/tmp/cap.json`, stdout writes a C++ header). The script's design is anchored by the runbook prose.
- **Plan 09-04 (`tests/unit/fixtures/mock_transport.hpp`)** can land in parallel with 09-03; runbook §9 forward-references it as the consumer of the hex-fixture pipeline output.
- **Phase 9.x follow-up (when the user takes a first capture)** consumes this runbook unchanged. No further changes to `docs/protocols/CAPTURING.md` are expected unless the workflow reveals friction.

Build verification: `pre-commit run --files docs/protocols/CAPTURING.md` exits 0 (mdformat + typos + reject-raw-captures all pass). `git log -1 --pretty=%B docs/protocols/CAPTURING.md | grep CAPTURE-02` returns 1.

______________________________________________________________________

*Phase: 09-research-captures-hygiene*
*Completed: 2026-05-15*
