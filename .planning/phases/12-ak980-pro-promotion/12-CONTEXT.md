# Phase 12: AK980 PRO Promotion - Context

**Gathered:** 2026-05-20
**Status:** Ready for planning
**Mode:** Smart-discuss (autonomous), grey areas accepted as ADR/ROADMAP-grounded defaults

<domain>
## Phase Boundary

Promote the `0c45:8009` AK980 PRO keyboard (over its 2.4G wireless dongle):
select one of 20 RGB lighting modes, set RGB brightness / speed / direction,
set a discrete sleep-timer, save/push profile changes deliberately, and keep
the wireless link from stalling keystrokes during RGB transitions. The
`devices.yaml` row stops advertising `clock`. Maturity `scaffolded` →
`partial`.

**Out of this phase:** capture-confirmation finalization (ARCH-05 is DEFAULT
VERDICT); real-hardware wireless-stall verification (needs the device).

</domain>

<decisions>
## Implementation Decisions

### Locked by Phase 9 ADR (DEFAULT VERDICT — capture-pending)

- **Clock (ARCH-05):** no RTC opcode in any AJAZZ corpus → remove `clock`
  from the `ak980pro` row; `setTime` stays `NotImplemented`. Same reasoning
  as DEVICES-05. Plan MUST cite the conditional status.

### Accepted grey-area defaults (2026-05-20)

- **RGB write path:** cmd `0x13`, 64-byte Report ID `0x04`, three-stage
  sequence (per the TaxMachine AK820 Pro clean-room corpus referenced in
  ARCH-05). Mark `// CAPTURE-PENDING` where byte-level detail is unconfirmed.
- **UI scale-mapping:** brightness (0..5), speed (0..5), direction (0..3)
  raw values are shielded behind labeled/percentage QML sliders — the UI
  never surfaces the raw 0..5 scale.
- **Sleep-timer:** discrete picker 1 / 5 / 10 / 30 min (cmd `0x17`);
  selection persists across reconnect.
- **Wireless safety (honesty-critical gate):** `ak980pro` record carries
  `isWireless = true`; `ProprietaryKeyboard::writeRgb` enforces a ≤10
  writes/sec rate-limit when `isWireless` so RGB transitions cannot stall
  keystrokes over the 2.4G link.

### Claude's Discretion

Profile save/push UX wording, rate-limiter implementation detail, and test
fixture shapes follow existing `ProprietaryKeyboard` + `IRgbCapable`
conventions.

</decisions>

\<code_context>

## Existing Code Insights

- `ProprietaryKeyboard` backend — implements `IRgbCapable`; `setMode` /
  brightness / speed / direction extensions land here. `writeRgb` gains the
  wireless rate-limit.
- `IClockCapable::setTime` — stays `NotImplemented` (WARN-once stub already
  present per v1.1 Time Sync work).
- `docs/protocols/keyboard/` — AK980 PRO / AK820 family protocol docs;
  cmd `0x13` (RGB), `0x17` (sleep-timer).
- `docs/_data/devices.yaml` — `ak980pro` row: remove `clock`, promote
  maturity `scaffolded` → `partial`.

**Build precondition:** C++ build currently fails to configure (Qt6
CorePrivate / qzipreader_p.h — needs qt6-qtbase-private-devel). Execution
blocked until resolved.

\</code_context>

<specifics>
## Specific Ideas (ROADMAP success criteria)

1. 20 RGB modes via `IRgbCapable::setMode` (cmd `0x13`, Report ID `0x04`, three-stage).
1. Brightness/speed/direction via `IRgbCapable` extensions; QML scale-mapped sliders.
1. Sleep-timer (cmd `0x17`); discrete picker 1/5/10/30 min; persists across reconnect.
1. **Honesty-critical gate:** `isWireless=true`; `writeRgb` ≤10 writes/sec rate-limit.
1. `devices.yaml` removes `clock` (ARCH-05); maturity `scaffolded` → `partial`.

</specifics>

<deferred>
## Deferred Ideas

- **Capture confirmation** — finalizes ARCH-05 + the RGB byte-level detail.
- **Real-hardware wireless-stall verification** — needs a physical `0c45:8009`
  - its 2.4G dongle.
- **Build fix** — Qt6 CorePrivate precedes any compile/test step.

</deferred>
