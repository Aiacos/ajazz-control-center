---
adr: ARCH-06
phase: 9
title: Composite-HID dedup decision for DeviceRegistry
status: DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)
default_verdict: NOT firing — 0c45:7016 is a separate wireless dongle on a different USB bus branch from ak980pro; treat as a standalone probed-tier device; NO dedup logic added to DeviceRegistry::enumerate
finalization_gate: Physical unplug test — with ak980pro's 2.4G dongle plugged in and ak980pro recognised, unplug the ak980pro 2.4G receiver; verify that 0c45:7016 does NOT disappear simultaneously. ~2-minute user-driven check; no capture tooling required
binds: 'Phase 13 DEVICES-08 (microdia_dongle_7016 catalogue entry) + DEVICES-09 (microdia_dongle stub doc); CONDITIONAL: if captures contradict, a new Phase 12.5 lands composite-HID dedup BEFORE Phase 12 and Phase 13 re-sequences'
confidence: HIGH (topology evidence from live lsusb 2026-05-15 is structurally unambiguous)
ratified: 2026-05-15
---

# ARCH-06: Composite-HID dedup decision for DeviceRegistry (DEFAULT VERDICT — PENDING CAPTURE CONFIRMATION)

**Status:** DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION) — ratified 2026-05-15

> **Honesty contract (D-05):** This verdict is **DEFAULT**, not final. Phase 13
> plans referencing this ADR (DEVICES-08 catalogue row, DEVICES-09 stub
> protocol doc) MUST cite the conditional status and gate on the Phase 9.x
> finalization run (a 2-minute physical unplug test from the user) before
> treating the separate-device assumption as locked. The architectural
> *decision* (NOT firing composite-HID dedup in `DeviceRegistry::enumerate()`)
> is decided here; the *physical confirmation* of the dongle hypothesis is
> the deferred finalization gate.

## Context

The user's machine on 2026-05-15 reports four AJAZZ-relevant USB devices
plugged in simultaneously, surfaced by live `lsusb` and the v1.1 hot-plug
arrival path:

- `0300:3004` (`akp03_variant_3004`, catalogued, Stream Dock 6-key)
- `0c45:8009` (`ak980pro`, catalogued, AK980 PRO keyboard 2.4G dongle)
- `3151:5007` (`ajazz_24g_8k`, catalogued, AJAZZ 2.4G 8K mouse)
- `0c45:7016` (uncataloged in v1.0/v1.1; the question this ADR resolves)

The fourth PID is the open architectural question: **is `0c45:7016` a
separate physical device** (a SONiX/Microdia wireless dongle paired with an
unidentified downstream input device), **or is it a SECONDARY HID interface
of the same physical AK980 PRO keyboard** (which would manifest as two USB
interface descriptors with distinct PIDs but the same physical-device
parent, mediated by the AK980 PRO's own 2.4G receiver)?

The choice is load-bearing for `DeviceRegistry::enumerate()`
(`src/core/include/ajazz/core/device_registry.hpp` + `device_registry.cpp`):
the v1.1 ARCH-02 migration keys the slot cache on `(vid, pid, serial)` per
the post-`shared_ptr<IDevice>` flyweight design (`device_registry.hpp:188-203`
"Per-(vid, pid) flyweight cache of backend instances (D-06). Keyed by
`(vendorId, productId)` — `serial` is intentionally not in the cache key").
If `0c45:7016` is the composite-HID secondary interface of `ak980pro`, the
sidebar shows TWO rows for one physical keyboard (broken UX, regresses
v1.1 Phase 8's per-row tooltip), and the v1.1 hot-plug debouncer's 300 ms
trailing-edge coalescing inside `(vid, pid, serial)` does NOT collapse
across the PID boundary (Pitfall 20 §"Why it happens" — composite HID is
"the norm for wireless gaming peripherals").

Pitfall 20 documents the composite-HID hypothesis and enumerates three
checks that, if any one holds, would confirm it: same
`/sys/bus/usb/devices/` parent path, simultaneous disappearance on unplug,
or children of the same hub port per `lsusb -t`.

## Default Verdict: NOT firing

Composite-HID dedup is **NOT** added to `DeviceRegistry::enumerate()`.
`0c45:7016` is treated as a separate physical device and gains a row in
`docs/_data/devices.yaml` at `probed` tier with `capabilities: []`
(Phase 13 DEVICES-08). The v1.1 ARCH-02 `(vid, pid, serial)` keying remains
unchanged; no new grouping pass, no parent-path resolver, no vendor-control
Usage-Page heuristic.

## Topology evidence (live `lsusb` 2026-05-15)

Four independent topology facts converge on the dongle hypothesis. Each
row is verbatim from the user's live `lsusb -v -d 0c45:7016` and
`lsusb -v -d 0c45:8009` capture on 2026-05-15, cross-referenced with
`/sys/bus/usb/devices/{1-13.1.2,1-10}/` and `lsusb -t`:

| Fact                             | `0c45:7016` (the unknown)                                           | `0c45:8009` (ak980pro 2.4G dongle)                                                 |
| -------------------------------- | ------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| USB sysfs parent path            | `usb1/1-13/1-13.1/1-13.1.2` (descendant of bus-1 port-13 hub chain) | `usb1/1-10` (bus-1 port-10, direct — no hub between device and root)               |
| Link speed                       | Full-Speed 12 Mbps                                                  | (varies by port — wireless dongles typically negotiate High-Speed on USB 2+ ports) |
| Interface shape                  | Two boot-keyboard HID interfaces with 8-byte EPs (IF 0 + IF 1)      | Vendor-control HID + boot-keyboard composite typical for Microdia wireless         |
| iManufacturer / iProduct strings | `iManufacturer="SONiX"` / `iProduct="USB DEVICE"` / bcdDevice 1.03  | Distinct vendor-specific strings (AJAZZ / AK980 PRO lineage)                       |

None of Pitfall 20's three composite-HID confirmations hold:

- **Different `/sys/bus/usb/devices/` parent paths.** `0c45:7016` lives at
  `1-13.1.2`; `ak980pro` lives at `1-10`. A composite-HID device exposes
  multiple interfaces on the *same* USB device address, which means the
  same `/sys/bus/usb/devices/X-Y/` parent for all interfaces. Different
  parent paths is the structural negation of the composite hypothesis.
- **`lsusb -t` shows them as children of DIFFERENT hub ports.** `0c45:7016`
  is a descendant of the bus-1 port-13 hub chain (through a 13.1 sub-hub);
  `ak980pro` plugs directly into bus-1 port-10. They are physically in
  different ports of the host's USB tree.
- **No simultaneous-disappearance evidence yet.** The Pitfall 20 third
  check — unplugging `ak980pro` and observing whether `0c45:7016` also
  disappears — has NOT been performed. It is deferred to the Phase 9.x
  finalization run (see §Captures-confirmation trigger below). The first
  two checks already structurally refute the composite hypothesis; the
  third is the user-driven sanity check that flips the verdict if it
  contradicts.

A fifth converging fact, documented in `.planning/research/FEATURES.md` §4:
the `iProduct="USB DEVICE"` / `iManufacturer="SONiX"` string pair plus two
boot-keyboard interfaces with 8-byte EPs at Full-Speed is the **canonical
signature of a separate wireless 2.4 GHz Microdia/SONiX receiver dongle** —
matched against Sagacious's 2013 documentation of `0c45:7000` as exactly
this for the iPazzPort KP-810-18BR. The Microdia `0c45:70xx` range is
heavily populated by SONiX OEM dongles; the AK980 PRO's tri-mode (BT/2.4G/
USB-C) presents itself as `0c45:8009` in any of the three modes — the
vendor product page is explicit that switching modes does not change
VID:PID.

The paired downstream wireless input device that transmits to `0c45:7016`
is **unidentified**. The Phase 13 DEVICES-09 stub doc
(`docs/protocols/keyboard/microdia_dongle.md`) documents the
identification methodology so a future SKU recognition can proceed
without redoing the topology forensics.

## Why NOT firing (rationale)

- **Topology rules out the composite hypothesis at the platform level.**
  USB devicefs (`/sys/bus/usb/devices/`) is the authoritative source of
  physical-device identity on Linux. Different parent paths mean different
  physical USB devices. This is not a heuristic; it is the kernel-level
  invariant.
- **Adding dedup infrastructure now is YAGNI.** The only consumer that
  would need composite-HID dedup is a single physical device exposing
  multiple PIDs from the same `/sys/bus/usb/devices/` parent. No such
  device exists in the v1.2 connected set (verified at HEAD against the
  four-device enumeration on 2026-05-15). Building infrastructure for a
  zero-consumer case is the textbook YAGNI failure mode.
- **The v1.1 ARCH-02 architecture is preserved.**
  `DeviceRegistry::enumerate()` keys on `(vid, pid, serial)` unchanged
  (`device_registry.hpp:114` + `:188-203`). The new `0c45:7016` row
  coexists with `0c45:8009` as separate sidebar entries, which is the
  correct UX per topology — they are different physical devices and the
  user could unplug one without the other.
- **Pitfall 20 was a precautionary research artefact, not a confirmed
  architectural requirement.** The pitfall lists three checks "in order"
  precisely so the maintainer first establishes whether the hypothesis
  fires before paying the implementation cost. Two of the three checks
  already structurally negate the composite hypothesis at HEAD; treating
  Pitfall 20 as confirmed without driving evidence would have shipped
  complexity for no value (Pitfall 20 §"How to avoid" explicitly conditions
  the dedup landing on the IF-confirmed branch — the IF-NOT-confirmed
  branch is precisely this ADR's verdict).

## Considered alternative: Composite-HID dedup in `DeviceRegistry::enumerate`

**REJECTED for v1.2.** The hypothetical implementation, sketched verbatim
from Pitfall 20 §"How to avoid":

```cpp
// In DeviceRegistry::enumerate() — composite-HID grouping:
// Group hid_device_info entries by (path-parent OR serial-if-non-empty)
// and pick the interface that advertises the vendor-control Usage Page
// (typically 0xFF00..0xFFFF) — that's the row we surface.
```

The vendor-control interface is the one we would send capability commands
to; the boot-keyboard interface would be the OS's concern. This is correct
architecture **IF** the topology says composite. Topology says it does
NOT. Building it without driving evidence costs:

- **Additional 60-100 LoC in `DeviceRegistry`** (parent-path resolver,
  vendor-control Usage-Page detector, grouping pass between
  `hid_enumerate(0, 0)` and the existing `(vid, pid, serial)` cache key).
  Plus the corresponding inverse code path in `open()` so the flyweight
  cache returns the correct underlying handle when the dedup'd row is
  opened.
- **2-3 new test cases in `tests/integration/test_composite_hid_dedup.cpp`**
  exercising the `MockHidEnumerator` "composite device" fixture (two
  `hid_device_info` entries with same parent path / serial, different PIDs)
  and asserting `DeviceRegistry::enumerate()` returns exactly one
  descriptor.
- **A new dedup edge case in the v1.1 hot-plug debouncer.** The
  `HotplugDebouncer` 300 ms trailing-edge coalesces within `(vid, pid, serial)` per Phase 4; if dedup happens at enumerate-time, the debouncer
  must coalesce across PIDs for the dedup'd group. Adds an interaction
  surface to the v1.1 ARCH-02 contract that did not previously exist.
- **Cross-platform parity work.** Windows hidapi's `serial_number` field
  is empty on some Microdia wireless dongles (Pitfall 20 §"Why it
  happens"), removing the secondary dedup key. The dedup would need a
  path-parent-only fallback, and the corresponding Windows-side parent-path
  lookup uses different syscalls (`SetupDi*` vs `/sys/bus/usb/devices/`).
- **Maintenance burden for a behaviour that should never fire.** Every
  future maintainer reading the dedup pass has to first understand what
  it is defending against, then verify the conditions still don't hold.

If the Phase 9.x finalization unplug test contradicts the dongle hypothesis,
the alternative is reopened — but the conditional path is documented as a
**new Phase 12.5** that lands the dedup infrastructure BEFORE Phase 12
(AK980 PRO RGB/sleep-timer/wireless rate-limit promotion). The conditional
probability is LOW per topology evidence; the cost of being wrong is well
under one phase of work.

## Captures-confirmation trigger (what would flip this)

Per D-05 honesty contract, the Phase 9.x finalization run requires a
**physical unplug test** from the user. Unlike ARCH-04 and ARCH-05, this
gate needs **no capture tooling** — no Wireshark, no `usbmon`, no
`tshark`/`usbrply` pipeline. It is a 2-minute bench test the user runs
during the Phase 9.x follow-up.

**Procedure:**

1. Confirm both `0c45:8009` (ak980pro) and `0c45:7016` are recognised
   simultaneously by `lsusb` (the v1.2 baseline state on 2026-05-15).
1. Unplug the `ak980pro` 2.4G receiver dongle (NOT the keyboard itself —
   the small USB-A receiver bundled with AK980 PRO).
1. Within 5 seconds, run `lsusb` again and check whether `0c45:7016` is
   still present.
1. **If `0c45:7016` REMAINS in `lsusb`:** ARCH-06 verdict stands. Phase 13
   DEVICES-08/09 proceed with the separate-device assumption. Promote
   this ADR's `status` from "DEFAULT VERDICT (PENDING CAPTURE
   CONFIRMATION)" to "Locked".
1. **If `0c45:7016` ALSO disappears at the same time:** ARCH-06 flips to
   "FIRING — composite-HID dedup REQUIRED before Phase 12." A new Phase
   12.5 lands the dedup infrastructure (per the Pitfall 20 sketch above),
   Phase 12 re-sequences to depend on it, and Phase 13 DEVICES-08 absorbs
   the dedup outcome rather than entering `microdia_dongle_7016` as a
   separate row.

The realistic outcome per topology evidence is the verdict stands. The
flip path is the precautionary path, documented so it is not surprising
if it fires.

## Binding to downstream plans

### Phase 13 PLAN (DEVICES-08) — `devices.yaml` row

`docs/_data/devices.yaml` gains a new row at `probed` tier:

```yaml
- codename: microdia_dongle_7016
  vendor_id: "0c45"
  product_id: "7016"
  family: dongle
  maturity: probed
  capabilities: []
  notes: |
    Separate wireless 2.4GHz Microdia/SONiX receiver dongle (NOT a composite
    interface of ak980pro). Topology evidence (live lsusb 2026-05-15):
    different USB bus branch (usb1/1-13/1-13.1/1-13.1.2 vs ak980pro's
    usb1/1-10), Full-Speed 12 Mbps, two boot-keyboard interfaces with 8-byte
    EPs, iProduct "USB DEVICE" / iManufacturer "SONiX" / bcdDevice 1.03.
    Paired downstream input device unknown — pending evtest /dev/hidrawN
    session per DEVICES-09 identification methodology.
    Ratified at ARCH-06 default verdict 2026-05-15.
```

Codename `microdia_dongle_7016` verified ASCII-only per Pitfall 32 (the
Win32 CMD codepage / Catch2 filter-mangling pattern from v1.1 Phase 4).
Family `dongle` is a new family value (or `unknown`/`keyboard` if the
catalogue schema rejects it); Phase 13 PLAN resolves the family-vocabulary
question against the existing `devices.yaml` schema.

### Phase 13 PLAN (DEVICES-09) — stub protocol doc

`docs/protocols/keyboard/microdia_dongle.md` (NEW, stub) documents:

- The HID descriptor of `0c45:7016` (raw bytes from
  `sudo cat /sys/class/hidraw/hidraw5/device/report_descriptor`, or
  human-readable form via `hidrd-convert`).
- The four topology facts above (sysfs parent, link speed, interface
  shape, iManufacturer/iProduct strings).
- The identification methodology so a future SKU using the same dongle
  family can be recognised without redoing the topology forensics
  (cross-reference `.planning/research/FEATURES.md` §4.4 step table).
- Pointer to this ADR for the architectural decision.

### Phase 13 UI back-fill (VERIFY-01..04)

Unaffected by this decision in the NOT-firing path. The sidebar gains a
new row for `microdia_dongle_7016` at `probed` tier. With `capabilities: []`, the v1.1 Phase 5 Sync-button visibility logic correctly hides the
button on this row (no `hasClock` capability), and the v1.1 Phase 8
MaturityRole tooltip displays the `probed` tier styling.

### CONDITIONAL re-sequencing (FIRING path, LOW probability)

If the Phase 9.x unplug test contradicts the dongle hypothesis, the plan
shape changes:

- **New Phase 12.5** (slug: `12.5-composite-hid-dedup-infrastructure`)
  lands composite-HID dedup in `DeviceRegistry::enumerate()` per the
  Pitfall 20 sketch above. Includes a `MockHidEnumerator` composite
  fixture plus `tests/integration/test_composite_hid_dedup.cpp` plus a
  Windows-side parent-path fallback.
- **Phase 12 re-sequences** to depend on Phase 12.5 (so the AK980 PRO
  promotion writes to the dedup'd row, not to two duplicate rows).
- **Phase 13 DEVICES-08 absorbs the dedup outcome.** No separate
  `microdia_dongle_7016` row; the topology evidence is documented in the
  `ak980pro` row's `notes:` field as the secondary-interface rationale.
- **DEVICES-09** still lands the stub doc, but it documents the
  secondary-interface relationship instead of a separate dongle.

Probability LOW per topology evidence; the flip path is documented so
nobody is surprised when (or if) it fires.

## Honesty contract (D-05)

The verdict is **DEFAULT (pending capture confirmation)** per D-05.
Phase 9.x finalization (the 2-minute unplug test) is mandatory before
this ADR's `status` is promoted to "Locked". Phase 13 plans referencing
this ADR MUST cite the conditional in their `<context>` block; a single
PR that promotes the ADR to "FINAL VERDICT" lands as part of the
Phase 9.x follow-up commit alongside the equivalent promotions of ARCH-04
and ARCH-05.

The "DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)" label appears in
three load-bearing places — the title line, the `status` frontmatter
field, and the bold Status line under the title — so no honesty-corruption
path elides it (any reader of the file in any form — frontmatter parser,
raw read, rendered markdown — sees the conditional). This mirrors the
ARCH-04 + ARCH-05 honesty-surface pattern established in plans 09-05 and
09-06.

## References

- `.planning/research/SUMMARY.md` §"ARCH-06 — Composite-HID dedup"
  (HIGH-confidence recommendation: NOT to fire; topology evidence
  enumerated).
- `.planning/research/FEATURES.md` §1 (topology table for all four
  connected devices — refutes composite hypothesis up front) + §4.1-4.4
  (Microdia/SONiX wireless-receiver pattern signature + identification
  methodology + proposed `devices.yaml` row).
- `.planning/research/PITFALLS.md` Pitfall 20 (composite-HID dedup
  hypothesis — three checks; this ADR documents the IF-NOT-confirmed
  branch). Cross-reference Pitfall 21 (wireless captures hit USB-layer
  not RF-layer) for the dongle architecture context.
- `.planning/research/ARCHITECTURE.md` §"ARCH-06 decision criteria"
  (default-verdict recommendation: NOT firing).
- `.planning/REQUIREMENTS.md` ARCH-06 (this ADR), DEVICES-08
  (`microdia_dongle_7016` catalogue row at `probed` tier), DEVICES-09
  (stub protocol doc).
- `.planning/phases/09-research-captures-hygiene/09-CONTEXT.md` D-05
  (ARCH default verdicts are PRO-FORMA — finalization gates promotion).
- `.planning/phases/09-research-captures-hygiene/ARCH-04.md` (companion
  ADR — same default-verdict shape + label discipline established here).
- `src/core/include/ajazz/core/device_registry.hpp:114` + `:188-203`
  (v1.1 ARCH-02 `(vid, pid, serial)` keying — unchanged by this ADR).
- v1.1 ARCH-02 (`HotplugMonitor::injectEvent` test-only shim) and
  v1.1 ARCH-03 (`shared_ptr<IDevice>` flyweight migration) — both
  preserved by this verdict.
- `.planning/milestones/v1.1-phases/03-architectural-decisions/ARCH-01-parser-choice.md`
  (template — this ADR mirrors its shape, with `status: Locked` replaced
  by `status: DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)`).
