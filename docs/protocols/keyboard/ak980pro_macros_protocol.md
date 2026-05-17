# AJAZZ AK980 PRO — macro recording & assignment protocol

Complete byte-level specification of the two-phase macro protocol:
**macro data upload** (the event-list body) and **macro-to-key
assignment** (binding a macro slot to a physical key). Supersedes
§3.10–§3.11 of `ak980pro_vendor.md`.

Source: Ghidra decompiles of `FUN_0042dc10` (wired macro data),
`FUN_0042d690` (wireless macro data), `FUN_0044be90` (macro→key
assignment), `FUN_0044ba20` (macro record-buffer upload via 0x23).
Raw decompiles at
`C:/Users/unilo/reverse-eng-workdir/ak980pro/decomp_targets/FUN_0042dc10.c`,
`FUN_0042d690.c`, `FUN_0044be90.c`, `FUN_0044ba20.c`.

______________________________________________________________________

## 1. Storage model (SQLite — recap)

The macro database lives at
`%LOCALAPPDATA%/<deviceLongName>/db/<deviceLongName>_datav1.db` and
contains three tables (full schema in §7.1 of `ak980pro_vendor.md`):

```sql
CREATE TABLE t_key_otherdata(
  macro_value INTEGER PRIMARY KEY AUTOINCREMENT,
  macro_desc  TEXT,           -- human-readable name
  param       TEXT,           -- serialized event list (see §3 below)
  type        INTEGER
);
```

The `param` field is a delimited string of `<type>,<value>,<delay>`
triplets (parsed by `FUN_0040a9a0` and `FUN_0040aff0` referenced
from the macro upload paths).

______________________________________________________________________

## 2. Two opcodes for two transports

| Opcode    | Path / role                                     | Function          | Notes |
| --------- | ----------------------------------------------- | ----------------- | ----- |
| `0x09 0x1C` | **Wired macro data upload** (28-byte chunks)  | `FUN_0042dc10`    | Sent through HID OUTPUT reports (`FUN_0044f5f0` → `WriteFile`, 33-byte report) |
| `0x19 0x04` + `0x15 0x04` | **Wireless macro data upload** (4 KB bulk) | `FUN_0042d690` | Sent through HID FEATURE reports (`FUN_0044eed0` → `HidD_SetFeature`, 65-byte report) |
| `0x14 0x1C` (wired) / `0x14 0x1C+0x10` (wireless) | **Macro→key assignment** (manual chunking) | `FUN_0044be90` | 33-byte short-feature reports, 7 chunks wired / 21 chunks wireless |
| `0x23 0x04` (wired body 0xC0 / wireless 0x240) | **Macro record-buffer upload** (alternative assignment) | `FUN_0044ba20` | 65-byte feature reports, body via `FUN_0044eed0` |

The 0x14 and 0x23 paths upload **the same assignment data** but through
different transports. Likely 0x14 is the per-chunk progress-tracking
variant and 0x23 is the bulk variant. Both terminate the envelope with
CMD_SAVE (0x02) and CMD_FINISH (0xF0).

______________________________________________________________________

## 3. Macro data body format (3 584-byte / 0xE00 buffer)

Used by **both wired (`0x09`) and wireless (`0x19`)** paths. The buffer
layout:

```
[0..399]      -- per-macro INDEX TABLE (100 slots × 4 bytes each)
              for slot N:
                bytes 4*N + 0..1 = cursor offset (LE uint16) into body
                bytes 4*N + 2..3 = unused / zero

[400..]       -- per-macro BODY (concatenated):
              for each macro:
                byte +0..1 : event_count (LE uint16)
                byte +2..7 : reserved / zero
                byte +8..  : event_count × 4-byte events

[last 2 bytes of valid data] : 0xAA 0x55 trailer
```

**Max slots**: 100 (since 400 bytes / 4 bytes per index entry = 100).
**Max body bytes**: 3 584 - 400 - 2 (trailer) = **3 182 bytes** for all
events combined. Each event is 4 bytes, so **up to 795 events total**
across all 100 slots.

The wired path uses the full 3 584 bytes (`memset(&local_e58, 0,
0xe00)`). The wireless path uses the same 3 584-byte buffer but caps the
**payload sent** at `iVar3 = 0xa8c = 2 700 bytes` (`if (iVar3 == 0xa8c ||
iVar3 + -0xa8c < 0)` short-circuits the build if the macros are larger),
so wireless gets **≤ 575 events**.

### 3.1 Per-event 4-byte format

| Bytes [0,1,2,3]                | Meaning                                                |
| ------------------------------ | ------------------------------------------------------ |
| `[delay_lo, delay_hi, ?, 0x50]` | **Delay event** — sleep `delay` ms (min clamped to 10) |
| `[0, 0, keycode, 0xB0]`        | **Key Down** — HID usage code translated via `FUN_00451570` |
| `[0, 0, keycode, 0x30]`        | **Key Up** — translated via `FUN_00451570`            |
| `[0, 0, 0x01, 0x90]`           | **Mouse Left Down**                                    |
| `[0, 0, 0x01, 0x10]`           | **Mouse Left Up**                                      |
| `[0, 0, 0x04, 0x90]`           | **Mouse Right Down** (note: value `0x04`, not `0x02`)  |
| `[0, 0, 0x04, 0x10]`           | **Mouse Right Up**                                     |
| `[0, 0, 0x02, 0x90]`           | **Mouse Middle Down** (note: value `0x02`, not `0x03`) |
| `[0, 0, 0x02, 0x10]`           | **Mouse Middle Up**                                    |

The **byte 3 opcode** is:

| Opcode | Meaning             |
| ------ | ------------------- |
| `0x10` | Mouse button Up     |
| `0x30` | Key Up              |
| `0x50` | Delay (sleep)       |
| `0x90` | Mouse button Down   |
| `0xB0` | Key Down            |

Note the **invariant `byte 3 & 0x10 == 0x10` for Up events** and
`& 0x80 == 0x80` for Down events, with `0x50` (delay) as the only one
where neither Down/Up bit is asserted.

**The "default" (delay) case**: when the recorded event type field
(stored at offset +8 in the DB record, see `local_e7c + 8` references
in the decompile) is **not** in {2, 3, 4, 5}, the event is encoded as
a delay using the value at offset +0x14 (= `delay_ms`, min 10).

### 3.2 Mouse-event type/value disambiguation table

The `t_key_otherdata` DB record uses two integer fields to describe a
mouse event:

| `type` | `value` | Meaning |
| ------ | ------- | ------- |
| 4      | 1       | Mouse Left Down |
| 4      | 2       | Mouse Right Down |
| 4      | 3       | Mouse Middle Down |
| 5      | 1       | Mouse Left Up |
| 5      | 2       | Mouse Right Up |
| 5      | 3       | Mouse Middle Up |

The wire byte `[0,0,WIRE,X0]` uses **HID button bitmask values**
(`0x01` left, `0x02` middle, `0x04` right) **NOT** the DB `value` field
(1=left, 2=right, 3=middle). So our serializer must remap:

```cpp
constexpr std::uint8_t mouseValueToWire(int dbValue) noexcept {
    switch (dbValue) {
        case 1: return 0x01;  // Left → bit 0
        case 2: return 0x04;  // Right → bit 2
        case 3: return 0x02;  // Middle → bit 1
        default: return 0x00;
    }
}
```

This is **easy to get wrong** — write a test specifically for it.

### 3.3 Mouse movement / wheel — NOT seen in the wire format

The decompile does **not** handle mouse movement (relative XY) or wheel
events in the 4-byte event format. Only the 6 mouse-button events
above. If the user records a mouse-move macro, it presumably gets
discarded by the binary (or stored in the DB but skipped on upload).

This is a **device limitation** — the keyboard's macro engine doesn't
support pointer movement. Document this in our UI.

______________________________________________________________________

## 4. Macro data upload (`0x09 0x1C` wired, `FUN_0042dc10`)

```
Build 3 584-byte buffer per §3 above.
Chunk count = ⌈used_bytes / 0x1C⌉, where used_bytes = current cursor
              position (`local_e78`).
Trailer 0xAA 0x55 at last 2 bytes of valid data.

For chunk index i in [0..chunk_count):
   byte 0 : 0x00              (Report ID)
   byte 1 : 0x09              (CMD_MACRO_DATA_WIRED)
   byte 2 : 0x1C              (chunk length; last chunk = remainder)
   byte 3 : (byte)i           (chunk index — 8 bits, so max 256 chunks
                              = 7 168 bytes ≤ 3 584 buffer; comfortable)
   bytes 4..31 : 28 bytes from buf[i*0x1C + ...]
   bytes 32..63 : zero (33-byte short-feature report on wire)
   Sleep(2)
```

Sent via `FUN_0044f5f0(handle, packet, 0x41)` — 33 bytes on wire (note
the param_2=0x41 is for the checksum computation; only 33 bytes are
WriteFile'd). Each packet ends with byte 8 = checksum (sum of bytes
0..7 + 9..63 mod 256, with byte 8 itself treated as 0 during summing).

______________________________________________________________________

## 5. Wireless macro data upload (`0x19 0x04` + `0x15 0x04`, `FUN_0042d690`)

The wireless dongle gets the macro body via the **FEATURE-report bulk
path**, which is faster than the wired chunk path.

### 5.1 Envelope

```
1. CMD_MACRO_BEGIN: byte 1 = 0x19, byte 2 = 0x04 — sent as 65-byte
   FEATURE report via FUN_0044eed0.
2. CMD_MACRO_CHUNKINFO: byte 1 = 0x15, byte 2 = 0x04, byte 8 =
   chunk_count_lo — sent as 65-byte FEATURE report. Sleep(30).
3. BULK BODY: send `chunk_count * 64` bytes via FUN_0044eed0 with
   length = chunk_count * 64 (the function splits internally at
   64-byte boundaries). Sleep(30).
4. CMD_SAVE: byte 1 = 0x02, byte 2 = 0x04 — sent as 65-byte FEATURE
   report.
```

Total chunks `chunk_count = ⌈used_bytes / 64⌉ + 1 or 2` (the decompile
has an unusual `+1 / +2` based on whether `used_bytes & 0x3F == 0`).

### 5.2 Sentinel write before envelope

Just before chunk 1 in the body, the binary writes `*(buf + used_bytes
+ 2) = 0x55AA` (trailer at end of last full chunk's tail). This means
chunk count is always **at least one chunk past the actual data** to
contain the trailer.

______________________________________________________________________

## 6. Macro-to-key assignment (`0x14`, `FUN_0044be90`)

This is the **per-key binding** that says "key at light_index N invokes
macro M". The header packet establishes the macro group ID, then a
600-byte source blob is chunked.

### 6.1 Header packet (`0x05 0x10 0x00 0x80 …`)

```
byte 0 : 0x00              (Report ID)
byte 1 : 0x05              (CMD_KEY_REMAP)
byte 2 : 0x10              (sub-op = assignment-table)
byte 3 : 0x00
byte 4 : 0x80              (assignment-table magic)
bytes 5..8 : zero
byte 9 : 0x00
byte 10 : *(this + 0x814)  (macro group ID, 1 byte; rest zero)
bytes 11..17 : zero
bytes 18..19 : 0xAA 0x55   (trailer at end of relevant region)
bytes 20..32 : zero
```

Sent via `FUN_0044f5f0(handle, header, 0x41)` (33-byte wire).

### 6.2 Source blob (600 bytes scratch, populated by light_index)

The vendor's source buffer is **600 bytes** (`memset(&local_2b0, 0,
600)`). For each `KeyItem` returned by `FUN_00409220(lvar5, …)`:

**Wired** (`*(param_1 + 0x7ac) == 0`):
```c
if (*(int *)(iVar4 + 0x10) > 0) {   // key has assigned macro
    *(buf + iVar2) = 0xFF;          // 1 byte per LED, 0xFF = "yes"
}
```

**Wireless** (`*(param_1 + 0x7ac) != 0`):
```c
*(buf + iVar2 * 4 + 0) = (char)iVar2;            // light_index echo
*(buf + iVar2 * 4 + 1) = *(iVar4 + 0x10);        // keycode low byte
*(buf + iVar2 * 4 + 2) = *(iVar4 + 0x11);        // keycode mid byte
*(buf + iVar2 * 4 + 3) = *(iVar4 + 0x12);        // keycode high byte / flags
```

The blob is 1 byte/LED wired (192-byte payload after chunking) or
4 bytes/LED wireless (576-byte payload). Trailer `0xAA 0x55` is written
at:
- **Wired**: offset 190..191 (= last 2 bytes of 192-byte payload),
  i.e. `local_1f2 = 0x55AA` at ebp-0x1F2 ≡ buffer offset 0xBE = 190.
- **Wireless**: offset 574..575 (= last 2 bytes of 576-byte payload),
  i.e. `local_72 = 0x55AA` at ebp-0x72 ≡ buffer offset 0x23E = 574.

### 6.3 Chunking (`0x14 0x1C/0x18 wired` or `0x14 0x1C/0x10 wireless`)

**Wired**: 7 chunks, indices 0..6.

```
For i in 0..6:
  byte 0 : 0x00              (Report ID)
  byte 1 : 0x14              (CMD_MACRO_ASSIGN)
  byte 2 : 0x1C              (or 0x18 on the last chunk i==6 — = 24 = remainder)
  byte 3 : (byte)i           (chunk index)
  bytes 4..31 : 28 bytes from buf[i*0x1C + ...]
  Sleep(2)
```

Total wired payload = 6 × 28 + 24 = **192 bytes** (= 192 light_index
slots × 1 byte/LED).

**Wireless**: 21 chunks, indices 0..0x14.

```
For i in 0..0x14:
  byte 1 : 0x14
  byte 2 : 0x1C              (or 0x10 on the last chunk i==0x14 — = 16 = remainder)
  byte 3 : (byte)i
  bytes 4..31 : 28 bytes from buf[i*0x1C + ...]
  Sleep(2)
```

Total wireless payload = 20 × 28 + 16 = **576 bytes** (= 144
light_index slots × 4 bytes/LED).

### 6.4 Why wired = 192 vs wireless = 576?

- **Wired** stores only "is assigned" as a single bit per key, packed
  into one byte per LED. The macro group ID (header byte 10) selects
  which macro slot triggers — the firmware looks up the assignment
  table by macro group, finds keys where `byte == 0xFF`, and binds
  them all to the same macro.

- **Wireless** stores the full key remap entry per LED (3 bytes
  keycode + 1 byte light_index echo), allowing **per-key macro
  assignment** (each key can bind to a different macro). The 2.4 GHz
  dongle has more RAM and handles a richer mapping.

This asymmetry **is** unusual and deserves verification on hardware.
The wired path may be a degenerate case — likely the wired firmware
exposes only one "active macro" at a time.

______________________________________________________________________

## 7. Alternate assignment via `0x23 0x04` (`FUN_0044ba20`)

This is the **bulk variant** of the assignment upload. The body buffer
is identical to §6.2 (600 bytes wired, same builder), but the transport
is:

```
1. FUN_0044b910(param_1) — opens envelope with CMD_START (0x18) + 0x13
   marker + macro group + CMD_SAVE + CMD_FINISH. (See §7.1 below.)
2. CMD_START: byte 1 = 0x18, byte 2 = 0x04 — 65-byte FEATURE report.
3. CMD_MACRO_REC_BUFFER: byte 1 = 0x23, byte 2 = 0x04, byte 8 = 3
   (wired) or 9 (wireless) — 65-byte FEATURE report.
4. BULK BODY: 0xC0 (192) bytes wired or 0x240 (576) bytes wireless,
   sent via FUN_0044eed0 (internally chunks at 64-byte boundaries
   for the body > 64 bytes case).
5. CMD_SAVE: byte 1 = 0x02 — 65-byte FEATURE report.
6. CMD_FINISH: byte 1 = 0xF0 — 65-byte FEATURE report.
```

### 7.1 `FUN_0044b910` — the "macro envelope opener"

```
1. CMD_START   : byte 1 = 0x18
2. CMD_MARKER  : byte 1 = 0x13, byte 8 = 1
3. CMD_DATA    : byte 1 = 0x80, byte 4 = (param_1+0x814) macro group ID,
                 bytes 18..19 = 0xAA 0x55
4. CMD_SAVE    : byte 1 = 0x02
5. CMD_FINISH  : byte 1 = 0xF0
```

This is essentially the §6 header packet wrapped in the standard 5-step
envelope, suggesting **the 0x23 0x04 path is the canonical post-V1.0
upload** and 0x14 chunked is legacy.

______________________________________________________________________

## 8. Wire-format checksum

Re-derived: FUN_0044f5f0 computes byte 8 = `sum(bytes[0..param_2-1])
mod 256` over the caller's buffer, where byte 8 itself is summed as 0
(since the store happens after the loop). Specifically:

```c
uint8_t checksum = 0;
for (size_t i = 0; i < param_2; i++) {
    checksum += buffer[i];   // buffer[8] reads 0 from memset
}
buffer[8] = checksum;
```

For 33-byte FEATURE reports (param_2 = 0x21), the sum is over bytes
0..32 with byte 8 = 0.

For the calls that pass `param_2 = 0x41` (= 65), the sum is over bytes
0..64, but only bytes 0..32 are sent. So the firmware can only validate
the checksum against bytes 0..32 (which it doesn't have the full info
to verify). This is mostly cosmetic.

______________________________________________________________________

## 9. Code corrections required

### 9.1 Files to add (no macro upload code exists yet)

1. **`src/devices/keyboard/include/ajazz/keyboard/macro_uploader.hpp`**
   — `class MacroUploader` (`QObject`) with:
   - `upload(const QList<Macro>& macros, bool isWireless)`
   - `assignMacroToKey(int macroGroup, const QMap<int /*lightIdx*/, int /*macroSlot*/>& assignment, bool isWireless)`
   - signals `uploadProgress(int percent)`, `uploadFinished(bool ok)`

2. **`src/devices/keyboard/src/macro_uploader.cpp`** — implementation.

3. **`src/devices/keyboard/proprietary_protocol.hpp`** — add:

   ```cpp
   // Macro upload opcodes (FUN_0042dc10, FUN_0042d690).
   inline constexpr std::uint8_t kCmdMacroDataWired   = 0x09; // sub 0x1C
   inline constexpr std::uint8_t kCmdMacroBeginWireless = 0x19; // sub 0x04
   inline constexpr std::uint8_t kCmdMacroChunkInfoWireless = 0x15; // sub 0x04
   inline constexpr std::uint8_t kCmdMacroAssignChunked = 0x14; // sub 0x1C
   inline constexpr std::uint8_t kCmdMacroRecBuffer     = 0x23; // sub 0x04

   inline constexpr int kMacroBufferBytes      = 3584;  // 0xE00
   inline constexpr int kMacroIndexTableBytes  = 400;
   inline constexpr int kMacroMaxSlots         = 100;
   inline constexpr int kMacroWirelessBodyMax  = 2700;  // 0xA8C
   inline constexpr int kMacroEventBytes       = 4;
   inline constexpr int kMacroPerSlotHeaderBytes = 8;   // event_count (2) + reserved (6)

   inline constexpr std::uint8_t kMacroOpKeyDown  = 0xB0;
   inline constexpr std::uint8_t kMacroOpKeyUp    = 0x30;
   inline constexpr std::uint8_t kMacroOpDelay    = 0x50;
   inline constexpr std::uint8_t kMacroOpMouseDown = 0x90;
   inline constexpr std::uint8_t kMacroOpMouseUp  = 0x10;

   // Mouse value remap (DB value → wire byte bitmask).
   constexpr std::uint8_t macroMouseDbToWire(int dbValue) noexcept {
       return dbValue == 1 ? 0x01 :     // Left
              dbValue == 2 ? 0x04 :     // Right
              dbValue == 3 ? 0x02 : 0;  // Middle
   }
   ```

### 9.2 Unit test coverage

```cmake
ajazz_add_test(ak980pro_macros_protocol_test SOURCES
  test_macro_event_serialization.cpp
  test_macro_buffer_layout.cpp
  test_macro_assignment_chunking.cpp
)
```

CTest filter tag: `ak980pro-macros`.

Critical tests:

```cpp
TEST_CASE("Key down for A (HID 0x04) serializes correctly", "[ak980pro-macros]") {
    auto event = makeKeyDown(/*hidCode=*/0x04);
    const auto bytes = serializeEvent(event);
    CHECK(bytes == std::array<std::uint8_t,4>{0, 0, 0x04, 0xB0});
}

TEST_CASE("Mouse right down serializes to wire byte 0x04 (not DB 2)",
          "[ak980pro-macros]") {
    auto event = makeMouseDown(MouseButton::Right);
    const auto bytes = serializeEvent(event);
    CHECK(bytes[2] == 0x04);   // bitmask for right, not DB value 2
    CHECK(bytes[3] == 0x90);   // mouse-down op
}

TEST_CASE("Delay event clamps to min 10ms", "[ak980pro-macros]") {
    auto event = makeDelay(3);
    const auto bytes = serializeEvent(event);
    CHECK(bytes[0] == 10);
    CHECK(bytes[1] == 0);
    CHECK(bytes[3] == 0x50);
}

TEST_CASE("100-slot macro buffer index table sized correctly",
          "[ak980pro-macros]") {
    auto buf = buildMacroBuffer({});
    CHECK(buf.size() >= 400);
    CHECK(*reinterpret_cast<const std::uint16_t*>(buf.data() + 0) == 400);
}

TEST_CASE("Wired assignment payload is exactly 192 bytes for 192 LEDs",
          "[ak980pro-macros]") {
    auto payload = buildAssignmentPayloadWired(/*assignments=*/192_unassigned);
    CHECK(payload.size() == 192);
    CHECK(payload[190] == 0xAA);
    CHECK(payload[191] == 0x55);
}

TEST_CASE("Wireless assignment payload is exactly 576 bytes for 144 LEDs",
          "[ak980pro-macros]") {
    auto payload = buildAssignmentPayloadWireless(/*assignments=*/144_unassigned);
    CHECK(payload.size() == 576);
    CHECK(payload[574] == 0xAA);
    CHECK(payload[575] == 0x55);
}

TEST_CASE("Wireless assignment chunk 0x14 (last) has length byte 0x10",
          "[ak980pro-macros]") {
    auto chunks = chunkAssignmentPayloadWireless(payload);
    CHECK(chunks.size() == 21);
    CHECK(chunks[20][2] == 0x10);  // last chunk length = 16 bytes
    CHECK(chunks[20][1] == 0x14);  // opcode 0x14
    CHECK(chunks[20][3] == 0x14);  // chunk index 20 = 0x14
}
```
