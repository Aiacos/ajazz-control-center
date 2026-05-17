# AJAZZ AK980 PRO — TFT/LCD image upload protocol (deep dive)

Complete byte-level specification of the 240×135 RGB565 TFT screen
upload protocol. Supersedes §5 of `ak980pro_vendor.md`.

Source: Ghidra decompilation of `FUN_004231c0` (primary path, **HID
FEATURE-report chunks**) and `FUN_00422920` (alternate path, **HID
4097-byte bulk write**). See raw decompiles at
`C:/Users/unilo/reverse-eng-workdir/ak980pro/decomp_targets/FUN_004231c0.c`
and `FUN_00422920.c`.

______________________________________________________________________

## 1. Geometry (from `config.xml` + `rgb-keyboard.xml`)

```xml
<screen gif_headlength="256" gif_maxframes="140" gif_count="1"
        width="240" height="135"/>
```

- **Screen**: 240 × 135 px
- **Pixel format**: RGB565 (16 bpp, 2 bytes/pixel)
- **Frame size on the wire**: 240 × 135 × 2 = **64 800 bytes**
- **Max frames per "GIF" upload**: **140**
- **Per-frame delay**: client-side timing — see §5
- **GIF header on the wire**: 256 bytes (per `gif_headlength`) — see §4
- **Per-chunk payload**: **28 bytes (0x1C)** in the primary path,
  **4096 bytes (0x1000)** in the bulk path
- **Inter-chunk Sleep**: 2 ms

______________________________________________________________________

## 2. Transport convention

The TFT upload uses the **HID FEATURE-report transport** (FUN_0044f5f0
→ FUN_00451220 → `WriteFile`) for both header and chunk packets. Each
packet is exactly **33 bytes on the wire** (`OutputReportByteLength =
0x21`). Wire layout for every TFT packet:

```
byte 0 : 0x00          (HID Report ID — fixed)
byte 1 : opcode-low
byte 2 : opcode-high   (with 0x80 marker bit on chunks)
byte 3 : extra
bytes 4..31 : 28 bytes RGB565 pixel data OR header data
byte 32 : 0            (checksum slot — but TFT path doesn't compute
                       checksum; FUN_0044f5f0 still writes byte 8 of
                       its 33-byte buffer with sum mod 256, but the
                       firmware doesn't validate this for the TFT
                       opcode based on observed behavior)
```

> **Note on byte 0**: The vendor binary's local buffer puts the opcode
> at offset 1 directly (no leading 0x04 frame magic), since this path
> goes through `WriteFile`. The frame magic 0x04 only appears for paths
> that go through `HidD_SetFeature` (see §2.1 of
> `ak980pro_vendor.md` for the transport distinction).

______________________________________________________________________

## 3. Primary upload path (`FUN_004231c0`, 28-byte chunks)

This is the "fine-grained progress, low memory" path. Used by default.

### 3.1 Buffer preparation

```c
// Compute total chunk count for one frame
int frame_size      = 64800;        // 240 × 135 × 2
int chunk_count     = (frame_size + 0x1C - 1) / 0x1C;   // = 2 315
int total_chunks    = chunk_count * frames_in_gif;

// Allocate scratch buffer (4-byte aligned, +1 chunk of slack)
byte* buf = malloc(frame_size + 0x1C);
memset(buf, 0xFF, frame_size + 0x1C);   // white-fill, RGB565 0xFFFF

// Per-frame: write 2-byte delay header at start of each frame slot
buf[0] = (byte)frame_count;
for (int i = 0; i < frame_count; i++) {
    buf[i + 1] = (byte)(frame[i].delay_centisec / 2);   // 2ms units
    fetch_RGB565_pixels_into(buf + offset_for_frame(i));
}
```

The buffer carries:
- Byte 0 = total frame count (≤140)
- Bytes 1..frame_count = per-frame delay in 2 ms units (one byte each)
- Pixel data starting at byte `gif_headlength` = 256

### 3.2 Header packet (`0x7F 0x03`)

```
byte 0 : 0x00              (HID Report ID)
byte 1 : 0x7F              (CMD_SCREEN_HEADER)
byte 2 : 0x03              (sub-op = "begin frame stream")
byte 3 : LCD-select index + 1   (= 1 for single-LCD)
bytes 4..7 : total_chunks (little-endian uint32; only lower 24 bits used)
bytes 8..32 : zero
```

Sent once via `FUN_0044f5f0(handle, buf, 0x41)` with `param_2 = 0x41`.

### 3.3 Chunk packet (chunk index `i` in `[0..total_chunks)`)

```
byte 0 : 0x00                       (HID Report ID)
byte 1 : (i & 0xFF)                 (chunk index low 8 bits)
byte 2 : 0x80 | ((i >> 16) & 0xFF)  (0x80 marker + chunk index bits 16-23)
byte 3 : (i >> 8) & 0xFF            (chunk index bits 8-15)
bytes 4..31 : 28 bytes RGB565 pixel data (from buf + i*0x1C + 256)
```

Sent via `FUN_0044f5f0(handle, buf, 0x41)` per chunk, then `Sleep(2)`.

**17-bit chunk index** is actually a **24-bit chunk index split across
bytes 1, 3, and the low nibble of byte 2**:

```
chunk_idx = byte[1] | (byte[3] << 8) | ((byte[2] & 0x7F) << 16)
```

With 24 bits available, max chunk count = 2^24 = 16 777 216, far above
the practical limit (140 frames × 2 315 chunks/frame = 324 100 chunks).

The **MSB of byte 2 is the "chunk marker"** (always set to 0x80) — this
is what distinguishes a chunk from a header packet (whose byte 2 = 0x03).

### 3.4 Per-frame progress

The vendor binary updates an `MProgress` widget after every chunk:

```c
progress_percent = (chunks_sent * 100) / total_chunks;
MProgress::SetProgress(ctrl, progress_percent);
```

### 3.5 No acknowledgement on the read path

The vendor binary does **not** poll for ACKs during a TFT upload. It
simply sends all chunks and assumes success. The firmware is expected
to buffer + render the GIF asynchronously.

______________________________________________________________________

## 4. Alternate upload path (`FUN_00422920`, 4 097-byte bulk chunks)

This is the "high-throughput, less progress feedback" path. Used in
parallel or as a fallback (the trigger condition is not yet
identified — likely a `config.xml` flag we haven't decoded).

### 4.1 Three-packet envelope

```
1. CMD_START (length 0x41 via FUN_0044eed0):
   byte 0 : 0x04          (frame magic — FUN_0044eed0 prepends 0x00 ReportID)
   byte 1 : 0x18          (CMD_START)
   bytes 2..63 : zero
   (on-wire: [0x00, 0x04, 0x18, 0x00, 0x00, …])

2. CMD_SCREEN_BEGIN_BULK (length 0x41 via FUN_0044eed0):
   byte 0 : 0x04
   byte 1 : 0x72                  (CMD_SCREEN_BEGIN_BULK — NEW opcode discovered)
   byte 2 : 0x00
   byte 3 : LCD-select index + 1
   byte 4 : total_4k_chunks low byte
   byte 5 : total_4k_chunks high byte
   bytes 6..63 : zero
   (on-wire: [0x00, 0x04, 0x72, 0x00, lcd, total_lo, total_hi, ..., 0x00])

3. BULK CHUNKS (length 0x1001 = 4097 via FUN_0044f2d0):
   Per chunk i in [0..total_4k_chunks):
   byte 0 : 0x00          (Report ID for bulk transport; FUN_0044f2d0
                          sets it directly via WriteFile)
   bytes 1..4096 : memcpy(buf + i*0x1000, 0x1000)
   (No chunk index field — chunks are sent in strict order)
```

### 4.2 Closing

```
4. CMD_SAVE (length 0x41):
   byte 0 : 0x04
   byte 1 : 0x02          (CMD_SAVE)
   bytes 2..63 : zero
```

**Total payload size**: `total_4k_chunks * 4096` ≥ the buffer size
(rounded up). For a 1-frame 64 800 B image: 16 chunks (= 65 536 bytes,
some padding). For a 140-frame GIF: ~140 × 16 = 2 240 chunks ≈ 9 MB.

This path is **far faster** than the 28-byte path (only 16 chunks vs
2 315 per frame), so it's suspicious that the binary supports both.
Hypothesis: the 28-byte chunk path is used for **resumable / canceleable**
uploads where the per-chunk index lets the firmware detect drops, and
the 4 KB path is used for **first-time / factory-reset** uploads.

______________________________________________________________________

## 5. Bandwidth budget

### Primary path (28-byte HID FEATURE chunks)

- 33 bytes on-wire per chunk × 2 ms inter-chunk = 16 500 byte/s effective
- One frame = 2 315 chunks × 2 ms = **4 630 ms ≈ 4.63 s/frame**
- 140-frame GIF = **648 s ≈ 10.8 min/full upload** 🙃

Yes, the vendor's full GIF upload genuinely takes **10+ minutes** on
the slow path. The `MProgress` bar is necessary because the user is
going to sit there waiting.

### Alternate path (4 KB HID bulk chunks)

- 4097 bytes on-wire per chunk × 2 ms = 2 MB/s effective
- One frame = 16 chunks × 2 ms = **32 ms/frame**
- 140-frame GIF = **4.5 s/full upload** ✅

This is **143× faster**. For our reimplementation, **always use the bulk
path** — and if the firmware refuses, fall back to the chunked path with
a clear progress indicator.

______________________________________________________________________

## 6. RGB565 encoding direction

The pixel buffer is filled by `MUI::LCDViewList::GetImageRGB565Data` —
a mui.dll export that converts the user's drawing (an
`MUI::ImageView::CreateARGB32Bitmap`) into RGB565.

**Pixel order**: row-major, **top-down** (matches GDI's `biHeight = -h`
convention seen in `CreateARGB32Bitmap`). Each pixel:

```
byte 0 : R[7:3] | G[7:5]            (= RGB565 high byte)
byte 1 : G[4:2] | B[7:3]            (= RGB565 low byte)
```

i.e. **big-endian RGB565 on the wire** (this is the standard
SPI-display byte order: `0xRRRR_RGGG_GGGB_BBBB`).

### Verifying byte order (recommendation for our impl)

When we reimplement, send a test image with:
- Pixel (0, 0) = pure red = `0xF800` (R=31, G=0, B=0)
- Pixel (1, 0) = pure green = `0x07E0`
- Pixel (2, 0) = pure blue = `0x001F`

If the device shows them in correct row across the top, the layout is
top-down row-major. If the colors are wrong, swap the byte order
(`htobe16` / `htole16`).

______________________________________________________________________

## 7. ACK protocol on the read-back path

**There is none** for TFT uploads. The binary issues chunks and assumes
success. The only "feedback" is the `MProgress` bar incrementing.

If we want to detect transport errors we'd need to:
- Watch for HID write failures (Qt: `QHidDevice::write` returning -1)
- Add an explicit `CMD_FINISH` after the last chunk (opcode `0xF0`) and
  wait for it to come back via the input-report poll path (no
  guarantee the firmware ACKs `0xF0` either)

Recommendation: **don't add fake ACKs**. Display the progress bar
honestly, and let the user retry on a timeout (we should set a 60 s
upload timeout per frame in the worker thread).

______________________________________________________________________

## 8. Code corrections required

### 8.1 Files to add (no existing TFT code in our tree)

We have **no AK980 PRO TFT upload code** in our tree as of this commit
(only the Stream Deck DRA rect-addressable path landed in 24c0965
covers a different device). To implement:

1. **`src/devices/keyboard/include/ajazz/keyboard/screen_uploader.hpp`**
   — new header with `class ScreenUploader` (`QObject`) exposing
   `uploadGif(const QString& path)` slot and `progress(int percent)`
   signal.

2. **`src/devices/keyboard/src/screen_uploader.cpp`** — implementation
   that:
   - Decodes the GIF via `QMovie` to a `QList<QImage>` (max 140 frames)
   - Converts each frame to RGB565 via a small helper (Eigen-style
     loop or, ideally, a SIMD path on AVX2)
   - Builds the per-frame delay header (byte 0 = frame count,
     bytes 1..frames = per-frame delay/2)
   - Selects the **bulk path (0x72 opcode)** by default; falls back to
     the **chunked path (0x7F + 0x80-chunks)** on bulk failure
   - Emits `progress` every chunk

3. **`src/devices/keyboard/proprietary_protocol.hpp`** — add:

   ```cpp
   // TFT screen upload opcodes (FUN_004231c0, FUN_00422920).
   inline constexpr std::uint8_t kCmdScreenHeader  = 0x7F;  // chunked path
   inline constexpr std::uint8_t kCmdScreenSubBegin = 0x03;
   inline constexpr std::uint8_t kCmdScreenChunk   = 0x80;  // marker bit
   inline constexpr std::uint8_t kCmdScreenBulkBegin = 0x72; // bulk path
   inline constexpr std::uint8_t kCmdScreenSave   = 0x02;   // CMD_SAVE

   // Buffer constants
   inline constexpr int kTftWidth          = 240;
   inline constexpr int kTftHeight         = 135;
   inline constexpr int kTftFrameBytes     = kTftWidth * kTftHeight * 2;  // 64 800
   inline constexpr int kTftChunkPayload   = 28;     // 0x1C
   inline constexpr int kTftBulkChunkSize  = 4096;   // 0x1000
   inline constexpr int kTftMaxFrames      = 140;
   inline constexpr int kTftGifHeader      = 256;
   inline constexpr int kTftInterChunkMs   = 2;
   ```

### 8.2 Unit test coverage

Add to `tests/devices/keyboard/CMakeLists.txt`:

```cmake
ajazz_add_test(ak980pro_tft_protocol_test SOURCES
  test_tft_packet_builder.cpp
  test_tft_chunk_index_encoding.cpp
  test_tft_rgb565_conversion.cpp
)
```

CTest filter tag: `ak980pro-tft`.

Fixture shape:

```cpp
// test_tft_chunk_index_encoding.cpp
TEST_CASE("Chunk index 0 encodes correctly", "[ak980pro-tft]") {
    const auto pkt = buildTftChunk(/*idx=*/0u, /*pixels=*/{});
    CHECK(pkt[0] == 0x00);  // ReportID
    CHECK(pkt[1] == 0x00);  // low 8 bits
    CHECK(pkt[2] == 0x80);  // marker (no high bits set)
    CHECK(pkt[3] == 0x00);  // mid 8 bits
}

TEST_CASE("Chunk index 2314 (last of one frame) encodes", "[ak980pro-tft]") {
    const auto pkt = buildTftChunk(/*idx=*/2314u, /*pixels=*/{});
    CHECK(pkt[1] == 0x0A);  // 2314 & 0xFF = 0x0A
    CHECK(pkt[2] == 0x80);  // (2314 >> 16) & 0x7F = 0
    CHECK(pkt[3] == 0x09);  // (2314 >> 8) & 0xFF = 9
}

TEST_CASE("Chunk index 324_099 (last of 140-frame GIF) encodes", "[ak980pro-tft]") {
    const auto pkt = buildTftChunk(/*idx=*/324099u, /*pixels=*/{});
    CHECK(pkt[1] == 0x83);
    CHECK(pkt[2] == 0x84);  // 0x80 | ((324099 >> 16) & 0x7F) = 0x80 | 0x04
    CHECK(pkt[3] == 0x18);
}
```

### 8.3 Cross-team coordination

- Add a `tools/ajazz-tft-validate` CLI (one-shot) that takes a GIF
  path, runs the encoder + chunk-builder, dumps every packet hex to
  stdout. This is a **debugging aid only**, not a production tool.
  Single-file program, ~150 lines.
