# Contract — App ↔ mirajazz Sidecar (JSON over stdio)

**Impl**: `streamdock-host/src/main.rs` (+ `kind.rs`); app proxy `src/app/src/sidecar_stream_dock_device.cpp`;
codec `src/app/src/sidecar_protocol.cpp` (the only currently unit-tested piece —
`tests/unit/test_sidecar_protocol.cpp`).

The sidecar is a **dumb driver**: raw input out, rendered images + brightness in. It owns the HID
handles for the whole process lifetime (one mirajazz `CRT DIS` on first output → no wedge). It knows
nothing about actions, plugins, profiles, or event names.

## Transport

- Newline-delimited JSON, one object per line, over the sidecar's stdin (commands) and stdout
  (events). Launched with `--allow-output` to enable rendering commands.
- App handshake: `open()` spawns the binary (resolved via `$AJAZZ_STREAMDOCK_HOST` → beside-exe →
  PATH), blocks ≤5 s for the `ready` event, then switches to async stdout draining.

## Commands (app → sidecar)

| Command                  | Fields                                              | Effect                                                                                                                                         |
| ------------------------ | --------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `ping`                   | —                                                   | → `pong`                                                                                                                                       |
| `set_brightness`         | `{serial, percent}`                                 | mirajazz `set_brightness()` (LIG)                                                                                                              |
| `set_image`              | `{serial, key, touchzone, width, height, rgba_b64}` | base64 RGBA → `RgbaImage` → mirajazz `set_button_image` + `flush` (JPEG encode / rotation / chunking / BAT handled by mirajazz)                |
| `render_test`            | `{serial}`                                          | paint numbered keys + strip zones                                                                                                              |
| **`keep_alive`** *(NEW)* | `{serial}`                                          | send `CRT CONNECT` to hold the handle alive while idle — **the missing command** that makes `SidecarStreamDockDevice::keepAlive()` non-trivial |

## Events (sidecar → app)

| Event                                    | Fields                                                                                               |
| ---------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `connected`                              | `{serial, vid, pid, firmware, family, name}` (per device at startup)                                 |
| `ready`                                  | `{device_count, output_allowed}`                                                                     |
| `input`                                  | `{serial, code, state, raw}` — `code = raw[9]`, `state = raw[10]`; ACK frames (ASCII "ACK") filtered |
| `pong` / `ok` / `error` / `device_error` | acks + error reporting                                                                               |

## Geometry & image format (per family, in `kind.rs`)

| Family     | Keys | Touch zones      | Encoders | Image (key)               | Notes                                                             |
| ---------- | ---- | ---------------- | -------- | ------------------------- | ----------------------------------------------------------------- |
| AKP05 / N4 | ~10  | 4 (BAT wire 1–4) | 4        | 112×112 `Rot180`          | zones 128×128 `Rot180`; protocol v3; HW-confirmed `0x0300:0x3004` |
| AKP03 / N3 | 6    | 0                | 3        | 60×60 `Rot90`             | protocol v2                                                       |
| AKP153     | 15   | 0                | 0        | 85×85 `Rot90` mirror-both | protocol v1; wins the `0x0300:0x1001` collision                   |

## Key indexing

- App key indices are **1-based**; mirajazz keys are **0-based**. `hwKeyForKeyIndex()` row-remaps.

## Known correctness items (this plan)

1. **`keepAlive()` is an empty no-op** (`sidecar_stream_dock_device.cpp:293`) while
   `StreamDockControlService` arms a ~1 s timer — add the `keep_alive` command above and wire it.
1. **`EncoderReleased` dropped** — AKP03 v3 emits a real encoder-release the app discards
   (`stream_dock_input_service.cpp:306`); add `EncoderBinding::onRelease` and route it.
1. **PROVISIONAL input map** (`mapSidecarInput`, `:42`) — encoder codes/polarity, touch zones
   `0x40..0x43`, `tapPos.y=0` are demo-unit guesses; keep behind the synthetic-event test path until a
   retail AKP05E/N4 confirms them.
1. **Enumeration duplication** — the sidecar self-enumerates HID while the app keeps its own
   `DeviceRegistry`/hotplug; reconcile only via the `connected`/`serial` event (coupling smell to
   watch, not to rip out in this plan).

## Contract acceptance

- Codec round-trip: `test_sidecar_protocol.cpp` (9 cases).
- Family/geometry resolution + ACK detection: cargo tests in `kind.rs` + `main.rs` (10).
- **NEW**: a dedicated `SidecarStreamDockDevice` unit test (handshake, event parse, input map,
  keep-alive send) — currently absent.
