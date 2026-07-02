# RE Methods & Tooling

How every wire-format finding in this dossier was obtained. Clean-room
discipline: a recon engineer reads vendor binaries/decompiles/JS and writes
*findings*; an implementer writes our code from those findings without copying
vendor source.

## 1. Static — Ghidra headless

- **Keyboard** `DeviceDriver.exe` (PE32 x86, MFC) → project `ak980_proj`; 39
  `FUN_*.c` dumps + `ghidra_hid_dump.json` (HID call sites + strings) +
  `mui_dll_inventory.json` (6604 UI exports). GhidraScripts: `ExtractHidCalls.java`,
  `FindHidCallers.java`, `DumpFunctionsByAddr.java`, `EnumerateMuiExports.java`.
- **Stream Dock** `SDLibrary1.dll` (+ `SDLibrary1.pdb`) → `ghidra_SDLibrary1_dll.json`
  (127 HID/WinUSB/network call sites, 81 functions). `ExtractStreamDockCalls.java`.
- **Mouse legacy** `OemDrv.exe` / `Mouse Drive Beta.exe` (+ `costura64.hidusb.dll`)
  / `AK820MAX.exe` (+ `witmodSdk.dll`) → the OemDrv / HIDUsb / Witmod dialect
  findings (vendor-protocol-notes Findings 11-15).
- Resolved-symbol names are cited in the per-device files; their bodies are NOT
  reproduced. Raw dumps live in `reverse-eng-workdir/` (local, not committed; a
  private MEGAsync copy is the cross-machine transfer channel).

## 2. Dynamic — Frida live-hook (the decisive method)

`scripts/aj_mouse_frida_capture.py` (and the equivalent ad-hoc keyboard hook)
attach to the running vendor process and hook `hid.dll!HidD_SetFeature` /
`HidD_GetFeature` (Frida 17 API: `Module.getGlobalExportByName`,
`ptr.readByteArray`). This is what **hardware-confirmed** the keyboard time-sync

- battery and caught the mouse `0x28` clock packet (with the required `0xD7`
  marker that static analysis alone missed). Do NOT hook `WriteFile` globally — it
  crashes the session.

* Keyboard vendor process: `DeviceDriver.exe`.
* Mouse vendor stack: Electron `AJAZZ Driver（R）.exe` + native `iot_driver_v193.exe`
  (Rust `tonic` gRPC at `127.0.0.1:3814`). Hook `iot_driver`, not the Electron app.

## 3. Direct hardware probes (no vendor driver needed — work on Linux)

- `scripts/ak980_tft_probe.py` — keyboard: `--enumerate`, `--settime HH:MM [--delay --readback]`, `--battery`, TFT upload modes.
- `scripts/aj_mouse_probe.py` — mouse: `--enumerate`, `--clock HH:MM`,
  `--battery`, `--battery-watch SECS` (re-acquires across a replug).

These open the vendor control collection directly via `hidapi` and replay our
reconstructed wire format, isolating "app bug" from "device/wire" — and are the
way to reproduce/verify on Fedora (where the Windows-only Frida path is
unavailable).

## 4. OSS corpora (read-only cross-checks, never vendored)

Keyboard: `gohv/EPOMAKER-Ajazz-AK820-Pro`, `KyleBoyer/TFTTimeSync-node`,
`aar-rafi/aks075-linux`, TaxMachine AK820 Pro. Mouse: (vendor JS is the primary
source; no mature OSS corpus for `0x3151`). Stream Dock: `mishamyrt/ajazz-sdk`,
`4ndv/opendeck-akp03`, `naerschhersch/opendeck-akp05`, `4ndv/mirajazz`,
`OpenActionAPI/rust-elgato-streamdeck`, `superdeee/pyajazz`,
`tomekceszke/ajazz-akp03e`, Bitfocus Companion. Full citation tags resolve in
`docs/protocols/streamdeck/_research-sources.md`.

## 5. Clean-room boundary (project hard rule)

`CLAUDE.md` / `proprietary.md`: *no vendor firmware/driver/SDK is disassembled or
reused in the repo*. This dossier records findings + symbol references; vendor
binaries, decompiled C bodies, and extracted JS stay out of the repo. Raw USB
`.pcap` is rejected by `scripts/reject-raw-captures.sh` (keystroke-recovery
risk); only sanitised control-channel evidence (`capture-evidence.md`) is
committed.
