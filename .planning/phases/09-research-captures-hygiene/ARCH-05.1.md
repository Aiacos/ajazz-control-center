---
adr: ARCH-05.1
phase: 9
amends: ARCH-05
title: AK980 PRO firmware RTC discovered — partial flip of ARCH-05 default verdict
status: FINAL (source-level corroboration from two independent corpora)
default_verdict_preserved_for: akp03_variant_3004 + akp05/Mirabox N4 family (Stream Dock — no RTC)
flipped_for: ak980pro (Sonix SN32F299 family, VID:PID 0x0c45:0x8009)
ratified: 2026-05-17
---

# ARCH-05.1: AK980 PRO firmware RTC — amendment to ARCH-05 default verdict

**Status:** FINAL — amends ARCH-05 (2026-05-15 default verdict) per the D-05
honesty contract clause: *"If the finalization run does flip the verdict on one
device (still vanishingly unlikely), a follow-up ADR (`ARCH-05.1` or similar)
ratifies the flip — never an in-place edit to this document, which preserves the
audit trail."*

This amendment is the realisation of that clause: targeted, source-level
research (not Phase 9.x physical captures yet, but stronger than corpus
absence-of-evidence) located the firmware RTC wire format on the AK980 PRO.
ARCH-05 stands as written for the Stream Dock families; it is **partially
flipped for `ak980pro`** by this amendment.

## What changed

The original ARCH-05 default verdict claimed: *"NO RTC opcode exists in any of
the four reference corpora."* Two new corpora targeting the same Sonix SN32F299
MCU family (which AK980 PRO uses at VID:PID `0x0c45:0x8009`) document an
explicit firmware RTC reachable via opcode `0x28`:

1. **`gohv/EPOMAKER-Ajazz-AK820-Pro`** — Rust port targeting the AK820 Pro
   (which AK980 PRO is a near-sibling of; same chipset family). File
   `src/protocol.rs` lines 19–256 define `CMD_TIME = 0x28`, `CMD_SAVE = 0x02`,
   and the three packet builders `time_preamble_packet()` /
   `time_data_packet(year, month, day, hour, minute, second)` /
   `save_packet()`.

1. **`KyleBoyer/TFTTimeSync-node`** — TypeScript daemon for the AK820 Pro TFT
   time-sync. File `src/packets.ts` `getConfigureTimePacket()` /
   `getUpdateTimePacket()` / `getSavePacket()` produce byte-for-byte identical
   packets to the gohv Rust source. Cross-corroboration is at the
   per-offset-per-value level.

Both corpora are referenced and ratified as primary sources for this amendment.

## Wire format (locked, source-level corroborated)

**Four sequential 64-byte HID Feature reports**, each sent via
`hid_send_feature_report` (USB control-endpoint `SET_REPORT` request), NOT
via `hid_write` (interrupt OUT). This is a critical detail: an earlier draft
of this implementation used Output Reports and was a silent no-op against
firmware. Agent B's disassembly of the vendor `DeviceDriver.exe` (2026-05-17)
confirmed it imports `HidD_SetFeature` for this code path.

After the four packets, sleep 100ms before any subsequent HID I/O to let the
firmware commit the RTC to NV-RAM (gohv `usb.rs` pattern; races without this
sleep can drop the SAVE).

**Packet 1 — Start (control packet for CMD_START, opcode 0x18):**

```
byte  0:  0x04   // HID Report ID (default)
byte  1:  0x18   // CMD_START — resets firmware time-sync state machine
byte  8:  0x01   // configure-mode marker
bytes 2–7, 9–63: 0x00
```

**Packet 2 — Preamble (control packet for CMD_TIME, opcode 0x28):**

```
byte  0:  0x04   // HID Report ID (default)
byte  1:  0x28   // CMD_TIME
byte  8:  0x01   // configure-mode marker
bytes 2–7, 9–63: 0x00
```

**Packet 3 — Time data (HID Report ID 0x00, the magic discriminator):**

```
byte  0:  0x00   // HID Report ID — NOT the default 0x04; firmware uses
                 //                  the magic 0x5A at byte 2 as the real
                 //                  discriminator, but the report id must
                 //                  also flip to 0x00 for this packet
byte  1:  0x01   // fixed
byte  2:  0x5A   // magic
byte  3:  year - 2000  (single byte; pre-2000 saturates to 0)
byte  4:  month (1..12)
byte  5:  day   (1..31)
byte  6:  hour  (0..23)
byte  7:  minute (0..59)
byte  8:  second (0..59)
byte  9:  0x00
byte 10:  0x04
bytes 11–61: 0x00
byte 62:  0xAA   // delimiter high
byte 63:  0x55   // delimiter low
```

**Packet 4 — Save (control packet for CMD_SAVE):**

```
byte  0:  0x04   // HID Report ID (default)
byte  1:  0x02   // CMD_SAVE — distinct from CmdCommitEeprom (0x0E)
bytes 2–63: 0x00
```

The vendor app sends local time (not UTC); both reference corpora pass through
the host's local Date components without UTC normalisation. The host
implementation therefore converts `std::chrono::system_clock::time_point` via
`localtime_s` / `localtime_r` before encoding the time-data packet.

## Three-witness rule (Pitfall 19) — re-evaluation for `ak980pro`

ARCH-05 argued the three-witness rule was structurally unsatisfiable on `ak980pro`
because no round-trip witness exists (the 1.14" TFT was claimed to be
host-pixel-driven only). **This amendment refutes that argument:** if the
device acknowledges the 3-packet sequence and the TFT shows the time we sent
(after the firmware composites a clock widget itself), the round-trip witness
IS observable.

Witnesses for `ak980pro` clock capability post-amendment:

1. **Capture witness (source-level):** two independent corpora (`gohv`,
   `KyleBoyer`) document byte-for-byte identical 64-byte packet layouts for
   the same opcode `0x28` on the same chipset family. The probability that
   two unrelated reverse-engineers fabricated identical false byte sequences
   is vanishingly small.
1. **Round-trip witness (TO BE CONFIRMED, Phase 9.x physical test):** after
   sending the 3-packet sequence with a deliberately wrong year (e.g. year
   2099), the keyboard's TFT clock widget should display 2099. If it does:
   round-trip witness confirmed.
1. **Negative witness (TO BE CONFIRMED, Phase 9.x):** sending a deliberately
   bad value (year `0xFF` representing 2255, or zeroed packet body) should
   produce a visible-but-wrong time, not a no-op. This proves the firmware
   parses the field rather than ignoring unrecognised opcodes.

Witnesses (2) and (3) are **achievable** on `ak980pro` (unlike on
`akp03_variant_3004` where no firmware-rendered clock widget exists) because
the AK980 PRO has a firmware-composited TFT clock widget when the user enables
that on-screen mode. The negative witness can be tested without risk because
sending a wrong time only changes a display; nothing destructive happens.

The full promotion of `ak980pro.capabilities[clock]` from `scaffolded` to
`functional` is gated on a Phase 9.x physical test that confirms witnesses (2)

- (3). Until then, this amendment promotes the maturity tier on the *capability
  backbone* (the wire format + setTime implementation) from `NotImplemented` to
  `Ok / IoError` and updates `devices.yaml` accordingly: `ak980pro.maturity`
  goes from `scaffolded` to `partial` (other capabilities — `rgb`, `macros`,
  `layers` — remain in `feature_summary.pending:` per Pitfall 29 honesty contract).

## Honesty-contract preservation

The v1.1 D-02 contract (*no lying success UX on time sync*) is reinforced, not
weakened: prior to this amendment, `setTime()` returned `NotImplemented` and
the per-row glyph stayed at exclamation. Post-amendment, `setTime()` actually
writes the 3-packet 0x28 envelope and returns `Ok` only if all three HID writes
succeed (it returns `IoError` if the transport throws, never `Ok` on a no-op).
The UX glyph flips to OK only when the firmware actually accepts the bytes —
which the Phase 9.x physical test will visually confirm.

The D-05 contract is also honoured: this is a NEW ADR file (ARCH-05.1) that
ratifies the flip, not an in-place edit of ARCH-05. ARCH-05's audit trail is
preserved for the Stream Dock family verdict (which stands).

## What stands from ARCH-05 (no change)

- **`akp03_variant_3004` (`0x0300:0x3004`)**: `hasClock=false` stays. Clock
  widgets on the AKP03/AKP05 family are host-rendered images via the `display`
  capability (`BAT` opcode). Companion's `streamdock.ts` source-level audit
  (Agent A research, 2026-05-17) confirmed the AKP05/Mirabox N4 surface has
  ZERO time/clock opcodes — `CRT`, `DIS`, `CLE`, `STP`, `LIG`, `LBLIG`, `BAT`,
  `SETLB`, `CONNECT` exhaustively cover the surface.
- **`akp05` / `mirabox_n4`** (`0x0300:0x5001` / `0x6603:0x1007`): same as
  `akp03_variant_3004`. The 800×100 main LCD strip clock will be implemented
  as a host-rendered `TftClockWidget` re-uploaded via the new image_pipeline
  (`encodeForDevice` → `MAI` opcode) at ~1 update per minute.
- **`ajazz_24g_8k` (`0x3151:0x5007`)**: mouse, not in scope for clock.
- **`microdia_dongle_7016` (`0x0c45:0x7016`)**: dongle, not in scope for clock.

## Forbidden anti-feature (preserved from ARCH-05)

The ban on *synthesizing a fake `setSystemTimeOn` wire format from bytes that
"look like time"* still stands for the Stream Dock family. This amendment does
not weaken that — it ratifies a wire format that is **explicit, named, and
cross-corroborated**, not a heuristic-inferred byte pattern.

## Implementation status (as of this ADR — amended 2026-05-17 same-day)

- **Wire format constants:** `src/devices/keyboard/src/proprietary_protocol.hpp`
  `CmdStartTime = 0x18`, `CmdSetTime = 0x28`, `CmdSaveRtc = 0x02`,
  `TimeDataReportId = 0x00`.
- **Packet builders:** `buildSetTimeStart()` / `buildSetTimePreamble()` /
  `buildSetTimeData(...)` / `buildSetTimeSave()` in
  `src/devices/keyboard/src/proprietary_keyboard.cpp`.
- **setTime backend:** `ProprietaryKeyboard::setTime()` calls all 4 builders
  in sequence via `ITransport::writeFeature()` (NOT `write()`), followed by
  a 100ms `std::this_thread::sleep_for` settle window. Returns `Ok` on
  success, `IoError` on transport throw.
- **Unit tests:** 8 new `[clock]`-tagged cases in
  `tests/unit/test_proprietary_keyboard_protocol.cpp` pinning byte layout
  of START + PREAMBLE + DATA + SAVE, pre-2000 saturation, year boundary,
  and 4-opcode distinctness invariants (CMD_START / CMD_TIME / CMD_SAVE /
  CmdCommitEeprom all mutually distinct).
- **`devices.yaml` row:** `ak980pro.maturity` promoted `scaffolded` →
  `partial` with `notes:` line citing this ADR. Clock capability stays in
  `capabilities:` (was already there from v1.1 scaffolding).

### Initial-draft post-mortem (same-day, 2026-05-17)

The first commit (`9787962`) used `ITransport::write()` (HID Output Reports
via interrupt OUT endpoint) and only 3 packets, missing the START. Agent B's
disassembly of `DeviceDriver.exe` (2026-05-17 ~15:30) revealed that the
vendor app imports `HidD_SetFeature` — i.e. uses the HID Feature Report path
(control endpoint `SET_REPORT`). Re-reading `gohv/usb.rs` confirmed
`send_feature()` is called for all 4 packets and `start_packet()` is the
first of them. The amendment commit corrects both errors. Lesson: when a
reverse-engineered Rust source uses an unfamiliar transport call
(`send_feature` vs `write`), trace it to the underlying `hidapi` function
name before assuming it maps to your project's `transport.write()`.

## Captures-confirmation trigger (Phase 9.x physical test)

The deferred Phase 9.x physical test for `ak980pro` clock capability is now:
*"Plug AK980 PRO, run the app, click Sync Time, confirm visual round-trip on
the 1.14" TFT clock widget (witness 2). Then click Sync Time after manually
setting host time to year 2099 (witness 3 negative). Both must succeed for
`ak980pro.maturity` to promote `partial` → `functional`."*

This replaces the prior Phase 9.x trigger for `ak980pro` (which asked the user
to confirm the absence of a setTime byte sequence in vendor-app captures).

## References

- Primary source 1: `github.com/gohv/EPOMAKER-Ajazz-AK820-Pro/src/protocol.rs`
  (lines 19–256, accessed 2026-05-17).
- Primary source 2: `github.com/KyleBoyer/TFTTimeSync-node/src/packets.ts`
  (`getConfigureTimePacket` / `getUpdateTimePacket` / `getSavePacket`).
- Bonus source: `github.com/mstoiakevych/ajazz-clock-sync` (AJ199/AJ159 mouse
  dock RTC — uses same opcode `0x28` but DIFFERENT layout, big-endian year at
  bytes 8-9 + fixed `0xd7` at byte 7; NOT in scope for v1.2 keyboard backend).
- Bonus source: `github.com/aar-rafi/aks075-linux` README explicitly markets
  "Time sync — sync the keyboard's clock to your system time" and credits the
  gohv repo.
- Companion `streamdock.ts` opcode audit (Agent A research, 2026-05-17):
  confirms AKP05/Mirabox N4 surface has NO time opcode — Stream Dock family
  verdict from ARCH-05 stands.
- ARCH-05 (this ADR's parent; default verdict 2026-05-15) for the Stream Dock
  family verdict, which is preserved unchanged.
- v1.1 D-02 honesty contract (Phase 5 retrospective; `TimeSyncService`
  canonical mix-in consumer pattern).
- Phase 9 D-05 honesty contract (default verdicts are PRO-FORMA; flips via
  amendment-ADR, never in-place edit).
