# AKP05E input-report decoding — corrections for `akp05.cpp`

> **Technical note, 2026-05-27.** Derived from a clean-room Ghidra pass over the
> vendor `SDLibrary1.dll` (read path) **and** `Stream Dock AJAZZ.exe` (the Qt slot
> that interprets input), cross-checked against a live AKP05E (`0300:3004`,
> firmware `V3.AKP05E.01.007`). It corrects the **encoder** and **touch-strip**
> decoding in `src/devices/streamdeck/src/akp05.cpp::parseInputReport`. The
> **key** decoding is vendor-confirmed correct.
>
> **Confidence legend** used throughout:
>
> - **[CONFIRMED]** — proven from the decompiled binary (and, for VER, from
>   live hardware).
> - **[PROVISIONAL]** — architecture proven, but the exact numeric constants for
>   the AKP05E SKU are **not** pinned and require one live input capture
>   (see §7). Do **not** hard-code these as fact.

______________________________________________________________________

## 0. Provenance (reproducible)

| Artifact                                                | Location                                       | How produced                                                                                                 |
| ------------------------------------------------------- | ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `SDDevice::readDataFromHidDevice` @ RVA `0x180021280`   | `reverse-eng-workdir/akp05_input_parse.json`   | Ghidra 12.1 headless, project `C:\temp\ghidra_sd_proj` name `sd_sdk` (SDLibrary1.dll), `DumpAkp05Input.java` |
| `SDActionCanvasWidget::handleKeyEvents` @ `0x1400d02b0` | `reverse-eng-workdir/akp05_keyevent_slot.json` | project `sd_exe` (Stream Dock AJAZZ.exe + 69 MB PDB), `DumpKeyEventSlot.java`                                |
| dispatch jump tables `DAT_1400d9dcc` etc.               | `reverse-eng-workdir/akp05_jumptable.json`     | `DumpJumpTable.java`                                                                                         |

All raw RE material stays under `C:\Users\unilo\reverse-eng-workdir` (NOT the
repo, per the clean-room policy). Only the findings below are public.

______________________________________________________________________

## 1. Confirmed input architecture

**[CONFIRMED]** Device input is read over **HID** via `hid_read_timeout` on the
vendor interface (MI_00) — *not* WinUSB. `SDDevice::readDataFromHidDevice` reads
a report, emits `sigReadData` (raw bytes) when enabled, then dispatches by the
leading bytes:

| Leading bytes                           | Meaning                   |
| --------------------------------------- | ------------------------- |
| `HANDLER`                               | finish-write notification |
| `error`                                 | device error              |
| `ACK` (0..2) + `O`,`K` (5,6) + `00` (9) | write-acknowledgement     |
| `V1.` / `V2.` / `V3.`                   | firmware-version response |

For a **modern** device (codename **not** `StreamDock[293]`/`[295]`/`XF-A3501`),
the read thread does, verbatim:

```c
// readDataFromHidDevice, modern-family branch:
uVar10  = (uint)(byte)_Dst->field_0xa;   // = report byte 10  -> "state"
pcVar11 = (char *)(byte)_Dst->field_0x9; // = report byte 9   -> "key code"
sigHandleKeyEvents(this, /*keyCode=*/_Dst[9], /*state=*/_Dst[10]);
```

So **only two bytes** cross the signal boundary: `keyCode = report[9]`,
`state = report[10]`. Everything else (which control, direction, gesture) is
derived **in the EXE slot** from those two bytes. The slot is:

```c
// Stream Dock AJAZZ.exe @ 0x1400d02b0
void SDActionCanvasWidget::handleKeyEvents(SDDevice *dev, int keyCode, int state);
```

`handleKeyEvents` is a ~227 KB **multi-device** dispatcher: it branches by device
codename (e.g. `OMNIDIALV25`), maps `keyCode` through
`Utilities::mapToSoftwareLocation(keyCode)` and per-family jump tables
(`DAT_1400d9dcc[keyCode-0x33]` → cases 0..9, plus further switches for the
encoder cases `0xa..0x11`), and finally calls the per-control handlers
(`SDActionCanvasLabel::performActionKnob`, `SDActionTouchBarWidget::*`).

**Net invariant for us:** report byte 9 = a *control/action code*; report byte 10
= a *context value* whose meaning depends on the byte-9 class (edge for keys, X
for touch; see below).

______________________________________________________________________

## 2. Keys — **[CONFIRMED] keep as-is**

### Current code (correct)

`src/devices/streamdeck/src/akp05.cpp:238-257` (`parseInputReport`):

```cpp
if (frame[0]==0x41 && frame[1]==0x43 && frame[2]==0x4b) return std::nullopt; // "ACK"
auto const tag = frame[9];
if (tag >= 1 && tag <= KeyCount) {
    bool const pressed = frame[10] != 0x00;
    ev.kind  = pressed ? Kind::KeyPressed : Kind::KeyReleased;
    ev.index = tag;                       // 1-based key index
}
```

### Verdict

- ACK discard on `frame[0..2] == "ACK"`: **matches** the vendor's `ACK..OK`
  classifier. **Keep.**
- key code at `frame[9]`, press/release edge at `frame[10]`: **matches** the
  vendor (`keyCode`, `state`). **Keep.**
- **[PROVISIONAL] one caveat:** the EXE runs `keyCode` through
  `Utilities::mapToSoftwareLocation` before using it as a slot index. For the
  simplest family the code *is* the 1-based location, which is what we assume.
  Confirm in the live capture (§7) that the AKP05E's 10 keys really emit
  `frame[9] ∈ {1..10}` (very likely, but unverified). No code change expected.

______________________________________________________________________

## 3. Encoders — **[CONFIRMED WRONG] must be reworked**

### Current code (incorrect model)

`src/devices/streamdeck/src/akp05.cpp:259-280`:

```cpp
// tag in [0x20..0x2f]; low nibble = encoder index;
// frame[10] = signed int8 rotation delta; frame[11] = button edge.
if ((tag & 0xf0u) == 0x20u) {
    auto const encIndex = tag & 0x0fu;
    auto const rot      = static_cast<std::int8_t>(frame[10]);   // <-- WRONG
    auto const btn      = frame[11];                             // <-- WRONG
    if (rot != 0) { ev.kind = Kind::EncoderTurned; ev.value = rot; }
    else          { ev.kind = btn ? Kind::EncoderPressed : Kind::EncoderReleased; }
}
```

### Evidence (why it is wrong)

In `handleKeyEvents`, encoder rotation is **not** decoded from a delta byte.
The rotation **direction is encoded in the value of `keyCode` (report byte 9)**
and routed through a jump table to a `KnobActionType`:

```c
// handleKeyEvents, encoder switch (cases come from a per-family jump table):
case 0x10: KVar35 = KnobCounterclockwiseRotation; break;   // CCW
case 0x11: KVar35 = KnobClockwiseRotation;        break;   // CW
// ... the pairs 0xa/0xb, 0xc/0xd, 0xe/0xf, 0x10/0x11 = the 4 encoders × {CCW,CW}
// (KnobPressed is a third, separate case)
SDActionCanvasLabel::performActionKnob(label, KVar35, false);
```

`KnobActionType ∈ { KnobClockwiseRotation, KnobCounterclockwiseRotation, KnobPressed }`. There is **no rotation-magnitude byte** anywhere in the path —
one report = one detent in a fixed direction. Consequences:

1. `frame[10]` is **not** a signed rotation delta. Reading it as `int8` delta is
   wrong; on a real CW click our code would mis-read direction/magnitude.
1. The encoder identity + direction live in `frame[9]`, not in a `0x20..0x2f`
   low-nibble. The `(tag & 0xf0)==0x20` mask is a guess from the OSS corpus and
   is **not** what the vendor binary does.
1. `frame[11]` is **not** a button edge.

### Proposed correction

Decode encoders **from `frame[9]` (the action code) against a per-device table**,
not from a delta byte:

```cpp
// PSEUDO — exact code values are [PROVISIONAL] until the live capture (§7).
struct EncoderCode { std::uint8_t code; std::uint8_t index; EncAction action; };
// EncAction ∈ { RotateCW, RotateCCW, Press }
// e.g. (ILLUSTRATIVE, NOT FINAL):
//   enc0: CCW=0x.., CW=0x.., Press=0x..   ... enc3: ...
static constexpr std::array<EncoderCode, EncoderCount*3> kEncoderCodes = { ... };

if (auto e = lookupEncoderCode(frame[9])) {
    ev.index = e->index;
    switch (e->action) {
    case RotateCW:  ev.kind = Kind::EncoderTurned; ev.value = +1; break;
    case RotateCCW: ev.kind = Kind::EncoderTurned; ev.value = -1; break;
    case Press:     ev.kind = Kind::EncoderPressed; break; // release synthesized host-side
    }
}
```

- Keep `InputEvent::Kind::EncoderTurned` with `value = +1 / -1` (matches the
  "fixed ±1 detent" reality and the existing host-side accumulator pattern).
- Encoders emit **press only** (no release) — Companion synthesises the release,
  and our `poll()` already maps `EncoderPressed`→value 1 / `EncoderReleased`→0,
  so keep synthesising a release after each press (as the existing AKP03 path
  does).
- **[PROVISIONAL]** The `kEncoderCodes` table values for the AKP05E are NOT
  known. The `0x10→CCW / 0x11→CW` mapping above is observed in *a* family branch
  of `handleKeyEvents`; it was **not** proven to be the AKP05E branch. Pin the
  real codes from the live capture (§7) before landing the table.

______________________________________________________________________

## 4. Touch strip — **[CONFIRMED WRONG] must be reworked**

### Current code (incorrect model)

`src/devices/streamdeck/src/akp05.cpp:285-314`:

```cpp
// tag in [0x30..0x3f]; low nibble = gesture; frame[10..11] = BE16 X (0..639).
if ((tag & 0xf0u) == 0x30u) {
    auto const gesture = tag & 0x0fu;
    auto const x = (frame[10] << 8) | frame[11];          // <-- WRONG (BE16)
    if (x >= akp05::TouchStripRangeX) return std::nullopt; // <-- WRONG (range 640)
    switch (gesture) { 0x0:Tap 0x1:SwipeLeft 0x2:SwipeRight 0x3:LongPress }
}
```

### Evidence (why it is wrong)

In `handleKeyEvents`, the touch strip is handled by `SDActionTouchBarWidget`, and
the **X coordinate is the single `state` argument (report byte 10)** — passed
straight into `getTouchbarLocationFromX`:

```c
// handleKeyEvents, touch branch (distinct keyCodes, NOT a 0x30-tag low nibble):
//   keyCode 0x98 -> performActionKeyDownItem(getTouchbarLocationFromX(state))  // touch down
//   keyCode 0x99 -> performActionKeyUpItem  (getTouchbarLocationFromX(state))  // touch up
//   keyCode 0x97 -> getTouchbarLocationFromX(state)                            // move
//   keyCode 0x78/0x79 -> setCoreX(state)
//   keyCode 0xb1/0xb2 -> changeN4ProTouchbarMode(...)                          // mode toggle
iVar18 = SDActionTouchBarWidget::getTouchbarLocationFromX(touchbar, /*X=*/state);
```

Consequences:

1. The touch X is a **single byte = `frame[10]` (range 0..255)**, **not** a
   big-endian 16-bit value spanning `frame[10..11]`. A one-byte field structurally
   cannot encode `0..639`, so both the `(frame[10]<<8)|frame[11]` read **and**
   the `TouchStripRangeX = 640` clamp are wrong.
1. The event type (down / up / move) is a **distinct `keyCode` at `frame[9]`**,
   not a low-nibble gesture code under a `0x30` tag.
1. **"Swipe left/right" is not a firmware event.** The firmware reports
   down/move/up + an X position; the vendor derives swipe vs tap **host-side**
   (from the X delta between down and up) and maps X→zone via
   `getTouchbarLocationFromX`. Our `TouchSwipeLeft/Right` kinds are therefore a
   host-side abstraction, not a wire fact.

### Proposed correction

```cpp
// PSEUDO — exact codes [PROVISIONAL] until §7.
if (auto t = lookupTouchCode(frame[9])) {     // {Down, Up, Move}
    std::uint8_t const x = frame[10];          // single byte, 0..255
    // zone = mapXToZone(x)  (0..TouchZoneCount-1), mirroring getTouchbarLocationFromX
    // tap vs swipe is derived host-side from the Down->Up X delta, NOT here.
    ev.value = x;
    ev.index = mapXToZone(x);
    ev.kind  = /* TouchDown / TouchUp / TouchMove — see struct note below */;
}
```

- Touch X is `frame[10]` (0..255). Replace the BE16 read and the `TouchStripRangeX = 640` clamp; the real X scale + whether it is absolute pixels (scaled),
  a 0..255 fraction, or already a zone index is **[PROVISIONAL]** — confirm in §7.
- Derive **zone (0..3)** from X (the device aligns 4 zones to the 4 encoders);
  keep `TouchZoneCount = 4`.
- Tap/swipe classification moves **host-side** (track Down→Up, compare X). Do not
  expect distinct swipe-left/right wire codes.

______________________________________________________________________

## 5. Downstream impact

| Site                                                                          | Change                                                                                                                                                                                                                                                                                                                           |
| ----------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `akp05_protocol.hpp:286-302` `InputEvent`                                     | `EncoderTurned.value` stays a signed step but is now `±1` derived from the code, not a delta byte. Touch `value` is a single-byte X (0..255). Reconsider the `TouchSwipeLeft/Right`/`TouchTap`/`TouchLongPress` kinds: prefer `TouchDown/TouchUp/TouchMove` at the wire layer and synthesise tap/swipe above `parseInputReport`. |
| `akp05_protocol.hpp:281-284, 291, 294-297` doc comments                       | Rewrite: byte 9 is an *action code* (not a `1..N / 0x20.. / 0x30..` tag range); byte 10 is edge (keys) **or** single-byte X (touch); encoders carry no delta.                                                                                                                                                                    |
| `akp05_protocol.hpp:82` `TouchStripRangeX = 640`                              | Wrong (X is one byte). Replace with the real range from §7, or drop the clamp and validate against `TouchZoneCount`.                                                                                                                                                                                                             |
| `akp05.cpp:493-503` `poll()` encoder mapping                                  | Still OK: `EncoderTurned`(±1) / `EncoderPressed`(1) / `EncoderReleased`(0). Keep synthesising the encoder-press release.                                                                                                                                                                                                         |
| `akp05.cpp:504-519` `poll()` touch packing                                    | The \`gesture\<<16                                                                                                                                                                                                                                                                                                               |
| `tests/unit/test_akp05_protocol.cpp`, `tests/unit/test_akp05_touch_strip.cpp` | These currently lock the **wrong** encoder-delta and BE16-X behaviour. They must be updated to the corrected model and re-pinned against the §7 capture fixtures (do NOT treat the current expected bytes as ground truth).                                                                                                      |

______________________________________________________________________

## 6. Honesty / scope guardrails

- The **key** path is vendor-confirmed; ship it.
- The **encoder** and **touch** paths are confirmed *wrong in structure*; the
  corrected *structure* (no delta byte; single-byte X; direction/type in byte 9)
  is **[CONFIRMED]**. The exact **numeric codes** and **X scale** for the AKP05E
  are **[PROVISIONAL]** and MUST come from a live capture (§7) before encoder/touch
  are promoted past `scaffolded`/`partial`. Per the project honesty contract, do
  not present encoder/touch as `functional` until the live values are pinned.
- Phase 25 (hardware verify) should treat encoder + touch as not-yet-correct.

______________________________________________________________________

## 7. What to capture live to finalize (one short session)

The capture path on the dev Windows box was blocked (USBPcap dead on xHCI/USB4;
usbip/WSL2 did not forward the device's high-rate interrupt-IN; the device only
streams once the vendor app has it). The cheapest reliable finalisation is a
**native-Linux hidraw** capture (the device binds to `hid-generic`; no usbip):

1. On a Linux host, open MI_00 (vendor HID, usage page 0xFFA0) and read raw
   reports while the device streams. (Our backend's open path already targets
   this interface; if it does not stream, push a profile/image first to mirror
   the vendor "connected" state.)
1. For **each** control, record `report[9]` (code) and `report[10]` (context):
   - 10 keys press+release → confirm `report[9] ∈ {1..10}`, `report[10]` edge.
   - each of the 4 encoders: one CW click, one CCW click, one press → capture the
     distinct `report[9]` code per (encoder, direction/press); confirm `report[10]`.
   - touch: down + move + up at several X positions across the strip, in each of
     the 4 zones → capture the distinct `report[9]` codes (down/up/move) and the
     `report[10]` X values; derive the X→zone mapping and the X range.
1. Land the captured codes as `tests/integration/fixtures/akp05e/*.h` (via
   `scripts/hex-to-cpparray.py`), fill the `kEncoderCodes` / `lookupTouchCode`
   tables, and rewrite the unit tests.

Until then, this note + the Ghidra dumps are the source of truth for the
*structure*; the *values* remain provisional.

### 7.1 Tested 2026-05-28: §7's "push a profile/image first" assumption is **REFUTED on this unit**

A live attempt to execute §7's plan on the physically-connected white-label
`0x0300:0x3004` ("HOTSPOTEKUSB HID DEMO") unit did **not** produce any input.
The full ground-truth audit:

- **Output works** (kernel + framing both correct): `CRT LIG` / `CLE` /
  `DIS` / `CONNECT` keep-alive / full `BAT` image push on `/dev/hidraw16`
  with the POSIX `0x00` report-id prepend all visibly drove the panel; an
  85×85 JPEG pushed to `BAT keyIndex=1` rendered on the touch strip (matching
  the wire-byte map in commit `037bd8d` — strip is byte 5, NOT key 1; the
  re-confirmation also closes §2.2 "renders on Linux" positively for AKP05).
- **Comms work**: `GET_FEATURE` report-id `0x01` returns
  `V3.AKP05E.01.007` (the proven mirajazz firmware probe, commit `5ec18d9`).
- **Kernel-side input path is correct**: `usbmon` confirms the interrupt-IN
  URB is armed on EP `0x82` at hidraw-open (`S Ii:1:NNN:2 -115:1 512 <`),
  i.e. the host is correctly polling the device's input endpoint.
- **Device-side: silent.** Across every host-readable path — hidraw raw
  read, GET_REPORT polling (HIDIOCGINPUT / HIDIOCGFEATURE), evdev
  `event264`, and raw `usbmon` filtered to the device — **zero input
  reports arrive on press**, regardless of activation state (bare open,
  after `VER`, after `DIS`+`LIG`, after a periodic `CRT CONNECT`
  keep-alive at 1 s cadence, after a full `BAT`→chunks→`ULEND` image push).
- **Reference library also captures nothing.** A minimal binary built
  against `4ndv/mirajazz` (its own `async_hid` backend, its private
  `initialize()` = `CRT DIS` + `CRT LIG`, `Device::connect(dev, 3, 10, 4)`
  matching the AKP05E geometry) opened the device successfully and read
  for ~60 s with zero `(key, state)` events on press, twice — including
  immediately after a fresh USB replug. mirajazz is the authoritative
  library this project's firmware probe was modelled on.

**Conclusion:** input streaming on this `0x3004` *white-label / "HID DEMO"*
unit is not reachable via Linux HID — not by our backend, not by mirajazz,
not by any documented activation sequence. The kernel does its part; the
device declines to emit. Most likely: demo/engineering-firmware variant
with the input-reporting path disabled or stubbed (this is a deliberate
non-retail "DEMO" SKU per §14.1 of `akp05_vendor.md`).

**What remains viable for the §7 capture:**

- **Frida-on-Windows vendor-app** (the *decisive* method per
  `dossier/methods-and-tooling.md` §2 — the same method that cracked the
  AK980 keyboard time-sync + the AJ mouse `0x28` clock). Hook
  `Stream Dock AJAZZ.exe`'s `HidD_SetFeature` / `WriteFile` / interrupt
  reads against a retail device to capture the live `report[9]`/`report[10]`
  values. Pre-requisite is a Windows host + a unit where the vendor app
  receives input (i.e. NOT this white-label).
- **Retail AKP05E / Mirabox N4** — repeating the Linux hidraw capture
  pattern below on a non-demo unit would isolate "demo firmware" vs
  "AKP05E family-wide". Not currently possible.

Until either is available, the encoder/touch decode rework in §7 remains
genuinely **blocked on hardware** — but no longer on a missing technique
or undiscovered enable command. We exhausted the documented vendor protocol
plus the reference library on the only physically-available unit.

**Capture pattern used (reproducible)** — for the retail-unit retest:
open `/dev/hidraw*` matching `0x0300:0x3004` usage-page `0xFFA0` `O_RDWR`;
send `CRT DIS` then `CRT LIG` on the same handle (each = 1-byte `0x00`
report-id prepend + 1024-byte CRT-framed payload); start a periodic
`CRT CONNECT` keep-alive (~1 s) on that handle; then `select()`/read it
in a loop, printing `buf[9]`/`buf[10]` per arriving report. Confirmation
of the kernel side: `usbmon` should show `S Ii:1:<addr>:2 -115:1 512 <`
after the open (URB armed); data arrival prints as a `C Ii:...` line.

______________________________________________________________________

## 8. Cross-references

- `docs/protocols/streamdeck/akp05_vendor.md` (§1.2 backends, §2 opcode table,
  §14.1 the 0x3004 = AKP05E correction).
- `docs/protocols/streamdeck/akp05.md` (§"Tag byte at offset 9" — the hypothesis
  this note partially confirms / partially refutes).
- `docs/protocols/streamdeck/akp_device_matrix.md:262` (`AKP05/MBox-N4 … hid+winusb`).
- Auto-memory `akp05e-capture-findings` (the live-session log + this conclusion).
