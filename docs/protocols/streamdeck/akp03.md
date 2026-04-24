# AJAZZ AKP03 / Mirabox N3 — Wire Protocol (work in progress)

The AKP03 is a small 6-key + knob controller sold under both the AJAZZ and Mirabox brands. Its wire protocol is closely related to the AKP153 but uses:

- 6 keys instead of 15
- PNG-encoded 72×72 images instead of JPEG 85×85
- An additional encoder/knob with press, rotate-left, rotate-right events

## Identification

| Property    | Value (provisional) |
| ----------- | ------------------- |
| Vendor ID   | `0x0300`            |
| Product ID  | `0x3001`            |
| Packet size | 512 bytes           |

The VID/PID above is a placeholder in the registry and will be refined once a capture is committed. Contributions welcome — see [`docs/guides/ADDING_A_DEVICE.md`](../../guides/ADDING_A_DEVICE.md).

## Reference implementation

See [`opendeck-akp03`](https://github.com/4ndv/opendeck-akp03) for a Rust implementation that can be used as an independent cross-check during capture annotation. Our backend is **not** derived from its code; the reference is cited for verification purposes only.

## Status in this repository

| Area                   | State      |
| ---------------------- | ---------- |
| Backend scaffolding    | ✅ present |
| Image upload           | 🟠 stubbed |
| Encoder events         | 🟠 stubbed |
| Integration test fixt. | 🟠 missing |
