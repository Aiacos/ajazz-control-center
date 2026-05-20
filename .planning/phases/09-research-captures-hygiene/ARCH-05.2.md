---
adr: ARCH-05.2
phase: 9
amends: ARCH-05
title: AJAZZ 2.4G 8K mouse host-rendered TFT clock — ratified as a legitimate IClockCapable use
status: FINAL (ratifies as-built design; physical visual confirmation is a Phase 11 hardware-verify)
default_verdict_preserved_for: akp03_variant_3004 + akp05/Mirabox N4 family (Stream Dock — no clock surface at all)
flipped_for: ajazz_24g_8k (SONiX 0x3151:0x5007) — host-rendered TFT clock exposed via IClockCapable
sibling_amendment: ARCH-05.1 (ak980pro firmware RTC, opcode 0x28)
ratified: 2026-05-20
---

# ARCH-05.2: AJAZZ 2.4G 8K mouse TFT clock — amendment to ARCH-05

**Status:** FINAL — amends ARCH-05 (2026-05-15 default verdict) per the D-05
honesty-contract clause that flips are ratified by a follow-up ADR, never an
in-place edit. Sibling to ARCH-05.1 (which flipped `ak980pro`); this one
addresses the third connected device, `ajazz_24g_8k`.

## What changed

ARCH-05 §"`ajazz_24g_8k`" said: *"N/A — this mouse does not advertise `clock`
in v1.1; `hasClock` stays implicitly `false`."* ARCH-05.1 reaffirmed *"mouse,
not in scope for clock."* Both are now **superseded for this device** by the
as-built implementation that landed 2026-05-18 (commits `656fb1c`, `0e9bb04`,
P3.12.1/.2):

- The `ajazz_24g_8k` has a TFT LCD "basetta" (dock display).
- The backend renders a clock face **host-side** into a `QImage` and pushes it
  to the TFT via opcode `0x25` (`SETTFTLCDDATA`, chunked RGB565), wired through
  `IClockCapable` so the existing `TimeSyncService` drives periodic refreshes.
- `register.cpp` sets `hasClock = tft` (true when `hasTftBasetta(codename)`),
  `devices.yaml` lists `clock`, and `tests/unit/test_aj_series_tft_clock.cpp`
  pins the render/upload path.

## Why this is the host-rendered `TftClockWidget`, ratified

This is **exactly the "acceptable alternative" ARCH-05 explicitly endorsed**
(ARCH-05 §"Acceptable alternative: host-rendered TftClockWidget"): the host
renders the clock face and uploads pixels; the device displays an image, not a
firmware RTC. The single open design question was the **interface seam**:
expose it via `IDisplayCapable` (pure image) or `IClockCapable` (time-driven)?

**Decision: `IClockCapable` is the correct seam for this device.** Rationale:

- The capability is genuinely time-driven, not a one-shot image: `TimeSyncService`
  must re-push on a cadence to keep the displayed time current. Routing it
  through `IClockCapable` reuses the v1.1 Time Sync plumbing (Sync button,
  auto-sync-on-connect, per-row glyph) without duplicating a time loop in the
  display path.
- It does NOT violate the v1.1 D-02 honesty contract. D-02 forbids a **lying
  success UX** — showing "synced OK" when nothing happened. Here the success is
  honest: `setTime()` returns `Ok` only when the TFT image push succeeds, and
  the TFT then visibly shows the time we sent. The user sees the time they set;
  the glyph reflects a real, observable state change.

### Honest distinction from ARCH-05.1 (ak980pro)

|                         | `ak980pro` (ARCH-05.1)               | `ajazz_24g_8k` (this ADR)                      |
| ----------------------- | ------------------------------------ | ---------------------------------------------- |
| Mechanism               | firmware RTC (device keeps time)     | host-rendered TFT image (host re-pushes)       |
| Opcode                  | `0x28` (CMD_TIME, 4-packet envelope) | `0x25` (SETTFTLCDDATA, chunked RGB565)         |
| After unplug/replug     | device retains time                  | host must re-push on reconnect                 |
| `IClockCapable` honesty | true RTC                             | honest host-driven display, NOT a firmware RTC |

Both honestly "show the time you set." The catalogue/notes MUST NOT imply the
mouse has a firmware RTC — its `notes:` describe it as a host-rendered TFT
widget so the maturity record stays honest (Pitfall 19).

## Three-witness rule (Pitfall 19) — re-evaluation for `ajazz_24g_8k`

1. **Capture/source witness:** the as-built opcode `0x25` upload path is
   implemented + unit-tested (`test_aj_series_tft_clock.cpp`); the render
   pipeline is host-side and deterministic.
1. **Round-trip witness (Phase 11 physical):** plug the mouse, Sync Time,
   confirm the TFT basetta visibly shows the host time.
1. **Negative witness (Phase 11 physical):** set host time to year 2099, Sync,
   confirm the TFT shows 2099 (proves the rendered value tracks input, not a
   static image).

Witnesses (2)+(3) are achievable (the TFT is observable). Until the physical
confirmation, the mouse `clock` is honestly **`partial`** on the clock axis.

## What stands from ARCH-05 / ARCH-05.1 (no change)

- `akp03_variant_3004`, `akp05`/`mirabox_n4`: NO clock surface (host-rendered
  keyface clock, if ever added, goes via `display`/`BAT`/`MAI`, not
  `IClockCapable`). ARCH-05 stands.
- `ak980pro`: firmware RTC via `0x28`. ARCH-05.1 stands.
- `microdia_dongle_7016`: dongle, not in scope.

## Downstream doc alignment (this pass, 2026-05-20)

- `PROJECT.md`: ARCH-05.2 decision-log row added; "Out of Scope" row
  "Render-time-on-keyface clock widget" annotated with the `ajazz_24g_8k`
  exception (host-rendered TFT clock via `IClockCapable` is IN scope, ratified
  here).
- `REQUIREMENTS.md`: ARCH-05 amendment note extended for the mouse.
- `docs/_data/devices.yaml`: `ajazz_24g_8k` `notes:` cite ARCH-05.2.

## References

- As-built: `src/devices/mouse/src/register.cpp` (`hasClock = tft`),
  `src/devices/mouse/src/aj_series.cpp` (opcode `0x25` SETTFTLCDDATA host-render
  path), `tests/unit/test_aj_series_tft_clock.cpp`. Commits `656fb1c`,
  `0e9bb04` (P3.12.1/.2, 2026-05-18).
- ARCH-05 §"Acceptable alternative: host-rendered TftClockWidget" (the pattern
  this ratifies).
- ARCH-05.1 (sibling amendment; the firmware-RTC case for `ak980pro`).
- v1.1 D-02 honesty contract (no lying success UX on time sync).
