# AJAZZ AK980 PRO — TFT/LCD image upload protocol (deep dive)

Complete byte-level specification of the 240×135 RGB565 TFT screen upload
protocol, **rebuilt from scratch from the vendor decompile on 2026-05-21**.
Supersedes §5 of `ak980pro_vendor.md` and the earlier (transposed) revision
of this file.

> **STATUS: PROVISIONAL — decompile-derived, NOT hardware-confirmed.**
> Every byte layout below is read directly from the Ghidra decompile (the most
> authoritative source we have), but **no USB/Frida capture of a TFT upload
> exists yet**. Per the project rule "when the RE and the hardware disagree,
> the hardware wins": treat §3/§4 as a hypothesis to verify against the
> physical device, and update this doc + the code if a capture contradicts it.
> The open questions that only a capture can settle are listed in §8.

Source: Ghidra decompilation of `FUN_004231c0` (primary path, **HID
output-report chunks**) and `FUN_00422920` (alternate path, **HID 4097-byte
bulk write**), plus the transport helpers `FUN_0044f5f0` (output-report write
+ readback), `FUN_00451220` (`WriteFile`), `FUN_0044eed0` (feature-report
write), `FUN_00451440` (`HidD_SetFeature`). Raw decompiles at
`C:/Users/unilo/reverse-eng-workdir/ak980pro/decomp_targets/`.

The C++ implementation lives in `src/devices/keyboard/src/proprietary_keyboard.cpp`
(`encodeTftChunkIndex`, `buildTftChunkedHeader`, `buildTftChunkedPayload`,
`stampTftChecksum`, `encodeRgb565`, `buildScreenBulkBegin`, `uploadTftImage`)
with constants in `proprietary_protocol.hpp` and tests in
`tests/unit/test_ak980_tft_chunked.cpp` + `test_proprietary_keyboard_protocol.cpp`.

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
- **Per-chunk payload**: **28 bytes (0x1C)** in the primary path,
  **4096 bytes (0x1000)** in the bulk path
- **Inter-chunk Sleep**: 2 ms (`FUN_004231c0:299`)
- **`gif_headlength`=256**: a config value; whether 256 header bytes are
  prepended to the wire stream is an open question — see §3.1 + §8.

______________________________________________________________________

## 2. Transport convention

The two upload paths use **two different HID transports** in the vendor binary:

| Path | Helper chain | Win32 call | Wire kind | Logical length |
| --- | --- | --- | --- | --- |
| Chunked (§3) | `FUN_004231c0`→`FUN_0044f5f0`→`FUN_00451220` | `WriteFile` | **output report** | **33 bytes** (0x21), padded to `OutputReportByteLength` |
| Bulk small pkts (§4) | `FUN_00422920`→`FUN_0044eed0`→`FUN_00451440` | `HidD_SetFeature` | **feature report** | 65 bytes (0x41) |
| Bulk 4 KiB blocks (§4) | `FUN_00422920`→`FUN_0044f2d0`→`FUN_00451220` | `WriteFile` | output report | 4097 bytes (0x1001) |

So the **chunked path is an output report** — our code sends it via
`ITransport::write()`, NOT `writeFeature()`. The **bulk path's control packets
are feature reports** (report-id 0x00 prepended by `FUN_0044eed0`), like the
hardware-verified time-sync envelope.

> **⚠ Transport caveat.** The only AK980 PRO surface confirmed against real
> hardware is **time-sync**, which uses **feature reports** (IOCTL_HID_SET_FEATURE,
> 65-byte, report id 0x00, on the 0xFF13 vendor collection — see
> `proprietary_protocol.hpp`). The chunked-TFT decompile uses **output
> reports**. Whether the physical panel accepts output reports for image upload
> is **unverified**. If a capture shows it only takes feature reports, flip
> `uploadTftImage` back to `writeFeature()` and drop the §3.5 checksum.

### 2.1 Report ID

Every TFT packet's byte 0 is the **HID Report ID = 0x00** (unnumbered). The
chunked path puts it directly in the buffer (`FUN_004231c0` leaves byte 0 = 0);
the bulk path's `FUN_0044eed0` memsets a 65-byte buffer (byte 0 = 0x00) and
copies the caller frame starting at byte 1. The `0x04` "frame byte" that the
bulk path carries at byte 1 is a *data* byte, not the report id — identical to
the time-sync convention.

______________________________________________________________________

## 3. Primary upload path (`FUN_004231c0`, 28-byte output-report chunks)

The default "fine-grained progress, low memory" path.

### 3.1 Buffer preparation

```c
// FUN_004231c0:61-79
int pixel_start  = field[0x527c4];               // runtime pixel-start offset (see §8)
int total_bytes  = pixel_start + W*H*frames*2;    // :61-62
int total_chunks = ceil(total_bytes / 0x1C);      // :63-67
byte* buf = malloc(total_bytes + 0x1C);
memset(buf, 0xFF, total_bytes + 0x1C);            // white-fill (:72-79)

buf[0]            = (byte)frame_count;            // :144
buf[1 + i]        = max(1, frame[i].delay / 2);   // per-frame delay, :153-156
// pixel data written from offset `pixel_start` onward (:138, :162-163)
```

The chunk loop reads from `buf + 0` (`puVar9 = local_7c`, `FUN_004231c0:271`)
and advances 28 bytes per chunk (`:298`) — i.e. **chunk 0 transmits the
frame-count + delay header bytes first**, then pixels. The header region is NOT
a separate packet; it is the leading bytes of the chunk stream.

> **Open question (§8):** `pixel_start = field[0x527c4]` is only ever *read* in
> the decompile corpus, never assigned, so its value is unknown. If it equals
> the `gif_headlength=256` from `config.xml`, then bytes 0..255 are the header
> region and `total_chunks = ceil((256 + 64800)/28) = 2324` for one frame. If
> it is 0, `total_chunks = ceil(64800/28) = 2315`. **The current implementation
> assumes 0 (no header prefix, 2315 chunks)** because the decompile does not pin
> 256. This is the single most important thing a capture must resolve.

### 3.2 Header packet (`0x7F 0x03`) — `FUN_004231c0:250-267`

```
byte 0  : 0x00                 (HID Report ID)
byte 1  : 0x7F                 (CmdScreenHeader)        ; :252 low byte of 0x037F
byte 2  : 0x03                 (CmdScreenSubBegin)      ; :252 high byte
byte 3  : 0x00                                          ; :257
byte 4  : lcdSelect + 1        (1-based; single-LCD = 1); :253-255
bytes 5..7 : total_chunks      (24-bit little-endian)   ; :253-256
byte 32 : transport checksum   (sum of bytes mod 256)   ; FUN_0044f5f0:43-50
```

Sent once via `FUN_0044f5f0(handle, buf, 0x41)` (`:267`).

### 3.3 Chunk packet — `FUN_004231c0:273-298`

```
byte 0  : 0x00                              (HID Report ID)
byte 1  : 0x80 | ((i >> 16) & 0x7F)         (0x80 marker + index bits 16-22) ; :274-279, :287
byte 2  : i & 0xFF                          (index low 8 bits)               ; :287
byte 3  : (i >> 8) & 0xFF                   (index middle 8 bits)            ; :284
bytes 4..31 : 28 bytes RGB565 pixel data    (puVar9[0..6])                   ; :280-286
byte 32 : transport checksum                                                 ; FUN_0044f5f0:50
```

**Decoder:** `chunk_idx = byte2 | (byte3 << 8) | ((byte1 & 0x7F) << 16)`.

The **0x80 marker lives on byte 1** (not byte 2). It distinguishes a chunk
packet (byte 1 has 0x80 set) from the header packet (byte 1 = 0x7F). With 23
usable index bits the max is 8 388 607 chunks, well above the practical limit
(140 frames × 2 315 = 324 100).

> **Correction history.** The pre-2026-05-21 revision of this doc + the code
> put the index low byte on byte 1 and the 0x80 marker on byte 2, and put
> LCD-select on header byte 3 with a uint32 count at bytes 4..7. Both were
> byte-transpositions vs `FUN_004231c0`; the impl, doc, and tests were
> self-consistent but contradicted the decompile. Fixed 2026-05-21.

### 3.4 Checksum (byte 32)

`FUN_0044f5f0:43-50` sums all 65 buffer bytes and stores the low 8 bits at
`param_1[8]` = **byte offset 32**, then writes 0x21 = 33 bytes
(`FUN_00451220`, `:68`). Byte 32 is the 33rd byte, so the checksum **is**
included in the wire write (the report tail 33..64 is truncated). With the
checksum slot and tail zero at sum time, this reduces to
`sum(bytes[0..31]) mod 256`. `stampTftChecksum()` reproduces this.

Whether the firmware *validates* it is unknown (§8).

### 3.5 Per-write readback / ACK

`FUN_0044f5f0:91-128` reads back 0x21 bytes after each write (`FUN_00451300` =
overlapped `ReadFile`) and retries up to 20× (`Sleep(10)`) until the response's
bytes 1-3 echo the request's bytes 1-3. This is a generic feature of the
output-report helper, applied per header/chunk. The current implementation
does **not** poll for this ACK (fire-and-forget); a capture should confirm
whether the panel actually echoes, and whether dropping the readback causes
dropped chunks at speed.

### 3.6 Bandwidth

33 bytes/chunk × ~2 ms ⇒ one frame = 2 315 chunks × 2 ms ≈ **4.6 s/frame**; a
140-frame GIF ≈ **10.8 min**. The vendor shows an `MProgress` bar (`:300-307`).

______________________________________________________________________

## 4. Alternate upload path (`FUN_00422920`, 4 KiB bulk chunks)

The "high-throughput" path (143× faster). Scaffolded in code
(`buildScreenBulkBegin`) but **not wired** into `uploadTftImage` — `ITransport`
has no bulk-write surface yet, and no capture confirms the envelope.

### 4.1 Four-packet envelope

```
1. CMD_START         (feature report, FUN_00422920:255,265):
     [0]=00 [1]=04 [2]=0x18 ...                      ; local_1018 = 0x1804

2. CMD_SCREEN_BEGIN_BULK (feature report, :267-270):
     [0]=00 [1]=04 [2]=0x72 [3]=lcdSelect+1
     [9]=count_lo [10]=count_hi                       ; count = ceil(total/0x1000)
   (pre-prepend buffer stores 0x7204 + lcd@offset2 + count@offset8/9;
    FUN_0044eed0's report-id prepend shifts these to wire 3/9/10)

3. BULK CHUNKS       (output report, :284-296):
     [0]=00 [1..4096]=memcpy(buf + i*0x1000, 0x1000)  ; 4097 bytes, no index field

4. CMD_SAVE          (feature report, :319-329):
     [0]=00 [1]=04 [2]=0x02 ...                       ; local_1018 = 0x0204
```

### 4.2 Path selection (bulk vs chunked)

**Not determinable from the decompile.** `FUN_004231c0` and `FUN_00422920` are
near-identical siblings with no internal flag check; the caller (one level up,
not in the corpus) decides. Likely a `config.xml` flag we have not decoded. See
§8.

______________________________________________________________________

## 5. RGB565 encoding

The pixel buffer is filled by `MUI::LCDViewList::GetImageRGB565Data`
(`FUN_004231c0:162`) — a `mui.dll` export **not in the decompile corpus**, so
byte order and walk direction are **not provable from the decompile**. The
28-byte chunk is a verbatim `memcpy` of whatever that function produced (no
swap in `FUN_004231c0`).

Our `encodeRgb565` currently emits **big-endian RGB565** (high byte first),
**top-down row-major** with a nearest-neighbour resample to 240×135. This
matches the standard SPI-display byte order (`0xRRRR_RGGG_GGGB_BBBB`) but is
**unverified** — see §8 for the test-image procedure.

______________________________________________________________________

## 6. Implementation map

| Concern | Symbol | File |
| --- | --- | --- |
| Chunk index split | `encodeTftChunkIndex` | `proprietary_keyboard.cpp` |
| Header packet | `buildTftChunkedHeader` | `proprietary_keyboard.cpp` |
| Chunk packet | `buildTftChunkedPayload` | `proprietary_keyboard.cpp` |
| Byte-32 checksum | `stampTftChecksum` | `proprietary_keyboard.cpp` |
| RGBA8 → RGB565 | `encodeRgb565` | `proprietary_keyboard.cpp` |
| Bulk begin (scaffold) | `buildScreenBulkBegin` | `proprietary_keyboard.cpp` |
| Upload orchestration | `uploadTftImage` (`ITftDisplayCapable`) | `proprietary_keyboard.cpp` |
| Constants | `kTft*`, `CmdScreen*` | `proprietary_protocol.hpp` |
| Tests | `[ak980][tft][chunked]`, `[proprietary][protocol][tft]` | `tests/unit/` |

______________________________________________________________________

## 7. No fake ACKs

Do not synthesize a success result the device did not confirm. The honest UX is
a progress bar that advances per chunk and a per-frame upload timeout (60 s in
the worker thread) that lets the user retry. Even though `FUN_0044f5f0` has a
readback (§3.5), we have not confirmed the panel echoes it, so we must not claim
delivery on its basis without a capture.

______________________________________________________________________

## 8. Open questions — a capture must resolve these

1. **`pixel_start` / `field[0x527c4]` value** — is the 256-byte `gif_headlength`
   region prepended to the chunk stream (⇒ 2 324 chunks/frame, chunk 0 leads
   with the frame-count byte) or not (⇒ 2 315, current assumption)? Resolve via
   the init code that assigns the field, or a capture of chunk 0 + the total
   chunk count in the header packet.
2. **Transport** — does the physical panel accept **output reports** for image
   upload, or only **feature reports** (as time-sync does)? If feature-only,
   revert `uploadTftImage` to `writeFeature()` and drop the §3.4 checksum.
3. **Checksum validation** — does the firmware check byte 32, or ignore it?
4. **Readback / ACK (§3.5)** — does the panel echo bytes 1-3 per chunk? Is the
   fire-and-forget approach safe at speed, or do chunks drop?
5. **RGB565 byte order + walk direction (§5)** — send a test frame with pixel
   (0,0) = pure red `0xF800`, (1,0) = green `0x07E0`, (2,0) = blue `0x001F`. If
   the top row shows R,G,B left-to-right, the layout is BE top-down row-major.
   Otherwise swap byte order (`htole16`) and/or flip the row walk.
6. **Bulk vs chunked trigger (§4.2)** — which `config.xml` flag (or firmware
   version) selects the bulk path?

When a capture answers any of these, update the relevant §, flip the code if
needed, and remove the corresponding row here.

______________________________________________________________________

## 9. Capturing the TFT upload

Per `docs/protocols/CAPTURING.md` + `.planning/research/captures/README.md`:
upload an image from the vendor app while capturing the 0xFF13 collection,
extract the control-channel bytes with `scripts/hex-to-cpparray.py`, and commit
only the sanitised hex fixture (never the raw `.pcap`). A Frida hook on
`WriteFile` / `HidD_SetFeature` (the same method that pinned the time-sync
framing) is the most direct route and sidesteps the keystroke-recovery privacy
risk of a bus-wide `.pcap`.
