# HOW-TO: Reverse-engineer a new AJAZZ device (capture → wire format → code)

A practical, end-to-end playbook for adding support for a **new** AJAZZ device
(keyboard, mouse, Stream Dock, or a rebadge) or a new vendor app. Written so a
first-time contributor — or future-you — can follow it without re-discovering the
methods we learned the hard way. Community-friendly: nothing here requires
internal knowledge beyond what's in this repo + the device + its vendor driver.

> **Clean-room contract (non-negotiable).** You may *read* the vendor binary /
> decompile / JS to learn the wire format, then write our code from your
> **findings**. You may NOT copy vendor source, commit vendor binaries /
> decompiles / extracted JS, or commit raw `.pcap` (keystroke-recovery risk —
> rejected by `scripts/reject-raw-captures.sh`). Keep raw material local or on a
> private channel (e.g. MEGAsync). A recon engineer ≠ the implementer is the
> ideal separation. See `CLAUDE.md` / `docs/protocols/keyboard/proprietary.md`.
>
> **Source-of-truth ordering: when the RE and the hardware disagree, the
> hardware wins.** Treat every decompile-derived value as a hypothesis until a
> live device confirms it.

______________________________________________________________________

## TL;DR loop

```
enumerate HID → get vendor app → static (Ghidra/JS) → Frida live-hook the vendor
→ decode wire format → confirm on hardware (set a WRONG value!) → write builders +
byte-pinned tests → document (protocol doc + dossier + sanitised evidence)
```

Most features need only: HID enumeration + a Frida hook of the vendor while you
click the button + a direct hidapi replay. Ghidra/USB-capture are the heavy
artillery for the parts Frida can't reach.

______________________________________________________________________

## Step 0 — Identify the device & its interfaces

1. **VID:PID.** `lsusb` (Linux) / Device Manager (Windows) / the probe below.
   AJAZZ VID prefixes seen so far: `0x0c45` (Microdia — AK980 PRO keyboard),
   `0x3151` (SONiX — VIA keyboards + AJ mice), `0x0300`/`0x6602`/`0x6603`/`0x5548`
   (Mirabox/Stream Dock), `0x3554`/`0x248A`/`0x249A` (AJ199/legacy mice).
1. **Enumerate the HID collections** and find the **vendor control collection**
   (it is almost never the boot keyboard/mouse). Use a 10-line hidapi probe:
   ```python
   import hid
   for d in hid.enumerate(0xVVVV, 0xPPPP):
       print(hex(d['usage_page']), hex(d['usage']), d['interface_number'],
             d['path'].decode(errors='replace'))
   ```
   The control collection is a **vendor usage page** (`0xFF13` on AK980 PRO,
   `0xFFFF` on AJ mice, `0xFFA0` on AKP05E). **Gotcha:** several collections can
   share one usage page (the AJ mouse has two `0xFFFF`; control is usage `0x02`)
   — record both `usage_page` AND `usage`.
1. **Report length & report-id.** Note `featureReportByteLength` /
   `output_report_length`. The HID report id is byte 0 of every buffer (0x00 for
   unnumbered). Many AJAZZ devices put a `0x04` *frame byte* at byte 1 and the
   real opcode at byte 2 — that `0x04` is data, NOT the report id (a classic
   multi-week trap).

Template: `scripts/ak980_tft_probe.py --enumerate`, `scripts/aj_mouse_probe.py --enumerate`.

______________________________________________________________________

## Step 1 — Get & classify the vendor software

AJAZZ ships per-family Windows/macOS drivers (catalogued in
`docs/research/vendor-software-inventory.md`). Download to an ephemeral dir, hash
it, and **classify**:

- **Native (MFC/Qt/Delphi)** — e.g. AK980 `DeviceDriver.exe`, Stream Dock
  `SDLibrary1.dll`, AJ199 `OemDrv.exe`. → Ghidra (Step 2a).
- **Electron** (has `resources/app/`, `ffmpeg.dll`, `icudtl.dat`) — e.g. the AJ
  mouse `AJAZZ Driver（R）`. The protocol is in bundled JS; often there's a
  separate native helper doing the HID (`iot_driver.exe`, a Rust gRPC server). →
  JS extraction (Step 2b) + Frida the native helper (Step 3).

Identify the HID transport library: `libusb-1.0.dll` (vendor uses libusb),
`hid.dll` imports (`HidD_SetFeature`/`GetFeature`), or a Rust `hidapi`.

______________________________________________________________________

## Step 2a — Static analysis: native binaries (Ghidra)

1. Ghidra headless import the binary (+ its `.pdb` if present — hugely helps).
1. Find the **HID call sites**: search imports/calls for `HidD_SetFeature`,
   `HidD_GetFeature`, `WriteFile`, `ReadFile`, `DeviceIoControl`,
   `hid_write`/`hid_send_feature_report`. A GhidraScript that dumps every call
   site + the calling function is the fastest start (we used
   `ExtractHidCalls.java` / `FindHidCallers.java` / `DumpFunctionsByAddr.java`).
1. Decompile the **transport wrappers** (the functions that build a buffer and
   call the HID primitive). Read off: report size (`0x21`=33, `0x41`=65,
   `0x1001`=4097), where the opcode lands, where the checksum is stamped, whether
   there's a readback after the write.
1. Decompile the **per-command builders** — each UI action sets specific byte
   offsets. Record opcode + sub-opcode + every field offset.
1. Cite functions by name (`FUN_xxxxxxxx` / PDB symbol) in your notes; **never
   paste decompiled bodies into the repo.**

What this WON'T give you: values computed in a separate DLL not in your dump
(e.g. `mui.dll::GetImageRGB565Data` byte order), and runtime-only state. Those go
to Frida/hardware.

______________________________________________________________________

## Step 2b — Static analysis: Electron / JS drivers

1. Extract: if `resources/app.asar` exists, `npx asar extract app.asar out/`;
   often AJAZZ ships it **unpacked** under `resources/app/`. Look for
   `*_beautified.js` (someone already beautified it) or run `js-beautify`.
1. The **main process** JS (`main_dist/main.js`) holds the device I/O; the
   **renderer** (`dist/static/js/main.js`) holds the UI + opcode enums.
1. Grep for the protocol: `FEA_CMD_`, `usagePage`, `featureReportByteLength`,
   `writeFeature`, `sendFeature`, `report`, `checksum`, `0x` opcodes, and the
   command builder functions (`setXxx`, `buildXxx`). Enum tables (e.g.
   `_RateToNum`, `FEA_CMD_SET_OLEDCLOCK = 40`) are gold.
1. If the JS hands bytes to a **native helper** over gRPC/IPC (AJ mouse →
   `iot_driver` at `127.0.0.1:3814`), the JS builds the *body* (opcode at byte 0)
   but the helper prepends the report id + checksum — so the report-id VALUE and
   the checksum algorithm are NOT in the JS. Get them from Frida/Ghidra-of-the-helper.

______________________________________________________________________

## Step 3 — Dynamic capture (the decisive step)

### 3a. Frida live-hook of the vendor process (best signal-to-noise)

This is what hardware-confirmed nearly everything this project knows. Hook the
HID primitives in the **running vendor process**, then trigger the action in the
vendor UI and read the exact bytes.

- Target: the process that actually does the HID. For Electron stacks that's the
  native helper (`iot_driver_v193.exe`), NOT the Electron app.
- Hook `hid.dll!HidD_SetFeature` + `HidD_GetFeature` (feature reports). Add
  `WriteFile`/`ReadFile` only if needed and **filtered tightly** — hooking
  `WriteFile` globally crashes the session.
- Frida 17 API notes (changed from 16): use `Module.getGlobalExportByName(name)`
  (or `Process.getModuleByName('hid.dll').getExportByName(name)`), and
  `ptr.readByteArray(len)` (NOT `Memory.readByteArray`).
- Dump the first ~32–67 bytes; highlight the opcode you expect. Trigger the
  action; reconnect/replug to catch init-time packets.

Template: `scripts/aj_mouse_frida_capture.py` (target it at the right process +
opcode). This caught the mouse `0x28` clock packet **with the required `0xD7`
marker** that static analysis alone missed.

### 3b. USB bus capture (when there's no host driver to hook, or to see init)

Cross-platform reference: `docs/protocols/CAPTURING.md` (Wireshark + usbmon +
dumpcap) and the recon runbooks `docs/research/vendor-recon-runbook-{fedora,windows}.md`.

- **Linux:** `sudo modprobe usbmon`, capture on `usbmonN` with Wireshark/tshark.
- **Windows:** USBPcap (ships with Wireshark) — but note USBPcap 1.5.4 does NOT
  bind xHCI/USB4 root hubs on modern PCs; use **usbipd-win + WSL2** instead.
- **SECURITY:** a bus-wide `.pcap` taken while a keyboard is attached contains
  plaintext keystrokes (passwords). NEVER commit raw `.pcap`/`.pcapng` (the
  pre-commit hook rejects them). Extract only the device-of-interest control
  bytes into a sanitised hex fixture (`scripts/hex-to-cpparray.py`), delete the
  pcap, and keep only the sanitised evidence (see `capture-evidence.md`).

### 3c. Direct hidapi probe (isolate app-vs-device, and verify your decode)

Once you have a hypothesis, replay it directly with hidapi (no vendor app) and
watch the device. This separates "our app bug" from "wrong wire format". The
probes (`scripts/ak980_tft_probe.py`, `scripts/aj_mouse_probe.py`) are templates
— copy one, set the VID/PID + control usage page/usage, and add a `--yourcmd`
mode that builds your packet.

______________________________________________________________________

## Step 4 — Decode the wire format (recurring AJAZZ patterns)

Things to determine, and the patterns we've seen:

- **Transport:** feature report (`HidD_SetFeature`/`hid_send_feature_report`)
  vs output report (`WriteFile`/`hid_write`) vs input read. They are NOT
  interchangeable; the device only listens on one.
- **Report id / framing:** byte 0 = report id (0x00 unnumbered). Watch for the
  `0x04` frame byte at byte 1 + opcode at byte 2 (AK980 feature path) vs opcode
  at byte 1 directly (output path) vs `'C''R''T'` at bytes 0–2 (Stream Dock, no
  report-id byte at all).
- **Required marker bytes:** some commands need a fixed sentinel or the firmware
  silently ignores the packet — e.g. the AK980 time-data `0x5A` magic at byte 3,
  the AJ-mouse clock `0xD7` at byte 8. If a perfectly-shaped packet does nothing,
  diff it against the captured vendor packet byte-by-byte and look for a constant
  you're missing.
- **Endianness:** mixed even within one vendor — AK980 RTC year is a single
  2000-offset byte; AJ-mouse clock year is uint16 **big-endian**; chunk counts
  are often little-endian. Don't assume.
- **Checksums:** AK980 output path = `sum(bytes) mod 256` at a fixed byte; AJ
  mouse = **BIT7** = `sum(body) & 0x7F` at the last byte; feature paths often
  have none (integrity via an `0xAA 0x55` trailer). The clock packets had NO
  checksum. Confirm whether the firmware even validates it.
- **Handshake / pacing:** the vendor often does `Sleep → SET_REPORT → GET_REPORT`
  per packet. Back-to-back writes can be a **silent no-op** — the AK980 time-sync
  needed a ~30 ms inter-packet delay + a GET readback. If a sequence "succeeds"
  but nothing happens, add the delay + readback.
- **Envelopes:** config commits are often multi-packet: `START(0x18) → DATA → SAVE(0x02) → FINISH(0xF0)`. Capture the whole sequence, not just the data packet.
- **Chunked uploads:** images/macros split into N fixed-size chunks + a header
  with the total count + a commit sentinel (e.g. `ULEND`). Watch the chunk-index
  encoding (it may be split across bytes with a marker bit).

______________________________________________________________________

## Step 5 — Confirm on real hardware (the part people skip)

**Set a deliberately WRONG value so a no-op is distinguishable from success.**
The vendor app syncs the *correct* time/state, so "it works" and "it's a no-op"
look identical if the device already shows the right value. Examples that caught
real bugs this project: set the clock to `11:11` (obviously wrong) and watch it
correct; read battery and compare `0x64`=100% to the real charge.

Apply the **three-witness rule** before claiming a feature works: (1) a capture
shows the vendor bytes, (2) a round-trip witness — the device visibly follows an
injected value, (3) a negative witness — a deliberately-wrong value produces a
visibly-wrong result (proving the firmware parses the field).

Watch for platform differences: on Linux/hidraw, `hid_write` byte 0 is the report
number — packets that start with data at byte 0 (Stream Dock `CRT`) need a `0x00`
prepend on Linux/macOS that Windows omits; and `hid_enumerate` may not populate
`usage` for non-primary collections (fall back to usage-page matching).

______________________________________________________________________

## Step 6 — Turn findings into code

1. **Constants + pure builders** in `*_protocol.{hpp,cpp}`: every opcode as a
   named constant; each command a pure function returning a fixed-size
   `std::array` with byte-exact field placement and the checksum stamped last.
   Pure = no I/O, unit-testable in isolation.
1. **Transport selection:** set `controlUsagePage` (+ `controlUsage` if the
   device has multiple collections on one page) in `register.cpp`, threaded to
   `makeHidTransport`. Use `writeFeature()` vs `write()` per the captured transport.
1. **Device backend** implements the capability interface (`IClockCapable`,
   `IBatteryCapable`, …). Add the per-packet handshake (delay + best-effort
   readback) if the vendor used one.
1. **Byte-pinned Catch2 tests** via the `MockTransport` DI seam
   (`makeXxxWithTransport`): assert every field offset of every packet. These are
   the regression guard that stops a future transposition (we shipped a
   self-consistent-but-wrong TFT layout once exactly because the test agreed with
   the bug).
1. **Honest capability (D-02/D-05):** if you can't confirm a feature, return
   `NotImplemented` / `nullopt` — never a fake success toast. Mark the maturity
   `scaffolded`/`partial`/`functional` honestly in `docs/_data/devices.yaml`.

______________________________________________________________________

## Step 7 — Document

- Per-device byte-level spec under `docs/protocols/<family>/`.
- Add/extend the dossier section in `docs/research/reverse-engineering/`.
- Add the **sanitised** control-channel bytes to `capture-evidence.md` (NEVER raw
  pcap).
- Fill the **confidence matrix** (hardware-confirmed / decompile / provisional)
  and the **unexplored** list — be honest about what you didn't verify.

______________________________________________________________________

## Consolidated gotcha checklist

- [ ] Opened the **vendor control collection** (usage page + usage), not the boot
  interface.
- [ ] Right **transport** (feature vs output report) — they're not interchangeable.
- [ ] Correct **report id** at byte 0 (0x00 unnumbered); not confusing the `0x04`
  frame byte for the report id.
- [ ] **GET buffer ≥ device FeatureReportByteLength** (a 64-byte buffer for a
  65-byte report makes `hid_get_feature_report` fail on Windows).
- [ ] hidapi GET returns the reply **with** the report-id byte at index 0
  (offsets shift by 1 vs a decompile that strips it).
- [ ] Required **marker/magic bytes** present (`0x5A`, `0xD7`, …).
- [ ] **Endianness** per field (don't assume).
- [ ] **Checksum** algorithm + position (or none) — and whether the firmware checks it.
- [ ] **Per-packet delay + readback** if the vendor used a request/response handshake.
- [ ] **Full envelope** (START/SAVE/FINISH), not just the data packet.
- [ ] **Linux report-id prepend** for byte-0-data packets; **usage fallback** on hidraw.
- [ ] Verified with a **deliberately-wrong value** on real hardware.
- [ ] **No vendor source, binaries, decompiles, or raw pcap in the repo.**

## Tooling reference

- Probes/capture templates: `scripts/ak980_tft_probe.py`,
  `scripts/aj_mouse_probe.py`, `scripts/aj_mouse_frida_capture.py`.
- Capture runbooks: `docs/protocols/CAPTURING.md`,
  `docs/research/vendor-recon-runbook-{fedora,windows}.md`.
- Pcap → sanitised C++ fixture: `scripts/hex-to-cpparray.py`.
- Per-device wire specs: `docs/protocols/{keyboard,mouse,streamdeck}/`.
- This dossier: `docs/research/reverse-engineering/` (`methods-and-tooling.md`,
  `capture-evidence.md`, `unexplored.md`, the per-device files).
