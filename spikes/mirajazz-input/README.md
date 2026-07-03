# mirajazz input spike

Throwaway experiment on the `experiment/mirajazz` branch. Question being
answered: **does driving the AKP05/N4 family through the [`mirajazz`](https://github.com/4ndv/mirajazz)
Rust crate behave better than the in-tree C++ device layer — specifically for
input, which is the part that "isn't working"?**

Not part of the CMake build. Pure `cargo`.

## What the source review already told us (before running anything)

- **Output brings nothing new.** mirajazz drives output with the same
  `CRT/BAT/STP` opcodes and `112x112 Rot180` geometry the C++ already
  implements (`opendeck-akp05/src/mappings.rs` ↔ our `62817ac`). So there is
  no output mode here.
- **Wedge risk is real but avoidable.** mirajazz `initialize()`
  (`device.rs:351`) sends `CRT DIS` first, and every *output* op calls it. Our
  hardware notes say `DIS`-on-open wedges this demo panel. The **input** path
  (`connect` → `get_reader` → `raw_read_data`) never calls `initialize()`, so
  these modes are safe.
- **opendeck's AKP05 support is unverified.** Its `inputs.rs` is all
  `TODO: verify with actual hardware`; the filled-in codes came from the N4.

## Run

```sh
cd spikes/mirajazz-input
cargo run            # enum mode: list HID interfaces, confirm the descriptor
cargo run -- input   # connect + dump raw input frames for 20s
```

For `input`, press keys / turn encoders / touch the strip during the 20s window.

## Expected outcome on the live `0x0300:0x3004` demo unit

- `enum`: the AKP05 control interface appears and `list_devices` matches it.
- `input`: firmware reads `V3.AKP05E.01.007`; **zero** input frames — the
  same wall the C++ and 4 other methods already hit. That CONFIRMS the demo
  firmware is the limiter, not the library. Real codes would only appear on a
  retail AKP05E / Mirabox N4.
