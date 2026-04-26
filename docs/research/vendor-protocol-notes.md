<!--
  vendor-protocol-notes.md — RE task 2 deliverable (scaffold).

  Clean-room observations of the AJAZZ vendor protocols. Every entry
  must trace to a capture (USB / WebSocket / IPC / on-disk artefact)
  produced under controlled conditions, NOT to vendor source.

  Read `docs/research/README.md` for the rules. Critical ones to
  re-read:

    1. The single engineer who reads vendor sources for a given
       module writes the spec but does NOT contribute to the matching
       implementation file in src/. A second "clean" engineer
       implements from the spec. This rule is enforced socially —
       the commit logs of `src/devices/<x>/` and the corresponding
       section here MUST NOT share an author.
    2. Captures live in this repo only as **descriptions** (header
       summaries, byte-level layouts, state-machine notes). Raw
       captures (.pcap, .usbmon trace files, .json IPC dumps)
       belong in the encrypted out-of-repo vault, indexed here by
       capture id.
    3. Every entry carries a capture id like `cap-2026-04-26-akp03-001`
       so the vault index can be looked up after the fact.

  Until captures land, this file holds only the section skeleton +
  the methodology guide. Do not invent protocol details.
-->

# Vendor protocol — clean-room notes

Per-module protocol & feature inventory derived from controlled
captures of the AJAZZ first-party desktop apps. **Read
[`docs/research/README.md`](README.md) before contributing** — there
are hard clean-room and capture-vaulting rules.

> **Status — 2026-04-26**: scaffold only. No captures landed yet.
> The methodology section is canonical; sections per device family
> below are placeholders that recon engineers should fill as they
> close captures.

## Methodology

### Capture environments

Every capture must record the environment so it can be reproduced
or refuted. Required metadata for each capture:

| Field                | Example                                                                                                       | Why                                                                               |
| -------------------- | ------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------- |
| `capture-id`         | `cap-2026-04-26-akp03-001`                                                                                    | Stable handle for cross-references; date-prefixed so the timeline is recoverable. |
| `host-os`            | `Windows 11 Pro 23H2 (build 22631.3007)`                                                                      | Vendor app behavior changes with USB-stack version.                               |
| `vendor-app`         | `Stream-Dock-AJAZZ-Installer_Windows_global.exe` Content-MD5 `a1828628…` (see `vendor-software-inventory.md`) | Different build → different protocol.                                             |
| `device`             | `AJAZZ AKP03 / Mirabox N3 — VID 0x0300 PID 0x3001 — firmware fw-string-as-reported`                           | Some commands are firmware-gated.                                                 |
| `usb-stack`          | `Wireshark 4.4 + USBPcap 1.5.4.0` (Windows) / `usbmon` mod + `tshark` (Linux)                                 | Stack-specific framing.                                                           |
| `start-time`         | `2026-04-26T14:00:00Z`                                                                                        | Reorder captures by chronology.                                                   |
| `duration`           | `12 min`                                                                                                      | Sanity-check whether claims about "every event" are exhaustive.                   |
| `interaction-script` | "Boot vendor app, click 4 keys, rotate dial 30°, sleep + wake host."                                          | Without this, the capture cannot be reproduced.                                   |

### Tool checklist (no installations preserved on the host)

The recon host should be a disposable VM or a tools-removed-on-cleanup
workstation. The following toolchain is the recommended baseline; it
is provided as a checklist, NOT as a supply chain — install via OS
package manager, use, then remove with the matching uninstall step
recorded in the capture journal:

| Tool                              | Purpose                                                                                     | Install / remove                                                                                                  |
| --------------------------------- | ------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `wireshark` + `usbpcap` (Windows) | USB capture.                                                                                | `winget install WiresharkFoundation.Wireshark` / matching `winget uninstall …`.                                   |
| `tshark` + `usbmon` (Linux)       | USB capture without GUI.                                                                    | `dnf install wireshark-cli` / `dnf remove …` (Fedora) — `usbmon` is in-tree (`modprobe usbmon` / `rmmod usbmon`). |
| `asar` (npm)                      | Unpack Electron payloads.                                                                   | `npm install -g @electron/asar` / `npm uninstall -g @electron/asar`.                                              |
| `js-beautify` (npm)               | Reformat minified Electron renderer code for *reading* (never re-typing into our codebase). | `npm install -g js-beautify` / `npm uninstall -g js-beautify`.                                                    |
| `ghidra` or `radare2`             | Decompile native binaries (firmware, .dll glue).                                            | Vault-resident — install in disposable VM only.                                                                   |
| `ILSpy` / `dnSpyEx`               | Decompile .NET payloads.                                                                    | Vault-resident — install in disposable VM only.                                                                   |
| `signify` / `gpg`                 | Verify vendor download signatures.                                                          | Standard system tooling.                                                                                          |

For Linux hosts: any `dnf install` / `dnf remove` operation MUST be
recorded in the capture journal so the operator can revert the host
to a known-clean state. Per repo policy
([`feedback_no_system_mutations.md`](../../README.md)), no system-level
mutations should persist past the capture session.

### Protocol description shape

Each section below should follow this shape so downstream
implementers have a uniform contract to work against:

1. **Transport summary** — USB-HID? Custom HID interface? WebSocket
   over localhost? File-system poll?
1. **Endianness, framing, command set** — byte-level, with
   `report-id`, length, opcode, payload, terminator (where
   applicable). Always observed, never re-typed from vendor source.
1. **State machine** — handshake, steady-state, reconnect, sleep /
   wake, error recovery.
1. **Open questions** — list explicitly the things that recon could
   not verify with the captures listed. Implementers MUST treat
   these as defensive boundaries (validate-and-reject, never assume).
1. **Capture references** — link every observation to its
   `capture-id`. A statement without a capture-id is a guess and
   should not be in the doc.

## Stream Dock — `streamDock` IPC + USB-HID

> **Status — 2026-04-26**: not yet captured. Existing OSS work
> (`opendeck-akp03`, `opendeck-akp153`, `mirajazz`) covered the
> AKP03 + AKP153 USB-HID surface from independent recon — those
> independent results inform `src/devices/streamdeck/` already.
> The vendor desktop app's WebSocket layer (`localhost:port`,
> Stream Deck SDK-2 dialect) and its plugin lifecycle have NOT
> been captured under our methodology. The Plugin SDK doc
> (`docs/architecture/PLUGIN-SDK.md`) is our spec; this section
> will track the deltas observed by recon.

Sub-sections to fill once captures land:

- `streamDock` WebSocket — connection handshake, auth (if any),
  event dialect (Stream Deck SDK-2 superset).
- AKP03 / AKP153 / AKP05 USB-HID — already-documented protocol
  cross-checks (see `docs/protocols/streamdeck/*.md`); new entries
  here only when the vendor app uses commands the OSS does not.
- AKP815 (with screen) USB-HID — not yet supported in our backend.
- Firmware update flow — observed delivery of the firmware blob
  - the subset of HID reports used to apply it. **High value**:
    vendor knows how to recover from a half-applied flash; capture
    the retry / rollback path before any of our own DFU work starts.

## Keyboards — proprietary protocol

> **Status — 2026-04-26**: not yet captured. Existing
> `src/devices/keyboard/` proprietary backend already covers
> AK-series RGB zones + macro upload, derived from independent
> recon. Vendor app's macro-record format and HE actuation curve
> (AK820 Max HE / AK680 Max) are unverified by our captures.

Sub-sections to fill:

- AK980 PRO — protocol mapping (currently scaffolded only).
- AK820 / AK820 Pro / AK820 Max RGB — macro encoding, layer
  switching, RGB curve format.
- AK680 Max (HE) — magnetic-switch analog actuation curve format.
- Wireless tri-mode — pairing handshake on the 2.4 GHz dongle.

## Mice — AJ-series proprietary

> **Status — 2026-04-26**: not yet captured. AJ199 / AJ159 / AJ339
> already enumerated and DPI / RGB working via independent recon.

Sub-sections to fill:

- AJ199 family (No-RGB / Max / Carbon Fibre) — DPI stage encoding,
  RGB ramp, polling-rate selector.
- AJ339 / AJ380 — confirm whether protocol matches AJ199 family;
  protocol parity matrix ends up in
  [`vendor-feature-matrix.md`](vendor-feature-matrix.md).
- Battery level + sleep timeout — not yet exposed by our
  `IMouseCapable` interface.

## Plugin SDK — `space.key123.vip/StreamDock/plugins`

The plugin store API surface is documented at
`https://sdk.key123.vip/en/guide/overview.html` and we already
implement the `productInfo/list` catalogue endpoint via
`src/app/src/streamdock_catalog_fetcher.cpp`. This section will host
the deltas a recon pass surfaces (per-plugin signed bundle layout,
Sigstore-equivalent verification, install / uninstall / update
state machine).

## How to add a section

1. Pick a `capture-id` and run captures end-to-end on a disposable
   host. Record the metadata table at the top of the section.
1. Write the protocol summary in your own words, using the shape
   above. Cite every byte-level claim with the capture id.
1. Open a PR. The PR author MUST NOT also be the one who will
   implement the matching `src/devices/...` module — flag this in
   the PR description so the implementer is a different engineer.
1. Once merged, file a TODO entry per gap surfaced (parity backlog,
   per RE task 4 in `TODO.md`).
