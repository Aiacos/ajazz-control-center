---
phase: 12-ak980-pro-promotion
plan: '02'
mode: retrospective-reconciliation
reconciled: '2026-05-27'
subsystem: keyboard-ak980pro-lighting
tags: [devices, keyboard, ak980pro, rgb, lighting, KEYBOARD-01, KEYBOARD-02, retrospective]
dependency_graph:
  requires: []
  provides: [ak980pro-20-mode-rgb-picker]
  affects: [IFirmwareLightingCapable, LightingService, RgbPicker.qml]
tech_stack:
  added: []
  patterns: [table-driven-mode-catalogue, capability-mixin, Q_INVOKABLE-facade]
key_files:
  created:
    - src/devices/keyboard/include/ajazz/keyboard/ak980_lighting.hpp
  modified:
    - src/core/include/ajazz/core/capabilities.hpp
    - src/devices/keyboard/src/proprietary_keyboard.cpp
    - src/devices/keyboard/src/proprietary_protocol.hpp
    - src/app/src/lighting_service.hpp
    - src/app/src/lighting_service.cpp
    - src/app/qml/RgbPicker.qml
    - tests/unit/test_proprietary_keyboard_protocol.cpp
status: PARTIAL
metrics:
  reconciled: '2026-05-27'
  tasks_completed: 1
  tasks_planned: 2
  tasks_partial: 1
---

# Phase 12 Plan 02: 20-Mode Firmware RGB + Direction (KEYBOARD-01/02) — Reconciliation

**Mode:** retrospective-reconciliation. Documents the shipped state vs the plan.

**One-liner:** The KEYBOARD-01 table-driven 20-mode firmware RGB picker (opcode
0x13, 5-packet envelope, brightness/speed) **SHIPPED** ahead of GSD bookkeeping.
The KEYBOARD-02 *direction* plumbing — the plan's stated "one honest gap" — did
**NOT** ship: the wire builder accepts direction, but it is hardcoded to 0 at the
backend and is absent from the interface, service, and QML.

## Tasks Completed

| Task | Name                                                         | Commit           | Files                                                                                                          | Status  |
| ---- | ------------------------------------------------------------ | ---------------- | -------------------------------------------------------------------------------------------------------------- | ------- |
| 1    | direction on IFirmwareLightingCapable + backend directionMax | 6f11966 (modes)  | ak980_lighting.hpp, proprietary_protocol.hpp, proprietary_keyboard.cpp, test_proprietary_keyboard_protocol.cpp | PARTIAL |
| 2    | direction through LightingService + RgbPicker sliders        | a350af8 (picker) | capabilities.hpp, lighting_service.{hpp,cpp}, RgbPicker.qml, application.{hpp,cpp}                             | PARTIAL |

**What DID ship (KEYBOARD-01, the bulk):**

- `6f11966` — the 20-mode enum (`AK980LightingMode`, Static=0x00 … LedOff=0x13)
  in `ak980_lighting.hpp`, plus the `buildSetRgbModeData(modeId, r,g,b, rainbow, brightness, speed, direction)` DATA-packet builder in `proprietary_protocol.hpp`.
  The builder DOES have a `direction` parameter, writes it to byte 11
  (`pkt[11] = std::min<std::uint8_t>(direction, 3)`), and clamps to 3.
- `9a67916` — emits CMD_FINISH 0xF0 as the 5th packet of the lighting envelope.
- `a350af8` — `IFirmwareLightingCapable` (`availableFirmwareModes()`,
  `setFirmwareLightingMode(modeId, brightness, speed)`, `brightnessMax()`,
  `speedMax()`), the `LightingService` Q_INVOKABLE facade (`setMode(codename, modeId, brightness, speed)`, `brightnessMaxFor`, `speedMaxFor`), and
  `RgbPicker.qml`. The catalogue is table-driven from `ak980_lighting.hpp`.

## Deviations from Plan

### 1. Direction NOT plumbed (KEYBOARD-02 gap — the plan's whole reason to exist)

The plan's objective: "The genuinely missing piece is **direction (0..3)**…
plumb `direction` through the interface, the backend, the QML facade, and the
slider UI." **This did not happen.** As shipped:

- `capabilities.hpp:325` — `setFirmwareLightingMode(std::uint8_t modeId, std::uint8_t brightness, std::uint8_t speed)` — **no `direction` param, no
  `directionMax()` accessor.**
- `proprietary_keyboard.cpp:968` — the backend override still calls
  `buildSetRgbModeData(..., /*direction*/ 0)`. Direction is **hardcoded to 0**;
  the user cannot set it.
- `ak980_lighting.hpp` — has `kAK980LightingBrightnessMax`/`SpeedMax` but **no
  `kAK980LightingDirectionMax`** constant.
- `lighting_service.hpp:84` — `setMode(codename, modeId, brightness, speed)` —
  **no direction arg, no `directionMaxFor()`.**
- `RgbPicker.qml` — brightness/speed sliders only; **no direction control.**

So KEYBOARD-01 (mode selection, brightness, speed) is delivered; KEYBOARD-02's
direction half is NOT. The wire-layer is ready (byte 11 + clamp exist), only the
end-to-end plumbing above it is missing.

### 2. PLANNING-RISK note carried as designed

The plan acknowledged KEYBOARD-01's spec wording (`IRgbCapable::setMode`) was
satisfied via the already-shipped `IFirmwareLightingCapable` + `LightingService. setMode` surface, NOT a literal `IRgbCapable` rename. That is accurate to the
shipped code — the dedicated mix-in is the as-built capability.

### 3. Test coverage

The 20-mode + brightness/speed + clamp assertions live in
`tests/unit/test_proprietary_keyboard_protocol.cpp` (added in `6f11966`), NOT a
dedicated `test_ak980_firmware_lighting.cpp` (the plan's interface block named that
file, but the e2e lighting assertions were folded into the protocol test). No
`p3[11] == <direction>` direction assertion exists because direction is never set
from above the wire builder.

## Maturity & witness

- **Software:** 20-mode picker + brightness/speed = SHIPPED and table-driven.
  Direction = wire-builder-only, NOT user-reachable.
- **Hardware witness:** per dossier §5, the 20-mode RGB path is
  **decompile + corpus (TaxMachine AK820 Pro clean-room + Ghidra `FUN_0042b0a0`),
  IMPL, but NO live RGB witness.** The byte-11 direction enum ordering
  (Left=0/Down=1/Up=2/Right=3) remains ARCH-05 DEFAULT VERDICT / capture-pending —
  and is doubly unconfirmed since nothing drives it.
- **Carry-forward:** KEYBOARD-02 direction plumbing (interface →
  `directionMax()` → service → labeled QML control) is outstanding. A live RGB
  witness on a physical `0c45:8009` is the promotion gate for the whole RGB tier.

## Self-Check

- Confirmed `setFirmwareLightingMode` has no `direction`/`directionMax` in
  `capabilities.hpp`; backend passes `/*direction*/ 0` at line 968;
  `kAK980LightingDirectionMax` absent; `directionMaxFor` absent from
  `lighting_service.hpp`; no direction control in `RgbPicker.qml`. Verdict:
  KEYBOARD-01 shipped, KEYBOARD-02 direction NOT shipped → PARTIAL.
