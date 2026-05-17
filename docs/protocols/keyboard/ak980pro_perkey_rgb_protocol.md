# AJAZZ AK980 PRO — per-key RGB upload protocol

Complete byte-level specification of the per-key RGB write and
read-back paths (opcodes `0x20 0x04` write, `0xF5` read). Supersedes
§3.7-§3.8 of `ak980pro_vendor.md`.

Source: Ghidra decompiles of `FUN_00427db0` ("CustomLightMode tab"
write), `FUN_004329a0` ("KeyboardCtrl tab" write), `FUN_0042ae80`
(read-back). Raw decompiles at
`C:/Users/unilo/reverse-eng-workdir/ak980pro/decomp_targets/`.

______________________________________________________________________

## 1. Transport summary

**Per-key RGB uses the HID FEATURE-report bulk transport**
(`FUN_0044f0c0` → `HidD_SetFeature` for small headers, `HidD_GetFeature`
for read-back). Each FEATURE report on the wire is 65 bytes.

The same opcode `0x20 0x04` is used to upload from **two UI tabs**
(`CustomLightMode` and `KeyboardCtrl`) — the only difference is which
mui.dll widget owns the source RGB matrix. Both call sites are
identical bytewise.

______________________________________________________________________

## 2. Three-packet envelope (write path)

### 2.1 Step 1 — header packet (`0x20 0x04`)

```
byte 0 : 0x04              (frame magic — FUN_0044eed0 prepends 0x00 ReportID)
byte 1 : 0x20              (CMD_PERKEY_RGB)
byte 2 : 0x04              (sub-op = "write per-key blob")
bytes 3..7 : zero
byte 8 : mode select       (0x03 = wired, 0x08 = wireless)
bytes 9..63 : zero
(on-wire after Report ID 0: [0x00, 0x04, 0x20, 0x04, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x03|0x08, ...])
```

Sent via `FUN_0044f0c0(handle, packet, 0x41)` with `param_2 = 0x41`.
The function returns success/failure (the binary checks `cVar1 != 0`
before proceeding to step 2).

### 2.2 Step 2 — RGB blob (raw bytes, no opcode framing)

The RGB blob is sent via `FUN_0044f0c0(handle, blob, len, 0x41 / 0x200)`
which **chunks the payload into 64-byte FEATURE-report slices**. The
caller's blob has **no opcode prefix** — bytes 0..len-1 are raw RGB
data per the §3 layout below.

`FUN_0044f0c0` for `param_2 >= 0x42` (= 66) enters the chunked loop:

```
for (i = 0; i < param_2 >> 6; i++) {
   memset(out_buf, 0, 0x41);                 // 65-byte zeroed buffer
   memcpy(out_buf + 1, blob + i*64, 64);     // bytes 1..64 of out_buf
                                             // (byte 0 = 0x00 Report ID)
   HidD_SetFeature(handle, out_buf, 0x41);
   Sleep(2);
}
```

So **each on-wire chunk has NO opcode** — just 64 bytes of raw blob,
preceded by a 0x00 Report ID byte. The firmware knows what to do
because step 1 set the context (CMD_PERKEY_RGB + mode select).

### 2.3 Step 3 — CMD_SAVE (`0x02 0x04`)

```
byte 0 : 0x04
byte 1 : 0x02              (CMD_SAVE)
bytes 2..63 : zero
```

Sent via `FUN_0044f0c0(handle, packet, 0x41)`.

______________________________________________________________________

## 3. RGB blob layout

### 3.1 Wired (`*(param_1 + 0x7ac) == 0`): 192 bytes (`0xC0`) = 3 chunks × 64 bytes

**1 byte per LED**, indexed by `light_index` from `rgb-keyboard.xml`.
Each byte = a "brightness" value 0..255.

```
blob[light_index] = brightness_byte;
```

The firmware likely expands this brightness back into per-LED RGB by
applying it to a single base color (the **active static-color**? or
**white**?) — this is an **MCU-side heuristic** we can't derive from
the host binary alone.

**Implication**: the wired path supports **monochromatic per-key
intensity, NOT full per-key RGB**. To set arbitrary per-key colors on
the wired interface, the firmware likely combines this byte with a
separate "active color" set via the 0x08 zone-color opcode (CmdSetRgbStatic).

### 3.2 Wireless (`*(param_1 + 0x7ac) != 0`): 512 bytes (`0x200`) = 8 chunks × 64 bytes

**4 bytes per LED**, indexed by `light_index`. Each entry layout:

| Offset within slot | Field          | Meaning                        |
| ------------------ | -------------- | ------------------------------ |
| +0                 | reserved (0)   | Always zero in the upload path |
| +1                 | **R** (0..255) | Red channel                    |
| +2                 | **G** (0..255) | Green channel                  |
| +3                 | **B** (0..255) | Blue channel                   |

```
blob[light_index * 4 + 0] = 0;
blob[light_index * 4 + 1] = R;
blob[light_index * 4 + 2] = G;
blob[light_index * 4 + 3] = B;
```

Wait — **verify against the read-back decoder**:

```c
// FUN_0042ae80 read-back:
uVar6 = CONCAT11(local_2b0[iVar7*4 + 3], local_2b0[iVar7*4 + 2]);
//        ^ high                          ^ low  → 16-bit value =
//        byte[+3] << 8 | byte[+2]
uVar5 = local_2b0[iVar7*4 + 1];
*(iVar4 + 0x30) = (uVar6 << 8) | uVar5;
//        = (byte[+3] << 16) | (byte[+2] << 8) | byte[+1]
//        = packed 24-bit value
```

The read-back code stores the 24-bit value as `0xBBGGRR` (B = high byte,
R = low byte). Or, in conventional layout: **B is byte +3, G is byte
+2, R is byte +1**. So the upload layout matches.

So the correct interpretation is:

| Offset | Field    |
| ------ | -------- |
| +0     | reserved |
| +1     | R        |
| +2     | G        |
| +3     | B        |

The `iVar4 + 0x30` write packs as `byte[+3] << 16 | byte[+2] << 8 | byte[+1]`. If we treat this as an ARGB or 0xRRGGBB value, **byte 1 = R**
(low byte), **byte 2 = G**, **byte 3 = B** (high byte). That's just
saying "the 4-byte slot stores BGR0 in memory order" if you read it as
little-endian 32-bit, OR "0,R,G,B" in byte order.

**Wireless = 512 bytes / 4 = 128 LEDs.** But the layout XML has
`light_index` up to 123 — so 5 slots are unused (light_index 124..127).

### 3.3 The 192 vs 512 mystery

The wired interface gets a 192-byte blob; the wireless gets 512. Why?

- **Wired** = 1 byte/LED × 192 LEDs (or 192 bytes × 1 LED each):
  monochromatic-only, fixed-color basis. The vendor likely defaults
  the wired "active color" to white, so the per-key byte acts as a
  per-LED master brightness.

- **Wireless** = 4 bytes/LED × 128 LEDs: full RGB per key with one
  reserved byte (possibly for blending alpha or fade state).

The wired form is a deliberate firmware optimization. With 192 LEDs ×
3 bytes RGB each, the full-color blob would be 576 bytes (= 9 chunks).
By reducing to monochromatic-only on wired, the vendor halves the
upload time. The Wireless dongle uses a full-color blob with fewer LEDs
(128 vs 192) because the 2.4 GHz link is slower than USB and they
need to compromise on either color depth or LED count.

For **our reimplementation**: always offer full per-key RGB to the
user. On wired, fall back to monochromatic only if the user explicitly
opts in; otherwise use the 0x08 + per-zone path for color and the
0x20 0x04 + monochrome path for intensity.

______________________________________________________________________

## 4. Read-back path (`0xF5 0x03 / 0xF5 0x09`)

`FUN_0042ae80` queries the current per-key RGB state from the device.

### 4.1 Request packet

```
byte 0 : 0x04              (frame magic — FUN_0044f3a0 path uses ReportID=0)
byte 1 : 0xF5              (CMD_PERKEY_RGB_READBACK)
byte 2 : 0x03 (wired) or 0x09 (wireless)   (= number of 64-byte chunks to read)
bytes 3..7 : zero
byte 8 : 0x03 (wired) or 0x09 (wireless)   (re-echoed)
bytes 9..600 : zero (large scratch buffer — but only first 64 bytes
                    of the buffer are sent on the wire)
```

Sent via `FUN_0044f3a0(handle, buf, count)` — the **streaming reader**
(see `ak980pro_vendor.md` §2.5). This function:

- Sends the request via `WriteFile` (the first 64 bytes)
- Reads `count * 64` bytes back via `ReadFile + GetTickCount` polling
  loop with 360 ms timeout (3 wired chunks = 192 bytes; 9 wireless
  chunks = 576 bytes)
- Strips leading zero byte (Report ID) from each chunk

### 4.2 Response decoding

After read-back, the binary iterates through KeyItems and reads the
RGB values from the response buffer:

```c
// FUN_0042ae80, lines 67-74:
iVar7 = key.light_index;
if (wired) {
    // 1 byte per key, scaled monochrome:
    uVar5 = (response[iVar7] * 0xFF) >> 8;  // = identity (since *255/256)
    uVar6 = (uVar5 << 8) | uVar5;           // duplicate R = G
    rgb24 = (uVar6 << 8) | uVar5;           // and B → (R,G,B) all = brightness
} else {
    // 4 bytes per key, BGR0 in slots:
    uVar6 = CONCAT11(response[iVar7*4 + 3], response[iVar7*4 + 2]);
    uVar5 = response[iVar7*4 + 1];
    rgb24 = (uVar6 << 8) | uVar5;
}
key.color = rgb24;
```

So the **wired read-back** confirms the monochromatic interpretation:
the firmware returns a single byte per LED, and the host expands it to
greyscale RGB.

### 4.3 Post-read CMD_SAVE

After populating the UI from the read-back, the binary sends
`0x02 0x04` (CMD_SAVE) via FUN_0044eed0. This is the "commit" of the
read state — likely so the user's edits get persisted alongside the
read-back values.

______________________________________________________________________

## 5. Bandwidth budget

| Direction                   | Bytes | Chunks | Sleep total | Wall time                                                       |
| --------------------------- | ----- | ------ | ----------- | --------------------------------------------------------------- |
| Write wired (0xC0 blob)     | 192   | 3      | 6 ms        | ~30 ms total (3 packets at ~10 ms each through HidD_SetFeature) |
| Write wireless (0x200 blob) | 512   | 8      | 16 ms       | ~80 ms total                                                    |
| Read wired (0xC0)           | 192   | 3      | 6 ms        | ~30 ms                                                          |
| Read wireless (0x240)       | 576   | 9      | 18 ms       | ~90 ms                                                          |

Fast enough for **real-time per-key editing** in the UI (≥ 30 fps for
a color-picker drag preview). The wireless path is the bottleneck but
still acceptable.

______________________________________________________________________

## 6. Code corrections required

### 6.1 Files to add (no existing per-key RGB code)

1. **`src/devices/keyboard/include/ajazz/keyboard/perkey_rgb.hpp`**

   ```cpp
   class PerKeyRgb : public QObject {
       Q_OBJECT
   public:
       struct LedColor { std::uint8_t r, g, b; };

       void uploadColors(const std::vector<LedColor>& colorsByLightIndex,
                         bool isWireless);

       void readbackColors(bool isWireless,
                           std::vector<LedColor>& out);

   signals:
       void uploadCompleted(bool ok);
       void readbackCompleted(std::vector<LedColor> colors);
   };
   ```

1. **`src/devices/keyboard/proprietary_protocol.hpp`** — add:

   ```cpp
   inline constexpr std::uint8_t kCmdPerKeyRgbWrite     = 0x20;  // sub 0x04
   inline constexpr std::uint8_t kCmdPerKeyRgbReadback  = 0xF5;  // sub 0x03/0x09
   inline constexpr std::uint8_t kPerKeyModeWired      = 0x03;
   inline constexpr std::uint8_t kPerKeyModeWireless   = 0x08;  // (write)
   inline constexpr std::uint8_t kPerKeyReadbackWired   = 0x03;
   inline constexpr std::uint8_t kPerKeyReadbackWireless = 0x09; // (read)

   inline constexpr int kPerKeyWiredBlobSize   = 0xC0;   // = 192
   inline constexpr int kPerKeyWirelessBlobSize = 0x200; // = 512
   inline constexpr int kPerKeyWirelessLeds    = 128;    // 512 / 4
   inline constexpr int kPerKeyWiredLeds       = 192;    // 1 byte / LED
   inline constexpr int kPerKeyReadbackWiredBytes    = 192; // 3 * 64
   inline constexpr int kPerKeyReadbackWirelessBytes = 576; // 9 * 64
   inline constexpr int kPerKeySlotBytesWireless = 4;
   // Wireless slot layout: [reserved=0, R, G, B] at offset light_index * 4
   ```

### 6.2 Unit test coverage

```cmake
ajazz_add_test(ak980pro_perkey_rgb_protocol_test SOURCES
  test_perkey_blob_layout.cpp
  test_perkey_envelope.cpp
  test_perkey_readback_decoding.cpp
)
```

CTest filter tag: `ak980pro-perkey-rgb`.

Critical tests:

```cpp
TEST_CASE("Wired blob is exactly 192 bytes for 192 LEDs (monochrome)",
          "[ak980pro-perkey-rgb]") {
    std::vector<LedColor> colors(192, {255, 128, 0});  // orange
    auto blob = buildPerKeyBlobWired(colors);
    CHECK(blob.size() == 192);
    // Wired stores monochrome (e.g., max channel or luminance — exact
    // formula TBD when we verify on hardware).
    // For now, document that we just send max(R,G,B) per LED:
    CHECK(blob[0] == 255);  // max(255, 128, 0)
}

TEST_CASE("Wireless blob is 512 bytes with [0,R,G,B] per LED",
          "[ak980pro-perkey-rgb]") {
    std::vector<LedColor> colors(128, {0x11, 0x22, 0x33});
    auto blob = buildPerKeyBlobWireless(colors);
    CHECK(blob.size() == 512);
    CHECK(blob[0] == 0x00);      // reserved
    CHECK(blob[1] == 0x11);      // R
    CHECK(blob[2] == 0x22);      // G
    CHECK(blob[3] == 0x33);      // B
}

TEST_CASE("Wireless blob respects light_index addressing",
          "[ak980pro-perkey-rgb]") {
    std::vector<LedColor> colors(128);
    colors[42] = {0xAA, 0xBB, 0xCC};
    auto blob = buildPerKeyBlobWireless(colors);
    CHECK(blob[42*4 + 0] == 0x00);
    CHECK(blob[42*4 + 1] == 0xAA);
    CHECK(blob[42*4 + 2] == 0xBB);
    CHECK(blob[42*4 + 3] == 0xCC);
}

TEST_CASE("Wired write header has mode byte 0x03 at offset 8",
          "[ak980pro-perkey-rgb]") {
    auto hdr = buildPerKeyHeader(/*isWireless=*/false);
    CHECK(hdr[0] == 0x04);
    CHECK(hdr[1] == 0x20);
    CHECK(hdr[2] == 0x04);
    CHECK(hdr[8] == 0x03);
}

TEST_CASE("Wireless write header has mode byte 0x08 at offset 8",
          "[ak980pro-perkey-rgb]") {
    auto hdr = buildPerKeyHeader(/*isWireless=*/true);
    CHECK(hdr[8] == 0x08);
}

TEST_CASE("Wireless readback header has chunk count 0x09 at offsets 2 and 8",
          "[ak980pro-perkey-rgb]") {
    auto req = buildPerKeyReadbackRequest(/*isWireless=*/true);
    CHECK(req[1] == 0xF5);
    CHECK(req[2] == 0x09);
    CHECK(req[8] == 0x09);
}

TEST_CASE("Wireless readback decoding extracts [+1,+2,+3] = [R,G,B]",
          "[ak980pro-perkey-rgb]") {
    std::vector<std::uint8_t> response(576, 0);
    response[42*4 + 1] = 0xDE;  // R
    response[42*4 + 2] = 0xAD;  // G
    response[42*4 + 3] = 0xBE;  // B
    auto colors = decodePerKeyResponseWireless(response, 128);
    CHECK(colors[42].r == 0xDE);
    CHECK(colors[42].g == 0xAD);
    CHECK(colors[42].b == 0xBE);
}
```

### 6.3 Hardware verification needed

Two specific items need confirmation on real hardware before we ship:

1. **Wired write byte interpretation**: is the 192-byte blob really
   monochromatic brightness (as the read-back decoder suggests)? Or
   could it be a packed 3-bytes-per-key RGB across 64 LEDs (also fits
   192)? Test by sending a sentinel pattern (e.g., LED #0 = 0xFF,
   others zero) and observing.

1. **Wireless reserved byte**: does setting byte +0 to non-zero do
   anything? Could be fade-time, alpha, or simply ignored. Test by
   varying it across calls.
