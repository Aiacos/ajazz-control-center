# AJAZZ Stream Dock — firmware DFU protocol

> Deep RE pass against `FirmwareUpgradeTool.exe` (1.8 MB) plus the
> `aKDFU` sentinel in `SDLibrary1.dll` / `Stream Dock AJAZZ.exe`.
> 2026-05-17. Companion to [`akp05_vendor.md`](./akp05_vendor.md).
>
> **TL;DR**: the vendor uses an **Allwinner-style USB-upgrade protocol**
> (`AIC.FW` firmware image, libusb-based bulk-transfer chunking with
> CRC16 verification per FWC, CBW/CSW handshake á la USB Mass Storage).
> The upgrade is performed by a **separate process** (`FirmwareUpgradeTool.exe`)
> which is the only binary in the bundle that links `libusb-1.0.dll`.
> The main app's role is limited to detecting the `aKDFU` 4-byte
> sentinel in firmware blobs and launching the tool.
>
> **We MUST NOT reimplement OTA in our open-source backend.** Treat
> firmware update as out-of-scope and let the user run the vendor
> tool. Our backend should only detect VID/PID disappearance during
> update (the device re-enumerates into DFU mode with a different PID)
> and resume cleanly when the device reappears.

## 1. Binary inventory

| File                          | Size  | Role                                                                                                   |
| ----------------------------- | ----- | ------------------------------------------------------------------------------------------------------ |
| `FirmwareUpgradeTool.exe`     | 1.8 MB | Qt 5 GUI shell; statically links the upgrade core (no separate `upgcmd.exe` shipped despite the embedded reference string `.\upgcmd\upgcmd.exe` at offset 491 — that's a leftover from an older dev tree where `upgcmd` was a CLI subcommand) |
| `libusb-1.0.dll`              | 180 KB | Bulk USB I/O — used ONLY by FirmwareUpgradeTool (`SDLibrary1.dll` does NOT call any `libusb_*` API; the JSON dump confirms this) |
| `aKDFU` sentinel              | 4 bytes | A 4-byte magic string found in `SDLibrary1.dll` (`sdlibrary1_strings.txt:29251`) and `Stream Dock AJAZZ.exe` (`streamdock_exe_strings.txt:608649`). Almost certainly the in-binary marker the main app checks to decide whether to dispatch a downloaded firmware blob to the upgrade tool. |
| `AIC.FW`                      | 6 bytes | Firmware image format magic / header (`firmware_tool_strings.txt:282`). The classic Allwinner image format. |

## 2. End-to-end OTA flow

### 2.1 Main app side (`Stream Dock AJAZZ.exe`)

1. User triggers "Firmware Update" or the app's periodic version-check
   hits `https://cdn1.key123.vip/Stock/firmware/firmware-version-check.json`
   (or one of the Custom/Prajnasys/StreamDock variants per `akp05_vendor.md`
   §4.3) and finds a newer version for the connected device.
2. Main app downloads the firmware blob (HTTPS via libcurl) and stores
   it under `%APPDATA%\HotSpot\Stream Dock AJAZZ\firmware\<codename>\<version>.aic`.
3. Main app validates the file header: first 4 bytes must be `aKDFU` or
   the first 6 bytes must be `AIC.FW` (the two formats observed). If
   neither matches, the download is rejected with a notification.
4. Main app spawns `FirmwareUpgradeTool.exe <path-to-.aic>` via
   `QProcess::startDetached`. The main app then either exits the
   device's read/write threads gracefully (so the tool can claim the
   USB interface) or keeps them running for non-DFU devices — TBD via
   capture.

### 2.2 Upgrade tool side (`FirmwareUpgradeTool.exe`)

UI flow (per Qt object names in `firmware_tool_strings.txt:466..506`):

- Main window: `centralwidget` with `progressBar`, `Title_Label`,
  `Pause_Continue_Button`, `Cancel_Button`,
  `UpgradeFileLoad_Button` (file picker),
  `UpgradeFilePath_EditLine`.
- States: `Updating......`, `Updating successfully!`,
  `failed to update`, `Please don't close when updating`.
- Calls `libusb_init()`, `libusb_get_device_list()`, scans for the
  target device, then `libusb_open_device_with_vid_pid(vid, pid)` and
  `libusb_claim_interface(handle, ifaceNum)`.

Core upgrade loop (per `__do_fwc_upgrade` strings 320-334):

```
Try to scan ... (ping the device repeatedly until it responds in DFU mode)
For each FWC (firmware component) in the image:
    Send FWC meta (name, size, target partition)
    Get block size from device
    Read FWC data from .aic file in chunks
    For each chunk:
        Send CBW (Command Block Wrapper)  — like USB Mass Storage
        Send data
        Read CSW (Command Status Wrapper)
        If retry needed, "Resend firmware component"
    Send "FWC final data"  → device computes CRC
    Read CRC from device
    If CRC mismatch: "Error, get fwc crc failed. expect 0x%X, got 0x%X"
    "FWC CRC is OK"
    Burn (write to flash): "FWC burn result is OK"
    Run: "FWC run result is OK"
Switching to new stage, please ignore the error message.
... (next stage)
"Updating successfully!!!Please click OK!"
```

The exact "stages" observed:

1. **Ping / detect**: `Try to scan` loop until the device returns a
   valid response in DFU mode. The DFU mode device re-enumerates with
   a different VID:PID (the "UU device" in the strings) — likely
   `0x1f3a:????` for Allwinner BROM, but this needs USB capture.
2. **Run to uboot**: the device's BROM (Boot ROM) is told to jump to
   the bundled U-Boot loader (`ramboot 0x%lx %s %d` at offset 333).
3. **Upload upgrade firmware components** (FWCs): each FWC is a
   (target-partition, name, data) triple from the .aic image.
4. **Burn**: device writes the FWC to its flash storage.
5. **Run**: device executes the newly-flashed component (verifies it
   boots cleanly before committing).
6. **Switch stage / repeat** as needed.
7. **Reboot to normal mode**: device re-enumerates back to its
   original VID:PID.

## 3. CBW / CSW transport layer

Strings 371-391 indicate the tool uses a USB Mass Storage-like
**Command Block Wrapper / Command Status Wrapper** protocol over the
bulk endpoints (NOT the HID transport):

```
Send CBW, tag 0x%x
  CBW tag 0x%x, size %d, %s(%d)
Send data, len %d
  Send tag 0x%x, size %d/%d, %s(%d)
Read CSW, tag 0x%x
  CSW tag 0x%x, size %d, %s(%d)
CSW is not matched, Sig %#x, Tag got %#x, expect %#x
```

Per the USB BBB (Bulk-Only Bulk-only Transport) spec:

| Field        | CBW                                                              | CSW                                  |
| ------------ | ---------------------------------------------------------------- | ------------------------------------ |
| Signature    | 4 bytes (BBB-spec: 0x43425355 `USBC`; vendor likely customised)  | 4 bytes (0x53425355 `USBS`)          |
| Tag          | 4-byte transaction id; CSW.Tag must match CBW.Tag                | echoes CBW.Tag                       |
| Direction    | bit in flags                                                     | n/a                                  |
| Length / status | data-transfer length / OK/Phase Error/Stall                   | residue + status                     |

The vendor's customisation is the **`AIC.FW` framing** wrapping the
CBW/CSW pairs — see §4.

## 4. AIC.FW firmware image format

Format (inferred from strings + size/`%u` `%lx` `%lu KB` format strings
seen in the partition-table dump at lines 256-267):

```
Offset  Size   Field
0x00    6      Magic: "AIC.FW"
0x06    2      Pad / version
0x08    4      Total file size (LE32)
0x0C    4      Number of FWCs (LE32)
0x10    N×Δ    FWC metadata table:
                each entry:
                  - 64 bytes Name (zero-terminated)
                  - 4 bytes Target media type
                  - 4 bytes Target partition number
                  - 8 bytes Offset in .aic file (LE64)
                  - 8 bytes FWC data size (LE64)
                  - 4 bytes CRC32 (compared against device-computed CRC)
                  - padding
After metadata: FWC data blobs
```

The exact byte-by-byte layout requires a hex-dump of a real
`.aic` file we don't yet have. The `image.target` and
`image.target.data` strings at lines 294 and 317 are the partition-
descriptor field names.

**`aKDFU` sentinel**: Distinct from `AIC.FW`. `aKDFU` is the marker
the main app uses to recognise an AJAZZ-customised firmware blob. It
likely sits at a fixed offset (probably offset 0 of the device-side
encrypted wrapper around the `AIC.FW` payload). This is consistent
with the vendor's pattern of decrypting downloaded firmware in-app
(see `decryptFile(...)` at `streamdock_exe_strings.txt:760598`,
AES-GCM with a hardcoded key `a7e61c373e219033c21091fa607bf3b8` at
offset `760609`) before passing it to `FirmwareUpgradeTool.exe`.

## 5. USB descriptor changes during DFU

When a device enters DFU mode (after host sends a DFU-trigger packet —
not yet captured), it re-enumerates with:

- A different VID:PID pair (the "UU device" in tool strings —
  conventionally Allwinner BROM is `0x1f3a:0xefe8` for sun8i, or
  `0x1f3a:0xefe9` for sun9i, but the actual PID needs USB capture).
- Different USB class — likely `bInterfaceClass = 0xFF` (vendor-specific)
  rather than HID class.
- Two bulk endpoints (one IN, one OUT) on a single interface.

The endpoint addresses are **discovered** by the tool:
`FAILED to get UU device endpoint addresses!` (string at offset 449)
tells us the tool scans descriptors at open time rather than
hardcoding.

## 6. Hot-plug behaviour during DFU

The main app sees:

1. Original device's VID:PID disappears (`StreamDockWatcher.exe`
   fires a `WM_DEVICECHANGE` event).
2. UU device's VID:PID appears.
3. Main app **does not** open the UU device — it's owned by the
   upgrade tool.
4. Main app shows "Firmware update in progress" UI.
5. When upgrade tool exits (success or failure), it triggers the
   device to reboot; original VID:PID reappears.
6. Main app re-opens the device, queries version (`CRT VER`), and
   confirms the new version matches the expected post-update version.

## 7. Recovery / failure modes

From strings:

- `Connection recovery failed ...` (line 326) — the tool retries
  bringing the device back into DFU mode if the BROM connection
  drops mid-upgrade.
- `Resend firmware component.` (line 327) — automatic FWC re-send
  on transient USB errors.
- `Switching to new stage, please ignore the error message.` (line
  302) — between stages the device drops the USB endpoint briefly;
  expected error.

There is **no automatic rollback** on flash failure. If `FWC burn
result` returns non-zero and the FWC happened to be the bootloader
itself, the device is bricked until a hardware JTAG flash. Mitigation:
the tool emits `jtag unlock ok.` / `jtag unlock failed.` strings (lines
247-248) so a JTAG recovery path exists for hardware-equipped users.

## 8. Why we do NOT reimplement this

1. **Not Stream-Deck-protocol code.** It's an Allwinner SoC upgrade
   suite that happens to be embedded in the Stream Dock product. The
   wire format is bound to the SoC, not the Stream Dock surface.
2. **Security & liability**. A botched flash bricks the device. We
   ship community software; the vendor ships warranty.
3. **Maintenance burden**. New Allwinner silicon shifts the wrapper
   protocol; we'd be chasing the vendor forever.
4. **Trivial workaround**. We can simply detect "device disappeared,
   wait for re-enumeration, re-acquire" — most users keep both our app
   and the vendor's tool installed.

## 9. What our backend SHOULD do

1. **Detect DFU transition**: when an open device disappears, check
   whether a sibling USB device with a "DFU-class" VID:PID
   (configurable allow-list initially seeded from `[opendeck-akp05]`
   captures) appears within 5 s. If yes, log "device entered DFU
   mode" and suspend any device-specific UI; do NOT attempt to
   reopen the original VID:PID.
2. **Detect DFU completion**: poll for the original VID:PID. When it
   reappears, wait 2 s (let firmware boot), then re-open and re-query
   firmware version. If version differs from cached, refresh the UI
   and notify the user.
3. **Expose a "Launch Firmware Update Tool" action**: a small UI
   button that invokes the vendor's `FirmwareUpgradeTool.exe` if
   it's installed. Detect via:
   - Linux: `/opt/Stream Dock AJAZZ/FirmwareUpgradeTool` (vendor install dir)
   - macOS: `/Applications/Stream Dock AJAZZ.app/Contents/Resources/FirmwareUpgradeTool.app`
   - Windows: registry `HKLM\SOFTWARE\HotSpot\StreamDock\InstallPath`
4. **Never download or unpack `.aic` files ourselves**.

## 10. Code corrections required

| File                                                              | Change                                                                                                                                                                                                                                  | Breaking? | Tests needed                                                       |
| ----------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------- | ------------------------------------------------------------------ |
| `src/devices/streamdeck/include/ajazz/streamdeck/streamdeck.hpp`  | Add `enum class DfuState { Normal, Entering, InProgress, Returning }` and a `signal dfuStateChanged(DfuState)` on `IDevice`.                                                                                                            | additive  | `IDeviceTest::emitsDfuState_onDisappearReappear`                   |
| `src/core/src/device_registry.cpp`                                | Add a separate `dfuVidPid` allow-list (initially empty; future PRs add per-family DFU pairs as they get captured). When a device disappears, check the registry's DFU list before treating the disappearance as "user unplugged".       | additive  | `DeviceRegistryTest::detectsDfuTransition_whenSiblingVidPidAppears`|
| `src/host/ui/src/firmware_update_action.cpp` (new)                | Implement the "Launch vendor tool" action per §9.3. Detect tool path per OS; spawn via `QProcess::startDetached`; show a warning if not installed.                                                                                       | additive  | `FirmwareUpdateActionTest::detectsVendorToolPath_perOS`            |
| `src/devices/streamdeck/src/akp05.cpp` (`Akp05Device::open`)      | Add a 2-second backoff after the device's VID:PID reappears post-DFU — the firmware boot is slow and the first `VER` query right after reappearance often times out.                                                                    | additive  | `Akp05ReopenAfterDfuTest::waits2sBeforeFirstVer`                   |
| `docs/protocols/streamdeck/_research-sources.md`                  | Add citation `[firmware-tool-strings-2026-05-17]` → `C:\temp\firmware_tool_strings.txt`.                                                                                                                                                | additive  | n/a                                                                |
| `TODO.md`                                                         | Add capture queue items: (1) UU-device VID:PID for AKP05/N4; (2) one `.aic` file hex-dump to confirm header format; (3) DFU-trigger HID packet (`QUCMD`-class?); (4) per-family DFU VID:PID list build.                                  | additive  | n/a                                                                |
| **(NO file)**                                                     | **Explicitly DO NOT** add a libusb dependency to `ajazz_core`. Re-affirm the COD-031 boundary: libusb stays out of our core.                                                                                                              | n/a       | `BoundaryTest::ajazzCore_doesNotLinkLibusb` (grep-based)           |

## 11. References

- `firmware_tool_strings.txt` — extracted strings from
  `FirmwareUpgradeTool.exe`, 4 602 strings (`C:\temp\firmware_tool_strings.txt`).
- USB Mass Storage Bulk-Only Transport spec, USB-IF rev. 1.0 (for
  the CBW/CSW pattern).
- Allwinner livesuite / sunxi-tools documentation (community)
  — for AIC.FW format details. We do not link these in our docs
  because we do not implement the format.
- `[opendeck-akp05]` (Mirabox N4) — has not yet captured the DFU
  transition; capture queue.
