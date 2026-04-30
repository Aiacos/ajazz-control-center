<!--
  vendor-recon-runbook-windows.md — operator runbook for the
  Windows-VM portion of the AJAZZ vendor reverse-engineering
  campaign.

  This file documents WHAT to do, in WHAT ORDER, with WHAT TOOLS,
  to extend the static-analysis findings landed in
  `vendor-protocol-notes.md` (Findings 5–10, captured 2026-04-29)
  with **runtime captures** of the vendor app driving real AJAZZ
  hardware. The runbook is the deliverable that lets a downstream
  operator pick up the recon track without re-discovering the
  toolchain.

  Read `docs/research/README.md` first — clean-room and no-
  redistribution rules apply to every step. The single-engineer
  rule from § 1 of that doc is enforced here too: the operator
  who runs this runbook MUST NOT contribute to the matching
  `src/devices/<x>/` module afterward.

  **What this runbook does NOT cover** — and explicitly forbids:
    - Running the vendor installers on the operator's primary
      workstation. The "VM" in this doc is a HARD requirement,
      not a recommendation. Vendor installers may install kernel-
      mode HID filter drivers that persist across reboots.
    - Decompiling vendor binaries (Ghidra / IDA / dnSpyEx). This
      runbook only covers BEHAVIOURAL captures (Wireshark, file-
      system / registry diffs). Decompilation is a separate track
      with its own clean-room split.
    - Publishing raw `.pcapng` captures inside the repo. Captures
      live in the encrypted out-of-repo vault, indexed by capture-
      id from `vendor-protocol-notes.md`.
-->

# Vendor recon runbook — Windows VM track

> **Status — 2026-04-29**: scaffold + tested commands. No runtime
> captures landed yet — the runbook itself is the deliverable that
> unblocks them.

This runbook is the operator-facing companion to
[`vendor-protocol-notes.md`](vendor-protocol-notes.md). Findings
5–10 there were produced by static analysis on a regular Windows
host (no installer execution); the captures listed below require a
**disposable Windows VM** because they involve installing the
vendor software, connecting real hardware, and recording USB
traffic. The runbook keeps the operator's primary workstation
clean.

## 0. Prerequisites

Before starting the runbook, confirm:

- [ ] The static-analysis pass in `vendor-protocol-notes.md` is
  familiar to you. The runtime captures here cross-validate
  and refine those findings.
- [ ] You have **physical access to at least one AJAZZ device** in
  the priority list below. A capture without a real device is
  a paperweight.
- [ ] The clean-room split is acknowledged: **the operator running
  this runbook will NOT later commit code to the matching
  `src/devices/<family>/` module.** A different "clean"
  engineer reads the spec produced by this runbook and writes
  the implementation.
- [ ] An encrypted out-of-repo vault exists for raw captures (e.g.
  a VeraCrypt volume mounted at `D:\re-vault\` on the host,
  shared into the VM as a read-write folder). Raw `.pcapng`
  files NEVER enter the repo.

Priority device list (capture order — highest information density
first):

| Priority | Device               | Captures the                                          |
| -------- | -------------------- | ----------------------------------------------------- |
| 1        | AKP153 / AKP153E     | Stream Dock USB-HID surface + WebSocket localhost     |
| 2        | AKP03 / AKP05        | Stream Dock + encoder / dial / touch-strip frames     |
| 3        | AK820 Max RGB        | Qt-5 keyboard driver wire format, RGB curve commands  |
| 4        | AJ199 (any variant)  | Mouse HID Feature Reports — DPI / RGB / button remap  |
| 5        | AJ159 / AJ179 (any)  | Cross-validate against the AJ159 device manifest      |
| 6        | AK980 PRO (Microdia) | Disambiguates `0x0c45:0x8009` from the proprietary AK |

## 1. Disposable Windows VM setup

Pick whichever hypervisor you prefer; the runbook is written
hypervisor-agnostic. Recommended baseline:

- **Hyper-V on Windows 11 Pro (host)** — built-in, snapshots cheap,
  USB device passthrough via `Set-VMHost` Enhanced Session.
- **VirtualBox** — cross-platform, free, mature USB passthrough
  via the Extension Pack. Good if the host is Linux or macOS.
- **VMware Workstation Pro** — best USB-3 passthrough on Windows
  hosts; license required.

VM specs:

| Setting   | Value                                                           |
| --------- | --------------------------------------------------------------- |
| Guest OS  | Windows 10 22H2 or Windows 11 23H2 (English-US locale)          |
| RAM       | 4 GB (8 GB if running Stream Dock)                              |
| Disk      | 60 GB dynamic (~25 GB used after install)                       |
| Network   | NAT, no VPN, no proxy (so vendor app's CDN reach is observable) |
| USB       | USB-3 controller passthrough enabled                            |
| Snapshots | `clean-base`, `tools-installed`, `vendor-installed`             |

After first boot:

```powershell
# Install the recon toolchain (in the VM only)
winget install --silent --accept-source-agreements --accept-package-agreements `
  WiresharkFoundation.Wireshark `
  Python.Python.3.13 `
  7zip.7zip
# USBPcap ships inside the Wireshark installer; verify:
Get-Service USBPcap | Format-List Name, Status
```

Take snapshot `tools-installed` immediately. **All subsequent
captures revert to this snapshot before installing a new vendor
app**, so cross-app contamination (registry keys, shared services,
left-over kernel filters) is eliminated.

## 2. Per-device capture procedure

For each priority device:

### 2.1. Revert to `tools-installed` snapshot

This is the discipline that makes the runbook reproducible. Skip
this step and you cannot trust that a finding is attributable to
a single vendor app.

### 2.2. Plug in the device

Plug in via the host first to confirm Windows enumerates it
(Device Manager → HID-compliant device → properties → Hardware
Ids → record `VID_xxxx&PID_xxxx&MI_xx`). Then attach the USB
device to the VM via the hypervisor's USB passthrough.

In the VM, confirm enumeration:

```powershell
Get-PnpDevice -Class HIDClass | Where-Object FriendlyName -match 'AJAZZ|Mirabox|Stream Dock|Gaming|HID-compliant'
```

### 2.3. Start the capture

```powershell
# Wireshark CLI form. Replace USBPcapN with the bus that has the device.
& 'C:\Program Files\Wireshark\dumpcap.exe' `
  -i USBPcap1 `
  -w "C:\re-vault\captures\cap-2026-04-DD-<codename>-001.pcapng" `
  -P  # PCAP-NG format (lets us add a comment block per packet)
```

`USBPcap1` is the typical bus number for the device-attached USB
controller. Use `dumpcap -D` to list all interfaces and pick the
one corresponding to the AJAZZ device's VID:PID.

### 2.4. Install the vendor app

From the inventory in
[`vendor-software-inventory.md`](vendor-software-inventory.md),
download the installer **inside the VM** to
`C:\re-vault\installers\<codename>\`. Verify the SHA-256 against
the inventory's recorded hash before running:

```powershell
$expected = 'whatever-the-inventory-says'
$actual = (Get-FileHash 'C:\re-vault\installers\<file>.exe' -Algorithm SHA256).Hash.ToLower()
if ($actual -ne $expected) { throw "HASH MISMATCH — vendor URL may have rotated, abort and re-inventory." }
```

Run the installer, accept defaults, do NOT enable autostart-on-
login (we want manual control), do NOT report telemetry.

### 2.5. Exercise the device methodically

The capture's value is **proportional to how disciplined the
interaction script is**. One change at a time, with a wall-clock
note for each.

Per `docs/wiki/Reverse-Engineering.md` § "Capture workflow":

1. **Start the capture with the device unplugged.**
1. **Plug in the device** — enumeration descriptors are critical.
1. **Open the vendor app.**
1. For each **one** setting change (e.g. brightness 0 → 100):
   change the setting, click Apply, immediately note the wall-
   clock time in a text editor next to the capture.
1. Change every parameter at least three times so you can see the
   encoding (constant vs delta).
1. Change **one thing at a time**. A capture with N simultaneous
   changes is almost useless.

Per-device interaction scripts (target ≈ 10 minutes each):

- **AKP153**: open the vendor app → set brightness 0/50/100 →
  upload a 1×1 red, 1×1 green, 1×1 blue tile to key 0 → press
  every key once → press the same key 5 times rapidly (auto-
  repeat) → power-cycle the device by unplug / replug.
- **AKP03 / AKP05**: AKP153 script + rotate encoder CW 30° / CCW
  30° / press encoder → AKP05 only: tap touch-strip at 4 points
  along its length.
- **AK820 Max RGB**: open driver → switch each of the 6 RGB modes
  (流光 / 呼吸 / 常亮 / 霓虹 / 七彩波浪 / 关闭) → set all-keys
  red 50%, green 50%, blue 50% → record one macro of 4 keystrokes
  → toggle one key to a media key → save profile → restart driver.
- **AJ199 (any)**: open driver → set DPI stage 1 to 400, stage 2
  to 1600 → cycle DPI button → set polling rate 125/500/1000 →
  toggle each LED mode → set each button to a different macro
  action → record 1 macro of 3 keystrokes → save profile.
- **AJ159 / AJ179**: AJ199 script.

### 2.6. Stop the capture

```powershell
# Ctrl-C in the dumpcap window, or kill the process from another shell:
Stop-Process -Name dumpcap -Force
```

### 2.7. Sanitise and decode

Captures may contain serial numbers or credentials in HID fields.
Run the **descriptor-only** + **selective fields** decode before
anything lands in the spec:

```powershell
# Top-level summary (frame counts, USB descriptors, interfaces)
& 'C:\Program Files\Wireshark\tshark.exe' `
  -r 'C:\re-vault\captures\cap-2026-04-DD-<codename>-001.pcapng' `
  -Y 'usb' `
  -T fields `
  -e frame.number `
  -e usb.bus_id -e usb.device_address `
  -e usb.endpoint_address `
  -e usb.transfer_type `
  -e usb.capdata `
  > 'C:\re-vault\decoded\cap-2026-04-DD-<codename>-001.tsv'
```

**The `.tsv` is what gets quoted in `vendor-protocol-notes.md`,
not the `.pcapng`.** The `.pcapng` stays in the vault under its
capture-id.

### 2.8. Append findings to the spec

Open
[`vendor-protocol-notes.md`](vendor-protocol-notes.md), find the
matching `## Stream Dock` / `## Keyboards` / `## Mice` section,
and add a sub-heading:

```markdown
### <device codename> — <feature observed>

> **Capture id**: `cap-2026-04-DD-<codename>-001`. Method: ...

[byte-level layout of the relevant Feature Reports / OUT
endpoints, with prose paraphrasing each one's purpose]

Open questions:
- ...
```

The byte-level layout MUST be re-derived from the capture, not
copied from the vendor source's `driver_sensor.h` (Finding 10).
A spec entry without a `capture-id` is a guess and will be
rejected at PR review.

### 2.9. Cleanup

```powershell
# Forget the vendor app's data
Stop-Service -Name 'AJAZZ*' -ErrorAction SilentlyContinue
# Revert the VM to `tools-installed` for the next device
```

## 3. Stream Dock special case — admin install on disposable VM

The Stream Dock Windows installer is an Advanced Installer / Caphyon
EXE bootstrap (Finding 5). Static extraction stops at the opaque
`[0]` PE-in-PE payload. Two ways forward in the VM:

### 3.1. Full install (preferred for runtime captures)

Just run the bootstrap and let it install — the runtime captures
in § 2.5 are the deliverable. Standard procedure.

### 3.2. Admin extract (for static cross-check)

To enumerate the Qt 5 binaries actually shipped (Finding 3 was
inferred from strings), use Caphyon's documented `/extract` flag:

```powershell
& 'C:\re-vault\installers\streamdock\Stream-Dock-AJAZZ-Installer_Windows_global.exe' `
  /extract /silent `
  TARGETDIR='C:\re-vault\extracted\streamdock-msi'
```

If `/extract` is not honoured by this specific Advanced Installer
build, fall back to:

```powershell
# Run msiexec admin-install on the embedded MSI once the
# bootstrap has dropped it to %TEMP%\AdvancedInstaller_*\
$msi = Get-ChildItem $env:TEMP\AdvancedInstaller_* -Filter '*.msi' -Recurse | Select-Object -First 1
& msiexec.exe /a $msi.FullName /qn TARGETDIR='C:\re-vault\extracted\streamdock-msi'
```

`msiexec /a` is the Microsoft-blessed admin-install: it extracts
the MSI's file table to a flat tree **without** running the
`InstallExecuteSequence` actions (no driver registration, no
service install, no kernel filter registration). The result is
the same set of Qt 5 binaries and `index.html_*` PI pages that
the user mode install would drop, minus the runtime side effects.

Static cross-checks to land in `vendor-protocol-notes.md` after
the admin extract:

- [ ] Confirm Qt 5 framework versions (read `Qt5Core.dll`'s
  VersionInfo).
- [ ] Enumerate the `index.html_*` PI pages (file count + each
  one's size + which `$SD` events the JS calls). HTML / JS
  content is configuration-shaped; reading is clean-room
  acceptable. **Do NOT** copy the JS into our `pi_bridge.cpp`.
- [ ] Read the locale `.json` files for the action catalogue
  strings — these are i18n, like the `text.xml` files inspected
  in Finding 9 for keyboards / mice.
- [ ] Inspect `FirmwareUpgradeTool.exe`'s PE imports — does it
  link `Qt5SerialPort.dll`? That confirms / refutes the
  USB-CDC firmware-upgrade hypothesis from Finding 1.

## 4. Mirabox-branded Stream Dock — special case

The discovery page `https://mirabox.key123.vip/download` is JS-
rendered and not crawl-friendly. From the VM (with a real browser
session):

1. Open Edge / Chrome → DevTools → Network tab → load the page.
1. Trigger any download button → record the resolved CDN URL the
   browser hits. That is the artefact URL to add to the inventory.
1. HEAD that URL with `curl.exe -I` and capture Content-Length,
   Last-Modified, ETag, Content-MD5.
1. Append the row to
   [`vendor-software-inventory.md`](vendor-software-inventory.md)
   under the existing "Stream Dock — Mirabox generic" placeholder
   row.

## 5. Logging the run

Every capture session must be logged in the **recon journal**
(out-of-repo, e.g. `C:\re-vault\journal\2026-04-DD.md`). One
line per session:

```text
2026-04-DD T HH:MM  cap-2026-04-DD-<codename>-NNN  <duration>  <interaction-script-summary>  <vault-path>
```

The journal is the audit trail for "what the operator saw, when".
Without it, individual captures lose attribution to a specific
vendor app version, hardware revision, and interaction script —
which makes them un-citable in clean-room.

## 6. Common pitfalls

- **Forgot to revert the snapshot before installing a second
  vendor app.** Restart at § 2.1 and discard the contaminated
  capture.
- **Captured with the device on the host's USB stack instead of
  the VM's.** Symptom: zero `usb.capdata` in the decode. Confirm
  via `Get-PnpDevice` inside the VM — the device must enumerate
  there, not just on the host.
- **Used `tshark -e usb.data_fragment` instead of `usb.capdata`.**
  Stream-Dock-class devices send their commands inside a single
  Feature Report; `data_fragment` is for split USB transfers.
  `capdata` is the canonical field.
- **Edited the spec without a `capture-id`.** The PR will be
  blocked. Always cite the capture-id; if you don't have one, you
  don't have a fact.
- **Ran the installer on the host machine "to see what it does".**
  This violates the runbook and contaminates the host's HID
  filter stack until the next OS reinstall. Snapshot-revert is
  not optional.

## 7. After the runbook closes a capture

Once a capture has produced a section in
`vendor-protocol-notes.md`:

1. Update [`vendor-feature-matrix.md`](vendor-feature-matrix.md):
   flip the matching ❓ cell to ✅ (vendor capability confirmed)
   or ❌ (vendor capability refuted), with the `capture-id`
   appended to the Tracking column.
1. If the capture surfaced a parity gap, file a TODO entry into
   the **Protocol parity backlog** section of
   [`TODO.md`](../../TODO.md#reverse-engineering--vendor-parity-multi-day-research).
1. If the capture surfaced a stability technique we should port
   (reconnect debounce timing, firmware update retry strategy,
   sleep / wake handshake), append a row to
   [`vendor-techniques.md`](vendor-techniques.md).

The runbook is complete when `vendor-protocol-notes.md` has at
least one runtime capture per priority-list device, and
`vendor-feature-matrix.md` has zero ❓ cells in the
keyboard / mouse / Stream Dock sections.
