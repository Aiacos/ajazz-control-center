# Phase 13: Catalogue + v1.1 UI Verifies Back-Fill - Context

**Gathered:** 2026-05-20
**Status:** Ready for planning
**Mode:** Smart-discuss (autonomous), grey areas accepted as ADR/ROADMAP-grounded defaults

<domain>
## Phase Boundary

Two distinct, separable workstreams:

1. **Catalogue + doc (DEVICES-08/09) — autonomous-executable now, no build,
   no hardware beyond ARCH-06's existing topology evidence.** Add the
   `microdia_dongle_7016` (`0c45:7016`) catalogue row at `probed` tier and a
   new `microdia_dongle.md` topology/identification stub.
1. **v1.1 UI verifies (VERIFY-01..04) — operator checklist, gated on a
   running app (build + the 4 physical devices).** Close the four
   real-hardware visual UI verifications that v1.1 deferred.

</domain>

<decisions>
## Implementation Decisions

### Locked by Phase 9 ADR

- **ARCH-06 (composite-HID dedup):** `0c45:7016` is a SEPARATE dongle, NOT a
  composite interface of the AK980 PRO. Catalogue row carries `family: dongle`, `capabilities: []`, `probed` tier, with `notes:` citing ARCH-06's
  negative verdict. (DEFAULT VERDICT — cite conditional status.)

### Accepted grey-area defaults (2026-05-20)

- Dongle catalogue row: `probed` tier, `capabilities: []`, `family: dongle`.
- `docs/protocols/keyboard/microdia_dongle.md`: NEW stub documenting the HID
  descriptor + USB topology + identification methodology so a future SKU
  enumerating the same way is recognised.
- **VERIFY-01..04 are authored as an operator checklist and DEFERRED** —
  they are real-hardware visual checks needing the running app and physical
  devices; this phase produces the checklist + acceptance wording, not a
  green run.

### Split-feasibility decision (user, 2026-05-20)

Plan the phase as (at least) two plans: the catalogue/doc plan
(DEVICES-08/09) is **autonomous-executable now**; the VERIFY plan is an
operator checklist gated on build + hardware.

</decisions>

\<code_context>

## Existing Code Insights

- `docs/_data/devices.yaml` — gains the `microdia_dongle_7016` row.
- `docs/protocols/keyboard/microdia_dongle.md` — NEW stub.
- The four VERIFY items target already-shipped v1.1 Time Sync UI:
  - VERIFY-01: sidebar Sync-button shows iff `hasClock=true` (DeviceRow + DeviceModel HasClockRole).
  - VERIFY-02: Settings "auto-sync on connect" toggle persists across restart (QSettings "Time/AutoSync").
  - VERIFY-03: `IClockCapable::setTime` → `NotImplemented` shows an exclamation glyph + tooltip, never a false "synced".
  - VERIFY-04: sidebar MaturityRole tooltip matches the `devices.yaml` `notes:` for the row; all 5 tier values.

**Note:** the catalogue/doc workstream needs neither the build nor hardware.
The VERIFY workstream needs the running app (build fix) + the 4 devices.

\</code_context>

<specifics>
## Specific Ideas (ROADMAP success criteria)

1. `microdia_dongle_7016` (`0c45:7016`) row at `probed` tier, `capabilities: []`, `family: dongle`; notes cite ARCH-06. *(autonomous now)*
1. NEW `docs/protocols/keyboard/microdia_dongle.md` stub (HID descriptor + topology + ID methodology). *(autonomous now)*
1. VERIFY-01: Sync-button visibility tracks `hasClock`. *(operator)*
1. VERIFY-02: auto-sync toggle survives app restart. *(operator)*
1. VERIFY-03: NotImplemented → exclamation glyph + tooltip, never false success. *(operator)*
1. VERIFY-04: MaturityRole tooltip matches `devices.yaml` notes; all 5 tiers. *(operator)*

</specifics>

<deferred>
## Deferred Ideas

- **VERIFY-01..04 execution** — requires the running app (build fix) + the 4
  physical devices. This phase delivers the checklist; an operator runs it.
- **Build fix** — Qt6 CorePrivate precedes the VERIFY workstream.

</deferred>
