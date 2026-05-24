# AJ-series mouse battery — how the wireless charge read works

> **Status:** ✅ working, hardware-verified on Windows 2026-05-22 with a physical
> AJAZZ 2.4G 8K (`0x3151:0x5007`). The app logs `[battery] queried ajazz_24g_8k: 100%` and the sidebar chip shows the charge.
>
> This page is the solution narrative. The byte-level frame table lives in
> [`aj_series_opcode_table.md` §4](aj_series_opcode_table.md); the device-wide
> wire notes are in [`aj_series.md`](aj_series.md) and
> [`aj_series_vendor.md`](aj_series_vendor.md).

## TL;DR — the working method

The wireless mouse charge is **not** a one-shot HID query. The basetta (the USB
dock the mouse pairs to over 2.4 GHz) only mirrors the charge into its status
report **after** it receives the vendor's periodic **`0xF7` status poll**, which
brings up the 2.4 GHz telemetry link. So `AjSeriesMouse::batteryPercent()` does:

1. **SET_FEATURE the `0xF7` status poll** on the `0xFFFF`/usage-`0x02` control
   collection (interface 2 / MI_02 — the same collection used for the clock and
   all control writes):

   - report-id byte (index 0) = `0x00`
   - opcode (index 1) = `0xF7`
   - everything else `0x00` (no payload, no checksum needed)

   This replicates the heartbeat the vendor `iot_driver` sends ~1×/second.

1. **Wait ~30 ms** for the basetta to round-trip the mouse over 2.4 GHz.

1. **GET_FEATURE status report `0x05`** and read the charge. `parseBatteryCharge()`
   locates it by the leading byte:

   - **Windows:** hidapi keeps the requested report-id at index 0, so the frame
     is `05 00 00 64 01 01 01 02` → **charge at byte 3** (`0x64` = 100 %).
   - **Linux hidraw:** the unnumbered frame has no report-id prefix → **charge at
     byte 2**.

   One code path covers both (`chargeIndex = (frame[0] == 0x05) ? 3 : 2`).

Frame validity: a valid frame has the bytes between the report-id and the charge
== 0; a non-zero there is the transient garbage a wireless reconnect can return
(`05 ad 04 …`) and is rejected. A charge of `0` (with the rest of the frame also
zero) means the telemetry link is not up yet → reported as unknown / grey, never
a wrong 0 %.

This is **cross-platform on hidapi alone — no libusb** (COD-031 preserved).

## Why it showed `--%` before — the root cause

The symptom was a permanent `--%`. The cause was **not** a wrong report id or
byte offset, and **not** a Windows/libusb limitation (both were mid-investigation
dead ends, see below). The cause was that **nothing was sending the `0xF7`
poll**, so the basetta never brought up its 2.4 GHz telemetry link and every read
returned:

```
05 00 00 00 00 00 00 00
   ▲  ▲  ▲  └──────────┴─ status flags all-zero  → "link not ready"
   │  │  └─ charge = 0
   │  └─ byte 1 = 0
   └─ report id
```

Confirming details gathered on the live device:

- A scan of **all six** HID collections of the basetta (`0x000c`, `0x0001×3`,
  `0xffff/usage1`, `0xffff/usage2`) and every feature report id `0x00..0x0a`
  returned all-zero for the charge.
- Neither a passive read nor a `0x82` / `0x83` poke (as feature **or** output
  report) populated it.
- The basetta's own OLED did **not** show a battery indicator either — i.e. the
  basetta genuinely did not have the value until the link came up.
- The moment the `0xF7` poll was sent, the very next read returned
  `05 00 00 64 01 01 01 02` (status flags `01 01 01 02` = link up).

### Dead ends (recorded so we don't repeat them)

- **Passive GET_FEATURE, no poll.** Leaves the link "not ready" → all-zero on
  every platform. This was the actual `--%`.
- **`0x83` GET_BATTERY poke + read.** `0x83` is declared by the vendor but does
  **not** enable the dongle telemetry link; it does not populate the report.
- **`0x83` OUTPUT-write + interrupt-IN read.** Reads the wrong channel — the
  reply surfaces in the GET_FEATURE status report, not on an interrupt-IN read.
- **"Windows needs libusb / Linux-only".** Wrong: it came from reading the report
  without the `0xF7` poll, so it looked dead on Windows. With the poll the Windows
  read works immediately. The vendor's `iot_driver` links the same hidapi we do.
- **`0x40` host→device query.** No such opcode on this firmware.

## How the method was found

The `0xF7` heartbeat was identified in the vendor reverse-engineering corpus —
the `iot_driver` sends it (~1 Hz, SET_FEATURE report-id `0x00`, opcode `0xF7`,
zero payload) and then reads the status report back; the populated frame
`05 00 00 64 01 01 01 02` was captured from the running vendor driver. The
hypothesis was then confirmed empirically on hardware before landing the code.

## How to verify on hardware

With the mouse on and paired to the basetta (no vendor driver running):

```bash
python scripts/aj_battery_f7_probe.py
```

The probe opens the `0xFFFF`/usage-2 control collection, sends the `0xF7` poll
~1×/second, reads report `0x05`, and prints the frame + the byte-3 charge. A
working device prints `05 00 00 64 01 01 01 02  charge(byte3)=100 flags=01 01 01 02`.
If it stays `05 00 00 00 …` the link is not coming up (check the mouse is on and
actually paired to this basetta).

In the app, the `BatteryService` polls every 15 s; each poll runs the
poll→settle→read sequence above and logs `[battery] queried <codename>: N%`.

## Code / test pointers

- `src/devices/mouse/src/aj_series.cpp` — `batteryPercent()` (0xF7 poll + read)
  and `parseBatteryCharge()` (byte-3/byte-2 auto-detect).
- `src/devices/mouse/src/register.cpp` — `hasBattery` is gated to the wireless /
  dongle codenames.
- `tests/unit/test_aj_series_mock_transport.cpp` — `[battery]` cases cover the
  Windows (byte 3) and Linux (byte 2) frames, the 0xF7 poll, the all-zero
  "link not ready" frame, and the transient-garbage rejection.
- `scripts/aj_battery_f7_probe.py` — the hardware confirmation tool.
