---
adr: ARCH-05
phase: 9
title: IClockCapable::setTime wire-format outcome per device
status: DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)
default_verdict: NO RTC opcode in any AJAZZ corpus; hasClock=false on akp03_variant_3004 and ak980pro; setTime stays NotImplemented; PROJECT.md Out-of-Scope row preserved
finalization_gate: Captures from all 4 connected devices (0300:3004 + 0c45:8009 + 3151:5007 + 0c45:7016) during native-app time-set actions show no time-shaped byte sequence (Pitfall 19 three-witness rule)
binds: Phase 10 DEVICES-05 (akp03_variant_3004); Phase 12 DEVICES-06 (ak980pro); v1.1 D-02 honesty contract preserved
confidence: 'HIGH (four-corpus convergence: mirajazz, opendeck-akp03, ajazz-sdk, TaxMachine AK820 Pro clean-room)'
ratified: 2026-05-15
---

# ARCH-05: IClockCapable::setTime wire-format outcome per device (DEFAULT VERDICT - PENDING CAPTURE CONFIRMATION)

**Status:** DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION) - ratified 2026-05-15

> **Honesty contract (D-05):** This verdict is **DEFAULT**, not final. Phase 10
> (DEVICES-05) and Phase 12 (DEVICES-06) plans referencing this ADR MUST cite
> the conditional status and gate on the Phase 9.x finalization run (capture
> evidence from all 4 connected devices during native-app time-set UI actions)
> before treating `hasClock=false` and `setTime stays NotImplemented` as
> non-revisable. The *architectural posture* (no RTC opcode anywhere; clock
> capability demoted; `IClockCapable::setTime` returns `NotImplemented`) is
> decided here; *finalization* is subject to Pitfall 19 three-witness rule
> evaluation against captured evidence.

## Context

v1.1 shipped the time-sync scaffolding (Phase 5) on the assumption that some
AJAZZ devices expose a host-settable RTC. The v1.1 D-02 honesty contract
guarantees that `setTime` returns `NotImplemented` and the per-row glyph stays
at exclamation when the device cannot do it. v1.2 must answer definitively,
**per connected device**, whether the RTC exists. The question is BINARY per
device, not graduated - either a real opcode exists and the capability promotes
to `functional`, or it does not and `hasClock` is demoted to `false`.

Connected devices in v1.2 (live `lsusb`, 2026-05-15):

- **`akp03_variant_3004` (`0x0300:0x3004`)** - AKP03 family Stream Dock 6-key
  (HOTSPOTEKUSB HID DEMO iProduct). Currently advertises
  `capabilities: [display, encoder, clock]` in `docs/_data/devices.yaml`.
- **`ak980pro` (`0x0c45:0x8009`)** - Microdia 2.4G wireless mechanical keyboard
  with 1.14" TFT display + 8000 mAh battery. Currently advertises
  `capabilities: [rgb, macros, layers, clock]`.
- **`ajazz_24g_8k` (`0x3151:0x5007`)** - AJ-series 8K wireless mouse. Does NOT
  advertise `clock` in v1.1 catalogue; included here for completeness so the
  "all 4 devices" Phase 9.x finalization scope is unambiguous.
- **`microdia_dongle_7016` (`0x0c45:0x7016`)** - separate wireless dongle on a
  different USB bus branch from `ak980pro` (per ARCH-06 default verdict).
  Enters catalogue at `probed` tier with `capabilities: []`; clock not in scope.

The cross-research convergence on the no-RTC question is unusually strong:
FEATURES.md §1 + §2, PITFALLS.md Pitfall 19, ARCHITECTURE.md §"Capability
mix-in inventory", and research SUMMARY.md §"ARCH-05" all independently arrive
at the same default verdict. This ADR ratifies that convergence as the
load-bearing v1.2 honesty position.

## Default Verdict

**NO RTC opcode exists in any of the four reference corpora.** Specifically:

- **`mirajazz` crate** (v0.12.1, Rust binding for the AKP03 family,
  MIT-licensed) - documented opcodes are `DIS`, `LIG`, `LBLIG`, `SETLB`, `BAT`,
  `CLE`, `STP`, `HAN`, `CONNECT`. No `setTime`/`setRtc`/`TIM` command id; no
  time-shaped payload structure; the `flush()` model does not include time
  state.
- **`opendeck-akp03`** (Andrey V. Rust crate, PID coverage matrix for AKP03
  family) - confirms `0x3004` is NOT covered in the published PID matrix and
  the wire-format model has no RTC opcode in any covered SKU.
- **`ajazz-sdk`** (vendor-published partial SDK referenced in
  `docs/protocols/streamdeck/akp03.md`) - opcode table identical shape; no
  RTC-shaped opcode.
- **TaxMachine `ajazz-keyboard-software-linux`** (clean-room reference for
  `0c45:8009` at the same VID:PID as `ak980pro`) - full captured command table
  for AK820 Pro includes `MODE_COMMAND` (cmd 0x13, 20 RGB modes),
  `IMAGE_COMMAND` (cmd 0x72, TFT image upload), `SLEEP_TIME` (cmd 0x17), and
  the three-stage `START` (0x18) / `FINISH` (0xf0) envelope; there is NO
  `set_time` / `setRtc` entry in the captured protocol surface.

**Per-device state ratified by this ADR:**

- **`akp03_variant_3004` (`0x0300:0x3004`)**: `hasClock=false`; `setTime` stays
  `NotImplemented`; `docs/_data/devices.yaml` row removes `clock` from
  `capabilities:` list with a `notes:` line citing this ADR (Phase 10
  DEVICES-05).
  Reason: no RTC opcode in `mirajazz`, `opendeck-akp03`, or `ajazz-sdk` corpora.
  The marketing "clock widget" on AKP03-family keyfaces is a host-rendered
  image uploaded via the `display` capability (`BAT` opcode), not firmware
  time.

- **`ak980pro` (`0x0c45:0x8009`)**: `hasClock=false`; `setTime` stays
  `NotImplemented`; `docs/_data/devices.yaml` row removes `clock` from
  `capabilities:` list with the same ARCH-05 reasoning (Phase 12 DEVICES-06).
  Reason: TaxMachine AK820 Pro clean-room capture at the same VID:PID does not
  include any RTC-set opcode. The 1.14" TFT "clock display" the vendor app
  shows is a host-rendered image pushed via cmd `0x72` (`IMAGE_COMMAND`,
  deferred to v1.2.x `TftClockWidget` differentiator under DISPLAY-05) - the
  vendor app sends "current time" -> renders glyphs into a `QImage`-equivalent
  -> uploads as a normal image. Device firmware sees an image; it does not
  see a time.

- **`ajazz_24g_8k` (`0x3151:0x5007`)**: N/A - this mouse does not advertise
  `clock` in v1.1; `hasClock` stays implicitly `false`. Phase 11 verifies the
  absence (probe-and-confirm session captures cmd `0x21`..`0x50` envelope; no
  RTC opcode expected, none has been hinted at in any AJ-series corpus or
  vendor-protocol-notes finding).

- **`microdia_dongle_7016` (`0x0c45:0x7016`)**: N/A - dongle, no capabilities
  advertised yet (`capabilities: []` at `probed` tier per ARCH-06).

**PROJECT.md "Out of Scope" row 68** ("Real `IClockCapable::setTime` wire
formats - Blocked on firmware support - no AJAZZ device exposes a
host-settable RTC per vendor recon. Defer to a milestone where a backend
reverse-engineers firmware.") is **preserved unchanged**. This ADR ratifies
that preservation as the captures-driven default verdict for v1.2.

## Three-witness rule (Pitfall 19) - and why it cannot be satisfied for `clock`

For a capability to promote from `scaffolded` to `functional` under the
Pitfall 19 honesty contract, three witnesses must coexist:

1. **Capture witness:** the vendor's native AJAZZ app provably sends this byte
   sequence when the user takes the corresponding UI action (and only then,
   not at idle, startup, or arbitrary timer ticks). For RTC across the four
   connected devices, this is the **absence of evidence** in all four
   reference corpora plus the captures-confirmation expectation that
   Phase 9.x finalization will find no time-shaped byte sequence during a
   native-app time-set action.

1. **Round-trip witness:** the device exhibits an observable state change
   that matches the value sent. For `clock` on AKP03 family: there is **no
   firmware-rendered LCD clock widget** to read back from
   (`docs/protocols/streamdeck/akp03.md:113-114` lists features; clock-on-
   keyface is not among them - it is also separately listed as Out of Scope
   in PROJECT.md). Clock-on-keyface in any vendor app is a host-rendered
   image upload via `display`, not firmware time. For `ak980pro`: the 1.14"
   TFT renders a clock face only when the host sends pixels (TaxMachine
   corpus confirms - no `cmd 0xXX setTime` exists; only `cmd 0x72`
   `IMAGE_COMMAND`). **Round-trip witness is structurally unavailable on
   both AKP03 and ak980pro.**

1. **Negative witness:** sending a deliberately wrong value (e.g. epoch `0`)
   produces a different observable state, proving the device is actually
   parsing the field rather than no-op-ing on unknown opcodes. **Cannot be
   tested** when round-trip witness is absent - there is nothing observable
   to differ.

Two of three witnesses (round-trip + negative) are STRUCTURALLY unavailable
for `clock` on both AKP03 and ak980pro. Therefore: **even if Phase 9.x
captures showed a byte sequence that LOOKED like a `setTime`, the three-
witness rule would still block promotion to `functional`.** The default
verdict's strength comes from the structural impossibility of satisfying the
witness set, not just from the empirical absence in the corpora.

## Explicitly forbidden anti-feature (Pitfall 19 confirmation-bias trap)

**DO NOT** synthesize a fake `setSystemTimeOn` wire format from bytes that
"look like time" in a capture. A 4-byte little-endian Unix-ish epoch in a
packet whose 3-byte ASCII command word reads as `TIM`, `CRT`, `RTC`, or any
other plausible mnemonic could be:

- A `TIMer` field (sleep timer for display backlight or auto-sleep on AK980
  PRO via cmd `0x17`);
- A `LIG` brightness write with a payload that happens to fall in a
  time-looking range;
- A `CONNECT` keep-alive ping (AKP03 family) with cyclic timestamp-shaped
  values that have no semantic meaning to firmware;
- Random NOP padding;
- An out-of-band debug/factory-test opcode left in pre-production firmware
  (the `HOTSPOTEKUSB HID DEMO` iProduct on `0x3004` is a pre-production
  fingerprint per Pitfall 22).

Pitfall 19 documents this as the classic confirmation-bias trap: a
researcher hopes for a positive finding, finds a byte pattern that fits the
hypothesis, ships it, and the per-row glyph flips from exclamation to ok.
The v1.1 D-02 honesty contract is then violated silently. The Out-of-Scope
table in `.planning/REQUIREMENTS.md` row ("Synthesizing a fake
setSystemTimeOn wire format from bytes that 'look like time'") encodes this
prohibition as a milestone-level non-goal.

## Acceptable alternative: host-rendered TftClockWidget (v1.2.x / v1.3+ differentiator)

The AK980 PRO's 1.14" TFT can display a clock face IF the host sends pixels
through the `display` capability (cmd `0x72` `IMAGE_COMMAND`, chunked send
loop documented as `//TODO` in TaxMachine and deferred to v1.2.x under
DISPLAY-05). This is **acceptable** as a v1.3+ differentiator feature:

- The QML / UI layer renders the clock face (analog or digital, configurable)
  into a `QImage` host-side and ships it as a normal image upload through the
  `IDisplayCapable` interface, NOT through `IClockCapable::setTime`.
- The user sees "a clock on the screen" but the device firmware sees "an
  image upload" - the device is not setting any internal RTC; the host is
  driving the visual.
- This is the same architectural pattern that AKP03 family vendor apps use
  for clock-on-keyface widgets - host-rendered images uploaded as JPEG via
  `BAT` opcode.

This honest reframing is the canonical path for the user-visible "clock"
affordance. It preserves the v1.1 D-02 honesty contract (`IClockCapable:: setTime` continues to return `NotImplemented` everywhere; the per-row glyph
stays at exclamation because no device actually has a settable RTC) while
still delivering a user-facing clock UX through the appropriate capability
seam.

## Captures-confirmation trigger (what would flip this - vanishingly unlikely)

Per D-05 honesty contract, the Phase 9.x finalization run requires captures
from all 4 connected devices during native-app time-set UI actions. The
default verdict flips only if all three of the following hold for any one
device:

1. **Capture witness positive:** Phase 9.x captures show, on any of the four
   devices, a byte sequence that is sent by the vendor app ONLY when the user
   takes a time-set UI action (not at idle, not at startup, not at arbitrary
   timer ticks).

1. **Round-trip witness positive:** the device exhibits a verifiable
   observable state change in response. For AKP03 family this requires a
   firmware-rendered clock widget appearing for the first time (none has
   been documented in any corpus across the entire AKP03 SKU range). For
   ak980pro this requires a firmware-rendered clock face on the 1.14" TFT
   that does NOT depend on host pixel uploads (the TaxMachine corpus
   confirms current behavior is host-pixel-driven only).

1. **Negative witness positive:** sending a deliberately wrong value (epoch
   `0`, far-future epoch, malformed payload) produces a different observable
   state, proving the device is parsing the field.

All three would need to hold simultaneously. Per (2), this requires a clock
widget rendered by firmware that has **never** been documented in any of the
four corpora or any v1.0/v1.1 AJAZZ research artifact - vanishingly unlikely
given the four-corpus convergence on the no-RTC finding.

The realistic Phase 9.x outcome is the verdict stands. The finalization run
is the explicit pro-forma confirmation that v1.2's honesty contract rests on
captured evidence, not on absence-of-evidence-as-evidence. If the finalization
run does flip the verdict on one device (still vanishingly unlikely), a
follow-up ADR (`ARCH-05.1` or similar) ratifies the flip - never an in-place
edit to this document, which preserves the audit trail.

## Binding to downstream plans

- **Phase 10 PLAN (DEVICES-05)**: `docs/_data/devices.yaml` row for
  `akp03_variant_3004` removes `clock` from `capabilities:` and gains a
  `notes:` line: "no RTC opcode in mirajazz/opendeck-akp03/ajazz-sdk; LCD
  clock widgets are host-rendered images via the `display` capability, not
  firmware time. Ratified at ARCH-05 default verdict 2026-05-15." Maturity
  promotes `scaffolded` -> `functional` (display + encoder are real per
  ARCH-04 image-pipeline + existing encoder backend; clock honestly demoted
  rather than blocking promotion).

- **Phase 12 PLAN (DEVICES-06)**: same shape for `ak980pro`. `notes:` line
  cites ARCH-05 with the TaxMachine clean-room reasoning. Maturity promotes
  `scaffolded` -> `partial` (RGB modes + sleep-timer functional per KEYBOARD-
  01/02; macros / layers / per-key RGB / battery stay in
  `feature_summary.pending:` per Pitfall 29 honesty contract; clock honestly
  demoted).

- **Phase 13 VERIFY-01**: real-hardware visual verify that the Phase 5 Sync
  button hides on rows where `hasClock=false` post-DEVICES-05/06 demotions
  (cleanly via the existing capability-gating role from v1.1 TIMESYNC-04).

- **Phase 13 VERIFY-03**: real-hardware visual verify of the exclamation
  glyph + tooltip on `NotImplemented` rows - Pitfall 19 honesty contract
  enforcement at the per-row UX level. This ADR establishes the
  architectural ground truth that VERIFY-03 enforces visually.

## Honesty contract (D-05 + v1.1 D-02)

This verdict is **DEFAULT (pending capture confirmation)** per D-05. Phase
9.x finalization is mandatory before declaring this resolved as `Locked`.
Until then, downstream phases (10 DEVICES-05, 12 DEVICES-06) proceed with
the default verdict but MUST document the conditional in their plan
`<context>` blocks and surface it in their commit-message body. The v1.1
D-02 contract ("no lying success UX on time sync") is **reinforced, not
weakened**, by the default verdict - we are honestly saying "no AJAZZ device
exposes a host-settable RTC, so `setTime` honestly returns `NotImplemented`,
so the per-row glyph honestly stays at exclamation" instead of synthesizing
a fake `Ok` from a byte pattern that "looks like time."

The same shape as ARCH-04: the `status: DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)` frontmatter field is load-bearing; any tool or human reader
that drops it is breaking the contract.

## References

- `.planning/research/SUMMARY.md` §"Architecture Approach" / §"ARCH-05" (HIGH
  confidence cross-corpus finding; four-corpus convergence).
- `.planning/research/FEATURES.md` §1 (AKP03 variant 0x3004 - "no host-
  settable RTC; vendor-app clock widget is rendered-on-keyface image") + §2
  (AK980 PRO - "no clock / setTime / RTC command" in TaxMachine corpus;
  1.14" TFT clock is host-pushed image upload).
- `.planning/research/PITFALLS.md` Pitfall 19 (`clock` capability false-
  positive lie - three-witness rule + structural-impossibility-of-round-
  trip-witness argument + ARCH-04 ratification trigger now resolved by this
  ARCH-05 instead).
- `.planning/research/ARCHITECTURE.md` (capability mix-in inventory;
  `dynamic_cast<IClockCapable*>` consumer pattern preserved).
- `.planning/REQUIREMENTS.md` ARCH-05 + DEVICES-05 + DEVICES-06 + Out-of-
  Scope row ("Synthesizing a fake setSystemTimeOn wire format from bytes
  that 'look like time'").
- `.planning/PROJECT.md` "Out of Scope" row 68 ("Real `IClockCapable:: setTime` wire formats - no AJAZZ device exposes a host-settable RTC") -
  preserved unchanged.
- `.planning/phases/09-research-captures-hygiene/ARCH-04.md` (companion ADR
  - same `DEFAULT VERDICT (PENDING CAPTURE CONFIRMATION)` status pattern;
    cross-link for shape provenance).
- `.planning/phases/09-research-captures-hygiene/09-CONTEXT.md` §D-05 (ARCH
  default verdicts are PRO-FORMA - finalization gates promotion).
- v1.1 D-02 honesty contract (Phase 5 retrospective; TimeSyncService canonical
  mix-in consumer pattern - `dynamic_cast` + null-check + WARN-once + per-row
  exclamation glyph on `NotImplemented`).
- `docs/protocols/streamdeck/akp03.md` (clock-on-keyface absence documented;
  features table at lines 113-114).
- `docs/_data/devices.yaml` (devices catalogue with maturity tiers and
  current `capabilities:` lists for `akp03_variant_3004` line 272 + `ak980pro`
  line 303).
- `.planning/milestones/v1.1-phases/03-architectural-decisions/ARCH-01-parser-choice.md`
  (template - this ADR mirrors its shape via ARCH-04, with `status: Locked`
  replaced by `status: DEFAULT VERDICT`).
