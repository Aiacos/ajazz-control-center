# USB Capture Runbook — Wireshark + usbmon + dumpcap

This runbook documents how a developer with one of the 4 connected AJAZZ
v1.2 devices on their bench produces sanitised USB-HID capture fixtures
for the v1.2 milestone. It is the operational counterpart to
[`docs/policies/capture-data-hygiene.md`](../policies/capture-data-hygiene.md)
(the policy + Pitfall 17 hygiene boundary) and the
[`.planning/research/captures/README.md`](../../.planning/research/captures/README.md)
scratch sink. The companion methodology doc is
[`docs/protocols/REVERSE_ENGINEERING.md`](REVERSE_ENGINEERING.md).

**This is a DEVELOPER activity, not a user activity.** There is no in-app
sniffer dialog, no telemetry upload, no log-export-with-pcap-attached
flow. The control center never carries USB capture machinery in
production builds. All commands below are run by you, on your bench,
once, to produce a sanitised hex fixture that ends up under
`tests/integration/fixtures/<codename>/<label>.h`.

> **RAW CAPTURE FILES NEVER LEAVE YOUR LOCAL MACHINE.** The pre-commit
> hook (`scripts/reject-raw-captures.sh`, plan 09-01) rejects any
> `*.pcap` / `*.pcapng` at any path. The only thing that gets committed
> is the sanitised C++ array literal under
> `tests/integration/fixtures/<codename>/`. The raw `.pcap` and the
> intermediate `usbrply` JSON stay on your `/tmp` (or the gitignored
> `.planning/research/captures/` scratch sink) and get deleted as soon
> as the fixture is committed.

______________________________________________________________________

## 1. Prerequisites (one-time setup)

The agent does NOT install these for you (CLAUDE.md hard rule — no
system-level mutations from project tooling). Run these on your own
machine, once.

### 1.1 Wireshark + tshark + dumpcap

| Distro          | Command                                    |
| --------------- | ------------------------------------------ |
| Fedora          | `sudo dnf install wireshark wireshark-cli` |
| Debian / Ubuntu | `sudo apt install wireshark tshark`        |

The Debian/Ubuntu installer asks "Should non-superusers be able to
capture packets?" — answer **Yes** so that `dumpcap` gets the
`cap_net_raw` capability and the `wireshark` group is created. On
Fedora that group is created by the `wireshark-cli` post-install.

### 1.2 usbmon kernel module

```text
sudo modprobe usbmon
ls /sys/kernel/debug/usb/usbmon/
```

You should see entries like `0s`, `0u`, `1s`, `1u`, `2s`, `2u`, ...
One pair per USB bus on your machine. The `Nu` entries are the
binary-API monitors that `tshark` / `dumpcap` consume; the `Ns` entries
are the text-API monitors (do not use these — they truncate report
payloads). `0u` is the catch-all "all buses concatenated" monitor; we
explicitly **avoid** it (see step 3 — Pitfall 17 cross-cutting traffic
leak).

If `/sys/kernel/debug/usb/usbmon/` does not exist, the kernel does not
have `CONFIG_USB_MON` enabled. This is rare on mainstream Fedora /
Debian / Ubuntu kernels but common on minimal containers. Run the
capture on the host, not in the container.

To make `usbmon` survive a reboot:

```text
echo usbmon | sudo tee /etc/modules-load.d/usbmon.conf
```

### 1.3 wireshark group membership (non-root capture)

```text
sudo usermod -aG wireshark $USER
```

Then **log out and log back in** for the new group to take effect.
Verify:

```text
id | grep wireshark
```

If `id` does not list `wireshark`, you have not re-logged-in yet. As a
short-lived workaround within the current shell, use `newgrp wireshark`
— but the proper fix is to log out and back in so all future shells
inherit the membership.

### 1.4 usbrply (Python dev-time helper)

```text
pipx install usbrply
# or:
pip install --user usbrply
```

`usbrply` is the agreed `.pcap` → JSON tool — see STACK.md §
"Test-replay infrastructure" and PROJECT.md decision D-03. We do NOT
take a `libpcap` dependency in the in-tree codebase; the `.pcap` →
JSON conversion happens out-of-tree as a dev-time step. Pin to
`usbrply` v2.1.1 (STACK.md current; check `usbrply --version`).

### 1.5 References (cross-link)

- [`docs/policies/capture-data-hygiene.md`](../policies/capture-data-hygiene.md)
  — privacy threat write-up + the sanitised workflow.
- `.planning/research/STACK.md` § "NEW developer-prereqs" — the upstream
  tooling reference this runbook mirrors.
- `.planning/research/STACK.md` § "Test-replay infrastructure" —
  capture-flow rationale + DLT compatibility note.

______________________________________________________________________

## 2. Identify the device's USB bus and address

You need to know which `usbmonN` interface the target device is on
**before** you start capturing. Capturing on the wrong bus produces an
empty `tshark -Y` filter at extraction time, and capturing on the
all-buses monitor (`usbmon0`) cross-pollutes your `.pcap` with
keystrokes from the rest of your machine — exactly the Pitfall 17 leak
path the hygiene policy exists to prevent.

### 2.1 Find the (vid:pid) line

```text
lsusb
```

You will see lines like:

```text
Bus 003 Device 007: ID 0c45:8009 AKKO AK980 PRO
Bus 003 Device 008: ID 0c45:7016 AKKO 2.4G Receiver
Bus 002 Device 005: ID 0300:3004 AJAZZ AKP03
Bus 001 Device 011: ID 3151:5007 AJAZZ AJ199 8K
```

### 2.2 Confirm with descriptors

```text
lsusb -d 0c45:8009 -v 2>/dev/null | head -20
```

Read the `iManufacturer` and `iProduct` strings to confirm it is the
device you think it is. (Some `0c45:xxxx` PIDs are unrelated Microdia
webcams; do NOT assume the first match is correct.)

### 2.3 Identify the bus number

```text
lsusb -t
```

Read the topology tree to confirm the bus the device is on. Example:

```text
/:  Bus 003.Port 001: Dev 1, Class=root_hub, Driver=xhci_hcd/12p, 480M
    |__ Port 002: Dev 007, If 0, Class=Human Interface Device, Driver=usbhid, 12M
    |__ Port 002: Dev 007, If 1, Class=Human Interface Device, Driver=usbhid, 12M
```

The leading `Bus 003` maps to **`usbmon3`** for the capture interface.
Bus indices are 1-based; the all-buses concatenator `usbmon0` is
explicitly NOT what you want (Pitfall 17). Capturing per-bus is the
first half of the per-device filter; the display filter in step 4 is
the second half.

______________________________________________________________________

## 3. Per-device capture filter table

| Codename               | VID:PID   | Wireshark display filter                                                           | Notes                                                                                                                                                                                                                                                                                           |
| ---------------------- | --------- | ---------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `akp03_variant_3004`   | 0300:3004 | `usb.idVendor == 0x0300 && usb.idProduct == 0x3004`                                | Pitfall 22: capture one image-upload-first-chunk + one brightness change + one encoder rotate; diff against AKP03 OSS-corpus baseline (mirajazz / opendeck-akp03 / ajazz-sdk). 1024-byte chunks with last-chunk `0x01` flag is the mirajazz convention.                                         |
| `ak980pro`             | 0c45:8009 | `usb.idVendor == 0x0c45 && usb.idProduct == 0x8009 && usb.bInterfaceProtocol != 1` | Pitfall 17: filter out boot-keyboard reports with `usb.bInterfaceProtocol != 1` to keep only the vendor-control interface. Pitfall 24: the RGB transition sweep is the natural control-channel event window.                                                                                    |
| `ajazz_24g_8k`         | 3151:5007 | `usb.idVendor == 0x3151 && usb.idProduct == 0x5007`                                | Pitfall 21: cap capture file at 50 MB. Capture only during control-channel events (DPI button press, polling-rate change, LOD change, RGB mode change), NOT during sustained movement — 8 kHz polling fills capture buffers in single-digit seconds.                                            |
| `microdia_dongle_7016` | 0c45:7016 | `usb.idVendor == 0x0c45 && usb.idProduct == 0x7016 && usb.bInterfaceProtocol != 1` | Pitfall 17: drop boot-keyboard reports — Microdia dongles expose a HID-boot-keyboard interface even when they relay a non-keyboard device. Pitfall 20: confirm this is a SEPARATE dongle by unplugging `ak980pro` and verifying `0c45:7016` does NOT disappear; capture each PID independently. |

The filters above are **Wireshark display filter** syntax. They are
applied via `tshark -Y` at extraction time, NOT via `tshark -f`
(BPF capture filter — see step 4.2 below for the distinction; it is a
common usbmon footgun).

______________________________________________________________________

## 4. Capture session — step by step

### 4.1 Identify usbmonN per section 2

Suppose `lsusb -t` placed `0300:3004` on `Bus 002`. The capture
interface is `usbmon2`.

### 4.2 Display filter vs. capture filter (the usbmon footgun)

The `usb.idVendor == 0xVVVV && usb.idProduct == 0xPPPP` filter shape
is a **display filter** (Wireshark dialect, applied after-the-fact via
`-Y`). The `-f` flag on `tshark` / `dumpcap` is a **capture filter**
(libpcap BPF syntax), and BPF does NOT understand the
`usb.idVendor` keyword — feeding the display filter to `-f` produces
"syntax error in filter expression". For per-device filtering, the
mechanical workflow is:

1. Capture broadly on **one usbmon bus** (already a per-bus filter —
   keystrokes from a keyboard on a *different* bus never reach this
   `.pcap`).
1. Apply the per-device VID/PID display filter at extraction time
   via `-Y`.

Documenting this explicitly because it surprises every first-time
usbmon user.

### 4.3 Capture

```text
tshark -i usbmon2 -w /tmp/cap.pcapng -a duration:5
```

Flags:

- `-i usbmon2` — capture on bus 2 (substitute your bus number).
- `-w /tmp/cap.pcapng` — write raw frames to `/tmp` (NOT into the repo;
  the pre-commit hook rejects it anyway, but `/tmp` is the
  recommended scratch location — it is RAM-backed on most modern
  Linux systems, so deletion is immediate and trace-free).
- `-a duration:5` — auto-stop after 5 seconds. Bound the window
  explicitly so you do not accidentally leave the capture running.

For longer-running event windows, raise `duration:` — but stay under
~50 MB total file size for the AJAZZ 8K mouse (Pitfall 21):

```text
tshark -i usbmon1 -w /tmp/cap.pcapng -a filesize:51200 -a duration:30
```

### 4.4 Trigger the device action

While the capture is running, **manually trigger the one specific
device action you want to study**:

- For `akp03_variant_3004`: push an image from a parallel boot of the
  vendor app, OR change brightness, OR rotate one encoder click. ONE
  action per capture.
- For `ak980pro`: change one RGB mode (Pitfall 24 transition sweep is
  one natural window), OR toggle Bluetooth/2.4G/wired profile.
- For `ajazz_24g_8k`: press one of the DPI / polling-rate / LOD /
  RGB-mode buttons on the underside. AVOID sustained movement — even
  a 1-second swipe at 8 kHz produces 8000 interrupt-in packets.
- For `microdia_dongle_7016`: this dongle's "action" is its paired
  device pressing a key / clicking / moving. Capture a short
  click-or-keystroke window from the paired peripheral while the
  capture is running.

Operator-driven event windowing is what keeps captures
context-bounded and reviewable. Random "let it run for a minute" is
not the discipline.

### 4.5 Stop the capture

`-a duration:5` already bounds it. If you used interactive Wireshark
or `dumpcap`, Ctrl-C is the stop signal.

### 4.6 Read back the per-device frames

```text
tshark -r /tmp/cap.pcapng \
       -Y 'usb.idVendor == 0x0300 && usb.idProduct == 0x3004 && usb.capdata' \
       -T fields -e usb.capdata
```

You should see the device's bytes printed one frame per line. If the
output is empty:

- Wrong bus — re-check `lsusb -t`.
- Wrong filter — re-check the (vid:pid) typed into the `-Y` expression.
- Device action did not fire on the captured window — re-capture with
  a longer `-a duration:` and a clearer trigger.

For `ak980pro` / `microdia_dongle_7016`, prepend the boot-keyboard
drop to the display filter:

```text
tshark -r /tmp/cap.pcapng \
       -Y 'usb.idVendor == 0x0c45 && usb.idProduct == 0x8009 && usb.bInterfaceProtocol != 1 && usb.capdata' \
       -T fields -e usb.capdata
```

This is the Pitfall 17 enforcement at extraction time. Even if the
capture itself contained boot-keyboard reports from a different
interface on the device, `bInterfaceProtocol != 1` strips them.

______________________________________________________________________

## 5. Sanitise + convert to hex-fixture

### 5.1 Run usbrply

```text
usbrply -j /tmp/cap.pcapng > /tmp/cap.json
```

Output is JSON with one `data_out` / `data_in` entry per HID
transaction. `usbrply` handles both `DLT_USB_LINUX_MMAPPED` (Linux
usbmon) and `DLT_USBPCAP` (Windows USBPcap) transparently — same
command works for both.

### 5.2 Inspect by eye

```text
less /tmp/cap.json
```

Eyeball the JSON. Confirm:

- The transactions all belong to the device you targeted (usbrply has
  already filtered to the pcapng's content, but it does NOT enforce
  VID/PID — apply the device filter at the tshark stage above first if
  the pcapng contains more than one device).
- The event window is the action you wanted, not five minutes of
  ambient interrupt-in chatter.
- For `ak980pro` / `microdia_dongle_7016`, no entries appear that
  look like 8-byte boot-keyboard reports (one modifier byte + six
  keycode slots) — if any do, your tshark step did not strip them.

### 5.3 Invoke the converter (plan 09-03)

The next plan in this phase (09-03) lands
`scripts/hex-to-cpparray.py`. Once it exists:

```text
scripts/hex-to-cpparray.py /tmp/cap.json \
    --device akp03_variant_3004 \
    --capture image-upload-first-chunk \
  > tests/integration/fixtures/akp03_variant_3004/image_upload_first_chunk.h
```

- `--device` is the codename from `docs/_data/devices.yaml`.
- `--capture` is the human-readable label that becomes part of the
  C++ identifier and the filename.

Naming convention for `--capture`: `image_upload_first_chunk`,
`dpi_cycle`, `polling_rate_8000hz`, `rgb_mode_static_red`,
`encoder_rotate_cw`, `brightness_set_50`, `keymap_set_layer_1`.

### 5.4 Eyeball-review the generated header

Open the `.h` file. Confirm:

- It contains ONLY the bytes you expected (count them; for AKP03 image
  chunks it should be 1024 bytes; for the AJ-series envelope it should
  be 64 bytes).
- It contains NO keystroke-derived bytes from a boot-keyboard
  interface. This is the Pitfall 17 catastrophic leak path — if you
  see anything that looks like a 0x00 modifier byte + six keycode
  slots, do NOT commit. Re-capture with the boot-keyboard filter
  applied.
- The array length is sensible. A 600-KB hex header for what should
  be a one-chunk upload is a sign you captured the wrong window or
  forgot the per-device filter.

### 5.5 Commit ONLY the header

```text
git add tests/integration/fixtures/akp03_variant_3004/image_upload_first_chunk.h
git commit -m "test(fixtures): add AKP03 image-upload-first-chunk capture"
```

### 5.6 Delete the scratch artifacts

```text
rm /tmp/cap.pcapng /tmp/cap.json
```

`/tmp` is RAM-backed on most Linux systems (`tmpfs`); the bytes are
gone the moment the file is unlinked. There is no reason to keep raw
captures around once the sanitised fixture is committed and reviewable.

______________________________________________________________________

## 6. What you CANNOT do (anti-features)

These are NOT just "not yet implemented" — they are explicitly
out of scope, will be rejected at code review regardless of how they
are framed, and need not be re-litigated. Cross-referenced with
CLAUDE.md hard rules + STACK.md "Considered-and-deferred":

- **Do NOT attempt RF-air capture** with an SDR, CC2531 sniffer,
  HackRF, or any other radio. Pitfall 21: USB-side-only event
  windowing. The 2.4 GHz dongle decrypts the link and presents
  plaintext HID to the host — `usbmon` already gives us the layer we
  need. RF capture is out of scope per CLAUDE.md "no vendor RE".
- **Do NOT run the vendor's Windows app under wine, innoextract, or
  any Delphi-installer extraction tool.** CLAUDE.md hard rule:
  clean-room reverse engineering only. If the vendor app is necessary
  to *trigger* a device action that produces capturable USB traffic,
  run it on a separate VM-isolated machine and capture the USB side
  from `usbmon` here — never decompile the binary.
- **Do NOT extract, dump, or flash the dongle firmware.** Pitfall 21
  cross-cutting prohibition. We do not write firmware tools and we do
  not read firmware blobs.
- **Do NOT add a live in-app USB sniffer UI.** Captures are a
  DEVELOPER activity, not a USER activity (STACK.md
  "Considered-and-deferred" + capture-data-hygiene.md "Anti-features").
  No "advanced debug panel" feature requests for in-app `libpcap`
  shall pass review.
- **Do NOT add telemetry upload of captures.** Bug reports do not
  include packet dumps. If a bug report wants to share a capture, the
  bug report template instructs the user to share out-of-band — see
  `docs/policies/capture-data-hygiene.md` § "If you must share a raw
  capture out-of-band".
- **Do NOT commit the raw pcap or the intermediate JSON.** The
  CAPTURE-01 pre-commit hook blocks `*.pcap` / `*.pcapng` at any
  path; this is the belt-and-suspenders prose reinforcement.
- **Do NOT add a libpcap dependency in the agent codebase.**
  `scripts/hex-to-cpparray.py` consumes `usbrply` JSON, not raw
  `.pcap`. `libpcap` stays out of the in-tree codebase and out of the
  shipped wheel.

______________________________________________________________________

## 7. Troubleshooting

| Symptom                                                         | Cause                                                                                | Fix                                                                                                                                                                          |
| --------------------------------------------------------------- | ------------------------------------------------------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Permission denied opening /dev/usbmonN`                        | `wireshark` group membership not active in current shell                             | Log out and log back in, OR `newgrp wireshark` for the current shell. Verify with \`id                                                                                       |
| `/sys/kernel/debug/usb/usbmon/` does not exist                  | Kernel does not have `CONFIG_USB_MON` enabled                                        | Run on the host, not in a minimal container. Rare on Fedora / Debian / Ubuntu stock kernels.                                                                                 |
| `tshark -Y` returns 0 packets                                   | Wrong bus, wrong VID/PID in the display filter, or the device action did not fire    | Re-`lsusb -t` to confirm bus. Re-check VID/PID in the `-Y` expression. Increase `-a duration:` and re-trigger the device action with a clearer manual signal.                |
| `usbrply` emits empty JSON                                      | pcapng has zero frames matching the device                                           | Open the pcapng in Wireshark GUI to see what is actually in it. If the GUI shows traffic but usbrply ignores it, check `usbrply --version` against STACK.md pin.             |
| `usbrply` errors on JSON structure                              | Version mismatch — `usbrply` API changed between releases                            | Check `usbrply --version` matches STACK.md (v2.1.1). `pipx upgrade usbrply` or `pip install --user --upgrade usbrply==2.1.1`.                                                |
| `hex-to-cpparray.py` errors on input shape                      | `usbrply` JSON has unexpected structure (version mismatch, or odd `data_in` framing) | Confirm `usbrply --version`. Open the JSON in `less /tmp/cap.json` to see the actual structure; the converter expects per-transaction `data_out` / `data_in`.                |
| Capture file grows past 50 MB in seconds                        | Capturing the 8K mouse during sustained movement (Pitfall 21)                        | Stop capture. Re-capture during a control-channel event ONLY (DPI button press, RGB change). Cap with `-a filesize:51200`.                                                   |
| Hex header contains 8-byte rows that look like keyboard reports | Boot-keyboard interface bytes leaked through (Pitfall 17)                            | Discard the header — DO NOT commit. Re-extract with `usb.bInterfaceProtocol != 1` in the `-Y` filter. Confirm `ak980pro` and `microdia_dongle_7016` always carry this guard. |
| `usbmon0` capture is enormous and full of unrelated traffic     | `usbmon0` is the all-buses catch-all monitor                                         | Do NOT use `usbmon0`. Identify the per-device bus with `lsusb -t`, then capture on `usbmonN` for that bus only.                                                              |

______________________________________________________________________

## 8. Windows USBPcap (appendix)

Linux usbmon is the primary capture path for this project. Windows
USBPcap exists for the rare case where you need to confirm a vendor
opcode is identical between Linux- and Windows-driver firmware paths.

The mechanics are equivalent:

1. Install Wireshark for Windows; the installer offers to install
   USBPcap. Accept.
1. Reboot (USBPcap installs a USB-stack filter driver; a reboot is
   required for it to attach).
1. Open Wireshark. The capture interface list will include
   `USBPcap1`, `USBPcap2`, ... (one per USB root hub).
1. Right-click an interface → "Capture from this device only" lets you
   bind the capture to a single device's bus address — equivalent to
   the per-bus filter step 4.2 enforces on Linux.
1. Stop the capture and save as `.pcapng`.

`.pcapng` files from USBPcap use `DLT_USBPCAP` link-layer; `usbrply`
handles them transparently (no separate decode path). All subsequent
steps (5.1 → 5.6) are identical to the Linux flow.

Caveat: Windows boot-keyboard reports are subject to the same
Pitfall 17 leak as Linux. Apply `usb.bInterfaceProtocol != 1` in the
extraction display filter on `ak980pro` / `microdia_dongle_7016`
captures regardless of which OS produced the pcapng.

______________________________________________________________________

## 9. Cross-references

- [`docs/policies/capture-data-hygiene.md`](../policies/capture-data-hygiene.md)
  — privacy threat write-up + Pitfall 17 detail + sanitised workflow.
- [`.planning/research/captures/README.md`](../../.planning/research/captures/README.md)
  — scratch-sink semantics + gitignore reasoning.
- [`scripts/hex-to-cpparray.py`](../../scripts/hex-to-cpparray.py) —
  the converter (plan 09-03; lands next wave).
- [`docs/protocols/REVERSE_ENGINEERING.md`](REVERSE_ENGINEERING.md) —
  the broader clean-room methodology this runbook plugs into.
- `tests/unit/fixtures/mock_transport.hpp` — the wire-format assertion
  seam (plan 09-04; lands next wave).
- `.planning/research/PITFALLS.md` Pitfalls 17, 20, 21, 22, 24 — the
  cross-cutting risks this runbook mitigates.
- `.planning/research/STACK.md` § "NEW developer-prereqs" and
  § "Test-replay infrastructure" — the upstream tooling reference.
- `.planning/REQUIREMENTS.md` § CAPTURE-02 — the requirement this doc
  satisfies.
