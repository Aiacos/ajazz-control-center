# AJAZZ Control Center — Reverse-Engineering Dossier

> **Status:** living research dossier. Consolidates the complete wire-format
> reverse engineering for the three AJAZZ device families this project drives,
> the methods used, the reproducible capture evidence, and — deliberately — the
> areas **not yet explored**. Assembled 2026-05-21 from the per-device protocol
> docs, the C++ implementation + pinned tests, the Ghidra decompiles, the vendor
> Electron-driver JS, and live-hardware Frida/probe captures.

## Clean-room contract (read first)

This dossier is **clean-room**. It records *findings* — byte layouts, opcodes,
handshakes, decompiled-function and OSS-symbol *references* — and **does not
reproduce vendor source**: no decompiled C bodies, no extracted vendor
JavaScript, no vendor binaries. This matches the project's hard rule
(`CLAUDE.md`, `docs/protocols/keyboard/proprietary.md`): *"no vendor firmware,
driver, or SDK is disassembled or reused"* and the recon-engineer ≠ implementer
separation. The raw vendor material (Ghidra `FUN_*.c` dumps, the beautified
Electron `main_beautified.js`, the vendor installers/binaries) is kept **local**
under `C:/Users/unilo/reverse-eng-workdir/` and referenced here by
function/symbol name only — it is intentionally NOT committed.

**Raw USB packet captures (`.pcap`/`.pcapng`) are NOT committed**, by policy:
they contain plaintext keystrokes (passwords, etc.) recoverable with `tshark`,
so committing one is a security incident — the pre-commit hook
`scripts/reject-raw-captures.sh` rejects them at any path. What *is* committed is
the **sanitised, control-channel-only byte evidence** in
[`capture-evidence.md`](capture-evidence.md) (the exact wire bytes we observed
for clock/battery/etc. — no keyboard/mouse-coordinate reports).

> **Source-of-truth ordering.** The RE is the source of truth for wire formats,
> but it has gaps and provisional values. **When the RE and the hardware
> disagree, the hardware wins** — proven repeatedly this session (the AK980
> control collection is 0xFF13 not 0xFF00; time-sync needs a per-packet
> handshake the envelope alone lacks; the mouse clock needs a 0xD7 marker).
> Items marked PROVISIONAL / UNCONFIRMED are hypotheses, not facts.

## Contents

| File | Scope |
| --- | --- |
| [`HOWTO-reverse-engineer-a-device.md`](HOWTO-reverse-engineer-a-device.md) | **Start here to RE a NEW device/software.** End-to-end community playbook: enumerate → vendor app → Ghidra/JS static → Frida live-hook → decode → confirm on hardware → builders+tests → document. Includes the consolidated gotcha checklist. |
| [`ak980-keyboard.md`](ak980-keyboard.md) | AK980 PRO + proprietary keyboard family (Microdia/Sonix `0x0c45`, SONiX `0x3151` legacy) — RTC time-sync, battery, RGB (firmware 20-mode + per-key), macros, layers, settings, TFT image upload. |
| [`aj-series-mouse.md`](aj-series-mouse.md) | AJ-series mouse family (SONiX `0x3151`, AJ199 `0x3554`, legacy `0x248A`/`0x249A`) — OLED firmware clock, battery, DPI/poll/LOD/profiles/macros/keymatrix, the Electron+iot_driver gRPC stack, the OemDrv/HIDUsb/Witmod dialect split. |
| [`akp-streamdeck.md`](akp-streamdeck.md) | Stream Dock family (AKP03/AKP05/AKP153/AKP815 + Mirabox N3/N4) — CRT framing, image upload, brightness/clear/version, touch strip (DRA), boot logo, the report-id-on-Linux issue, DFU. |
| [`methods-and-tooling.md`](methods-and-tooling.md) | The RE methodology: Ghidra headless, Frida live-hook, the hidapi probe scripts, the OSS corpora, the clean-room workflow. |
| [`capture-evidence.md`](capture-evidence.md) | Sanitised control-channel byte dumps captured this session (the wire bytes needed to verify on Fedora without re-capturing). |
| [`unexplored.md`](unexplored.md) | Cross-cutting open questions + the capture wishlist: everything NOT yet hardware-confirmed, per device and protocol-wide. |

## Confidence legend (used throughout)

- **HARDWARE-CONFIRMED** — observed working on a physical device this session
  (Frida hook of the vendor driver and/or our own probe round-trip).
- **Decompile/corpus** — derived from the Ghidra decompile and/or an OSS corpus;
  builders implemented; no live witness yet.
- **PROVISIONAL** — decompile-only and untested; treat as a hypothesis.
- **Unexplored** — not yet reverse-engineered; listed in `unexplored.md`.

## Companion docs (already in the repo)

The per-device byte-level references under `docs/protocols/{keyboard,mouse,streamdeck}/`
remain the maintained protocol specs; this dossier consolidates + cross-links
them and adds the session's hardware findings + the unexplored map. The probe
and capture tools live in `scripts/` (`ak980_tft_probe.py`, `aj_mouse_probe.py`,
`aj_mouse_frida_capture.py`).
