---
phase: 12-ak980-pro-promotion
plan: '03'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: keyboard-ak980pro-sleep-timer
tags: [devices, keyboard, ak980pro, sleep-timer, KEYBOARD-03, retrospective]
dependency_graph:
  requires: []
  provides: [ak980pro-settings-batch-sleep-timer]
  affects: [ISettingsCapable, settings_service, SettingsRow.qml]
tech_stack:
  added: []
  patterns: [settings-batch-envelope, Q_INVOKABLE-facade]
key_files:
  created: []
  modified:
    - src/core/include/ajazz/core/capabilities.hpp
    - src/devices/keyboard/src/proprietary_keyboard.cpp
    - src/devices/keyboard/src/proprietary_protocol.hpp
    - tests/unit/test_ak980_settings_batch.cpp
status: NOT-SHIPPED
metrics:
  reconciled: '2026-05-27'
  tasks_completed: 0
  tasks_planned: 2
  note: sleep-timer ships via the 0x07 0x10 settings batch, NOT the dedicated 0x17 path
---

# Phase 12 Plan 03: Dedicated cmd 0x17 Sleep-Timer (KEYBOARD-03) — Reconciliation

**Mode:** retrospective-reconciliation. Documents the shipped state vs the plan.

**One-liner:** The KEYBOARD-03 **dedicated cmd 0x17 sleep-timer path**
(`CmdSleepTimer 0x17`, `buildSetSleepTimer`, a `setSleepTimer` envelope, a discrete
labeled QML picker, and a `test_ak980_sleep_timer.cpp`) was **NOT IMPLEMENTED**.
The sleep timer continues to ride the pre-existing settings-batch (`0x07 0x10`,
byte 10 `sleepTimerMinutes`) that shipped under `ISettingsCapable` — exactly the
path the plan set out to replace.

## Tasks Completed

| Task | Name                                                 | Commit | Files | Status      |
| ---- | ---------------------------------------------------- | ------ | ----- | ----------- |
| 1    | cmd 0x17 builder + `setSleepTimer` envelope + test   | —      | —     | NOT SHIPPED |
| 2    | Discrete picker in SettingsService + SettingsRow.qml | —      | —     | NOT SHIPPED |

## Verification (as of 2026-05-27)

- `grep -rn "CmdSleepTimer\|buildSetSleepTimer\|setSleepTimer\|0x17" src/devices/keyboard/src/`
  → **no match.** There is no `0x17` opcode constant, no builder, no envelope method.
- `tests/unit/test_ak980_sleep_timer.cpp` → **does not exist.**
- `proprietary_protocol.hpp` lines 101-112 + 543-546 (committed in `536f388`)
  carry the **settings batch** (`CmdSettingsBatch 0x07`, `SettingsBatchSub 0x10`),
  with `kSettingsByteSleepTime = 10` and `buildSettingsBatch(fnLayerSwitch, sleepTimerMinutes, keyResponseTimeLevel)`. The sleep timer is byte 10 of the
  batch, carried as a raw `sleepTimerMinutes` value.
- The plan's referenced `setKeyboardSettings` envelope MIRROR target exists
  (4-packet `START 0x18 → DATA 0x07 0x10 → SAVE 0x02 → FINISH 0xF0`), but the
  parallel `0x17` envelope it was meant to seed was never written.

## Deviations from Plan

**Entire plan unimplemented.** The dedicated 0x17 path does not exist; the
settings-batch path (which the plan explicitly intended to supersede with a
discrete picker) remains the only sleep-timer surface.

- **No discrete picker.** `SettingsService` has no `sleepTimerOptions()` /
  `setSleepTimer(codename, enumValue)`, and `SettingsRow.qml` exposes no discrete
  labeled duration picker mapped to a captured enum.
- **The source-vs-capture enum conflict the plan resolved on paper
  (objective gate 2) is therefore moot in code:** nothing emits the captured
  `{0=Never,1=1min,2=5min,3=30min}` enum via 0x17. Notably, a comment at
  `proprietary_protocol.hpp:238` (in the settings-batch doc) still lists the
  legacy `sleepTimerMinutes` vendor enum as `0/1/3/5/10/30` — which is itself the
  source-side list, not the captured 4-state 0x17 enum. The honesty conflict is
  unresolved at the wire layer because the dedicated path was never built.
- **Persistence:** the batch path does include `SAVE 0x02`, so a sleep value set
  via the batch is persisted; but it is a raw minute byte, not the discrete
  captured enum KEYBOARD-03 asked for.

## Maturity & witness

- **Software:** sleep timer exists ONLY as a field of the `0x07 0x10` settings
  batch (decompile-only impl per dossier §3.4 / §5). The dedicated `0x17`
  (`FUN_00414020`) opcode is catalogued in the dossier master table as
  `🟡 doc` (documented, not implemented) — matching the code.
- **Hardware witness:** the KEYBOARD-03 power-cycle-persistence witness (set
  timer, power-cycle, confirm survival on a physical `0c45:8009`) is moot for the
  0x17 path that does not exist; the batch path's persistence is likewise
  un-witnessed on hardware.
- **Carry-forward:** KEYBOARD-03 is open. Both the dedicated 0x17 wire path +
  discrete picker AND the captured-enum honesty resolution (no fabricated 10-min
  wire value) remain to be built. The settings-batch sleep byte is a working but
  non-discrete stand-in.

## Self-Check

- Confirmed no `0x17`/`CmdSleepTimer`/`buildSetSleepTimer`/`setSleepTimer` in the
  keyboard backend, no `test_ak980_sleep_timer.cpp`, and no
  `sleepTimerOptions`/`setSleepTimer` in `settings_service`. The only sleep-timer
  surface is the settings-batch byte 10. Verdict: NOT SHIPPED.
