# Reverse-Engineering Methodology

Every protocol implemented in AJAZZ Control Center is derived through a documented, clean-room procedure. The goal is three-fold:

1. Produce correct implementations that match the real hardware.
1. Keep a legally defensible paper trail: no closed-source code is read, disassembled or copied.
1. Make the knowledge re-usable by anyone who wants to audit, extend or port the backend.

## Workflow

### 1. Capture

1. Install the vendor's official software in a clean Windows (or macOS) VM. The VM must be isolated from your workstation's main profile.
1. In the VM, install Wireshark + USBPcap (Windows) or enable `usbmon` (Linux) on the host if the device works under Linux.
1. Connect the device. Identify its USB bus/address from `lsusb`/Device Manager.
1. Start a capture on the specific USB endpoint. Exercise every feature of the device one at a time — brightness, image upload, RGB effect, encoder press, DPI change, etc. Annotate the capture timestamps in a text file while you do it.

### 2. Annotate

- Export the capture to `captures/<device-codename>/<action>.pcapng`. Never commit raw captures: they may contain serial numbers, firmware blobs, or fingerprints of the vendor binaries. Commit only a **sanitized decode**.
- Decode packets with `tshark -r capture.pcapng -T fields -e usb.capdata > decoded.txt` and paste the relevant hex into the protocol document.

### 3. Document

- Create `docs/protocols/<family>/<device-codename>.md` if it does not exist.
- Write down every command's envelope, prefix, byte ranges, and the action it causes, citing the capture where it was observed.
- Include open questions: if a byte's purpose is unclear, mark it as `??` and describe what you tried.

### 4. Implement

- Write the encoder/decoder in a dedicated `<device>_protocol.hpp/.cpp` file under `src/devices/<family>/src/`. The file must contain **only protocol knowledge** (byte offsets, enums, helpers). No transport I/O.
- Add unit tests in `tests/unit/test_<device>_protocol.cpp` that construct a packet and check each byte against the documented spec. At least one test per command.
- Wire the protocol helpers to `ITransport` inside the backend class.

### 5. Verify

- Run the capture-replay integration tests (`tests/integration`). They feed the canned USB frames into the parser and check that the expected events come out.
- If you own the device, run a live smoke test on a checkout of the branch before merging.

## Tools we use

| Tool                      | Purpose                                   |
| ------------------------- | ----------------------------------------- |
| **Wireshark + USBPcap**   | Windows USB capture                       |
| **tshark**                | Headless export of decoded packets        |
| **`usbmon`**              | Linux kernel USB capture                  |
| **`lsusb -v`**            | Device descriptors (interface / endpoint) |
| **`hidapi` + `hid-dump`** | Inspect HID report descriptors            |
| **Python + `cffi`**       | One-off prototyping of command sequences  |
| **KiCad, oscilloscope**   | When all else fails, look at the wire     |

## Legal considerations

- **No disassembly** of vendor binaries. If a proprietary executable is unavoidable to trigger a capture, it is run only in a VM and never decompiled.
- **No vendored code** from reference projects: they are cited in `docs/protocols/` and their findings are re-verified from captures before being written in our own words and our own C++.
- **Clean-room split**: one contributor may run the vendor software and produce captures; another may write the implementation from those captures and the documentation. When a single contributor does both, they commit capture annotations before writing code — never the reverse — and the git history proves it.
- **GPL-3.0-or-later** is chosen to ensure downstream users cannot re-close the re-opened protocol.

## When you get stuck

- Compare your capture against [`elgato-streamdeck`](https://github.com/OpenActionAPI/rust-elgato-streamdeck), [`mirajazz`](https://crates.io/crates/mirajazz), [ZCube's AKP153 notes](https://gist.github.com/ZCube/430fab6039899eaa0e18367f60d36b3c), or [Den Delimarsky's Stream Deck Plus write-up](https://den.dev/blog/reverse-engineer-stream-deck-plus/) as independent verification — do **not** copy their code.
- Open an issue tagged `device-support` with the capture summary.
- Ping a maintainer on the project chat (details in `CONTRIBUTING.md`).
