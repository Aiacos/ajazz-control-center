<!--
  vendor-recon-runbook-fedora.md — Linux-side workflow for the
  AJAZZ vendor reverse-engineering campaign.

  Companion to `vendor-recon-runbook-windows.md`. The Windows
  runbook is for capturing what the vendor's official driver
  sends to the device; this Fedora runbook is for everything
  ELSE — verifying enumeration, ACLs, our own outgoing wire
  bytes, and HID report descriptors. Most validation work that
  closes the wire-format reconciliation (TODO § "AJ-series wire
  format reconciliation") can be done from Fedora alone, without
  touching Windows. Only the final "what does the vendor send"
  step needs a Windows VM.

  Read `docs/research/README.md` first — clean-room and no-
  redistribution rules apply to every step. The single-engineer
  rule from § 1 of that doc applies here too: if you read the
  spec excerpts in vendor-protocol-notes.md Findings 11–15 and
  then capture wire bytes from a real device, you become
  tainted and can no longer modify the matching `src/devices/<x>/`
  module. A different "clean" engineer reads only the byte-level
  spec produced by these captures (NOT the Findings themselves)
  and writes the implementation.
-->

# Vendor recon runbook — Fedora / Linux native track

> **Status — 2026-04-30**: scaffold + tested commands. Use this
> alongside `vendor-recon-runbook-windows.md` (which covers the
> VM-only "capture the vendor" steps).

This runbook documents the Linux-native workflow for validating
the device enumeration fix (commit `bef8e26`) and for capturing
**our own** outgoing HID Feature Reports for byte-level comparison
against the wire-format dialects documented in
[`vendor-protocol-notes.md`](vendor-protocol-notes.md) Findings
11–15.

The fundamental asymmetry: AJAZZ does not ship Linux drivers, so
**you cannot capture the vendor's bytes from Fedora**. But you
**can** capture every byte AJAZZ Control Center sends to the
device, and every byte the device sends back over `/dev/hidraw*`.
That is enough to validate ~80% of the spec (enumeration, our
wire format, HID report descriptor) without ever booting a
Windows VM.

## 0. Prerequisites

```bash
# Tools installed once on the Fedora host
sudo dnf install -y wireshark-cli hid-tools usbutils ripgrep jq

# usbmon is in-tree; load it lazily, unload after the session
# (per the clean-room methodology — no persistent system mutations).
sudo modprobe usbmon
ls -la /dev/usbmon*           # must show 7-8 devices, one per USB bus
```

Confirm the host's Wireshark group membership:

```bash
groups | grep -q wireshark || echo "WARN: not in wireshark group; tshark needs sudo"
```

## 1. Validate the VID:PID enumeration fix

Plug an AJ-series mouse into the Fedora host. The fix in commit
`bef8e26` widened our enumeration to cover VIDs `0x248A`,
`0x249A`, and `0x3554`. Verify the device enumerates under one of
these VIDs, **not** the previously-fictional `0x3554:0xF51A-D`.

```bash
# Look up by VID — should print the AJ-series mouse
lsusb -d 248a:           # AJ139 / AJ159 / AJ179 family (USB-wired modes 0x5C2E / 0x5D2E / 0x5E2E)
lsusb -d 249a:           # Same family in 2.4G dongle mode (alt VID)
lsusb -d 3554:           # AJ199 family (PIDs in 0xF500-0xF5D5 range)
```

Acceptance: at least one of the three lines shows a Bus / Device
ID. The reported `idProduct` should match one of the families
documented in `vendor-protocol-notes.md` Finding 8 (for `248a` /
`249a`) or Finding 9 (for `3554`'s base64-decoded `M_PID` /
`D_PID` lists).

If `lsusb` returns nothing for any of the three VIDs:

- The device may be in a USB-mode descriptor not yet enumerated
  — capture the actual VID:PID via `lsusb` (no filter) and add
  to `docs/_data/devices.yaml` + `src/devices/mouse/src/register.cpp`
  - `resources/linux/99-ajazz.rules`.
- Or the device is an AJ339 / AJ380 (which we removed from the
  registry on 2026-04-29 because their VID:PID was a guess —
  this is the runtime capture that confirms the right values).

Record the VID:PID for the next step.

## 2. Validate the udev `uaccess` rule

After commit `bef8e26`, `resources/linux/99-ajazz.rules` covers
`248a` / `249a` / `3554`. Install it and verify the ACL fires:

```bash
# Install the rule (system-mutation — record this in the recon journal
# so you remember to remove it at session end if you want a fully clean
# host afterward).
sudo install -m 644 resources/linux/99-ajazz.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --action=change --subsystem-match=hidraw

# Replug the device, then check:
ls -la /dev/hidraw*                 # one of these should be owned by your uid
                                    # (or have an ACL granting your uid rw)
getfacl /dev/hidraw0 2>/dev/null    # confirm `user:<you>:rw-` line is present
```

Acceptance: at least one `hidrawN` node has `user:<your-uid>:rw-`
in its ACL. If not, the udev rule did not match — verify
`ATTRS{idVendor}=="<lowercase hex>"` matches the `lsusb` output's
VID exactly (rules use lowercase hex).

## 3. Read the HID report descriptor

The vendor's HID report descriptor enumerates every Feature
Report ID, the wire-format scope, and field semantics — directly,
without disassembly. This is the fastest way to disambiguate
which of the three dialects (OemDrv / HIDUsb / Witmod) the device
actually exposes.

```bash
# Find the hidraw node for the device
ls -la /sys/class/hidraw/*/device/uevent | grep -i "AJAZZ\|248A\|249A\|3554"

# Decode the descriptor (replace hidrawN with the node from above)
sudo hid-decode /dev/hidraw0
```

The output names every Feature Report (`Feature(...)`) and
Output Report (`Output(...)`) by ID. Cross-reference against
`vendor-protocol-notes.md`:

- Report ID `0x01` + 20-byte feature → matches **HIDUsb dialect**
  (Finding 12.A) → AJ199 Max family.
- Report ID `0x05` + 64-byte feature → matches **our impl** (and
  the Witmod dialect, Finding 13.A) — common case for AK820 Max
  RGB / newer firmware.
- Report ID `0x08` + 17-byte feature → matches **OemDrv dialect**
  (Finding 11.A / 15.B) → AJ199 V1.0.

Record the **observed** report ID + feature length + count of
reports. That is half the wire-format question already answered.

## 4. Capture our app's outgoing Feature Reports

Run AJAZZ Control Center and capture the bytes it sends. Compare
to the spec excerpts.

```bash
# Identify the right usbmon bus first
lsusb -t                            # find the bus the AJAZZ device is on, e.g. Bus 003
# Bus 003 → /dev/usbmon3

# Start capture in one terminal
sudo tshark -i usbmon3 \
            -Y "usb.idVendor == 0x248a || usb.idVendor == 0x249a || usb.idVendor == 0x3554" \
            -T fields \
            -e frame.number -e usb.transfer_type \
            -e usb.bmRequestType -e usb.bRequest \
            -e usb.capdata \
            > capture-our-app.tsv

# In another terminal: build + run the app, exercise the device
make build
make run                            # plug device, change DPI, change RGB, etc.
                                    # do ONE setting change at a time, with a wall-clock note

# Stop tshark with Ctrl-C when done
```

Acceptance: at least one row in `capture-our-app.tsv` should have
a non-empty `usb.capdata` field with bytes matching our
`aj_series.cpp` envelope (`05 cmd sub len ... checksum`). If the
capture is empty:

- Confirm the app actually writes (it may be no-oping silently if
  the device handle didn't open — check `journalctl --user -t ajazz-control-center`
  for log lines).
- Confirm the capture was on the right bus (`lsusb -t` again).

Sanitise the `.tsv` before pasting any bytes into a public PR
(strip serial numbers, MAC-like fields, anything that looks
hash-like in the response).

## 5. Capture the device's response

Same `tshark` command but capture both directions:

```bash
sudo tshark -i usbmon3 \
            -Y "usb.idVendor == 0x248a || usb.idVendor == 0x249a || usb.idVendor == 0x3554" \
            -T fields \
            -e frame.number \
            -e usb.endpoint_address.direction \
            -e usb.capdata \
            > capture-bidirectional.tsv

# direction column: 0 = OUT (host → device), 1 = IN (device → host)
```

Acceptance: for every OUT Feature Report we send, an IN Feature
Report should arrive with the matching response. Cross-check
against the receive-side struct in Finding 12.C
(`UsbCommand { ReportId, id, CommandStatus, address, command[10], data[] }`)
to identify whether the device speaks the HIDUsb dialect.

## 6. Decision tree after captures

| Captured shape (our OUT + device IN)                                                   | Verdict                                                                                                                                         |
| -------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| Our 64-byte / RID 0x05 packet → device responds with structured data                   | **Our impl is correct for this device's firmware**. Mark in the inventory. Wire-format reconciliation NEEDED for OTHER firmware revisions only. |
| Our packet sent → device sends an error response (e.g. all-zero, status byte non-zero) | Wire-format mismatch confirmed. Re-derive byte-level by capturing vendor app on Windows VM (runbook § 2.5).                                     |
| Our packet sent → device silent (no IN report)                                         | Either wrong report ID or device is firewalling unknown commands. Same fix as above.                                                            |
| `tshark` shows no traffic at all                                                       | Device path not opened. Diagnose `hidapi` log via `AJAZZ_LOG=trace make run`.                                                                   |

## 7. Build + run our test suite

```bash
make build                          # cmake configure + build (debug, with sanitizers)
make test                           # ctest under the dev preset

# Specifically check the device-registry tests post-fix
ctest --preset dev --output-on-failure -R 'registry'

# clang-tidy on the changed files
make lint
```

Acceptance: all green. The 2026-04-29 fix bumped `mouseCount`
from 4 to 6 in the registry; the test `device registry enumerates all three families` should still pass (`mouseCount >= 4` was the
floor).

## 8. Cleanup

```bash
sudo rmmod usbmon                                          # unload kernel module
sudo rm /etc/udev/rules.d/99-ajazz.rules                   # if you want a fully clean host
sudo udevadm control --reload-rules

# Vault hygiene: any .tsv with non-trivial bytes goes to the
# encrypted out-of-repo vault, indexed by capture-id, NEVER
# committed to the repo.
shred -u capture-*.tsv
```

## 9. Clean-room reminder

If you read `vendor-protocol-notes.md` Findings 11–15 (the TAINTED
disassembly findings) AND then run the captures in §§ 4-6 above,
you have now consolidated tainted spec knowledge plus runtime wire
observations in the same operator instance. Per
`docs/research/README.md` § 1, you cannot afterward contribute to
`src/devices/{mouse,keyboard,streamdeck}/` for the AJAZZ devices in
scope. Hand the captures + a clean spec writeup to a different
engineer.

If you are doing both roles in alternating sessions: keep
**capture sessions** strictly separate from **implementation
sessions** in your local timeline, and never have the implementation
session re-read this runbook or any of Findings 11–15.
