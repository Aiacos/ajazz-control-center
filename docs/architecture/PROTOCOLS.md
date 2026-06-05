# Protocol layering and capabilities

This page is the contract between the device-protocol layer (`src/devices/**`) and everything above it. If you're writing a new device backend, this is the document you should read first.

## Layering

```
┌────────────────────────────────────────────────────────────────┐
│  Backend module (e.g. ajazz::streamdeck)                       │
│   - registerAll(IRegistry&) — registers factories at startup   │
│   - <Model>Device : public IDevice, IDisplayCapable, ...       │
│   - <Model>_protocol.hpp — wire-format helpers, no Qt/OS deps  │
└────────────────────────────────────────────────────────────────┘
            │ uses
┌────────────────────────────────────────────────────────────────┐
│  ITransport abstraction (src/core/include/ajazz/core)          │
│   - HidTransport (hidapi) is the only impl in production       │
│   - MockTransport for unit tests                               │
└────────────────────────────────────────────────────────────────┘
```

A backend's source tree therefore typically contains:

```
src/devices/streamdeck/
├── include/ajazz/streamdeck/streamdeck.hpp   # registerAll() + makeAkp815 decl
└── src/
    ├── register.cpp                          # IRegistry registration
    ├── akp815.cpp                            # IDevice impl (custom carve-out)
    ├── akp815_protocol.hpp                   # geometry constants
    ├── akp815_wire.hpp                       # v1-API wire-format helpers
    ├── akp815_wire.cpp
    └── akp_common_protocol.hpp               # family-shared command words
```

The AKP03 / AKP05-N4 / AKP153 Stream Dock families no longer have in-tree C++
backends: they are driven by the out-of-process **mirajazz Rust sidecar**
(`streamdock-host/`), proxied in by `SidecarStreamDockDevice`
(`src/app/src/sidecar_stream_dock_device.*`) and registered via
`streamDockSidecarDescriptors()`. **AKP815** is the carve-out that keeps a custom
C++ backend (it is not a mirajazz device). The `*_protocol.hpp` / `akp815_wire.*`
files are **pure wire-format**: no Qt, no logging, no I/O — `constexpr` constants
and free functions over `std::span<std::uint8_t>`, trivially unit-testable.

### streamdock-host sidecar JSON protocol

The app (`SidecarStreamDockDevice`) spawns `streamdock-host` and speaks
**newline-delimited JSON over stdin/stdout** — exactly one JSON object per line.
The sidecar enumerates every mirajazz-driven SKU at startup, then streams input
and accepts output commands.

**app → sidecar (commands):**

| `cmd`            | Fields                          | Notes                               |
| ---------------- | ------------------------------- | ----------------------------------- |
| `ping`           | —                               | Liveness probe; replies `pong`.     |
| `set_brightness` | `serial`, `percent`             | Output — requires `--allow-output`. |
| `set_image`      | `serial`, `key`, image payload  | Output — requires `--allow-output`. |
| `render_test`    | `serial`, …                     | Output — paints a test pattern.     |

**sidecar → app (events):**

| `event`        | Fields                                              | Meaning                                     |
| -------------- | -------------------------------------------------- | ------------------------------------------- |
| `connected`    | `serial`, `vid`, `pid`, `firmware`, `family`, `name` | One per physical unit found at startup.      |
| `ready`        | `device_count`, `output_allowed`                   | Enumeration finished.                        |
| `input`        | `serial`, `code` (byte 9), `state` (byte 10), `raw`  | A raw input report (see input note).         |
| `pong`         | —                                                  | Reply to `ping`.                             |
| `ok`           | `cmd`, `serial`                                    | A command succeeded.                         |
| `error`        | `msg`                                              | Protocol / command error (non-fatal).        |
| `device_error` | `serial`, `msg`                                    | A device read failed; its reader task exits. |

**Output gating (`--allow-output`).** Brightness/image commands trigger mirajazz
`initialize()`, which sends `CRT DIS` — known to risk wedging the demo panel via
open/close churn. A long-lived sidecar holds ONE handle and sends DIS once for
the handle lifetime; output stays gated behind `--allow-output` until that
no-wedge behaviour is validated on retail hardware.

**Input note.** The reader forwards the raw report's `code` (byte 9) and `state`
(byte 10); `ACK…OK` acknowledgement frames that ride the same channel are
discarded (`is_ack_frame`; see
`docs/protocols/streamdeck/akp05_input_corrections.md` §2.1). The byte→event
decode (key / encoder / touch) happens app-side in `mapSidecarInput`; the
per-family encoder/touch codes there are **PROVISIONAL** pending calibration on a
retail AKP05E (the `0x3004` demo unit emits no input).

## Capability catalog

| Mix-in             | What it models                              | Used by                    |
| ------------------ | ------------------------------------------- | -------------------------- |
| `IDisplayCapable`  | Per-key LCDs and main / touch-strip screens | All stream decks           |
| `IRgbCapable`      | One-zone, multi-zone, or per-LED RGB        | Keyboards, mice, deck rims |
| `IEncoderCapable`  | Endless rotary encoders, optional screens   | AKP05, AK680               |
| `IKeyRemappable`   | QMK/VIA-style remap + macros                | Keyboards                  |
| `IMouseCapable`    | DPI stages, polling, lift-off, battery      | AJ-series mice             |
| `IFirmwareCapable` | Firmware version + update                   | Anything with a bootloader |

A backend opts into any subset. The UI and Python SDK call:

```cpp
if (auto* d = dynamic_cast<IDisplayCapable*>(device.get())) {
    d->setKeyImage(keyIndex, rgba, w, h);
}
```

A backend may also expose its capability bitset via `IDevice::capabilities()` for cheap presence checks (no `dynamic_cast` round-trip).

## Adding a new device

1. **Document the wire protocol** — capture USB traffic with Wireshark/USBPcap, write a clean-room description in `docs/protocols/<family>/<codename>.md`. **Do not copy vendor source code** (clean-room policy, see CONTRIBUTING.md).
1. **Add a row** in `docs/_data/devices.yaml` with `status: scaffolded`. Run `python3 scripts/generate-docs.py` to refresh README + wiki tables.
1. **Implement the protocol helpers** in `src/devices/<family>/src/<codename>_protocol.hpp`:
   - `constexpr` command IDs and report sizes.
   - `buildXxx(std::span<std::uint8_t> out, ...)` builders.
   - `parseInputReport(std::span<std::uint8_t const>) -> std::optional<InputEvent>`.
1. **Implement the IDevice subclass** in `<codename>.cpp`. Implement only the capability mix-ins the device actually has — no stub-throwing methods.
1. **Add unit tests** under `tests/unit/test_<codename>_protocol.cpp`. Capture-replay style: known input bytes → expected `InputEvent`; known parameters → expected output bytes.
1. **Register the factory** in `src/devices/<family>/src/register.cpp` so the runtime registry can construct it from a USB VID/PID match.
1. **Flip the status** in `devices.yaml` to `functional` once basic open / poll / set-image / close round-trip works on real hardware.

## Wire-protocol cheatsheet

The conventions used by every `_protocol.hpp` file:

- All multi-byte integers in HID reports are **big-endian** unless noted otherwise. Use `static_cast<uint16_t>((hi << 8U) | lo)` rather than `memcpy`-from-network-order.
- Reports are zero-padded to the device's HID `output report length`. Builders should `std::ranges::fill` the output buffer first.
- Command IDs are `constexpr std::uint8_t kCmdXxx = 0x00`. Use a single struct of constants per family.
- Image transfers are **chunked** — each chunk carries an "is this the last chunk" flag at a fixed offset. Encode chunk count and "page number" inside the header builder.
- Input reports carry an event type byte (often called `tag`). Decoder is a switch on the tag's high nibble.

## USB IDs

| Family       | VID      | PIDs                                   |
| ------------ | -------- | -------------------------------------- |
| Stream decks | `0x0300` | `0x1001`, `0x1002`, `0x3001`, `0x5001` |
| Keyboards    | `0x3151` | `0x4024` … `0x4029`                    |
| Mice         | `0x3554` | `0xf51a` … `0xf51d`                    |

The full mapping lives in `docs/_data/devices.yaml` and is the source of truth for the runtime registry.
