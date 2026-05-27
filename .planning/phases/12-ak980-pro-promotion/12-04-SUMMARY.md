---
phase: 12-ak980-pro-promotion
plan: '04'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: keyboard-ak980pro-honesty-clock-ux
tags: [devices, keyboard, ak980pro, honesty, clock, battery, DEVICES-06, ARCH-05.1, retrospective]
dependency_graph:
  requires: []
  provides: [ak980pro-clock-setTime, ak980pro-battery, ak980pro-devices-row]
  affects: [docs/_data/devices.yaml, register.cpp, proprietary_keyboard.cpp]
tech_stack:
  added: []
  patterns: [4-packet-FEATURE-envelope, per-packet-handshake, devices.yaml-honesty]
key_files:
  created: []
  modified:
    - docs/_data/devices.yaml
    - src/devices/keyboard/src/register.cpp
    - src/devices/keyboard/src/proprietary_keyboard.cpp
    - src/devices/keyboard/src/proprietary_protocol.hpp
    - docs/protocols/keyboard/proprietary.md
    - tests/unit/test_ak980_clock_sync_e2e.cpp
    - tests/unit/test_proprietary_keyboard_protocol.cpp
status: PARTIAL
metrics:
  reconciled: '2026-05-27'
  tasks_completed: 1
  tasks_planned: 3
  tasks_partial: 1
  tasks_not_shipped: 1
---

# Phase 12 Plan 04: DEVICES-06 Honesty + Clock + Save/Flash UX — Reconciliation

**Mode:** retrospective-reconciliation. Documents the shipped state vs the plan.

**One-liner:** The clock disposition decision (KEEP per ARCH-05.1) is recorded and
the **clock + battery hardware-confirmed code shipped** ahead of GSD bookkeeping
(this is the strongest tier of Phase 12). The DEVICES-06 honesty bookkeeping
**partially** landed (`devices.yaml` updated but with deviations), the dedicated
`ak980pro.md` protocol doc was **NOT created**, and the save-vs-flash UX
(Pitfall 25, Task 3) was **NOT implemented**.

## Tasks Completed

| Task | Name                                                 | Commit            | Files                                       | Status      |
| ---- | ---------------------------------------------------- | ----------------- | ------------------------------------------- | ----------- |
| 1    | Decision record: ak980pro keeps clock per ARCH-05.1  | 9787962 / 807b250 | (decision; clock code shipped)              | DONE        |
| 2    | Honest devices.yaml row + ak980pro.md protocol doc   | b09302b, 29de348  | docs/\_data/devices.yaml (+ proprietary.md) | PARTIAL     |
| 3    | Host-save-vs-device-flash UX separation (Pitfall 25) | —                 | —                                           | NOT SHIPPED |

### Hardware-confirmed clock + battery code that underpins Task 1 (shipped)

| Concern                            | Commit  | Date       | What landed                                                                                                                 |
| ---------------------------------- | ------- | ---------- | --------------------------------------------------------------------------------------------------------------------------- |
| RTC `setTime` (ARCH-05.1 origin)   | 9787962 | 2026-05-17 | first firmware-RTC `setTime` implementation                                                                                 |
| 4-packet FEATURE envelope fix      | 17abda4 | 2026-05-17 | START 0x18 / PREAMBLE 0x28 / DATA / SAVE 0x02 envelope                                                                      |
| Real wDayOfWeek (not 0x04)         | d590392 | 2026-05-17 | byte-11 day-of-week fix + `ak980pro_vendor.md`                                                                              |
| report-id 0x00, 65-byte framing    | 1037aca | 2026-05-17 | `TimeReportSize = 65`, report-id 0x00                                                                                       |
| Vendor HID interface by usage pg   | 82c9689 | —          | select control collection by usage page                                                                                     |
| Disambiguate by usage 0xFF13       | 69c64a1 | —          | `.controlUsagePage = 0xFF13` (NOT 0xFF00)                                                                                   |
| Per-packet delay + readback        | 807b250 | 2026-05-21 | ~30ms settle + best-effort GET_REPORT per packet; 100ms after SAVE — **the hardware fix that made the clock actually move** |
| Battery via 0x20 sub 0x01          | eea50a5 | 2026-05-17 | battery query opcode + capability                                                                                           |
| Battery reads on hardware          | b55220a | 2026-05-21 | 65-byte GET_FEATURE buffer (64 fails), charge at `resp[4]`                                                                  |
| Genuine 0% vs "no battery"         | 3b2b937 | 2026-05-22 | P12 WR-04 — surface real 0%                                                                                                 |
| setTime year>2255 clamp            | f0441bd | 2026-05-22 | P12 WR-05 — `pkt[4] = (year>=2255)?0xFF:...` prevents wrap                                                                  |
| Log firmwareVersion HID failure    | c43980d | 2026-05-22 | stop swallowing I/O errors                                                                                                  |
| Correct stale setTime/RGB comments | 8dcd16d | 2026-05-22 | comment/honesty cleanup                                                                                                     |
| Document deferred 0x0A divergence  | 81f6c95 | 2026-05-22 | P12 CR-01 (see Deviations)                                                                                                  |

These verify in `proprietary_keyboard.cpp`: `setTime()` does
`sleep_for(30ms) → writeFeature → best-effort readFeature` per packet + 100ms
SAVE settle; `batteryPercent()` returns `std::min<std::uint8_t>(resp[4], 100)`;
`buildSetTimeData` clamps year ≥2255 to 0xFF. `register.cpp:70` sets
`.controlUsagePage = 0xFF13`.

## Deviations from Plan

### 1. Maturity flipped to `functional` — plan said keep `partial`

The plan (Task 2 + must_haves) states maturity **stays `partial`**. The shipped
`devices.yaml` ak980pro row is **`maturity: functional`** (flipped in `b09302b`,
2026-05-22, "refresh hardware-confirmed maturity tiers"). This is the honest
verdict given the live clock + battery + 0xFF13 witnesses, but it deviates from
the plan text. Note the dossier §5 nuance: the `functional` tier rests on the
**hardware-confirmed clock / battery / control collection** — RGB (20-mode) and
TFT lack a live witness, so they sit in `partial`/`pending` honestly.

### 2. No `docs/protocols/keyboard/ak980pro.md` created

Task 2 required a dedicated `ak980pro.md` documenting cmd 0x13 RGB + cmd 0x17
sleep-timer + cmd 0x28 RTC wire formats. **It was not created.** The row's
`protocol_doc:` still points at `docs/protocols/keyboard/proprietary.md`. Wire
detail instead lives across the existing `ak980pro_vendor.md`,
`ak980pro_tft_protocol.md`, `ak980pro_perkey_rgb_protocol.md`,
`ak980pro_macros_protocol.md`, and `proprietary.md` (the latter updated by
`807b250` / `81f6c95`). The information exists; the single consolidated doc the
plan named does not.

### 3. devices.yaml works/pending split is STALE vs plan

Task 2 said to move "20-mode RGB enum expansion" and "Sleep-timer wire format
(cmd 0x17 …)" from `pending` to `works`. They are **still under `pending`**
(lines 319-320) — consistent with reality, since (per 12-02) direction never
plumbed and (per 12-03) the dedicated 0x17 path never shipped. So the stale
pending lines are accidentally *more honest* than the plan's intended edit would
have been. `capabilities:` keeps `clock` and the `notes:` cite ARCH-05.1 + the
807b250 handshake fix, as the plan required.

### 4. Save-vs-flash UX (Task 3, Pitfall 25) NOT shipped

`grep saveProfile|pushToDevice src/app/src/settings_service.*` → **no match.**
There is no instant-host-save vs deliberate-throttled-NVM-flash separation, and
`SettingsRow.qml` has no "Save profile" / "Push to device" controls with a 60s
cooldown. Pitfall 25 (NVM wear from auto-flashing on every edit) is **unmitigated
in the UX** (threat T-12-12 open).

### 5. Clock question is an amendment-chain, not a live conflict

As the plan documents: ARCH-05's "Phase 12 removes clock" was superseded by
ARCH-05.1 (FINAL, 2026-05-17), the ratifying flip ARCH-05's own escape clause
anticipated. ak980pro KEEPS clock; ARCH-05 stands for the Stream Dock family only.
The as-built `hasClock=true` + `setTime` is correct and was not regressed.

### 6. CR-01: 0x0A legacy per-key RGB divergence DEFERRED (commit 81f6c95)

The zone-addressed `0x0A` `setRgbBuffer` path has an off-by-two AND diverges from
the Ghidra-confirmed `0x20/0x04` per-key RGB path. It is documented as a deferred
KNOWN ISSUE (`proprietary_keyboard.cpp:669`), has no live caller and no test, so
the off-by-two corrupts nothing today. Unification awaits a hardware round-trip.

## Maturity & witness (dossier §5, honest)

- **Hardware-confirmed:** time-sync envelope + 30ms/readback handshake
  (`0x18/0x28/0x02`); the `0xFF13` control collection; battery query
  (`0x20 0x01`, 65-byte buffer, `resp[4]`). Witnesses: Frida + live TFT clock +
  live battery read, 2026-05-21.
- **Decompile + corpus, NO live witness:** 20-mode firmware RGB (`0x13`),
  settings batch (`0x07 0x10`), per-key RGB (`0x20 0x04`).
- **Decompile-only, PROVISIONAL, NO capture:** TFT image upload
  (`0x7F`/`0x80`/`0x72`).
- **Negative on hardware:** the `0x0C 0x10` single-packet LCD time alias was tried
  and did NOT move the clock — the runtime path is the 4-packet envelope.
- **Net:** `devices.yaml` `functional` tier is justified by the clock + battery +
  control-collection hardware witnesses; RGB and TFT do not yet have a live
  witness and are honestly partial/pending. Promotion of RGB/TFT and the
  power-cycle / RGB-sweep witnesses remain Phase 12.x / Phase 13 deferred.

## Self-Check

- Confirmed: `hasClock`/`setTime`/battery code shipped and verified in
  `proprietary_keyboard.cpp` + `register.cpp` (0xFF13); `devices.yaml` row is
  `functional` with `clock` kept + ARCH-05.1 cited; `ak980pro.md` does NOT exist;
  pending list still carries the 20-mode + 0x17 lines; `saveProfile`/`pushToDevice`
  absent. Verdict: clock/battery DONE, honesty bookkeeping PARTIAL, save/flash UX
  NOT SHIPPED → PARTIAL overall.
